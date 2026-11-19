#pragma once

#include "Aerodynamics.h"
#include "GroundContact.h"
#include "Propulsion.h"

namespace flight_core {

struct Controls {
    double elevator_deg = 0.0; // Relative to identified reference trim; positive trailing edge down.
    double aileron_deg = 0.0;  // Positive right roll.
    double rudder_deg = 0.0;   // Positive right yaw.
    double throttle = .5;     // Development propulsion, NOT the identified motor calibration.
};

struct Aircraft {
    Vec3 position_enu_m;
    Vec3 body_velocity_mps; // GROUND velocity expressed in forward/right/down body axes.
    Vec3 body_rates_rps;
    double roll_rad = 0.0, pitch_rad = 0.0, heading_rad = 0.0;
    double time_s = 0.0;
    double separation = 0.0; // 0 attached, 1 separated; aerodynamic memory integrated with the state.
    GroundState ground;
    double flap_target_deg=0, flap_deg=0;
    GearState gear;
    Controls actual_controls{}; // Servo positions and engine spool; command remains a separate argument.
    bool actuator_initialized=false;
    double actual_steering_deg=0, actual_brake=0;
    double wheel_rotation_rad=0; // Rolling-distance display phase, not tire rotational inertia.
    double propeller_omega_rps=0; // Shaft angular speed rad/s, separate from body rates.
};

struct AerodynamicState {
    double speed_mps = 0, alpha_rad = 0, beta_rad = 0;
    double density_kg_m3 = 0, dynamic_pressure_pa = 0;
    double lift_coefficient = 0, drag_coefficient = 0, separation = 0;
    bool stall_warning = false, stalled = false;
    double load_factor = 0; // -aerodynamic Fz/(m*g), body normal load; gravity excluded.
    double lift_n = 0, drag_n = 0, side_force_n = 0, thrust_n = 0;
    Vec3 relative_velocity_body_mps{}, force_body_n{}, moment_body_nm{};
};

inline bool valid_controls(Controls c) {
    return std::isfinite(c.elevator_deg) && std::abs(c.elevator_deg) <= 25.0
        && std::isfinite(c.aileron_deg) && std::abs(c.aileron_deg) <= 25.0
        && std::isfinite(c.rudder_deg) && std::abs(c.rudder_deg) <= 25.0
        && std::isfinite(c.throttle) && c.throttle >= 0.0 && c.throttle <= 1.0;
}

inline Controls applied_controls(const Aircraft& aircraft,Controls command={},Environment environment={}) {
    auto actual=aircraft.actuator_initialized ? aircraft.actual_controls : command;
    if (environment.elevator_jammed) actual.elevator_deg=environment.elevator_jam_deg;
    if (environment.aileron_jammed) actual.aileron_deg=environment.aileron_jam_deg;
    if (environment.rudder_jammed) actual.rudder_deg=environment.rudder_jam_deg;
    return actual;
}

inline bool request_gear(Aircraft& aircraft,bool down,Environment environment={}) {
    if ((!down && !model_parameters(environment).retractable_gear) || !valid_gear(aircraft.gear) || aircraft.ground.crashed || (!down && aircraft.ground.on_ground)) return false;
    aircraft.gear.target_down=down;
    return true;
}

inline bool request_flaps(Aircraft& aircraft,double degrees,Environment environment={}) {
    if (!std::isfinite(degrees) || degrees<0 || degrees>model_parameters(environment).maximum_flap_deg
        || aircraft.ground.crashed) return false;
    aircraft.flap_target_deg=degrees;
    return true;
}
inline double maximum_thrust(double speed,double density,const FlightModelParameters& p) {
    if(dynamic_propeller(p)) return disk_thrust(p.shaft_power_w*std::clamp(density/1.225,0.,1.),speed,density,p);
    const double static_thrust=p.mass_kg*p.maximum_thrust_mps2;
    // shortcut: constant-efficiency shaft-power envelope, no RPM, torque, slipstream or reverse; replace with identified propeller data.
    return p.shaft_power_w>0 ? std::min(static_thrust,p.propeller_efficiency*p.shaft_power_w
        *std::clamp(density/1.225,0.0,1.0)/std::max(speed,1.0))
        : static_thrust*std::min(1.0,p.constant_power_above_mps/std::max(speed,1.0));
}

namespace fixed_wing_detail {
constexpr double rad = 3.14159265358979323846 / 180.0;
inline double norm(Vec3 v) { return std::hypot(v.x, std::hypot(v.y, v.z)); }
inline double dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}

// Hamilton quaternion: rotates body FRD vectors into local NED vectors.
struct Quaternion { double w=1, x=0, y=0, z=0; };
inline Quaternion multiply(Quaternion a, Quaternion b) {
    return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
        a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
}
inline double norm(Quaternion q) { return std::hypot(std::hypot(q.w,q.x),std::hypot(q.y,q.z)); }
inline Quaternion unit(Quaternion q) {
    const double n=norm(q);
    return {q.w/n,q.x/n,q.y/n,q.z/n};
}
inline Quaternion orientation(const Aircraft& a) {
    const double cr=std::cos(a.roll_rad*.5), sr=std::sin(a.roll_rad*.5);
    const double cp=std::cos(a.pitch_rad*.5), sp=std::sin(a.pitch_rad*.5);
    const double ch=std::cos(a.heading_rad*.5), sh=std::sin(a.heading_rad*.5);
    return {cr*cp*ch+sr*sp*sh, sr*cp*ch-cr*sp*sh,
        cr*sp*ch+sr*cp*sh, cr*cp*sh-sr*sp*ch};
}
inline Vec3 rotate(Quaternion q, Vec3 v) {
    const Vec3 vector{q.x,q.y,q.z};
    const auto t=cross(vector,v)*2;
    return v+t*q.w+cross(vector,t);
}
inline Vec3 inverse_rotate(Quaternion q, Vec3 v) { return rotate({q.w,-q.x,-q.y,-q.z},v); }
inline Vec3 enu(Vec3 ned) { return {ned.y,ned.x,-ned.z}; }
inline Vec3 ned(Vec3 value) { return {value.y,value.x,-value.z}; }
inline void euler(Aircraft& a, Quaternion q) {
    q=unit(q);
    const double sine=std::clamp(2*(q.w*q.y-q.z*q.x),-1.0,1.0);
    a.pitch_rad=std::asin(sine);
    if (std::abs(sine)>1.0-1e-14) {
        // At vertical attitude, yaw and roll are not individually observable.
        // Choose zero roll and an equivalent heading, without changing orientation.
        const auto right=rotate(q,{0,1,0});
        a.roll_rad=0;
        a.heading_rad=std::atan2(-right.x,right.y);
    } else {
        a.roll_rad=std::atan2(2*(q.w*q.x+q.y*q.z),1-2*(q.x*q.x+q.y*q.y));
        a.heading_rad=std::atan2(2*(q.w*q.z+q.x*q.y),1-2*(q.y*q.y+q.z*q.z));
    }
}
inline bool valid_environment(Environment e) {
    return finite(e.wind_enu_mps) && std::isfinite(e.origin_altitude_m) && e.model && valid_model_parameters(*e.model)
        && std::isfinite(e.engine_power_fraction) && e.engine_power_fraction>=0 && e.engine_power_fraction<=1
        && valid_controls({e.elevator_jam_deg,e.aileron_jam_deg,e.rudder_jam_deg,0})
        && std::isfinite(e.gust_start_time_s) && e.gust_start_time_s>=0
        && std::isfinite(e.gust_duration_s) && e.gust_duration_s>=0 && e.gust_duration_s<=3600
        && finite(e.gust_peak_enu_mps);
}
inline bool valid_aircraft(const Aircraft& a) {
    return finite(a.position_enu_m) && finite(a.body_velocity_mps) && finite(a.body_rates_rps)
        && std::isfinite(a.roll_rad) && std::isfinite(a.pitch_rad) && std::isfinite(a.heading_rad)
        && std::isfinite(a.time_s) && a.time_s>=0 && std::isfinite(a.separation)
        && a.separation>=-1e-12 && a.separation<=1+1e-12 && std::isfinite(norm(a.body_velocity_mps))
        && std::isfinite(a.propeller_omega_rps) && a.propeller_omega_rps>=0 && a.propeller_omega_rps<2000
        && std::isfinite(a.wheel_rotation_rad) && valid_ground_controls({a.actual_steering_deg,a.actual_brake,false})
        && valid_gear(a.gear) && (!a.actuator_initialized || valid_controls(a.actual_controls));
}

// Exact bounded first-order response. Above rate*tau error, the actuator first
// travels at its rate limit, then follows an exponential without overshoot.
inline double servo_response(double initial,double command,double elapsed,double tau,double rate) {
    const double error=command-initial, distance=std::abs(error), sign=std::copysign(1.0,error);
    const double transition=std::max(0.0,(distance-rate*tau)/rate);
    if (elapsed<=transition) return initial+sign*rate*elapsed;
    const double remaining=std::min(distance,rate*tau);
    return command-sign*remaining*std::exp(-(elapsed-transition)/tau);
}
inline Controls actuator_response(Controls initial,Controls command,double elapsed,Environment environment) {
    const auto& p=model_parameters(environment);
    Controls actual;
    actual.elevator_deg=environment.elevator_jammed ? environment.elevator_jam_deg
        : servo_response(initial.elevator_deg,command.elevator_deg,elapsed,p.servo_tau_s,p.servo_rate_deg_s);
    actual.aileron_deg=environment.aileron_jammed ? environment.aileron_jam_deg
        : servo_response(initial.aileron_deg,command.aileron_deg,elapsed,p.servo_tau_s,p.servo_rate_deg_s);
    actual.rudder_deg=environment.rudder_jammed ? environment.rudder_jam_deg
        : servo_response(initial.rudder_deg,command.rudder_deg,elapsed,p.servo_tau_s,p.servo_rate_deg_s);
    actual.throttle=command.throttle+(initial.throttle-command.throttle)*std::exp(-elapsed/p.engine_tau_s);
    return actual;
}

inline AerodynamicState loads(const Aircraft& a, Quaternion orientation_q, Environment environment, Controls controls) {
    const auto& p=model_parameters(environment);
    controls=applied_controls(a,controls,environment);
    AerodynamicState result;
    const auto relative=a.body_velocity_mps-inverse_rotate(orientation_q,ned(wind_at(environment,a.time_s)));
    result.relative_velocity_body_mps=relative;
    result.speed_mps=norm(relative);
    // Alpha is undefined without longitudinal-plane airflow (including pure
    // sideslip). Below 1 mm/s ignore contact-solver roundoff in that plane.
    result.alpha_rad=std::hypot(relative.x,relative.z)>1e-3 ? std::atan2(relative.z,relative.x) : 0;
    result.beta_rad=result.speed_mps>1e-3 ? std::asin(std::clamp(relative.y/result.speed_mps,-1.0,1.0)) : 0;
    result.density_kg_m3=isa_density_kg_m3(environment.origin_altitude_m+a.position_enu_m.z);
    result.dynamic_pressure_pa=.5*result.density_kg_m3*result.speed_mps*result.speed_mps;
    result.separation=std::clamp(a.separation,0.0,1.0);
    result.stall_warning=std::abs(result.alpha_rad)>=.8*p.stall_onset_rad || result.separation>.15;
    result.stalled=result.separation>=.5;
    const double separation=result.separation, attached=1-separation;
    const double authority=attached+p.separated_control_fraction*separation;
    const double damping=attached+p.separated_damping_fraction*separation;
    const double rate_speed=std::max(result.speed_mps,1.0);
    const double phat=a.body_rates_rps.x*p.span_m/(2*rate_speed);
    const double qhat=a.body_rates_rps.y*p.chord_m/(2*rate_speed);
    const double rhat=a.body_rates_rps.z*p.span_m/(2*rate_speed);
    const double de=controls.elevator_deg*rad, da=controls.aileron_deg*rad, dr=controls.rudder_deg*rad;
    // Bound the attached branch while separation catches up after a rapid alpha change.
    const double attached_alpha=std::clamp(result.alpha_rad,-p.fully_separated_rad,p.fully_separated_rad);
    const double flap=a.flap_deg*rad;
    const double lift_attached=p.flap_lift_per_rad*flap+p.lift_ref+p.lift_alpha*(attached_alpha-p.reference_pitch_rad)
        +p.lift_elevator*de+p.lift_pitch_rate*qhat;
    const double sa=std::sin(result.alpha_rad), ca=std::cos(result.alpha_rad);
    const double sb=std::sin(result.beta_rad), cb=std::cos(result.beta_rad);
    result.lift_coefficient=attached*lift_attached+separation*p.plate_lift*std::sin(2*result.alpha_rad);
    result.drag_coefficient=p.flap_drag_per_rad2*flap*flap+p.drag_zero+attached*p.induced_drag*lift_attached*lift_attached
        +separation*p.plate_drag*sa*sa+p.sideslip_drag*sb*sb;
    result.drag_coefficient=std::max(0.0,result.drag_coefficient+(p.retractable_gear ? gear_drag_coefficient*(a.gear.extension-1) : 0));
    const double side=authority*(p.side_beta*result.beta_rad+p.side_aileron*da+p.side_rudder*dr)
        +damping*(p.side_roll_rate*phat+p.side_yaw_rate*rhat);
    const double q_area=result.dynamic_pressure_pa*p.wing_area_m2;
    result.lift_n=q_area*result.lift_coefficient;
    result.drag_n=q_area*result.drag_coefficient;
    result.side_force_n=q_area*side;
    // Wind-axis lift and side vectors are orthogonal to relative air velocity;
    // the only aerodynamic work is -D*V, with D>=0 at every angle.
    const Vec3 along{ca*cb,sb,sa*cb}, lift{sa,0,-ca}, side_axis{-ca*sb,cb,-sa*sb};
    result.force_body_n=along*(-result.drag_n)+lift*result.lift_n+side_axis*result.side_force_n;
    const double pitch=attached*(p.flap_pitch_per_rad*flap+p.pitch_alpha*(attached_alpha-p.reference_pitch_rad)
        +p.pitch_speed*(result.speed_mps/p.reference_speed_mps-1))
        +separation*p.plate_pitch*std::sin(2*result.alpha_rad)
        +authority*p.pitch_elevator*de+damping*p.pitch_rate*qhat;
    const double roll=authority*(p.roll_beta*result.beta_rad+p.roll_aileron*da+p.roll_rudder*dr)
        +damping*(p.roll_rate*phat+p.roll_yaw_rate*rhat);
    const double yaw=authority*(p.yaw_beta*result.beta_rad+p.yaw_aileron*da+p.yaw_rudder*dr)
        +damping*(p.yaw_roll_rate*phat+p.yaw_rate*rhat);
    result.moment_body_nm={q_area*p.span_m*roll,q_area*p.chord_m*pitch,q_area*p.span_m*yaw};
    result.thrust_n=maximum_thrust(result.speed_mps,result.density_kg_m3,p)*controls.throttle*environment.engine_power_fraction;
    if(dynamic_propeller(p)) {
        const auto prop=propeller_data(a.propeller_omega_rps,result.speed_mps,result.density_kg_m3,controls.throttle,environment.engine_power_fraction,p);
        result.thrust_n=prop.thrust_n;
        const double momentum=p.propeller_spin_direction*p.propeller_inertia_kg_m2*a.propeller_omega_rps;
        result.moment_body_nm=result.moment_body_nm+Vec3{-p.propeller_spin_direction*prop.drive_torque_nm,-a.body_rates_rps.z*momentum,a.body_rates_rps.y*momentum};
    }
    result.moment_body_nm.y+=p.thrust_z_m*result.thrust_n; // r(FRD) x F: positive down offset pitches up.
    result.load_factor=-result.force_body_n.z/(p.mass_kg*gravity_mps2);
    return result;
}

struct IntegrationState { Aircraft a; Quaternion q; };
inline bool valid(const IntegrationState& state, Environment environment) {
    if (!valid_aircraft(state.a) || !std::isfinite(norm(state.q)) || norm(state.q)<=1e-12
        || !std::isfinite(isa_density_kg_m3(environment.origin_altitude_m+state.a.position_enu_m.z))) return false;
    const auto& model=model_parameters(environment);
    if (!std::isfinite(state.a.flap_deg) || !std::isfinite(state.a.flap_target_deg)
        || state.a.flap_deg<0 || state.a.flap_deg>model.maximum_flap_deg
        || state.a.flap_target_deg<0 || state.a.flap_target_deg>model.maximum_flap_deg
        || (!model.retractable_gear && !gear_down_locked(state.a.gear))) return false;
    const auto& o=model.operating;
    if (!o.restricted) return true;
    const auto aero=loads(state.a,unit(state.q),environment,{});
    const double height=environment.origin_altitude_m+state.a.position_enu_m.z;
    return aero.speed_mps>=o.minimum_speed_mps && aero.speed_mps<=o.maximum_speed_mps
        && height>=o.minimum_height_m && height<=o.maximum_height_m
        && (aero.speed_mps<1 || (std::abs(aero.alpha_rad)<=o.maximum_alpha_deg*rad && std::abs(aero.beta_rad)<=o.maximum_beta_deg*rad));
}
inline IntegrationState derivative(const IntegrationState& state, Controls controls, Environment environment) {
    const auto& p=model_parameters(environment);
    const auto orientation_q=unit(state.q);
    const auto aero=loads(state.a,orientation_q,environment,controls);
    const Vec3 gravity=inverse_rotate(orientation_q,{0,0,gravity_mps2});
    const auto v=state.a.body_velocity_mps, omega=state.a.body_rates_rps;
    IntegrationState derivative_state;
    derivative_state.a.position_enu_m=enu(rotate(orientation_q,v));
    derivative_state.a.body_velocity_mps=(aero.force_body_n+Vec3{aero.thrust_n,0,0})*(1/p.mass_kg)
        +gravity-cross(omega,v);
    // I*omega_dot = applied moment - omega cross (I*omega).
    const Vec3 angular_momentum{p.ix*omega.x-p.ixz*omega.z,p.iy*omega.y,p.iz*omega.z-p.ixz*omega.x};
    const auto rhs=aero.moment_body_nm-cross(omega,angular_momentum);
    const double det=p.ix*p.iz-p.ixz*p.ixz;
    derivative_state.a.body_rates_rps={(p.iz*rhs.x+p.ixz*rhs.z)/det,rhs.y/p.iy,
        (p.ixz*rhs.x+p.ix*rhs.z)/det};
    const double target=equilibrium_separation(aero.alpha_rad,environment);
    const double tau=target>state.a.separation ? p.separation_time_s : p.reattachment_time_s;
    // Contact resolves the actual airflow after this provisional free-flight
    // stage, then integrates its lag once. Do not integrate fictitious fall here.
    derivative_state.a.separation=state.a.ground.on_ground ? 0 : (target-state.a.separation)/tau;
    derivative_state.a.time_s=1;
    if(dynamic_propeller(p)) {
        const auto actual=applied_controls(state.a,controls,environment);
        const auto prop=propeller_data(state.a.propeller_omega_rps,aero.speed_mps,aero.density_kg_m3,actual.throttle,environment.engine_power_fraction,p);
        derivative_state.a.propeller_omega_rps=(prop.drive_torque_nm-prop.load_torque_nm)/p.propeller_inertia_kg_m2;
    }
    derivative_state.q=multiply(state.q,{0,omega.x*.5,omega.y*.5,omega.z*.5});
    return derivative_state;
}
inline IntegrationState add(const IntegrationState& a,const IntegrationState& d,double dt) {
    IntegrationState out=a;
    out.a.position_enu_m=out.a.position_enu_m+d.a.position_enu_m*dt;
    out.a.body_velocity_mps=out.a.body_velocity_mps+d.a.body_velocity_mps*dt;
    out.a.body_rates_rps=out.a.body_rates_rps+d.a.body_rates_rps*dt;
    out.a.separation+=d.a.separation*dt;
    out.a.propeller_omega_rps+=d.a.propeller_omega_rps*dt;
    out.a.time_s+=d.a.time_s*dt;
    out.q={a.q.w+d.q.w*dt,a.q.x+d.q.x*dt,a.q.y+d.q.y*dt,a.q.z+d.q.z*dt};
    return out;
}

inline Vec3 inverse_inertia(Vec3 moment,const FlightModelParameters& p) {
    const double det=p.ix*p.iz-p.ixz*p.ixz;
    return {(p.iz*moment.x+p.ixz*moment.z)/det,moment.y/p.iy,
        (p.ixz*moment.x+p.ix*moment.z)/det};
}

inline GroundState ground_report(const Aircraft& a,Quaternion q,GroundPlane plane,const FlightModelParameters& p=flight_model_parameters) {
    GroundState report=a.ground;
    report.available=plane.valid;
    report.per_wheel=plane.per_wheel;
    if (!plane.valid) {
        report.on_ground=false; report.wheels_on_ground=0; report.normal_force_n=0;
        report.clearance_m=0; report.minimum_wheel_clearance_m=0;
        report.wheel_normal_force_n={};report.wheel_clearance_m={};report.wheel_lateral_slip_mps={};
        return report;
    }
    report.clearance_m=dot(a.position_enu_m-plane.point_enu_m,plane.normal_enu);
    report.minimum_wheel_clearance_m=std::numeric_limits<double>::infinity();
    int nearby=0;
    const auto points=wheel_points(p);
    for (std::size_t wheel=0;wheel<3;++wheel) {
        const auto surface=wheel_surface(plane,wheel);
        const double gap=dot(a.position_enu_m+enu(rotate(q,points[wheel]))-surface.point_enu_m,surface.normal_enu);
        report.wheel_clearance_m[wheel]=gap;
        report.minimum_wheel_clearance_m=std::min(report.minimum_wheel_clearance_m,gap);
        if (gap<=.003) ++nearby;
    }
    report.wheels_on_ground=gear_down_locked(a.gear) ? std::min(report.wheels_on_ground,nearby) : 0;
    report.on_ground=report.wheels_on_ground>0;
    if (!report.on_ground) report.normal_force_n=0;
    for (const auto point:body_points(p))
        if (report.clearance_m+dot(enu(rotate(q,point)),plane.normal_enu)<-1e-5) report.body_strike=true;
    report.crashed=report.crashed || report.body_strike || report.gear_overload;
    return report;
}

// One rigid body against a supplied plane. Solve unilateral normal impulses and
// rolling/brake resistance and a Coulomb friction circle at three fixed gear points. Our own 3-D
// implementation of rigid-body constraint equations; it imports no physics code.
// See Catto, Sequential Impulses (GDC 2006), equations for contact effective mass.
inline void solve_ground(IntegrationState& state,const IntegrationState& before,
        GroundPlane plane,GroundControls controls,double h,Environment environment) {
    const auto& p=model_parameters(environment);
    if (!plane.valid) { state.a.ground=ground_report(state.a,state.q,plane,p); return; }
    const auto& gear=ground_parameters;
    const auto q=unit(state.q);
    const double minimum=ground_report(state.a,q,plane,p).minimum_wheel_clearance_m;
    const bool candidate=gear_down_locked(state.a.gear) && minimum<=.003;
    if (candidate) {
        // Contact uses a velocity solve and semi-implicit position update. This
        // prevents the free-flight gravity displacement from accumulating at rest.
        state.a.position_enu_m=before.a.position_enu_m;
        std::array<double,3> lambda_n{},lambda_f{},lambda_s{},gap{},closing{};
        std::array<Vec3,3> forward{},side{},n_body{};
        std::array<double,3> lateral_coefficient{};
        auto point_velocity=[&](Vec3 r) { return state.a.body_velocity_mps+cross(state.a.body_rates_rps,r); };
        auto effective_mass=[&](Vec3 r,Vec3 direction) {
            return 1/p.mass_kg+dot(cross(inverse_inertia(cross(r,direction),p),r),direction);
        };
        auto impulse=[&](Vec3 r,Vec3 value) {
            state.a.body_velocity_mps=state.a.body_velocity_mps+value*(1/p.mass_kg);
            state.a.body_rates_rps=state.a.body_rates_rps+inverse_inertia(cross(r,value),p);
        };
        for (std::size_t wheel=0;wheel<3;++wheel) {
            const auto r=wheel_points(p)[wheel];
            const auto surface=wheel_surface(plane,wheel);
            n_body[wheel]=inverse_rotate(q,ned(surface.normal_enu));
            const auto normal=n_body[wheel];
            gap[wheel]=dot(state.a.position_enu_m+enu(rotate(q,r))-surface.point_enu_m,surface.normal_enu);
            closing[wheel]=std::max(0.0,-dot(point_velocity(r),normal));
            const double angle=wheel==0 ? controls.steering_deg*rad : 0;
            const Vec3 axis{std::cos(angle),std::sin(angle),0};
            forward[wheel]=axis-normal*dot(axis,normal);
            const double length=norm(forward[wheel]);
            forward[wheel]=length>1e-9 ? forward[wheel]*(1/length) : Vec3{0,1,0};
            side[wheel]=cross(forward[wheel],normal); // Body FRD normal is Up; +side is wheel right.
            const double vf=dot(point_velocity(r),forward[wheel]),vs=dot(point_velocity(r),side[wheel]);
            lateral_coefficient[wheel]=p.tire_cornering_per_rad>0 && std::abs(vf)>1
                ? std::min(p.lateral_friction,p.tire_cornering_per_rad*std::abs(std::atan2(vs,std::abs(vf)))) : p.lateral_friction;
        }
        const double mu_f=p.rolling_friction+p.brake_friction*(controls.parking_brake ? 1 : controls.brake);
        for (int iteration=0;iteration<gear.solver_iterations;++iteration)
            for (std::size_t wheel=0;wheel<3;++wheel) {
                const auto r=wheel_points(p)[wheel];
                const auto normal=n_body[wheel];
                // Speculative contact permits closure only as far as this plane.
                const double target=gap[wheel]>=0 ? -gap[wheel]/h : -.2*gap[wheel]/h;
                const double previous=lambda_n[wheel];
                lambda_n[wheel]=std::max(0.0,previous+(target-dot(point_velocity(r),normal))/effective_mass(r,normal));
                impulse(r,normal*(lambda_n[wheel]-previous));
                const double old_f=lambda_f[wheel],old_s=lambda_s[wheel];
                const double limit_f=mu_f*lambda_n[wheel];
                const double next_f=std::clamp(old_f-dot(point_velocity(r),forward[wheel])
                    /effective_mass(r,forward[wheel]),-limit_f,limit_f);
                impulse(r,forward[wheel]*(next_f-old_f));
                // Rolling resistance is small but must not consume the entire
                // lateral tire budget: the total traction limit is mu_side*N.
                const double traction=p.lateral_friction*lambda_n[wheel];
                const double limit_s=std::min(lateral_coefficient[wheel]*lambda_n[wheel],
                    std::sqrt(std::max(0.0,traction*traction-next_f*next_f)));
                const double next_s=std::clamp(old_s-dot(point_velocity(r),side[wheel])
                    /effective_mass(r,side[wheel]),-limit_s,limit_s);
                lambda_f[wheel]=next_f; lambda_s[wheel]=next_s;
                impulse(r,side[wheel]*(next_s-old_s));
            }
        state.a.position_enu_m=before.a.position_enu_m+enu(rotate(q,state.a.body_velocity_mps))*h;
        GroundState report=before.a.ground;
        report.available=true; report.on_ground=false; report.wheels_on_ground=0; report.normal_force_n=0;
        report.wheel_normal_force_n={};report.wheel_lateral_slip_mps={};
        double touchdown_speed=0;
        for (std::size_t wheel=0;wheel<3;++wheel)
            if (lambda_n[wheel]>1e-7) {
                ++report.wheels_on_ground;
                report.normal_force_n+=lambda_n[wheel]/h;
                report.wheel_normal_force_n[wheel]=lambda_n[wheel]/h;
                report.wheel_lateral_slip_mps[wheel]=dot(point_velocity(wheel_points(p)[wheel]),side[wheel]);
                touchdown_speed=std::max(touchdown_speed,closing[wheel]);
            }
        report.on_ground=report.wheels_on_ground>0;
        if (report.on_ground && !before.a.ground.on_ground) report.impact_speed_mps=touchdown_speed;
        else if (report.on_ground && (report.wheels_on_ground>before.a.ground.wheels_on_ground
            || touchdown_speed>p.impact_limit_mps))
            report.impact_speed_mps=std::max(report.impact_speed_mps,touchdown_speed);
        report.gear_overload=report.gear_overload || (report.on_ground && touchdown_speed>p.impact_limit_mps);
        state.a.ground=report;
    } else {
        state.a.ground=before.a.ground;
        state.a.ground.on_ground=false; state.a.ground.wheels_on_ground=0; state.a.ground.normal_force_n=0;
        state.a.ground.wheel_normal_force_n={};state.a.ground.wheel_lateral_slip_mps={};
    }
    state.a.ground=ground_report(state.a,q,plane,p);
    if (state.a.ground.body_strike) {
        // Preserve the impact result for the caller, with the body on the surface
        // instead of returning nullopt and hiding the collision at the old position.
        double penetration=0;
        for (const auto point:body_points(p))
            penetration=std::max(penetration,-dot(state.a.position_enu_m+enu(rotate(q,point))-plane.point_enu_m,plane.normal_enu));
        state.a.position_enu_m=state.a.position_enu_m+plane.normal_enu*penetration;
        state.a.ground=ground_report(state.a,q,plane,p);
    }
    state.a.ground.crashed=state.a.ground.crashed || state.a.ground.body_strike || state.a.ground.gear_overload;
}
} // namespace fixed_wing_detail

