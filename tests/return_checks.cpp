#include "FlightOperations.h"
#include "ResearchProfile.h"
#include <iostream>
#include <cstdlib>

using namespace flight_core;
void require(bool value,const char* reason) { if(!value) { std::cerr<<reason<<'\n';std::exit(1); } }
int main() {
    auto model=research_profile(); Environment env; env.model=&model;
    const GroundPlane flat{{},{0,0,1},true}; const Runway runway{{},{0,1,0},{0,0,1},2000,60};
    for(const auto position:{Vec3{5000,5000,2600},Vec3{-5000,5000,500},Vec3{5000,5000,800},Vec3{0,-600,60}}) {
        auto a=level_trim(position,position.x<0 ? 3.14159265358979323846 : 0,65,env)->aircraft;
        auto op=start_landing(a,runway,env,flat,40);
        require(op.has_value(),"supported mission landing accepted");
        require(op->phase==FlightOperationPhase::returning,"unconfigured mission begins with return");
        const auto first=update_flight_operation(*op,a,env,flat,.01);
        require(first.has_value() && first->flap_deg==0,"first command retains clean high-speed configuration");
        auto previous=op->phase;
        for(int i=0;i<180000 && operation_active(*op);++i) {
            auto command=update_flight_operation(*op,a,env,flat,.01);
            require(command.has_value(),"return controller always produces a supported command");
            if(command->flap_deg) require(request_flaps(a,*command->flap_deg,env),"valid flap schedule");
            require(request_gear(a,true,env),"fixed gear stays down");
            auto next=step_aircraft(a,command->flight,.01,env,flat,command->ground);
            if(!next || next->ground.crashed) {
                std::cerr<<"failure phase="<<operation_phase_name(op->phase)<<" t="<<a.time_s<<" xyz="<<a.position_enu_m.x<<','<<a.position_enu_m.y<<','<<a.position_enu_m.z
                    <<" v="<<airspeed(a,env)<<" pitch="<<a.pitch_rad<<" flap="<<a.flap_deg<<'\n';
                require(false,"return physics failed");
            }
            a=*next;
            if(previous!=op->phase) {
                std::cout<<operation_phase_name(op->phase)<<" t="<<a.time_s<<" xyz="<<a.position_enu_m.x<<','<<a.position_enu_m.y<<','<<a.position_enu_m.z<<" v="<<airspeed(a,env)<<'\n';
                previous=op->phase;
            }
        }
        if(op->phase!=FlightOperationPhase::complete) std::cerr<<"end phase "<<operation_phase_name(op->phase)<<" xyz="<<a.position_enu_m.x<<','<<a.position_enu_m.y<<','<<a.position_enu_m.z<<" bank="<<a.roll_rad<<'\n';
        require(op->phase==FlightOperationPhase::complete && a.ground.on_ground,"continuous return lands and parks");
    }
    std::cout<<"Return checks passed: research control, not real aircraft performance validation.\n";
}
