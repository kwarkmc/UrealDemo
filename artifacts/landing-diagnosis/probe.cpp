// Isolated diagnosis: existing flight equations/controller, flat ground; no live Unreal inputs.
#define main ExistingResearchCheckMain
#include "../../tests/research_checks.cpp"
#undef main
#include <iomanip>

int main() {
    const auto model=profile(); Environment env; env.model=&model;
    const GroundPlane flat{{},{0,0,1},true};
    const Runway runway{{},{0,1,0},{0,0,1},2000,60};
    struct Case { const char* name; Vec3 position; double heading; };
    std::cout<<"case,accepted,first_command_valid,first_phase,first_flap_target_deg,first_waypoint_height_m,first_target_tas_mps,aileron_reversals_over_1deg,max_abs_aileron_deg,end_phase,end_wp,end_height_m,crashed\n";
    std::ofstream feasibility("trim-feasibility.csv");
    feasibility<<"case,height_m,target_tas_mps,actual_flaps_deg,level_trim_available\n";
    for (const auto test:{Case{"high_mission",{5000,5000,2600},0},Case{"mission_800m",{5000,5000,800},0},
                         Case{"mission_500m_away",{-5000,5000,500},3.14159265358979323846},Case{"low_aligned",{0,-600,60},0}}) {
        for(double flap:{0.,10.,30.}) feasibility<<test.name<<','<<test.position.z<<",40,"<<flap<<','<<bool(level_trim(test.position,test.heading,40,env,0,flap))<<'\n';
        const auto trim=level_trim(test.position,test.heading,65,env);
        require(trim.has_value(),"diagnostic initial trim exists");
        auto aircraft=trim->aircraft;
        auto operation=start_landing(aircraft,runway,env,flat,40);
        if (!operation) { std::cout<<test.name<<",false,,,,,,,,,,,\n"; continue; }
        const auto first_phase=operation_phase_name(operation->phase);
        auto first=update_flight_operation(*operation,aircraft,env,flat,.01);
        if(!first) { std::cout<<test.name<<",true,false,"<<first_phase<<",,,,,,,,,\n"; continue; }
        const double first_flap=first->flap_deg.value_or(-1),first_height=operation->navigation.waypoints[0].position_enu_m.z;
        int reversals=0,last_sign=0; double maximum=0;
        std::ofstream csv(std::string(test.name)+".csv");
        csv<<"time_s,phase,waypoint,altitude_m,tas_mps,roll_deg,aileron_command_deg,aileron_actual_deg,flap_actual_deg\n";
        for(int step=0;step<18000 && operation_active(*operation);++step) {
            const auto command=step==0 ? first : update_flight_operation(*operation,aircraft,env,flat,.01);
            require(command.has_value(),"diagnostic valid guidance");
            if(command->flap_deg) require(request_flaps(aircraft,*command->flap_deg,env),"diagnostic flaps");
            if(command->gear_down) require(request_gear(aircraft,*command->gear_down,env),"diagnostic gear");
            const double aileron=command->flight.aileron_deg;
            maximum=std::max(maximum,std::abs(aileron));
            const int sign=std::abs(aileron)>1 ? (aileron>0 ? 1:-1):0;
            if(sign && last_sign && sign!=last_sign) ++reversals;
            if(sign) last_sign=sign;
            const auto next=step_aircraft(aircraft,command->flight,.01,env,flat,command->ground);
            require(next.has_value(),"diagnostic finite integration"); aircraft=*next;
            if(step%10==0) csv<<aircraft.time_s<<','<<operation_phase_name(operation->phase)<<','<<operation->navigation.active_index<<','
                <<aircraft.position_enu_m.z<<','<<airspeed(aircraft,env)<<','<<aircraft.roll_rad*180/3.14159265358979323846<<','
                <<aileron<<','<<aircraft.actual_controls.aileron_deg<<','<<aircraft.flap_deg<<'\n';
            if(aircraft.ground.crashed) break;
        }
        std::cout<<test.name<<",true,true,"<<first_phase<<','<<first_flap<<','<<first_height<<",40,"<<reversals<<','<<maximum<<','
            <<operation_phase_name(operation->phase)<<','<<operation->navigation.active_index<<','<<aircraft.position_enu_m.z<<','<<aircraft.ground.crashed<<'\n';
    }
    return 0;
}