inline Vec3 velocity_enu(const Aircraft& a) {
    using namespace fixed_wing_detail;
    return enu(rotate(orientation(a),a.body_velocity_mps));
}
inline Vec3 enu_to_body(const Aircraft& a,Vec3 enu_vector) {
    using namespace fixed_wing_detail;
    return inverse_rotate(orientation(a),ned(enu_vector));
}
inline Vec3 relative_air_velocity_body(const Aircraft& a,Environment environment={}) {
    using namespace fixed_wing_detail;
    return a.body_velocity_mps-inverse_rotate(orientation(a),ned(wind_at(environment,a.time_s)));
}
inline double airspeed(const Aircraft& a,Environment environment={}) {
    return fixed_wing_detail::norm(relative_air_velocity_body(a,environment));
}
inline AerodynamicState air_data(const Aircraft& a,Environment environment={},Controls controls={}) {
    return fixed_wing_detail::loads(a,fixed_wing_detail::orientation(a),environment,controls);
}

inline GroundState ground_data(const Aircraft& a,GroundPlane plane={},Environment environment={}) {
    return fixed_wing_detail::ground_report(a,fixed_wing_detail::orientation(a),plane,model_parameters(environment));
}

// Place all three rigid wheel points on a local plane; speed is GROUND speed.
inline std::optional<Aircraft> ground_initial_condition(Vec3 position,double heading_rad,double speed_mps,
        GroundPlane plane,Environment environment={}) {
    using namespace fixed_wing_detail;
    if (!finite(position) || !std::isfinite(heading_rad) || !std::isfinite(speed_mps) || speed_mps<0 || speed_mps>15
        || !plane.valid || !valid_ground_plane(plane) || !valid_environment(environment)
        || !model_parameters(environment).operating.ground_supported) return std::nullopt;
    Vec3 forward{std::sin(heading_rad),std::cos(heading_rad),0};
    forward=forward-plane.normal_enu*dot(forward,plane.normal_enu);
    if (norm(forward)<1e-8) return std::nullopt;
    forward=forward*(1/norm(forward));
    const auto right=cross(forward,plane.normal_enu);
    Aircraft a;
    a.position_enu_m=position;
    a.position_enu_m.z+=(model_parameters(environment).cg_height_m-dot(position-plane.point_enu_m,plane.normal_enu))/plane.normal_enu.z;
    a.heading_rad=std::atan2(forward.x,forward.y);
    a.pitch_rad=std::atan2(forward.z,std::hypot(forward.x,forward.y));
    a.roll_rad=std::atan2(-right.z,plane.normal_enu.z);
    a.body_velocity_mps=enu_to_body(a,forward*speed_mps);
    a.actual_controls=applied_controls(a,{0,0,0,0},environment);
    a.actuator_initialized=true;
    a.separation=equilibrium_separation(air_data(a,environment).alpha_rad,environment);
    a.ground.available=true; a.ground.on_ground=true; a.ground.wheels_on_ground=3;
    a.ground.normal_force_n=model_parameters(environment).mass_kg*gravity_mps2*plane.normal_enu.z;
    a.ground=ground_data(a,plane,environment);
    return valid_aircraft(a) && std::isfinite(isa_density_kg_m3(a.position_enu_m.z+environment.origin_altitude_m))
        ? std::optional<Aircraft>{a} : std::nullopt;
}

