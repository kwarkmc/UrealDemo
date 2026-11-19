#include "FixedWing.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

using namespace flight_core;
namespace {
void require(bool condition,const char* message) {
    if (!condition) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
double length(Vec3 v) { return std::hypot(v.x,std::hypot(v.y,v.z)); }
double dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Aircraft advance(Aircraft a,Controls command,double duration,Environment environment={}) {
    const auto next=step_aircraft(a,command,duration,environment);
    require(next.has_value(),"systems step stays finite");
    return *next;
}
}

int main() {
    require(valid_model_parameters(flight_model_parameters),"default configuration is valid");
    // Synthetic numerical fixture, NOT C208B or any measured airframe.
    auto large=flight_model_parameters;
    large.mass_kg=1000; large.wing_area_m2=15; large.span_m=10; large.chord_m=1.5;
    large.ix=1000; large.iy=1500; large.iz=1800; large.ixz=50;
    large.reference_speed_mps=65;
    large.operating={65,2600,55,90,1000,5000,15,15,true,false,false,false};
    Environment large_env; large_env.model=&large;
    const auto large_trim=level_trim({0,0,2600},0,65,large_env);
    require(valid_model_parameters(large) && large_trim.has_value(),"synthetic large model trims above old 45 m/s ceiling");
    require(!level_trim({0,0,2600},0,19,large_env),"profile speed range rejects old fixed trim");
    const auto large_loads=air_data(large_trim->aircraft,large_env,large_trim->controls);
    require(std::abs(large_loads.force_body_n.z/large.mass_kg+gravity_mps2*std::cos(large_trim->aircraft.pitch_rad))<1e-8,
        "large trim balances normal force independently of mass scale");
    const auto large_next=step_aircraft(large_trim->aircraft,large_trim->controls,1,large_env);
    require(large_next && std::abs(airspeed(*large_next,large_env)-65)<1e-7,"large trim sustains declared TAS");
    auto area_model=large; area_model.wing_area_m2*=2;
    Environment area_env=large_env; area_env.model=&area_model;
    require(std::abs(air_data(large_trim->aircraft,area_env).lift_n/air_data(large_trim->aircraft,large_env).lift_n-2)<1e-13,
        "large fixture force follows independent q*S scaling");
    auto heavy_model=large; heavy_model.mass_kg*=2;
    Environment heavy_env=large_env; heavy_env.model=&heavy_model;
    auto numerical_state=large_trim->aircraft;
    numerical_state.actual_controls={4,3,0,0};
    const auto base_derivative=fixed_wing_detail::derivative({numerical_state,fixed_wing_detail::orientation(numerical_state)}, {},large_env);
    const auto heavy_derivative=fixed_wing_detail::derivative({numerical_state,fixed_wing_detail::orientation(numerical_state)}, {},heavy_env);
    const auto gravity_body=enu_to_body(numerical_state,{0,0,-gravity_mps2});
    require(length((base_derivative.a.body_velocity_mps-gravity_body)*.5-(heavy_derivative.a.body_velocity_mps-gravity_body))<1e-12,
        "same aerodynamic force produces inverse-mass acceleration");
    auto inertial_model=large; inertial_model.ix*=2; inertial_model.iy*=2; inertial_model.iz*=2; inertial_model.ixz*=2;
    Environment inertial_env=large_env; inertial_env.model=&inertial_model;
    const auto inertial_derivative=fixed_wing_detail::derivative({numerical_state,fixed_wing_detail::orientation(numerical_state)}, {},inertial_env);
    require(length(base_derivative.a.body_rates_rps*.5-inertial_derivative.a.body_rates_rps)<1e-12,
        "same moment produces inverse-inertia angular acceleration at zero angular rate");
    auto unsupported=large_trim->aircraft;
    unsupported.body_velocity_mps={100,0,0};
    require(!step_aircraft(unsupported,{},.01,large_env),"unsupported TAS rejects without clamping");
    unsupported=large_trim->aircraft; unsupported.position_enu_m.z=999;
    require(!step_aircraft(unsupported,{},.01,large_env),"unsupported height rejects");
    require(!ground_initial_condition({},0,0,{{},{0,0,1},true},large_env),"large fixture cannot use Thor ground initializer");
    require(!step_aircraft(large_trim->aircraft,{},.01,large_env,{{0,0,2595},{0,0,1},true}),"unsupported contact stops before ground");
    const auto original_mass=flight_model_parameters.mass_kg;
    auto invalid_large=large; invalid_large.operating.automation_supported=true;
    require(!valid_model_parameters(invalid_large) && flight_model_parameters.mass_kg==original_mass,
        "automation without ground and navigation is rejected and Thor remains independent");
    for (const auto& field:parameter_fields) {
        auto invalid=flight_model_parameters;
        invalid.*(field.member)=std::numeric_limits<double>::quiet_NaN();
        require(!valid_model_parameters(invalid),"every registered parameter rejects nonfinite input");
        int occurrences=0;
        for (const auto& other:parameter_fields) occurrences+=std::string_view(field.name)==other.name;
        require(occurrences==1,"configuration parameter names are unique");
    }
    auto invalid_inertia=flight_model_parameters;
    invalid_inertia.ixz=1;
    require(!valid_model_parameters(invalid_inertia),"inertia must be positive definite");
    invalid_inertia=flight_model_parameters;
    invalid_inertia.ix=1; invalid_inertia.iz=1; invalid_inertia.iy=.1; invalid_inertia.ixz=.9;
    require(!valid_model_parameters(invalid_inertia),"principal inertia triangle inequalities include xz coupling");
    const auto trim=level_trim({0,0,2600},0);
    require(trim && trim->aircraft.actuator_initialized,"trim includes initialized physical actuators");
    Aircraft start=trim->aircraft;
    start.actual_controls={0,0,0,0};
    const auto engine=advance(start,{0,0,0,1},.3);
    require(std::abs(engine.actual_controls.throttle-(1-std::exp(-1.0)))<2e-14,
        "engine spool matches the independent first-order analytic solution");
    const auto servo=advance(start,{4,-3,2,0},.08);
    require(std::abs(servo.actual_controls.elevator_deg-4*(1-std::exp(-1.0)))<2e-14
        && std::abs(servo.actual_controls.aileron_deg+3*(1-std::exp(-1.0)))<2e-14,
        "unsaturated servos match first-order analytic responses");
    const auto limited=advance(start,{25,-25,0,0},.05);
    require(std::abs(limited.actual_controls.elevator_deg-6)<1e-12
        && std::abs(limited.actual_controls.aileron_deg+6)<1e-12,
        "large servo commands respect the 120 deg/s slew bound");
    const auto crossing=advance(start,{25,0,0,0},.2);
    const double transition=(25-120*.08)/120;
    require(std::abs(crossing.actual_controls.elevator_deg-(25-120*.08*std::exp(-(.2-transition)/.08)))<1e-12,
        "rate-limited response joins the exponential without overshoot");

    Environment jam;
    jam.elevator_jammed=true; jam.elevator_jam_deg=-7;
    jam.aileron_jammed=true; jam.aileron_jam_deg=3;
    jam.rudder_jammed=true; jam.rudder_jam_deg=-2;
    const auto stuck=advance(start,{20,-20,20,1},.2,jam);
    require(stuck.actual_controls.elevator_deg==-7 && stuck.actual_controls.aileron_deg==3
        && stuck.actual_controls.rudder_deg==-2,"jammed surfaces remain at captured positions");
    const auto actual=air_data(stuck,jam,{25,25,25,1});
    const auto alternate_command=air_data(stuck,jam,{-25,-25,-25,0});
    require(length(actual.force_body_n-alternate_command.force_body_n)==0
        && length(actual.moment_body_nm-alternate_command.moment_body_nm)==0
        && actual.thrust_n==alternate_command.thrust_n,
        "air-data loads use actual actuators instead of bypassing command lag");
    Environment cut; cut.engine_power_fraction=0;
    Environment half; half.engine_power_fraction=.5;
    require(air_data(engine,cut).thrust_n==0
        && std::abs(air_data(engine,half).thrust_n/air_data(engine).thrust_n-.5)<1e-15,
        "engine failure scales available thrust independently of spool state");
    require(!level_trim({0,0,2600},0,19,cut),"failed engine does not fabricate level trim");

    Environment gust{{1,-2,3},0};
    gust.gust_start_time_s=2; gust.gust_duration_s=4; gust.gust_peak_enu_mps={4,6,-2};
    require(length(wind_at(gust,1)-gust.wind_enu_mps)==0
        && length(wind_at(gust,2)-gust.wind_enu_mps)==0
        && length(wind_at(gust,4)-(gust.wind_enu_mps+gust.gust_peak_enu_mps))<1e-14
        && length(wind_at(gust,3)-(gust.wind_enu_mps+gust.gust_peak_enu_mps*.5))<1e-14
        && length(wind_at(gust,6)-gust.wind_enu_mps)==0,
        "gust endpoints, quarter-cycle and peak match the 1-cosine equation");
    auto gust_sample=trim->aircraft; gust_sample.time_s=4;
    const auto expected_air=gust_sample.body_velocity_mps-enu_to_body(gust_sample,wind_at(gust,4));
    require(length(relative_air_velocity_body(gust_sample,gust)-expected_air)<1e-14
        && std::abs(airspeed(gust_sample,gust)-length(expected_air))<1e-14
        && length(air_data(gust_sample,gust).relative_velocity_body_mps-expected_air)<1e-14,
        "airspeed and aerodynamic loads sample the same simulation-time gust");

    auto second_model=flight_model_parameters;
    second_model.wing_area_m2*=1.5; second_model.mass_kg*=1.2;
    second_model.engine_tau_s=.6;
    Environment modified; modified.model=&second_model;
    const auto baseline=air_data(start), changed=air_data(start,modified), unchanged=air_data(start);
    require(valid_model_parameters(second_model)
        && std::abs(changed.lift_n/baseline.lift_n-1.5)<1e-14
        && length(unchanged.force_body_n-baseline.force_body_n)==0,
        "two aircraft models retain independent aerodynamic parameters");
    const auto slow_engine=advance(start,{0,0,0,1},.3,modified);
    require(std::abs(slow_engine.actual_controls.throttle-(1-std::exp(-.5)))<1e-14
        && engine.actual_controls.throttle>slow_engine.actual_controls.throttle,
        "actuator parameters remain isolated per model");
    const GroundPlane plane{{0,0,0},{0,0,1},true};
    const auto parked=ground_initial_condition({},0,0,plane,modified);
    require(parked && parked->actual_controls.throttle==0,"ground start initializes stopped engine");
    const auto rest=step_aircraft(*parked,{0,0,0,0},.2,modified,plane,{0,0,true});
    require(rest && std::abs(rest->ground.normal_force_n-second_model.mass_kg*gravity_mps2)<.1,
        "contact solver balances the selected aircraft mass");
    auto crosswind_start=*ground_initial_condition({},0,0,plane);
    Environment crosswind; crosswind.wind_enu_mps={2,0,0};
    require(air_data(crosswind_start,crosswind).alpha_rad==0,
        "pure crosswind does not invent longitudinal angle of attack");
    for (int i=0;i<100;++i) {
        const auto next=step_aircraft(crosswind_start,{.0019,.0039,.0001,1},.01,crosswind,plane,{});
        require(next && !next->ground.crashed,"crosswind ground acceleration keeps contact flow lag bounded");
        crosswind_start=*next;
    }

    gust.gust_start_time_s=0; gust.gust_duration_s=1;
    const auto one=advance(start,{4,2,1,1},.7,gust),two=advance(start,{4,2,1,1},.7,gust);
    require(length(one.position_enu_m-two.position_enu_m)==0 && one.separation==two.separation
        && one.actual_controls.elevator_deg==two.actual_controls.elevator_deg
        && one.actual_controls.throttle==two.actual_controls.throttle,
        "gust and actuator state replay deterministically");
    for (const double degrees:{-170.,-120.,-89.,-20.,0.,20.,89.,120.,170.}) {
        const double alpha=degrees*3.14159265358979323846/180;
        auto a=start;
        a.body_velocity_mps={19*std::cos(alpha),0,19*std::sin(alpha)};
        a.separation=equilibrium_separation(alpha,modified);
        const auto force=air_data(a,modified);
        require(std::isfinite(force.drag_n) && force.drag_n>=0
            && std::abs(dot(force.force_body_n,force.relative_velocity_body_mps)+force.drag_n*force.speed_mps)<1e-8,
            "configured full-angle aerodynamics preserve nonpropulsive drag");
        require(step_aircraft(a,{0,0,0,.5},.1,modified).has_value(),
            "near-vertical and reverse-airflow initial states integrate finitely");
    }
    Environment bad; bad.model=nullptr;
    require(!step_aircraft(start,{},.1,bad),"missing model pointer is rejected");
    bad={}; bad.engine_power_fraction=1.1;
    require(!step_aircraft(start,{},.1,bad),"invalid power fraction is rejected");
    bad={}; bad.gust_duration_s=-1;
    require(!step_aircraft(start,{},.1,bad),"invalid gust duration is rejected");
    std::cout<<"Systems checks passed: configuration isolation, actuator dynamics, faults and deterministic gust.\n";
}
