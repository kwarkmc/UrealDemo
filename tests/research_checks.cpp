#include "FlightOperations.h"
#include "ResearchProfile.h"
#include <fstream>
#include <iostream>
#include <regex>
#include <cstdlib>

using namespace flight_core;
void require(bool ok,const char* message) {
    if (!ok) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
void trace(const FlightOperation& op,const Aircraft& a) {
    std::cout<<operation_phase_name(op.phase)<<" t="<<a.time_s<<" xyz="<<a.position_enu_m.x<<','<<a.position_enu_m.y<<','<<a.position_enu_m.z
        <<" wp="<<op.navigation.active_index<<" V="<<airspeed(a)<<" pitch="<<a.pitch_rad<<" crash="<<a.ground.body_strike<<","<<a.ground.gear_overload<<" impact="<<a.ground.impact_speed_mps<<" flap="<<a.flap_deg<<'\n';
}
Aircraft fly(FlightOperation& op,Aircraft a,Environment env,GroundPlane flat,double duration,bool hold=false) {
    auto previous=op.phase;
    for (int i=0;i<duration/.01 && operation_active(op);++i) {
        const auto command=update_flight_operation(op,a,env,flat,.01);
        if (!command) trace(op,a);
        require(command.has_value(),"research guidance has valid command");
        if (command->gear_down) require(request_gear(a,*command->gear_down,env),"fixed gear respected by automation");
        if (command->flap_deg) require(request_flaps(a,*command->flap_deg,env),"automatic flap command accepted");
        const auto next=step_aircraft(a,command->flight,.01,env,flat,command->ground);
        if (!next) trace(op,a);
        require(next.has_value(),"research physics advances"); a=*next;
        if (previous!=op.phase) { trace(op,a); previous=op.phase; }
        if (a.ground.crashed) trace(op,a);
        require(!a.ground.crashed,"research operation avoids crash");
        if (hold && op.phase==FlightOperationPhase::hold) break;
    }
    trace(op,a); return a;
}
int main() {
    auto p=research_profile(); require(valid_model_parameters(p),"research assumptions pass parameter validation");
    Environment env; env.model=&p;
    const auto trim=level_trim({0,0,2600},0,65,env);
    require(trim.has_value(),"research cruise trims");
    const auto loads=air_data(trim->aircraft,env);
    require(fixed_wing_detail::norm(loads.moment_body_nm)/(p.mass_kg*gravity_mps2*p.span_m)<1e-12,"normalized trim moment residual");
    const auto next=step_aircraft(trim->aircraft,trim->controls,1,env);
    require(next && std::abs(airspeed(*next,env)-65)<1e-7,"cruise equilibrium maintained");
    require(maximum_thrust(80,1.225,p)*80<=p.shaft_power_w*p.propeller_efficiency+1e-8,"propulsive power cannot exceed assumed shaft power efficiency");
    require(maximum_thrust(0,1.225,p)==p.mass_kg*p.maximum_thrust_mps2,"finite static thrust limit");
    auto cut=env; cut.engine_power_fraction=0;
    require(air_data(trim->aircraft,cut).thrust_n==0,"engine cut has no thrust");
    auto flap=trim->aircraft; require(request_flaps(flap,30,env) && !request_flaps(flap,31,env),"flap input range");
    auto moved=step_aircraft(flap,trim->controls,.5,env);
    require(moved && moved->flap_deg>4.9 && moved->flap_deg<=5.0,"flap slew advances only with simulation time");
    require(!request_gear(flap,false,env),"fixed gear rejects retraction");
    Navigation route; route.waypoints={{{0,3000,2600},65}}; route.loiter_radius_m=1000;
    auto guided=trim->aircraft;
    for(int i=0;i<6000;++i) {
        const auto command=guide(route,guided,.01,env);
        require(command.has_value(),"research route controller command");
        const auto state=step_aircraft(guided,*command,.01,env);
        require(state.has_value(),"research route physics"); guided=*state;
    }
    require(std::abs(guided.position_enu_m.z-2600)<20 && std::abs(airspeed(guided,env)-65)<5,"route maintains altitude and TAS through capture and turn");
    require(route.loitering,"single waypoint transitions to research loiter");
    auto pulse=trim->controls; pulse.elevator_deg+=1;
    const auto manual=step_aircraft(trim->aircraft,pulse,.5,env);
    require(manual && manual->body_rates_rps.y<0,"manual down-elevator produces nose-down response");
    auto deflected=trim->aircraft;
    deflected.actual_controls=pulse;
    const double moment_delta=air_data(deflected,env).moment_body_nm.y-loads.moment_body_nm.y;
    const double analytical_acceleration=loads.dynamic_pressure_pa*p.wing_area_m2*p.chord_m*p.pitch_elevator
        *(3.14159265358979323846/180)/p.iy;
    require(std::abs(moment_delta/p.iy-analytical_acceleration)<1e-12,
        "frozen-state pitch acceleration agrees with coefficient and inertia calculation");
    for (bool rudder:{false,true}) {
        auto command=trim->controls;
        if (rudder) command.rudder_deg+=1; else command.aileron_deg+=1;
        auto lateral=trim->aircraft;
        lateral.actual_controls=command;
        const auto acceleration=fixed_wing_detail::derivative({lateral,fixed_wing_detail::orientation(lateral)},command,env).a.body_rates_rps;
        const double scale=loads.dynamic_pressure_pa*p.wing_area_m2*p.span_m;
        const double expected_roll=scale*(rudder ? p.roll_rudder : p.roll_aileron)*(3.14159265358979323846/180);
        const double expected_yaw=scale*(rudder ? p.yaw_rudder : p.yaw_aileron)*(3.14159265358979323846/180);
        require(std::abs((p.ix*acceleration.x-p.ixz*acceleration.z-expected_roll)/scale)<1e-12
            && std::abs((p.iz*acceleration.z-p.ixz*acceleration.x-expected_yaw)/scale)<1e-12,
            "lateral initial acceleration obeys coefficient moments and coupled inertia balance");
    }
    auto perturbed=p; perturbed.iy*=1.1;
    Environment perturbed_env=env; perturbed_env.model=&perturbed;
    const auto angular=fixed_wing_detail::derivative({deflected,fixed_wing_detail::orientation(deflected)},pulse,env).a.body_rates_rps;
    const auto slower=fixed_wing_detail::derivative({deflected,fixed_wing_detail::orientation(deflected)},pulse,perturbed_env).a.body_rates_rps;
    require(valid_model_parameters(perturbed) && std::abs(slower.y*1.1-angular.y)<1e-12,
        "Iy-only perturbation changes initial pitch acceleration inversely");
    auto response=[&](double h) {
        auto a=trim->aircraft;
        for(int i=0;i<static_cast<int>(2/h);++i) {
            const auto command=i*h>=.5 && i*h<1 ? pulse : trim->controls;
            const auto state=step_aircraft(a,command,h,env);
            require(state.has_value(),"time-step response integrates");
            a=*state;
        }
        return a;
    };
    const auto coarse=response(.01),medium=response(.005),fine=response(.0025);
    const double angle_coarse=std::abs(coarse.pitch_rad-medium.pitch_rad),angle_fine=std::abs(medium.pitch_rad-fine.pitch_rad);
    const double speed_coarse=std::abs(airspeed(coarse,env)-airspeed(medium,env));
    const double speed_fine=std::abs(airspeed(medium,env)-airspeed(fine,env));
    std::cout<<"Step refinement: pitch(rad) "<<angle_coarse<<" -> "<<angle_fine
        <<"; speed(m/s) "<<speed_coarse<<" -> "<<speed_fine<<'\n';
    // 1e-12 is a floating-point floor; larger differences must decrease with refinement.
    require(angle_fine<=std::max(1e-12,angle_coarse*.8) && speed_fine<=std::max(1e-12,speed_coarse*.8),
        "actual integrator step refinement reduces response differences");
    Environment gust=env; gust.gust_duration_s=2; gust.gust_peak_enu_mps={1,2,.5};
    const auto one=step_aircraft(trim->aircraft,trim->controls,1,gust);
    const auto two=step_aircraft(trim->aircraft,trim->controls,1,gust);
    require(one && two && fixed_wing_detail::norm(one->position_enu_m-two->position_enu_m)==0,"research gust replay deterministic");
    const GroundPlane flat{{},{0,0,1},true};
    const Runway runway{{},{0,1,0},{0,0,1},2000,60};
    auto parked=ground_initial_condition({0,30,0},0,0,flat,env);
    require(parked && std::abs(parked->position_enu_m.z-p.cg_height_m)<1e-12,"CG placed at selected gear height");
    auto rest=step_aircraft(*parked,{0,0,0,0},1,env,flat,{0,0,true});
    require(rest && std::abs(rest->ground.normal_force_n/(p.mass_kg*gravity_mps2)-1)<1e-5,"ground reaction balances weight");
    auto takeoff=start_takeoff(*parked,runway,env,flat,80,55);
    require(takeoff.has_value(),"research takeoff arms");
    auto airborne=fly(*takeoff,*parked,env,flat,180,true);
    require(takeoff->phase==FlightOperationPhase::hold && airborne.position_enu_m.z>75,"research takeoff reaches hold");
    auto landing=start_landing(airborne,runway,env,flat,40);
    require(landing.has_value(),"research return landing arms");
    auto stopped=fly(*landing,airborne,env,flat,1800);
    require(landing->phase==FlightOperationPhase::complete && stopped.ground.on_ground
        && fixed_wing_detail::norm(velocity_enu(stopped))<.2,"research round trip parks");
    const auto approach=level_trim({30,-600,12},0,40,env,0,30);
    require(approach.has_value(),"unstable approach fixture trims");
    auto unstable=start_landing(approach->aircraft,runway,env,flat,40);
    require(unstable.has_value(),"unstable approach can be armed for recovery");
    auto recovery=fly(*unstable,approach->aircraft,env,flat,60);
    require(unstable->go_arounds>=1 && recovery.position_enu_m.z>65,"unstable low approach physically goes around");
    auto reject=start_takeoff(*parked,runway,env,flat,80,55); env.engine_power_fraction=0;
    auto rejected=fly(*reject,*parked,env,flat,5);
    require(reject->phase==FlightOperationPhase::aborted && rejected.ground.on_ground,"engine failure rejects takeoff and parks");
    std::cout<<"Research assumptions checks passed; no real C208B performance validation.\n";
}