struct LevelTrim { Aircraft aircraft; Controls controls; };

inline bool solve_lateral_trim(const FlightModelParameters& p,double drag,double torque_coefficient,
        double beta,std::array<double,3>& result) {
    double matrix[3][4]{{p.side_beta-drag*(std::abs(beta)>1e-12 ? std::tan(beta)/beta : 1),p.side_aileron,p.side_rudder,0},
        {p.roll_beta,p.roll_aileron,p.roll_rudder,torque_coefficient},{p.yaw_beta,p.yaw_aileron,p.yaw_rudder,0}};
    for(int col=0;col<3;++col) {
        int pivot=col;for(int row=col+1;row<3;++row) if(std::abs(matrix[row][col])>std::abs(matrix[pivot][col])) pivot=row;
        if(std::abs(matrix[pivot][col])<1e-12) return false;
        for(int j=col;j<4;++j) std::swap(matrix[col][j],matrix[pivot][j]);
        const double divisor=matrix[col][col];for(int j=col;j<4;++j) matrix[col][j]/=divisor;
        for(int row=0;row<3;++row) if(row!=col) {
            const double factor=matrix[row][col];for(int j=col;j<4;++j) matrix[row][j]-=factor*matrix[col][j];
        }
    }
    for(int row=0;row<3;++row) result[row]=matrix[row][3];return true;
}

