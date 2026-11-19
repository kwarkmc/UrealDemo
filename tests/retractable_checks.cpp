#include "FlightOperations.h"
#include "ResearchProfile.h"
#include "../Source/UAVSim/LandingGearVisual.h"
#include <iostream>
#include <cstdlib>
using namespace flight_core;
void require(bool ok,const char* why) {if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main() {
    auto p=research_profile();Environment env;env.model=&p;
    require(p.retractable_gear && valid_model_parameters(p),"retractable research profile valid");
    for(int i=0;i<3;++i) {
        const auto root=landing_gear_visual::root(p,i),end=landing_gear_visual::center(p,i);
        const auto contact=wheel_points(p)[i];
        require(std::abs(end.x-contact.x)<1e-12 && std::abs(end.y-contact.y)<1e-12
            && std::abs(-end.z+p.wheel_radius_m-contact.z)<1e-12,"deployed wheel bottom equals physical contact point");
        require(std::abs(root.y)<p.chord_m*.4 && std::abs(root.z)<p.body_half_height_m,"hinge is attached to fuselage");
    }
    const GroundPlane flat{{0,0,0},{0,0,1},true};const Runway runway{{0,0,0},{0,1,0},{0,0,1},2000,60};
    auto a=*ground_initial_condition({0,30,0},0,0,flat,env);
    require(!request_gear(a,false,env),"ground retraction denied");
    auto op=start_takeoff(a,runway,env,flat,80,55);require(op.has_value(),"takeoff accepted");
    bool up=false,landing=false,down_before_touchdown=false;
    for(int i=0;i<160000 && op->phase!=FlightOperationPhase::complete;++i) {
        auto command=update_flight_operation(*op,a,env,flat,.01);require(command.has_value(),"supported flight command");
        if(command->gear_down) require(request_gear(a,*command->gear_down,env),"automatic gear request valid");
        if(command->flap_deg) require(request_flaps(a,*command->flap_deg,env),"automatic flap request valid");
        auto next=step_aircraft(a,command->flight,.01,env,flat,command->ground);
        require(next && !next->ground.crashed,"retractable research operation advances without crash");a=*next;
        up=up || gear_up_locked(a.gear);
        if(!landing && op->phase==FlightOperationPhase::hold && up) {
            op=start_landing(a,runway,env,flat,40);require(op.has_value(),"return with retracted gear accepted");landing=true;
        }
        if(landing && !a.ground.on_ground && gear_down_locked(a.gear)) down_before_touchdown=true;
    }
    require(up && landing && down_before_touchdown && op->phase==FlightOperationPhase::complete
        && a.ground.on_ground && gear_down_locked(a.gear),"automatic retract, return, deploy, land and park");
    std::cout<<"Hypothetical retractable derivative: automatic cycle passed at "<<a.time_s<<"s.\n";
}
