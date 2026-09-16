#include "FixedWing.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using namespace flight_core;

namespace {
void require(bool value,const char* message) {
    if (!value) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
Aircraft advance(Aircraft a,Controls c,double duration,double h,Environment env={}) {
    const int n=static_cast<int>(std::round(duration/h));
    for (int i=0;i<n;++i) {
        const auto next=step_aircraft(a,c,h,env);
        require(next.has_value(),"integration remains finite");
        a=*next;
    }
    return a;
}
double distance(Vec3 a,Vec3 b) { return std::hypot(a.x-b.x,std::hypot(a.y-b.y,a.z-b.z)); }
double dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
constexpr double rad=3.14159265358979323846/180;
Aircraft airflow(double speed,double alpha,double beta=0) {
    Aircraft a;
    a.position_enu_m={0,0,2600};
    a.pitch_rad=alpha;
    a.body_velocity_mps={speed*std::cos(alpha)*std::cos(beta),speed*std::sin(beta),speed*std::sin(alpha)*std::cos(beta)};
    a.separation=equilibrium_separation(alpha);
    return a;
}
}

int main() {
    // Standard-atmosphere reference values at geometric heights, SI units.
    require(std::abs(isa_density_kg_m3(0)-1.225)<1e-6
        && std::abs(isa_density_kg_m3(10000)-.41351)<1e-4
        && std::abs(isa_density_kg_m3(20000)-.08891)<1e-4,"ISA reference densities");
    require(std::isfinite(isa_density_kg_m3(20000+1e-10))
        && !std::isfinite(isa_density_kg_m3(20001)),"ISA accepts roundoff only at its upper boundary");
    for (const double altitude : {0.,2600.}) {
        const auto trim=level_trim({10,20,altitude},0);
        require(trim.has_value(),"level trim at sea level and terrain-scene altitude");
        const auto end=advance(trim->aircraft,trim->controls,60,.01);
        require(distance(end.position_enu_m,{10,1160,altitude})<1e-6,"60 s straight-level analytical equilibrium");
        require(distance(end.body_velocity_mps,trim->aircraft.body_velocity_mps)<1e-7,"trim velocity remains constant");
    }
    const Environment wind{{5,-3,0},0};
    const auto windy=level_trim({10,20,300},0,19,wind);
    require(windy.has_value(),"trim accepts explicit steady wind");
    const auto drift=advance(windy->aircraft,windy->controls,10,.01,wind);
    require(distance(drift.position_enu_m,{60,180,300})<1e-6
        && std::abs(airspeed(drift,wind)-19)<1e-7,"ground drift differs from constant relative airspeed");

    // Force work must be -D*V for arbitrary alpha/beta, including reverse flow.
    for (const double alpha : {-170.,-90.,-30.,-18.,0.,18.,45.,90.,170.})
        for (const double beta : {-25.,0.,30.}) {
            const auto a=airflow(19,alpha*rad,beta*rad);
            const auto load=air_data(a,{}, {-5,4,3,.5});
            require(load.drag_n>=0 && load.drag_coefficient>=0,"wind-axis drag is never propulsive");
            require(std::abs(dot(load.force_body_n,load.relative_velocity_body_mps)+load.drag_n*load.speed_mps)<1e-8,
                "lift and side forces do no work along airflow");
        }
    auto sea=airflow(19,4*rad); sea.position_enu_m.z=0;
    auto thin=sea; thin.position_enu_m.z=2600;
    auto fast=sea; fast.body_velocity_mps=fast.body_velocity_mps*2;
    const auto l0=air_data(sea),lh=air_data(thin),lv=air_data(fast);
    require(std::abs(lh.force_body_n.z/l0.force_body_n.z-lh.density_kg_m3/l0.density_kg_m3)<1e-12
        && std::abs(lv.force_body_n.z/l0.force_body_n.z-4)<1e-12,"aerodynamic force scales with rho and V squared");
    const auto onset=air_data(airflow(19,12*rad)), separated=air_data(airflow(19,20*rad));
    require(separated.lift_coefficient<onset.lift_coefficient*.6
        && separated.drag_coefficient>onset.drag_coefficient*1.5 && separated.stalled,"post-stall lift loss and increased drag");
    const auto attached_control=air_data(airflow(19,12*rad),{}, {1,0,0,.5});
    const auto separated_control=air_data(airflow(19,20*rad),{}, {1,0,0,.5});
    require(std::abs((separated_control.moment_body_nm.y-separated.moment_body_nm.y)
        /(attached_control.moment_body_nm.y-onset.moment_body_nm.y)-.15)<1e-12,"separated pitch-control authority is 15 percent");

    // Compare the near-trim control moment with the identified acceleration,
    // restoring the measured inertia coupling independently of the integrator.
    const auto trim=level_trim({0,0,0},0);
    require(trim.has_value(),"identified reference trim exists");
    const auto baseline=air_data(trim->aircraft,{},trim->controls);
    auto input=trim->controls; input.elevator_deg+=1;
    // Static coefficient checks prescribe the actual surface position. A new
    // command alone now moves the initialized servo only after time advances.
    auto deflected=trim->aircraft; deflected.actual_controls=input;
    const auto pitch=air_data(deflected,{},input);
    require(std::abs((pitch.moment_body_nm.y-baseline.moment_body_nm.y)/.144+141.57*rad)<1e-6,
        "reference elevator pitching acceleration retains Eq.25 sign and magnitude");
    input=trim->controls; input.aileron_deg+=1;
    deflected.actual_controls=input;
    const auto roll=air_data(deflected,{},input);
    const double determinant=.089*.162-.014*.014;
    const double actual_p=(.162*(roll.moment_body_nm.x-baseline.moment_body_nm.x)
        +.014*(roll.moment_body_nm.z-baseline.moment_body_nm.z))/determinant;
    const double expected_p=(.162*(.089*139.10)+.014*(.162*-17.22))/determinant*rad;
    require(std::abs(actual_p-expected_p)<1e-6 && actual_p>0,"reference aileron response retains coupled inertia and sign");

    // Separation is a state, with unequal separation/reattachment time constants.
    auto entering=airflow(9,18*rad); entering.separation=0;
    auto leaving=airflow(19,0); leaving.separation=1;
    constexpr double tiny=1e-7;
    const auto se=step_aircraft(entering,{},tiny),sl=step_aircraft(leaving,{},tiny);
    require(se && sl && std::abs(se->separation/tiny-.84375/.12)<1e-4
        && std::abs((sl->separation-1)/tiny+1/.60)<1e-4,"initial separation and reattachment derivatives match the first-order law");
    auto stalled=airflow(9,18*rad);
    const auto before=air_data(stalled);
    const auto recovered=advance(stalled,{4,0,0,1},2,.01);
    const auto after=air_data(recovered);
    std::cout<<"Recovery: alpha "<<before.alpha_rad/rad<<" -> "<<after.alpha_rad/rad
        <<" deg, V "<<before.speed_mps<<" -> "<<after.speed_mps<<" m/s, separation "
        <<before.separation<<" -> "<<after.separation<<", height loss "<<2600-recovered.position_enu_m.z<<" m.\n";
    require(after.alpha_rad<12*rad && after.speed_mps>12 && after.separation<.5
        && recovered.position_enu_m.z<stalled.position_enu_m.z,"nose-down input plus thrust lowers AoA and recovers stalled airflow");

    auto stopped=airflow(0,0); stopped.separation=0;
    const auto no_air=air_data(stopped,{}, {0,0,0,0});
    require(distance(no_air.force_body_n,{})==0 && distance(no_air.moment_body_nm,{})==0,"zero airflow gives zero aerodynamic loads");
    const auto falling=step_aircraft(stopped,{0,0,0,0},.05);
    require(falling && velocity_enu(*falling).z<0,"zero airflow continues ballistically without a forward-speed stop");
    auto vertical=stopped; vertical.pitch_rad=89*rad; vertical.body_rates_rps.y=.8;
    const auto crossed=step_aircraft(vertical,{0,0,0,0},.05);
    require(crossed.has_value(),"quaternion integration crosses vertical attitude");
    auto forward=*crossed; forward.body_velocity_mps={1,0,0};
    require(velocity_enu(forward).y<0 && velocity_enu(forward).z>.99,"vertical crossing preserves the physical forward vector");

    auto controls=trim->controls; controls.elevator_deg-=.8; controls.aileron_deg=.7; controls.rudder_deg=.2; controls.throttle+=.02;
    const auto coarse=advance(trim->aircraft,controls,1,.01),fine=advance(trim->aircraft,controls,1,.005);
    const auto reference=advance(trim->aircraft,controls,1,.0025),replay=advance(trim->aircraft,controls,1,.01);
    const double coarse_error=distance(coarse.position_enu_m,reference.position_enu_m);
    const double fine_error=distance(fine.position_enu_m,reference.position_enu_m);
    require(fine_error<coarse_error/8 && coarse_error<1e-4,"quaternion RK4 step-halving convergence");
    require(distance(coarse.position_enu_m,replay.position_enu_m)==0 && coarse.heading_rad==replay.heading_rad
        && coarse.separation==replay.separation,"same state/input/time replays exactly");
    require(!level_trim({0,0,2600},0,5) && trim_aircraft({0,0,2600},0,5)
        && trim_aircraft({0,0,2600},0,45),"initial conditions and sustainable level trim are distinct");
    require(!step_aircraft(trim->aircraft,{26,0,0,.5},.01)
        && !step_aircraft(trim->aircraft,{},0)
        && !step_aircraft(trim->aircraft,{},std::numeric_limits<double>::quiet_NaN())
        && !step_aircraft(trim->aircraft,{},.01,{{std::numeric_limits<double>::infinity(),0,0},0}),"invalid controls/time/environment rejected");
    std::cout<<"Fixed-wing checks passed. RK4 errors "<<coarse_error<<" / "<<fine_error<<" m. Development assumptions only.\n";
}
