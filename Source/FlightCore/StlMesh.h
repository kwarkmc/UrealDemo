#pragma once

#include "FlightCore.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace flight_core {

inline constexpr std::size_t stl_max_file_bytes=32*1024*1024, stl_max_triangles=200000;

struct StlMeshResult {
    std::vector<Vec3> vertices; // Triangle soup in source coordinates/units, preserving vertex winding.
    std::string error;         // Failure returns no partial mesh. Supplied normals are never trusted.
    explicit operator bool() const { return error.empty(); }
};

namespace stl_detail {
inline StlMeshResult fail(const char* error) { return {{},error}; }
inline bool space(char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n' || c=='\f' || c=='\v'; }
inline std::string_view trim(std::string_view text) {
    while (!text.empty() && space(text.front())) text.remove_prefix(1);
    while (!text.empty() && space(text.back())) text.remove_suffix(1);
    return text;
}

class Tokens {
    std::string_view bytes;
    std::size_t position=0;
public:
    explicit Tokens(std::string_view source):bytes(source) {}
    std::string_view next() {
        while (position<bytes.size() && space(bytes[position])) ++position;
        const auto start=position;
        while (position<bytes.size() && !space(bytes[position])) ++position;
        return bytes.substr(start,position-start);
    }
    bool take(std::string_view expected) { return next()==expected; }
    std::string_view line_name() {
        const auto start=position;
        while (position<bytes.size() && bytes[position]!='\r' && bytes[position]!='\n') ++position;
        return trim(bytes.substr(start,position-start));
    }
    bool number(double& value) {
        auto token=next();
        if (!token.empty() && token.front()=='+') {
            token.remove_prefix(1);
            if (!token.empty() && (token.front()=='+' || token.front()=='-')) return false;
        }
        if (token.empty()) return false;
        const auto parsed=std::from_chars(token.data(),token.data()+token.size(),value,std::chars_format::general);
        return parsed.ec==std::errc{} && parsed.ptr==token.data()+token.size() && std::isfinite(value);
    }
    bool vector(Vec3& value) { return number(value.x) && number(value.y) && number(value.z); }
};

inline std::uint32_t little_u32(const char* bytes) {
    const auto* p=reinterpret_cast<const unsigned char*>(bytes);
    return std::uint32_t(p[0]) | (std::uint32_t(p[1])<<8) | (std::uint32_t(p[2])<<16) | (std::uint32_t(p[3])<<24);
}
inline double little_float(const char* bytes) {
    static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"STL requires IEEE-754 float32");
    const auto bits=little_u32(bytes);
    float value=0;
    std::memcpy(&value,&bits,sizeof(value)); // No alignment or aliasing assumptions about input bytes.
    return value;
}
inline bool nondegenerate(const std::array<Vec3,3>& triangle) {
    if (!finite(triangle[0]) || !finite(triangle[1]) || !finite(triangle[2])) return false;
    auto u=triangle[1]-triangle[0], v=triangle[2]-triangle[0];
    if (!finite(u) || !finite(v)) return false;
    const double scale=std::max({std::abs(u.x),std::abs(u.y),std::abs(u.z),std::abs(v.x),std::abs(v.y),std::abs(v.z)});
    if (scale==0) return false;
    // Scale before the area test to avoid overflow from otherwise finite coordinates.
    u={u.x/scale,u.y/scale,u.z/scale}; v={v.x/scale,v.y/scale,v.z/scale};
    const Vec3 cross{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
    return std::hypot(cross.x,std::hypot(cross.y,cross.z))>0;
}
}

// One solid only. ASCII keywords are lowercase; the opening name may contain
// spaces, and an optional closing name must match. Binary attribute/color bytes
// are ignored. Exact binary length takes precedence even for a "solid" header.
// triangle_limit must be in [1,200000]; it can tighten, never increase the budget.
inline StlMeshResult parse_stl(std::string_view bytes,std::size_t triangle_limit=stl_max_triangles) {
    using namespace stl_detail;
    if (bytes.empty() || bytes.size()>stl_max_file_bytes) return fail("STL must contain 1 byte to 32 MiB.");
    if (triangle_limit==0 || triangle_limit>stl_max_triangles) return fail("STL triangle limit must be in [1,200000].");
    StlMeshResult result;
    if (bytes.size()>=84) {
        const auto count=little_u32(bytes.data()+80);
        const std::uint64_t expected=84+50*std::uint64_t(count);
        if (expected==bytes.size()) {
            if (count==0 || count>triangle_limit) return fail("Binary STL triangle count is empty or exceeds the budget.");
            result.vertices.reserve(std::size_t(count)*3);
            for (std::size_t i=0;i<count;++i) {
                const char* facet=bytes.data()+84+50*i;
                for (std::size_t component=0;component<3;++component)
                    if (!std::isfinite(little_float(facet+4*component))) return fail("Binary STL normal is not finite.");
                std::array<Vec3,3> triangle;
                for (std::size_t vertex=0;vertex<3;++vertex) {
                    const char* point=facet+12+12*vertex;
                    triangle[vertex]={little_float(point),little_float(point+4),little_float(point+8)};
                }
                if (!nondegenerate(triangle)) return fail("Binary STL has nonfinite coordinates or a degenerate triangle.");
                result.vertices.insert(result.vertices.end(),triangle.begin(),triangle.end());
            }
            return result;
        }
    }
    for (const unsigned char byte:bytes)
        if ((byte<32 || byte>126) && !space(static_cast<char>(byte)))
            return fail("STL is not ASCII, or binary length differs from 84 + 50 * triangle count.");
    Tokens tokens(bytes);
    if (!tokens.take("solid")) return fail("ASCII STL must begin with solid.");
    const auto name=tokens.line_name();
    while (true) {
        const auto keyword=tokens.next();
        if (keyword=="endsolid") {
            const auto closing_name=tokens.line_name();
            if ((!closing_name.empty() && closing_name!=name) || !tokens.next().empty())
                return fail("ASCII STL has a mismatched closing name or trailing text.");
            if (result.vertices.empty()) return fail("ASCII STL contains no facets.");
            return result;
        }
        if (keyword!="facet" || !tokens.take("normal")) return fail("ASCII STL expected facet normal or endsolid.");
        if (result.vertices.size()/3>=triangle_limit) return fail("ASCII STL triangle count exceeds the budget.");
        Vec3 ignored_normal;
        if (!tokens.vector(ignored_normal)) return fail("ASCII STL normal must contain three finite numbers.");
        if (!tokens.take("outer") || !tokens.take("loop")) return fail("ASCII STL expected outer loop.");
        std::array<Vec3,3> triangle;
        for (auto& point:triangle)
            if (!tokens.take("vertex") || !tokens.vector(point)) return fail("ASCII STL expected exactly three finite vertices.");
        if (!tokens.take("endloop") || !tokens.take("endfacet")) return fail("ASCII STL expected endloop and endfacet.");
        if (!nondegenerate(triangle)) return fail("ASCII STL has a degenerate or unrepresentable triangle.");
        result.vertices.insert(result.vertices.end(),triangle.begin(),triangle.end());
    }
}

} // namespace flight_core
