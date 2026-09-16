#include "FlightOperations.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using namespace flight_core;
namespace {
const GroundPlane flat{{0,0,0},{0,0,1},true};
const Runway runway{{0,0,0},{0,1,0},{0,0,1},1000,40};
void require(bool ok,const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
double norm(Vec3 p) { return std::hypot(p.x,std::hypot(p.y,p.z)); }
void trace(const FlightOperation& op,const Aircraft& a) {
    std::cout << operation_phase_name(op.phase) << " t=" << a.time_s << " xyz="
        << a.position_enu_m.x << ',' << a.position_enu_m.y << ',' << a.position_enu_m.z
        << " v=" << norm(a.body_velocity_mps) << " pitch=" << a.pitch_rad*180/3.141592653589793
        << " roll=" << a.roll_rad*180/3.141592653589793 << " msg=" << op.message << '\n';
}
Aircraft fly(FlightOperation& op,Aircraft a,Environment env,double duration,bool stop_at_hold=false) {
    auto phase=op.phase; trace(op,a);
    for (int i=0;i<static_cast<int>(duration/.01) && operation_active(op);++i) {
        const auto position=a.position_enu_m; const auto time=a.time_s;
        const auto command=update_flight_operation(op,a,env,flat,.01);
        require(command.has_value(),"operation produces finite commands");
        require(norm(a.position_enu_m-position)==0 && a.time_s==time,"guidance never moves aircraft or time");
        if (command->gear_down) {
            if (!*command->gear_down && a.gear.target_down)
                require(!a.ground.on_ground && ground_data(a,flat).clearance_m>=10
                    && velocity_enu(a).z>0,"automatic retraction waits for safe climbing AGL");
            require(request_gear(a,*command->gear_down),"automatic gear command uses the same core acceptance rules");
        }
        const auto next=step_aircraft(a,command->flight,.01,env,flat,command->ground);
        if(!next) { trace(op,a);std::cerr<<"cmd="<<command->flight.elevator_deg<<','<<command->flight.aileron_deg<<','
            <<command->flight.rudder_deg<<','<<command->flight.throttle<<" ground="<<command->ground.steering_deg
            <<','<<command->ground.brake<<" separation="<<a.separation<<'\n'; }
        require(next.has_value(),"same six-DOF advances automatic operation");
        a=*next;
        if (op.phase!=phase) { trace(op,a);phase=op.phase; }
        if(a.ground.crashed)trace(op,a);
        require(!a.ground.crashed,"automatic operation avoids body strike and hard impact");
        if(stop_at_hold && op.phase==FlightOperationPhase::hold)break;
    }
    trace(op,a);
    return a;
}
}

int main(int argc,char** argv) {
    auto parked=ground_initial_condition({0,30,0},0,0,flat);
    require(parked.has_value(),"parked runway initial condition");
    auto short_runway=runway;short_runway.length_m=150;
    require(!start_takeoff(*parked,short_runway,{},flat) && !start_takeoff(*parked,runway,{},{}),
        "short runway and missing ground reject takeoff");
    auto sideways=*parked;sideways.heading_rad=1;
    require(!start_takeoff(sideways,runway,{},flat),"misaligned departure rejected");
    auto invalid=runway;invalid.normal_enu={0,0,2};
    require(!start_takeoff(*parked,invalid,{},flat),"nonunit runway normal rejected");

    Environment wind;wind.wind_enu_mps={argc>1 ? std::atof(argv[1]) : 2,0,0};
    auto takeoff=start_takeoff(*parked,runway,wind,flat);
    require(takeoff.has_value(),"takeoff accepted with modest crosswind");
    auto airborne=fly(*takeoff,*parked,wind,120,true);
    require(operation_completed(*takeoff) && operation_active(*takeoff) && airborne.position_enu_m.z>45,
        "physical takeoff climbs to departure hold");
    require(gear_up_locked(airborne.gear),"automatic departure retracts gear before reaching hold");
    auto landing=start_landing(airborne,runway,wind,flat);
    require(landing.has_value(),"return landing accepted from actual takeoff state");
    auto stopped=fly(*landing,airborne,wind,700);
    require(landing->phase==FlightOperationPhase::complete && stopped.ground.on_ground
        && norm(velocity_enu(stopped))<.2 && std::abs(stopped.position_enu_m.x)<20
        && stopped.position_enu_m.y>0 && stopped.position_enu_m.y<1000 && gear_down_locked(stopped.gear),
        "crosswind full roundtrip extends and locks gear, lands and stops inside runway");

    auto final=level_trim({0,-600,60},0,18);
    require(final.has_value(),"aligned final starts from aerodynamic trim");
    auto approach=start_landing(final->aircraft,runway,{},flat);
    require(approach && approach->phase==FlightOperationPhase::final,"aligned approach enters final directly");
    stopped=fly(*approach,final->aircraft,{},150);
    require(approach->phase==FlightOperationPhase::complete && stopped.ground.on_ground,
        "zero-wind final flare and rollout complete");

    auto unstable=level_trim({30,-120,14},0,18);
    require(unstable.has_value(),"unstable final initial condition");
    auto goaround=start_landing(unstable->aircraft,runway,{},flat);
    require(goaround && goaround->phase==FlightOperationPhase::final,"unstable flight can start an approach");
    const auto goaround_command=update_flight_operation(*goaround,unstable->aircraft,{},flat,.01);
    require(goaround_command && goaround->phase==FlightOperationPhase::go_around
        && goaround->go_arounds==1,"excessive short-final cross track commands a go-around");
    const auto climbing=fly(*goaround,unstable->aircraft,{},35);
    require(climbing.position_enu_m.z>50,"go-around gains height through ordinary flight dynamics");

    auto late_gear=level_trim({0,-120,14},0,18);
    require(late_gear.has_value(),"late-gear approach initial condition");
    late_gear->aircraft.gear={false,0};
    auto gear_approach=start_landing(late_gear->aircraft,runway,{},flat);
    require(gear_approach.has_value(),"landing can start with gear stowed");
    const auto gear_command=update_flight_operation(*gear_approach,late_gear->aircraft,{},flat,.01);
    require(gear_command && gear_command->gear_down && *gear_command->gear_down
        && gear_approach->phase==FlightOperationPhase::go_around,
        "low final without downlock commands extension and a go-around");

    auto rejected=start_takeoff(*parked,runway,{},flat);
    require(rejected.has_value(),"engine-cut test starts healthy");
    auto rolling=fly(*rejected,*parked,{},1.5);
    Environment cut;cut.engine_power_fraction=0;
    stopped=fly(*rejected,rolling,cut,20);
    require(rejected->phase==FlightOperationPhase::aborted && stopped.ground.on_ground
        && norm(velocity_enu(stopped))<.2 && stopped.position_enu_m.y<150,
        "engine cut during ground roll rejects takeoff and brakes to a stop");
    require(!start_takeoff(*parked,runway,cut,flat),"known engine loss rejects operation start");
    std::cout << "operations_checks passed: development controller, not flight certification\n";
}
