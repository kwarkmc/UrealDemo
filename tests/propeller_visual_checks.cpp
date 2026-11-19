#include "../Source/UAVSim/PropellerGeometry.h"
#include <cstdlib>
#include <iostream>
using namespace propeller_visual;
void require(bool ok,const char* why) {if(!ok) {std::cerr<<why<<'\n';std::exit(1);}}
int main() {
    for(double r:{.15,1.3}) {
        for(const auto& mesh:{blades(r),spinner(r),blur(r)}) {
            require(!mesh.vertices.empty() && mesh.indices.size()%3==0,"complete mesh triangles");
            for(int index:mesh.indices) require(index>=0 && index<static_cast<int>(mesh.vertices.size()),"mesh index bounds");
            for(const auto& v:mesh.vertices) require(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)
                && std::hypot(v.y,v.z)<=r+1e-12 && v.alpha>=0 && v.alpha<=1,"finite radius/opacity bounds");
        }
        const auto m=blades(r);double y=0,z=0;
        for(const auto& v:m.vertices) {y+=v.y;z+=v.z;}
        require(std::abs(y)+std::abs(z)<1e-10,"three blades balanced around shaft axis");
    }
    require(blades(0).vertices.empty() && blur(NAN).vertices.empty(),"invalid radius rejected");
    require(blur_fraction(0)==0 && blur_fraction(2)==0 && blur_fraction(5)==1 && blur_fraction(20)==1,"stopped/low/full blur limits");
    double previous=0;
    for(int i=0;i<=100;++i) {const double blend=blur_fraction(i*.1);require(blend>=previous && blend<=1,"smooth monotone crossfade");previous=blend;}
    std::cout<<"Display mesh radius, symmetry, indices and rotation crossfade checked.\n";
}
