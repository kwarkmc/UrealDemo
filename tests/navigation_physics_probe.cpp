#include "Navigation.h"
#include "ResearchProfile.h"
#include <fstream>
#include <iomanip>
#include <iostream>
using namespace flight_core;
int main(int argc,char** argv) {
    if(argc!=2 && argc!=3) return 2;
    std::ofstream out(argv[1]);if(!out) return 2;
    out<<std::setprecision(16)<<"case,time_s,loiter,x_m,y_m,z_m,tas_mps,bank_deg,beta_deg,rudder_deg,radius_m,ideal_radius_m,radial_accel_mps2,lift_radial_mps2,side_radial_mps2,thrust_drag_radial_mps2,vertical_accel_mps2\n";
    auto p=research_profile(argc==3 ? argv[2] : RESEARCH_PROFILE);Environment env;env.model=&p;
    for(int which=0;which<4;++which) {
        Navigation nav;nav.loiter_radius_m=1000;
        nav.waypoints={{{which==0 ? 1000. : which==1 ? -1000. : 0.,which==2 ? -1000. : which==3 ? 3000. : 0.,2600},65}};
        auto trim=level_trim({0,0,2600},0,65,env);if(!trim) return 3;auto a=trim->aircraft;
        for(int i=0;i<=30000;++i) {
            auto c=guide(nav,a,.01,env);if(!c) return 4;
            if(i%100==0) {
                using namespace fixed_wing_detail;
                const auto data=air_data(a,env);const auto v=velocity_enu(a);const auto q=orientation(a);
                const double vh=std::hypot(v.x,v.y);const Vec3 right{v.y/vh,-v.x/vh,0};
                auto world=[&](Vec3 f){return enu(rotate(q,f))*(1/p.mass_kg);};
                const auto total=world(data.force_body_n+Vec3{data.thrust_n,0,0});
                const double radial=dot(total,right);
                const auto lift=world(Vec3{std::sin(data.alpha_rad),0,-std::cos(data.alpha_rad)}*data.lift_n);
                const auto side=world(Vec3{-std::cos(data.alpha_rad)*std::sin(data.beta_rad),std::cos(data.beta_rad),-std::sin(data.alpha_rad)*std::sin(data.beta_rad)}*data.side_force_n);
                out<<which<<','<<a.time_s<<','<<nav.loitering<<','<<a.position_enu_m.x<<','<<a.position_enu_m.y<<','<<a.position_enu_m.z<<','<<data.speed_mps<<','<<a.roll_rad/rad<<','<<data.beta_rad/rad<<','<<c->rudder_deg<<','
                    <<(std::abs(radial)>1e-9 ? vh*vh/radial : 0)<<','<<(std::abs(a.roll_rad)>1e-9 ? data.speed_mps*data.speed_mps/(gravity_mps2*std::tan(a.roll_rad)) : 0)<<','<<radial<<','<<dot(lift,right)<<','<<dot(side,right)<<','<<radial-dot(lift+side,right)<<','<<total.z-gravity_mps2<<'\n';
            }
            auto next=step_aircraft(a,*c,.01,env);if(!next) return 5;a=*next;
        }
        std::cout<<"case "<<which<<" loiter="<<nav.loitering<<" endpoint="<<a.position_enu_m.x<<','<<a.position_enu_m.y<<'\n';
    }
}
