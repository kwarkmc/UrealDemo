#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

// Display-only geometry: metres, +X nose, +Y right, +Z up. No aerodynamic data.
namespace propeller_visual {
constexpr double pi=3.14159265358979323846;
struct Vertex {double x,y,z,tip=0,alpha=1;};
struct Mesh {std::vector<Vertex> vertices;std::vector<int> indices;};
inline void quad(Mesh& m,int a,int b,int c,int d) {m.indices.insert(m.indices.end(),{a,b,c,a,c,d});}
inline Mesh blades(double radius) {
    Mesh m;if(!std::isfinite(radius) || radius<=0) return m;
    // Rounded cross-sections, swept/tapered tip and decreasing geometric twist.
    constexpr std::array<double,9> stations{.12,.20,.32,.48,.65,.80,.92,.98,1};
    constexpr std::array<double,9> widths{.045,.075,.105,.115,.110,.090,.065,.035,.003};
    constexpr int sides=12;
    for(int blade=0;blade<3;++blade) {
        const int start=static_cast<int>(m.vertices.size());
        const double angle=2*pi*blade/3,sa=std::sin(angle),ca=std::cos(angle);
        for(std::size_t station=0;station<stations.size();++station) {
            const double r=stations[station],w=widths[station],twist=(32-20*r)*pi/180;
            for(int side=0;side<sides;++side) {
                const double theta=2*pi*side/sides;
                const double chord=w*std::cos(theta),thickness=.10*w*std::sin(theta);
                const double x=chord*std::sin(twist)+thickness*std::cos(twist);
                const double y=chord*std::cos(twist)-thickness*std::sin(twist)-.07*r*r;
                const double z=std::sqrt(std::max(0.,r*r-y*y));
                m.vertices.push_back({radius*x,radius*(y*ca-z*sa),radius*(y*sa+z*ca),r>=.92 ? 1. : 0.,1});
            }
        }
        for(int ring=0;ring<static_cast<int>(stations.size())-1;++ring) for(int side=0;side<sides;++side) {
            const int next=(side+1)%sides;
            quad(m,start+ring*sides+side,start+(ring+1)*sides+side,start+(ring+1)*sides+next,start+ring*sides+next);
        }
        for(int side=1;side<sides-1;++side) {
            m.indices.insert(m.indices.end(),{start,start+side,start+side+1});
            const int end=start+(static_cast<int>(stations.size())-1)*sides;
            m.indices.insert(m.indices.end(),{end,end+side+1,end+side});
        }
    }
    return m;
}
inline Mesh spinner(double radius) {
    Mesh m;if(!std::isfinite(radius) || radius<=0) return m;
    constexpr int rings=12,sides=48;
    for(int ring=0;ring<=rings;++ring) {
        const double t=double(ring)/rings;
        const double r=.14*radius*std::sqrt(std::max(0.,1-t*t));
        for(int side=0;side<sides;++side) {
            const double angle=2*pi*side/sides;
            m.vertices.push_back({(.34*t-.04)*radius,r*std::sin(angle),r*std::cos(angle)});
        }
    }
    for(int ring=0;ring<rings;++ring) for(int side=0;side<sides;++side)
        quad(m,ring*sides+side,ring*sides+(side+1)%sides,(ring+1)*sides+(side+1)%sides,(ring+1)*sides+side);
    return m;
}
inline Mesh blur(double radius) {
    Mesh m;if(!std::isfinite(radius) || radius<=0) return m;
    constexpr std::array<double,6> rings{.13,.20,.45,.80,.96,1};
    constexpr std::array<double,6> alpha{0,.35,.65,.55,.35,0};
    constexpr int sides=128;
    for(std::size_t ring=0;ring<rings.size();++ring) for(int side=0;side<sides;++side) {
        const double angle=2*pi*side/sides;
        m.vertices.push_back({-.002*radius,radius*rings[ring]*std::sin(angle),radius*rings[ring]*std::cos(angle),0,alpha[ring]});
    }
    for(int ring=0;ring<static_cast<int>(rings.size())-1;++ring) for(int side=0;side<sides;++side)
        quad(m,ring*sides+side,ring*sides+(side+1)%sides,(ring+1)*sides+(side+1)%sides,(ring+1)*sides+side);
    return m;
}
inline double blur_fraction(double revolutions_per_second) {
    const double t=std::clamp((revolutions_per_second-2)/3,0.,1.);
    return t*t*(3-2*t);
}
} // namespace propeller_visual