// Solve zero normal acceleration, zero pitching moment, and zero longitudinal
// acceleration on the attached branch. No level equilibrium -> explicit failure.
inline std::optional<LevelTrim> level_trim(Vec3 position,double heading_rad,
        double speed_mps=reference_speed_mps,Environment environment={},double time_s=0,double flap_deg=0) {
    using namespace fixed_wing_detail;
    const auto& p=model_parameters(environment);
    const auto wind=wind_at(environment,time_s);
    if (!finite(position) || !std::isfinite(heading_rad) || !std::isfinite(speed_mps)
        || speed_mps<1 || !std::isfinite(flap_deg) || flap_deg<0 || flap_deg>p.maximum_flap_deg
        || speed_mps<p.operating.minimum_speed_mps || speed_mps>p.operating.maximum_speed_mps || !valid_environment(environment)
        || !std::isfinite(time_s) || time_s<0 || environment.engine_power_fraction<=0
        || !std::isfinite(isa_density_kg_m3(position.z+environment.origin_altitude_m))
        || std::abs(wind.z)>=speed_mps) return std::nullopt;
    const double flight_path=std::asin(-wind.z/speed_mps);
    auto candidate=[&](double alpha) {
        LevelTrim trim;
        auto& a=trim.aircraft;
        a.position_enu_m=position;
        a.flap_deg=a.flap_target_deg=flap_deg;
        a.heading_rad=heading_rad;
        a.time_s=time_s;
        a.pitch_rad=alpha+flight_path;
        a.body_velocity_mps=Vec3{speed_mps*std::cos(alpha),0,speed_mps*std::sin(alpha)}
            +inverse_rotate(orientation(a),ned(wind));
        trim.controls.elevator_deg=-(p.flap_pitch_per_rad*flap_deg*rad+p.pitch_alpha*(alpha-p.reference_pitch_rad)
            +p.pitch_speed*(speed_mps/p.reference_speed_mps-1))/p.pitch_elevator/rad;
        // Elevator affects both drag and the thrust needed for equilibrium.
        double beta=0;
        for (int i=0;i<(dynamic_propeller(p) ? 24 : p.thrust_z_m==0 ? 1 : 8);++i) {
            a.body_velocity_mps=Vec3{speed_mps*std::cos(alpha)*std::cos(beta),speed_mps*std::sin(beta),speed_mps*std::sin(alpha)*std::cos(beta)}
                +inverse_rotate(orientation(a),ned(wind));
            trim.controls.throttle=0;
            const auto aero=air_data(a,environment,trim.controls);
            const double thrust=p.mass_kg*gravity_mps2*std::sin(a.pitch_rad)-aero.force_body_n.x;
            trim.controls.throttle=thrust/(maximum_thrust(speed_mps,aero.density_kg_m3,p)*environment.engine_power_fraction);
            if(dynamic_propeller(p)) {
                trim.controls.throttle=disk_shaft_power(thrust,speed_mps,aero.density_kg_m3,p)
                    /(p.shaft_power_w*std::clamp(aero.density_kg_m3/1.225,0.,1.)*environment.engine_power_fraction);
                a.propeller_omega_rps=p.propeller_nominal_rpm*rad*6*std::cbrt(std::max(0.,trim.controls.throttle*environment.engine_power_fraction));
                const auto prop=propeller_data(a.propeller_omega_rps,speed_mps,aero.density_kg_m3,trim.controls.throttle,environment.engine_power_fraction,p);
                std::array<double,3> lateral;
                if(!solve_lateral_trim(p,aero.drag_coefficient,p.propeller_spin_direction*prop.drive_torque_nm
                    /(aero.dynamic_pressure_pa*p.wing_area_m2*p.span_m),beta,lateral)) {trim.controls.throttle=2;break;}
                beta=lateral[0];trim.controls.aileron_deg=lateral[1]/rad;trim.controls.rudder_deg=lateral[2]/rad;
            }
            if (p.thrust_z_m!=0 || dynamic_propeller(p)) {
                const double moment=air_data(a,environment,trim.controls).moment_body_nm.y;
                trim.controls.elevator_deg-=moment/(aero.dynamic_pressure_pa*p.wing_area_m2*p.chord_m*p.pitch_elevator*rad);
            }
        }
        return trim;
    };
    auto residual=[&](const LevelTrim& trim) {
        const auto aero=air_data(trim.aircraft,environment,trim.controls);
        return aero.force_body_n.z/p.mass_kg+gravity_mps2*std::cos(trim.aircraft.pitch_rad);
    };
    double low=-p.stall_onset_rad*.99, high=p.stall_onset_rad*.99;
    if (residual(candidate(low))*residual(candidate(high))>0) return std::nullopt;
    for (int i=0;i<48;++i) {
        const double mid=(low+high)*.5;
        if (residual(candidate(mid))>0) low=mid; else high=mid;
    }
    auto trim=candidate((low+high)*.5);
    if (!valid_controls(trim.controls) || std::abs(residual(trim))>1e-8) return std::nullopt;
    const auto moments=air_data(trim.aircraft,environment,trim.controls).moment_body_nm;
    if (norm(moments)>1e-8) return std::nullopt; // A jam can remove the requested equilibrium.
    trim.aircraft.actual_controls=applied_controls(trim.aircraft,trim.controls,environment);
    trim.aircraft.actuator_initialized=true;
    if (!valid({trim.aircraft,orientation(trim.aircraft)},environment)) return std::nullopt;
    return trim;
}

