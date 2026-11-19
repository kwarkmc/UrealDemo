#include "FlightOperations.h"
#include "ResearchProfile.h"
#include <iostream>
#include <cstdlib>
using namespace flight_core;
void require(bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
int main() {
    auto p=research_profile();Environment env;env.model=&p;
    require(dynamic_propeller(p) && valid_model_parameters(p),"dynamic research profile accepted");
    auto trim=level_trim({0,0,2600},0,65,env);require(trim.has_value(),"dynamic propeller trims");
    const auto data=air_data(trim->aircraft,env);
    const auto prop=propeller_data(trim->aircraft.propeller_omega_rps,65,data.density_kg_m3,trim->controls.throttle,1,p);
    require(std::abs(prop.drive_torque_nm-prop.load_torque_nm)<1e-8,"shaft torque equilibrium");
    require(fixed_wing_detail::norm(data.moment_body_nm)<1e-8 && std::abs(data.force_body_n.y)<1e-8,"trim cancels reaction torque and lateral force");
    auto steady=step_aircraft(trim->aircraft,trim->controls,1,env);
    require(steady && std::abs(steady->position_enu_m.z-2600)<1e-6 && std::abs(airspeed(*steady,env)-65)<1e-6,"full trim equilibrium maintained");
    for(double speed:{0.,10.,65.,110.}) {
        const double thrust=disk_thrust(100000,speed,1.225,p);
        require(std::abs(disk_shaft_power(thrust,speed,1.225,p)-100000)<1e-6,"actuator disk power and momentum closure");
    }
    auto cut=env;cut.engine_power_fraction=0;
    const auto coasting=propeller_data(trim->aircraft.propeller_omega_rps,65,data.density_kg_m3,trim->controls.throttle,0,p);
    require(coasting.drive_power_w==0 && coasting.drive_torque_nm==0 && coasting.thrust_n>0,"engine cut leaves rotor kinetic energy, no shaft input");
    auto spinning=trim->aircraft;spinning.body_rates_rps={0,.03,.04};
    const auto spinning_load=air_data(spinning,env);
    auto legacy=p;legacy.propeller_nominal_rpm=0;auto unspun=env;unspun.model=&legacy;
    const auto legacy_load=air_data(spinning,unspun);
    const double h=p.propeller_spin_direction*p.propeller_inertia_kg_m2*spinning.propeller_omega_rps;
    require(std::abs(spinning_load.moment_body_nm.x-legacy_load.moment_body_nm.x+p.propeller_spin_direction*prop.drive_torque_nm)<1e-8,"shaft reaction torque sign");
    require(std::abs(spinning_load.moment_body_nm.z-legacy_load.moment_body_nm.z-spinning.body_rates_rps.y*h)<1e-8,"gyroscopic yaw moment from cross product");
    const double gyro_pitch=spinning_load.moment_body_nm.y-legacy_load.moment_body_nm.y-p.thrust_z_m*(spinning_load.thrust_n-legacy_load.thrust_n);
    require(std::abs(gyro_pitch+spinning.body_rates_rps.z*h)<1e-8,"gyroscopic pitch moment from cross product");
    auto rundown=step_aircraft(trim->aircraft,trim->controls,.1,cut);
    require(rundown && rundown->propeller_omega_rps<trim->aircraft.propeller_omega_rps,"rotor slows after drive loss");
    auto half=trim->aircraft;
    for(int i=0;i<2;++i){auto next=step_aircraft(half,trim->controls,.05,cut);require(next.has_value(),"rundown half-step valid");half=*next;}
    require(std::abs(half.propeller_omega_rps-rundown->propeller_omega_rps)<1e-8,"rotor integration step consistency");
    for(int which=0;which<4;++which) {
        Navigation nav;nav.loiter_radius_m=1000;
        nav.waypoints={{{which==0 ? 1000. : which==1 ? -1000. : 0.,which==2 ? -1000. : which==3 ? 3000. : 0.,2600},65}};
        auto a=trim->aircraft;
        for(int i=0;i<30000 && !nav.loitering;++i) {
            const auto command=guide(nav,a,.01,env);require(command.has_value(),"close waypoint command valid");
            auto next=step_aircraft(a,*command,.01,env);require(next.has_value(),"close waypoint physics valid");a=*next;
        }
        require(nav.loitering,"lateral, reverse and forward final orbit acquired");
    }
    Navigation route;route.loiter_radius_m=1000;route.waypoints={{{1000,0,2600},65},{{1000,4000,2600},65},{{1000,7000,2600},65}};
    auto a=trim->aircraft;
    for(int i=0;i<80000 && !route.loitering;++i) {
        auto c=guide(route,a,.01,env);require(c.has_value(),"multi waypoint command valid");
        auto next=step_aircraft(a,*c,.01,env);require(next.has_value(),"multi waypoint physics valid");a=*next;
    }
    std::cout<<"multi route index="<<route.active_index<<" loiter="<<route.loitering<<" reacquire="<<route.reacquiring<<" xy="<<a.position_enu_m.x<<','<<a.position_enu_m.y<<" heading="<<a.heading_rad<<'\n';
    require(route.active_index==2 && route.loitering,"intermediate close waypoint reacquired, no skipping");
    auto jam=env;jam.rudder_jammed=true;jam.rudder_jam_deg=3;
    Navigation fault_route;fault_route.loiter_radius_m=1000;fault_route.waypoints={{{1000,0,2600},65}};
    auto faulty=trim->aircraft;
    for(int i=0;i<100;++i) {
        const auto command=guide(fault_route,faulty,.01,jam);
        require(command && valid_controls(*command),"rudder fault keeps command bounded");
        const auto next=step_aircraft(faulty,*command,.01,jam);
        require(next && applied_controls(*next,{},jam).rudder_deg==3,"jammed output follows fault, not controller request");faulty=*next;
    }
    require(fault_route.rudder_integral_rps==0,"rudder fault prevents integral windup");
    GroundPlane flat{{0,0,0},{0,0,1},true};flat.per_wheel=true;
    for(auto& surface:flat.wheels) surface={{0,0,0},{0,0,1},true};
    auto ground=*ground_initial_condition({0,0,0},0,8,flat,env);ground.body_velocity_mps.y=.05;
    const auto sliding=step_aircraft(ground,{0,0,0,0},.002,env,flat,{});
    require(sliding && sliding->body_velocity_mps.y>0 && sliding->body_velocity_mps.y<ground.body_velocity_mps.y,"finite tire slip opposes lateral velocity");
    require(fixed_wing_detail::norm(sliding->body_velocity_mps)<fixed_wing_detail::norm(ground.body_velocity_mps),"unpowered tire contact dissipates kinetic energy");
    auto rest=*ground_initial_condition({0,0,0},0,0,flat,env);
    flat.wheels[1].point_enu_m.z=.02;
    const auto bump=step_aircraft(rest,{0,0,0,0},.002,env,flat,{});
    require(bump && bump->ground.wheel_normal_force_n[1]>bump->ground.wheel_normal_force_n[2],"independent raised left surface changes wheel load distribution");
    flat.wheels[1].valid=false;
    require(!step_aircraft(ground,{0,0,0,0},.01,env,flat,{}),"missing wheel surface rejected explicitly");
    std::cout<<"Navigation and physics checks passed.\n";
}
