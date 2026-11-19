#include "FixedWing.h"
#include "ResearchProfile.h"
#include <cstdlib>
#include <iostream>
using namespace flight_core;
void require(bool ok,const char* reason) { if(!ok) {std::cerr<<reason<<'\n';std::exit(1);} }
int main() {
    auto p=research_profile(); Environment env;env.model=&p;
    auto trim=level_trim({0,0,500},0,65,env);
    require(trim.has_value(),"offset thrust must trim");
    const auto actual=air_data(trim->aircraft,env);
    auto centered=p;centered.thrust_z_m=0;Environment zero=env;zero.model=&centered;
    const auto base=air_data(trim->aircraft,zero);
    require(std::abs(actual.moment_body_nm.y-base.moment_body_nm.y-p.thrust_z_m*actual.thrust_n)<1e-8,"r cross F sign and units");
    require(std::abs(actual.moment_body_nm.y)<1e-8,"pitch moment equilibrium");
    for(double speed:{0.,1.,40.,65.,110.}) for(double density:{.5,1.225}) {
        const auto thrust=maximum_thrust(speed,density,p);
        require(thrust>=0 && thrust<=p.mass_kg*p.maximum_thrust_mps2+1e-9,"static thrust cap");
        require(thrust*speed<=p.propeller_efficiency*p.shaft_power_w*density/1.225+1e-7,"propulsive power cap");
    }
    const GroundPlane flat{{0,0,0},{0,0,1},true};
    auto a=*ground_initial_condition({0,0,0},0,0,flat,env);
    auto initial=a;
    for(int i=0;i<100;++i) {
        auto next=step_aircraft(a,{0,0,0,0},.01,env,flat,{30,1,false});
        require(next.has_value(),"ground response advances");a=*next;
        require(a.actual_steering_deg<=30*a.time_s+1e-8,"steering rate bound");
        require(std::abs(a.actual_brake-(1-std::exp(-a.time_s/p.brake_tau_s)))<1e-10,"independent first-order brake solution");
    }
    require(!a.ground.crashed && std::abs(a.position_enu_m.z-p.cg_height_m)<.002,"parked research equilibrium");
    require(std::abs(a.ground.normal_force_n-p.mass_kg*gravity_mps2)<1,"reaction balances weight");
    auto moving=*ground_initial_condition({0,0,0},0,15,flat,env);
    auto fast=step_aircraft(moving,{0,0,0,0},.1,env,flat,{30,0,false});
    require(fast && fast->actual_steering_deg<3.1,"speed steering protection and rate response");
    auto roll=step_aircraft(moving,{0,0,0,0},.002,env,flat,{});
    require(roll && std::abs(roll->wheel_rotation_rad-roll->body_velocity_mps.x*.002/p.wheel_radius_m)<1e-10,"wheel display uses rolling distance divided by radius");
    auto stopped=step_aircraft(initial,{0,0,0,0},.01,env,flat,{0,0,true});
    require(stopped && stopped->actual_brake==1,"parking brake immediate lock");
    p.wheel_radius_m=p.cg_height_m;require(!valid_model_parameters(p),"impossible wheel geometry rejected");
    std::cout<<"Refinement equations and research assumptions checked; real performance unvalidated.\n";
}