// Initial conditions may be outside sustainable level flight (e.g. a 5 m/s
// stalled start). level_trim distinguishes these from actual equilibria.
inline std::optional<Aircraft> trim_aircraft(Vec3 position,double heading_rad,
        double speed_mps=reference_speed_mps,Environment environment={}) {
    using namespace fixed_wing_detail;
    if (!finite(position) || !std::isfinite(heading_rad) || !std::isfinite(speed_mps)
        || speed_mps<model_parameters(environment).operating.minimum_speed_mps
        || speed_mps>model_parameters(environment).operating.maximum_speed_mps || !valid_environment(environment)
        || !std::isfinite(isa_density_kg_m3(position.z+environment.origin_altitude_m))) return std::nullopt;
    if (const auto equilibrium=level_trim(position,heading_rad,speed_mps,environment)) return equilibrium->aircraft;
    if (model_parameters(environment).operating.restricted) return std::nullopt;
    Aircraft a;
    a.position_enu_m=position;
    a.heading_rad=heading_rad;
    a.pitch_rad=model_parameters(environment).reference_pitch_rad;
    a.body_velocity_mps=Vec3{speed_mps*std::cos(a.pitch_rad),0,speed_mps*std::sin(a.pitch_rad)}
        +inverse_rotate(orientation(a),ned(wind_at(environment,a.time_s)));
    a.actual_controls=applied_controls(a,{},environment);
    a.actuator_initialized=true;
    return a;
}

