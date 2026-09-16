#include "FixedWing.h"
#include <cstdlib>
#include <iostream>
using namespace flight_core;
namespace {
void require(bool ok,const char* reason) { if (!ok) { std::cerr<<"FAIL: "<<reason<<'\n'; std::exit(1); } }
constexpr double rad=3.14159265358979323846/180;
const GroundPlane flat{{0,0,0},{0,0,1},true};
double norm(Vec3 v) { return std::hypot(v.x,std::hypot(v.y,v.z)); }
Aircraft run(Aircraft a,Controls c,GroundControls gc,double seconds,GroundPlane plane=flat) {
    for (int i=0;i<static_cast<int>(std::round(seconds/.01));++i) {
        const auto next=step_aircraft(a,c,.01,{},plane,gc);
        require(next.has_value(),"ground step is finite"); a=*next;
        if (a.ground.crashed) break;
    }
    return a;
}
}
int main() {
    const auto parked=ground_initial_condition({0,0,0},0,0,flat);
    require(parked.has_value(),"level ground spawn");
    const auto rest=run(*parked,{0,0,0,0},{0,0,true},5);
    std::cout<<"rest z="<<rest.position_enu_m.z<<" V="<<norm(rest.body_velocity_mps)<<" N="<<rest.ground.normal_force_n<<" wheels="<<rest.ground.wheels_on_ground<<" crash="<<rest.ground.crashed<<'\n';
    require(!rest.ground.crashed && rest.ground.on_ground && rest.ground.wheels_on_ground==3
        && norm(rest.body_velocity_mps)<.002 && std::abs(rest.position_enu_m.z-.18)<.002,"parked equilibrium holds without sinking");
    require(std::abs(rest.ground.normal_force_n-flight_model_parameters.mass_kg*gravity_mps2)<.05,"reaction impulses balance weight");
    const auto resting_air=air_data(rest);
    std::cout<<"rest alpha="<<resting_air.alpha_rad<<" beta="<<resting_air.beta_rad<<" separation="<<resting_air.separation<<'\n';
    require(resting_air.alpha_rad==0 && resting_air.beta_rad==0 && resting_air.separation<.01
        && !resting_air.stall_warning && !resting_air.stalled,"parked air data has no spurious stall from contact roundoff");
    const auto moving=ground_initial_condition({0,0,0},0,6,flat);
    require(moving.has_value(),"taxi initial condition");
    const auto coast=run(*moving,{0,0,0,0},{},1);
    const auto stopped=run(*moving,{0,0,0,0},{0,1,false},2);
    std::cout<<"brake V="<<norm(stopped.body_velocity_mps)<<" coast V="<<norm(coast.body_velocity_mps)<<" brake Y="<<stopped.position_enu_m.y<<" crash="<<stopped.ground.crashed<<'\n';
    require(!stopped.ground.crashed && norm(stopped.body_velocity_mps)<.05
        && stopped.position_enu_m.y<coast.position_enu_m.y,"wheel braking stops sooner than rolling friction");
    const auto steer=run(*parked,{0,0,0,.10},{20,0,false},3);
    std::cout<<"steer E="<<steer.position_enu_m.x<<" N="<<steer.position_enu_m.y<<" heading="<<steer.heading_rad/rad<<" crash="<<steer.ground.crashed<<'\n';
    require(!steer.ground.crashed && steer.position_enu_m.x>.2 && steer.heading_rad>5*rad,"positive nose-wheel steering turns right under thrust");
    require(std::abs(steer.body_rates_rps.z-steer.body_velocity_mps.x*std::tan(20*rad)/.47)<.02,
        "low-speed yaw follows the independent bicycle-model turning radius");
    const double slope=5*rad;
    const GroundPlane hill{{0,0,0},{0,-std::sin(slope),std::cos(slope)},true};
    const auto hillside=ground_initial_condition({0,0,0},0,0,hill);
    require(hillside.has_value(),"sloped ground spawn");
    const auto hillstop=run(*hillside,{0,0,0,0},{0,0,true},5,hill);
    std::cout<<"slope drift="<<norm(hillstop.position_enu_m-hillside->position_enu_m)<<" crash="<<hillstop.ground.crashed<<'\n';
    require(!hillstop.ground.crashed && norm(hillstop.position_enu_m-hillside->position_enu_m)<.02,"parking friction holds a five degree slope");
    auto takeoff=*parked;
    for (int i=0;i<1200 && !takeoff.ground.crashed;++i) {
        const double elevator=airspeed(takeoff)>11 ? -6 : 0;
        const auto next=step_aircraft(takeoff,{elevator,0,0,1},.01,{},flat,{});
        require(next.has_value(),"takeoff step"); takeoff=*next;
        if (!takeoff.ground.on_ground && takeoff.position_enu_m.z>2) break;
    }
    std::cout<<"takeoff z="<<takeoff.position_enu_m.z<<" V="<<airspeed(takeoff)<<" pitch="<<takeoff.pitch_rad/rad<<" crash="<<takeoff.ground.crashed<<'\n';
    require(!takeoff.ground.crashed && !takeoff.ground.on_ground && takeoff.position_enu_m.z>2,"thrust plus elevator produces physical liftoff");
    auto landing=*ground_initial_condition({0,0,0},0,10,flat);
    landing.position_enu_m.z=1; landing.body_velocity_mps.z=.6; landing.ground={};
    const auto landed=run(landing,{2,0,0,0},{0,.7,false},6);
    std::cout<<"landed z="<<landed.position_enu_m.z<<" V="<<airspeed(landed)<<" impact="<<landed.ground.impact_speed_mps<<" crash="<<landed.ground.crashed<<'\n';
    require(!landed.ground.crashed && landed.ground.on_ground && landed.position_enu_m.z>.15
        && norm(landed.body_velocity_mps)<.1,"gentle touchdown transitions into a braked ground roll");
    auto hard=landing; hard.body_velocity_mps={0,0,12}; hard.position_enu_m.z=.6;
    const auto crash=run(hard,{0,0,0,0},{},.2);
    require(crash.ground.crashed && crash.ground.gear_overload && crash.ground.impact_speed_mps>4
        && crash.position_enu_m.z>=.17,"high-speed touchdown reports crash without passing through the plane");
    auto wing=*parked; wing.roll_rad=35*rad; wing.ground={};
    const auto strike=run(wing,{0,0,0,0},{},.01);
    require(strike.ground.crashed && strike.ground.body_strike,"banked wing strike is detected");
    const auto replay=run(*parked,{0,0,0,.10},{20,0,false},3);
    require(norm(replay.position_enu_m-steer.position_enu_m)==0 && replay.heading_rad==steer.heading_rad,"ground run replays exactly");
    require(!step_aircraft(*parked,{0,0,0,0},.01,{},flat,{31,0,false})
        && !ground_initial_condition({},0,0,{{},{0,0,2},true}),"invalid steering and nonunit surface normals are rejected");

    auto ground_gear=*parked;
    require(gear_down_locked(ground_gear.gear) && !request_gear(ground_gear,false)
        && gear_down_locked(ground_gear.gear),"ground spawn locks gear down and rejects retraction without mutation");
    const auto gear_trim=level_trim({0,0,2600},0);
    require(gear_trim && gear_down_locked(gear_trim->aircraft.gear),"air initial condition also starts gear down");
    auto retracting=gear_trim->aircraft;
    require(request_gear(retracting,false) && retracting.gear.extension==1
        && !gear_down_locked(retracting.gear),"airborne gear command unlocks without instant retraction");
    const auto halfway=run(retracting,gear_trim->controls,{},1.5,{});
    require(std::abs(halfway.gear.extension-.5)<1e-12 && !gear_down_locked(halfway.gear)
        && !gear_up_locked(halfway.gear),"gear traverses half its stroke in 1.5 simulation seconds");
    const auto retracted=run(halfway,gear_trim->controls,{},1.5,{});
    require(gear_up_locked(retracted.gear) && retracted.gear.extension==0,"gear retracts and locks in three seconds");
    auto reversing=halfway;
    require(request_gear(reversing,true),"mid-cycle gear command can reverse in the air");
    const auto reversed=run(reversing,gear_trim->controls,{},1.5,{});
    require(gear_down_locked(reversed.gear),"half-stroke reversal returns down at the same physical travel rate");
    auto extending=retracted;
    require(request_gear(extending,true),"airborne extension accepted");
    const auto deployed=run(extending,gear_trim->controls,{},3,{});
    require(gear_down_locked(deployed.gear) && deployed.gear.extension==1,"full extension also takes three seconds");
    const auto gear_replay=run(retracting,gear_trim->controls,{},3,{});
    require(gear_replay.gear.extension==retracted.gear.extension
        && norm(gear_replay.position_enu_m-retracted.position_enu_m)==0,
        "gear travel and flight state replay deterministically");

    auto drag_up=gear_trim->aircraft; drag_up.gear={false,0};
    require(std::abs(air_data(gear_trim->aircraft).drag_coefficient-air_data(drag_up).drag_coefficient
        -gear_drag_coefficient)<1e-12,"gear-down reference drag is preserved and retraction removes the declared increment");
    auto zero_drag=flight_model_parameters;
    zero_drag.drag_zero=zero_drag.induced_drag=zero_drag.plate_drag=zero_drag.sideslip_drag=0;
    Environment zero_drag_environment; zero_drag_environment.model=&zero_drag;
    require(air_data(drag_up,zero_drag_environment).drag_coefficient==0,
        "gear drag adjustment cannot make configured aerodynamic drag negative");
    for (const double extension:{0.,.5}) {
        Aircraft belly;
        belly.position_enu_m={0,0,.16};
        belly.actual_controls={0,0,0,0}; belly.actuator_initialized=true;
        belly.gear={extension>0,extension};
        const auto falling=step_aircraft(belly,{0,0,0,0},.01,{},flat,{0,1,true});
        require(falling && !falling->ground.on_ground && falling->ground.wheels_on_ground==0
            && falling->ground.normal_force_n==0,"stowed or moving gear supplies no wheel support or braking");
        const auto impact=run(belly,{0,0,0,0},{0,1,true},.5);
        require(impact.ground.crashed && impact.ground.body_strike && impact.position_enu_m.z>=.049,
            "stowed or moving gear preserves body collision instead of passing through terrain");
    }
    auto invalid_gear=gear_trim->aircraft; invalid_gear.gear.extension=1.01;
    require(!step_aircraft(invalid_gear,gear_trim->controls,.01),"invalid deployment fraction is rejected");
    std::cout<<"Ground checks passed: development gear/contact assumptions only.\n";
}
