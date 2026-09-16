#include "StlMesh.h"
#include <cstdlib>
#include <iostream>

using namespace flight_core;
namespace {
void require(bool ok,const char* message) {
    if (!ok) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
void rejected(const std::string& bytes,const char* message,std::size_t limit=stl_max_triangles) {
    const auto result=parse_stl(bytes,limit);
    require(!result && !result.error.empty() && result.vertices.empty(),message);
}
void u32(std::string& bytes,std::size_t offset,std::uint32_t value) {
    for (std::size_t i=0;i<4;++i) bytes[offset+i]=static_cast<char>((value>>(8*i))&255);
}
void f32(std::string& bytes,std::size_t offset,float value) {
    std::uint32_t bits=0; std::memcpy(&bits,&value,sizeof(bits)); u32(bytes,offset,bits);
}
const std::string facet="facet normal 0 0 0\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\n";
const std::string ascii="solid sample name\n"+facet+"endsolid sample name\n";
std::string binary() {
    std::string bytes(134,'\0');
    const std::string header="solid binary, not ASCII!";
    std::copy(header.begin(),header.end(),bytes.begin());
    u32(bytes,80,1);
    f32(bytes,108,1); // Second vertex X; other coordinates and supplied normal are zero.
    f32(bytes,124,1); // Third vertex Y.
    bytes[132]=static_cast<char>(0xff); // Attribute/color bytes do not change geometry.
    return bytes;
}
}

int main() {
    const auto text=parse_stl(ascii), raw=parse_stl(binary());
    require(text && raw && text.vertices.size()==3 && raw.vertices.size()==3,"ASCII and solid-header binary parse one triangle");
    for (std::size_t i=0;i<3;++i)
        require(text.vertices[i].x==raw.vertices[i].x && text.vertices[i].y==raw.vertices[i].y
            && text.vertices[i].z==raw.vertices[i].z,"ASCII and little-endian binary preserve identical winding and coordinates");
    require(text.vertices[1].x==1 && text.vertices[2].y==1,"input normal zero is accepted and vertex order is preserved");
    auto scientific=ascii;
    scientific.replace(scientific.find("vertex 1 0 0"),12,"vertex +1e0 -0.0 0");
    require(bool(parse_stl(scientific)),"ASCII signed scientific notation is accepted independent of locale");
    require(bool(parse_stl("solid\r\n"+facet+"endsolid\r\n\t ")),"unnamed solids and CRLF/whitespace termination are accepted");

    auto broken=binary(); broken.pop_back();
    rejected(broken,"truncated solid-header binary is rejected");
    broken=binary(); broken.push_back('\0');
    rejected(broken,"binary trailing bytes are rejected");
    broken=binary(); u32(broken,80,2);
    rejected(broken,"binary triangle count must match exact byte size");
    broken=binary(); u32(broken,96,0x7f800000);
    rejected(broken,"binary nonfinite vertex is rejected");
    broken=binary(); u32(broken,84,0x7fc00000);
    rejected(broken,"binary nonfinite normal is rejected even though normals are not used");
    broken=binary(); f32(broken,124,0);
    rejected(broken,"binary duplicate vertex is degenerate");
    broken=ascii; broken.replace(broken.find("vertex 1"),8,"vertex nan");
    rejected(broken,"ASCII nonfinite coordinate is rejected");
    broken=ascii; broken.replace(broken.find("normal 0"),8,"normal inf");
    rejected(broken,"ASCII nonfinite normal is rejected");
    broken=ascii; broken.replace(broken.find("vertex 0 1 0"),12,"vertex 2 0 0");
    rejected(broken,"ASCII collinear triangle is rejected");
    broken=ascii; broken.erase(broken.find("endfacet"),8);
    rejected(broken,"ASCII missing facet terminator is rejected");
    broken=ascii; broken.insert(broken.find("endloop"),"vertex 0 0 1\n");
    rejected(broken,"ASCII facet must have exactly three vertices");
    rejected("solid sample\n"+facet,"ASCII missing endsolid is rejected");
    rejected(ascii+"unexpected\n","ASCII text after endsolid is rejected");
    rejected("solid sample name\n"+facet+"endsolid sample name unexpected\n","ASCII closing-line trailing text is rejected");
    rejected("solid sample\nendsolid sample\n","ASCII zero facets are rejected");
    rejected(std::string(84,'\0'),"binary zero facets are rejected");
    rejected("","empty file is rejected");

    auto two=binary(); two+=binary().substr(84); u32(two,80,2);
    require(bool(parse_stl(two,2)),"explicit triangle budget accepts its exact bound");
    rejected(two,"binary triangle budget is enforced",1);
    rejected("solid sample name\n"+facet+facet+"endsolid sample name\n","ASCII triangle budget is enforced",1);
    rejected(ascii,"zero triangle limit is rejected",0);
    rejected(ascii,"caller cannot increase global triangle limit",stl_max_triangles+1);
    std::string oversized_count(84+50*(stl_max_triangles+1),'\0');
    u32(oversized_count,80,static_cast<std::uint32_t>(stl_max_triangles+1));
    rejected(oversized_count,"binary count over 200000 is rejected before geometry allocation");
    rejected(std::string(stl_max_file_bytes+1,' '),"file size over 32 MiB is rejected");
    std::cout<<"STL checks passed: ASCII/binary grammar, winding, finite geometry and bounded input.\n";
}