// Classical RK4 with quaternion attitude, explicit fixed subintervals <= .01 s.
// Euler angles are observations only; no Euler-rate singularity enters the ODE.
inline std::optional<Aircraft> step_aircraft(const Aircraft& state,Controls controls,double dt_s,Environment environment={},
        GroundPlane ground={},GroundControls ground_controls={}) {
    using namespace fixed_wing_detail;
    if (!valid_aircraft(state) || !valid_controls(controls) || !valid_environment(environment)
        || !valid_ground_plane(ground) || !valid_ground_controls(ground_controls)
        || !std::isfinite(dt_s) || dt_s<=0 || dt_s>1) return std::nullopt;
    if (!model_parameters(environment).operating.ground_supported && ground.valid) {
        const double clearance=dot(state.position_enu_m-ground.point_enu_m,ground.normal_enu);
        // shortcut: no new-airframe contact geometry yet; stop conservatively before the surface.
        if (clearance<=model_parameters(environment).span_m+norm(state.body_velocity_mps)*dt_s) return std::nullopt;
        ground={};
    }
    if (state.ground.crashed) return state; // Caller must explicitly initialize after a crash.
    IntegrationState a{state,orientation(state)};
    // Legacy aggregate Aircraft callers seed once from their first command.
    // Explicit trim/ground initializers already supply the physical actuator state.
    a.a.actual_controls=applied_controls(a.a,controls,environment);
    a.a.actuator_initialized=true;
    if (!valid(a,environment)) return std::nullopt;
    const bool near_ground=ground.valid && ground_report(state,a.q,ground,model_parameters(environment)).minimum_wheel_clearance_m
        <5+norm(state.body_velocity_mps)*dt_s+norm(state.body_rates_rps)*dt_s;
    const double interval=near_ground ? ground_parameters.contact_substep_s : .01;
    const int count=std::max(1,static_cast<int>(std::ceil(dt_s/interval)));
    const double h=dt_s/count;
    for (int i=0;i<count;++i) {
        const auto before=a;
        const auto k1=derivative(a,controls,environment);
        auto s2=add(a,k1,h*.5);
        s2.a.actual_controls=actuator_response(before.a.actual_controls,controls,h*.5,environment);
        s2.a.flap_deg=servo_response(before.a.flap_deg,before.a.flap_target_deg,h*.5,.1,model_parameters(environment).flap_rate_deg_s);
        s2.a.gear=gear_after(before.a.gear,h*.5);
        if (!valid(s2,environment)) return std::nullopt;
        const auto k2=derivative(s2,controls,environment);
        auto s3=add(a,k2,h*.5);
        s3.a.actual_controls=s2.a.actual_controls;
        s3.a.gear=s2.a.gear;
        s3.a.flap_deg=s2.a.flap_deg;
        if (!valid(s3,environment)) return std::nullopt;
        const auto k3=derivative(s3,controls,environment);
        auto s4=add(a,k3,h);
        s4.a.actual_controls=actuator_response(before.a.actual_controls,controls,h,environment);
        s4.a.flap_deg=servo_response(before.a.flap_deg,before.a.flap_target_deg,h,.1,model_parameters(environment).flap_rate_deg_s);
        s4.a.gear=gear_after(before.a.gear,h);
        if (!valid(s4,environment)) return std::nullopt;
        const auto k4=derivative(s4,controls,environment);
        a=add(add(add(add(a,k1,h/6),k2,h/3),k3,h/3),k4,h/6);
        a.a.actual_controls=s4.a.actual_controls;
        a.a.gear=s4.a.gear;
        a.a.flap_deg=s4.a.flap_deg;
        if (!valid(a,environment)) return std::nullopt;
        a.q=unit(a.q);
        a.a.separation=std::clamp(a.a.separation,0.0,1.0);
        const auto& p=model_parameters(environment);
        double steering=ground_controls.steering_deg;
        if (p.steering_limit_accel_mps2>0) {
            const double limit=std::atan(p.steering_limit_accel_mps2*(p.nose_x_m-p.main_x_m)
                /std::max(1.0,a.a.body_velocity_mps.x*a.a.body_velocity_mps.x))/rad;
            steering=std::clamp(steering,-limit,limit);
        }
        a.a.actual_steering_deg=p.steering_tau_s>0 ? servo_response(before.a.actual_steering_deg,steering,h,p.steering_tau_s,p.steering_rate_deg_s) : steering;
        a.a.actual_brake=ground_controls.parking_brake ? 1 : p.brake_tau_s>0
            ? ground_controls.brake+(before.a.actual_brake-ground_controls.brake)*std::exp(-h/p.brake_tau_s) : ground_controls.brake;
        if (ground.valid) solve_ground(a,before,ground,{a.a.actual_steering_deg,a.a.actual_brake,ground_controls.parking_brake},h,environment);
        else a.a.ground=ground_report(a.a,a.q,ground,model_parameters(environment));
        if(a.a.ground.on_ground) a.a.wheel_rotation_rad=std::remainder(before.a.wheel_rotation_rad
            +a.a.body_velocity_mps.x*h/p.wheel_radius_m,2*3.14159265358979323846);
        if (before.a.ground.on_ground || a.a.ground.on_ground) {
            // Free-flight RK stages contain a provisional downward velocity even
            // at rest. Evolve flow lag from the actual contact-resolved airflow.
            const double target=equilibrium_separation(loads(a.a,a.q,environment,controls).alpha_rad,environment);
            const double tau=target>before.a.separation ? p.separation_time_s : p.reattachment_time_s;
            a.a.separation=target+(before.a.separation-target)*std::exp(-h/tau);
        }
        if (a.a.ground.crashed) {
            a.a.time_s=state.time_s+(i+1)*h;
            euler(a.a,a.q);
            return a.a;
        }
    }
    a.a.time_s=state.time_s+dt_s;
    euler(a.a,a.q);
    return valid_aircraft(a.a) ? std::optional<Aircraft>{a.a} : std::nullopt;
}

} // namespace flight_core
