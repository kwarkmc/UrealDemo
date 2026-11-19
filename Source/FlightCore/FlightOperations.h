#pragma once

#include "Navigation.h"
#include <string>

namespace flight_core {

struct Runway {
    Vec3 threshold_enu_m;
    Vec3 direction_enu{0,1,0}; // Threshold -> departure end; unit tangent to runway.
    Vec3 normal_enu{0,0,1};
    double length_m = 1000, width_m = 40;
};

enum class FlightOperationKind { none, takeoff, landing };
enum class FlightOperationPhase {
    idle, takeoff_roll, rotate, climb, hold, join, returning, configuring, aligning, final, flare, rollout,
    rejected_takeoff, go_around, complete, aborted
};

struct FlightOperation {
    FlightOperationKind kind = FlightOperationKind::none;
    FlightOperationPhase phase = FlightOperationPhase::idle;
    Runway runway;
    double target_agl_m = 50, target_speed_mps = 19;
    Vec3 target_enu_m;
    double along_m = 0, cross_track_m = 0;
    std::string message = "Inactive";
    int go_arounds = 0;
    double phase_time_s = 0, total_time_s = 0;
    Navigation navigation;
    Vec3 return_center_enu_m;
    double return_radius_m=0, final_distance_m=0, entry_height_m=80;
    double reference_speed_mps=0, reference_bank_rad=0, turn_direction=1, return_floor_height_m=80;
    bool behind_turn=false;

};

struct OperationCommand {
    Controls flight{0,0,0,0};
    GroundControls ground;
    std::optional<double> flap_deg;
    std::optional<bool> gear_down; // Apply through request_gear before step_aircraft; guidance does not mutate Aircraft.
};

inline const char* operation_kind_name(FlightOperationKind kind) {
    switch (kind) {
    case FlightOperationKind::none: return "none";
    case FlightOperationKind::takeoff: return "takeoff";
    case FlightOperationKind::landing: return "landing";
    }
    return "invalid";
}
inline const char* operation_phase_name(FlightOperationPhase phase) {
    switch (phase) {
    case FlightOperationPhase::idle: return "idle";
    case FlightOperationPhase::takeoff_roll: return "takeoff_roll";
    case FlightOperationPhase::rotate: return "rotate";
    case FlightOperationPhase::climb: return "climb";
    case FlightOperationPhase::hold: return "hold";
    case FlightOperationPhase::join: return "join";
    case FlightOperationPhase::returning: return "returning";
    case FlightOperationPhase::configuring: return "configuring";
    case FlightOperationPhase::aligning: return "aligning";
    case FlightOperationPhase::final: return "final";
    case FlightOperationPhase::flare: return "flare";
    case FlightOperationPhase::rollout: return "rollout";
    case FlightOperationPhase::rejected_takeoff: return "rejected_takeoff";
    case FlightOperationPhase::go_around: return "go_around";
    case FlightOperationPhase::complete: return "complete";
    case FlightOperationPhase::aborted: return "aborted";
    }
    return "invalid";
}
inline bool operation_active(const FlightOperation& op) {
    return op.kind != FlightOperationKind::none && op.phase != FlightOperationPhase::idle
        && op.phase != FlightOperationPhase::complete && op.phase != FlightOperationPhase::aborted;
}
inline bool operation_completed(const FlightOperation& op) {
    return op.phase == FlightOperationPhase::complete
        || (op.kind == FlightOperationKind::takeoff && op.phase == FlightOperationPhase::hold);
}

namespace operations_detail {
using fixed_wing_detail::dot;
using fixed_wing_detail::cross;
using fixed_wing_detail::norm;
constexpr double rad = 3.14159265358979323846 / 180;
inline double wrap(double angle) { return std::remainder(angle, 2 * 3.14159265358979323846); }
inline Vec3 right(const Runway& runway) { return cross(runway.direction_enu, runway.normal_enu); }
inline double heading(const Runway& runway) { return std::atan2(runway.direction_enu.x, runway.direction_enu.y); }
inline Vec3 point(const Runway& runway, double along, double across, double height) {
    return runway.threshold_enu_m + runway.direction_enu * along + right(runway) * across + runway.normal_enu * height;
}
inline bool valid_runway(const Runway& runway) {
    return finite(runway.threshold_enu_m) && finite(runway.direction_enu) && finite(runway.normal_enu)
        && std::abs(norm(runway.direction_enu)-1) < 1e-6 && std::abs(norm(runway.normal_enu)-1) < 1e-6
        && std::abs(dot(runway.direction_enu,runway.normal_enu)) < 1e-6 && runway.normal_enu.z > .98
        && std::isfinite(runway.length_m) && runway.length_m >= 300 && runway.length_m <= 3000
        && std::isfinite(runway.width_m) && runway.width_m >= 10 && runway.width_m <= 100;
}
inline bool ready(const Aircraft& a, const Runway& runway, const Environment& env, GroundPlane plane) {
    return valid_runway(runway) && fixed_wing_detail::valid_aircraft(a)
        && fixed_wing_detail::valid_environment(env) && plane.valid && valid_ground_plane(plane)
        && !ground_data(a,plane,env).crashed && std::abs(dot(env.wind_enu_mps,right(runway))) <= 5
        && dot(env.wind_enu_mps,runway.direction_enu) <= 3;
}
inline void phase(FlightOperation& op, FlightOperationPhase next, const char* message) {
    op.phase = next; op.phase_time_s = 0; op.message = message;
}
inline void telemetry(FlightOperation& op, const Aircraft& a) {
    const auto offset = a.position_enu_m-op.runway.threshold_enu_m;
    op.along_m = dot(offset,op.runway.direction_enu);
    op.cross_track_m = dot(offset,right(op.runway));
}
inline void prepare_return(FlightOperation& op,const Aircraft& a,const Environment& env) {
    const auto& model=model_parameters(env);
    telemetry(op,a);
    op.turn_direction=op.cross_track_m<0 ? -1 : 1;
    op.reference_speed_mps=std::clamp(a.flap_deg>.1 ? airspeed(a,env)
        : std::max(airspeed(a,env),model.operating.initial_speed_mps),model.route_min_mps,model.route_max_mps);
    op.reference_bank_rad=std::clamp(a.roll_rad,-25*rad,25*rad);
    op.return_radius_m=std::max(minimum_loiter_radius_m(op.reference_speed_mps)*2,op.reference_speed_mps*15);
    op.final_distance_m=std::max(600.,80+(op.entry_height_m-model.cg_height_m)/std::tan(3*rad));
    op.return_center_enu_m=point(op.runway,-op.final_distance_m,op.turn_direction*op.return_radius_m,op.entry_height_m);
    op.navigation={};
    op.navigation.loiter_radius_m=std::max(op.return_radius_m,minimum_loiter_radius_m(model.route_max_mps));
}
inline void go_around(FlightOperation& op,const char* message) {
    ++op.go_arounds;
    phase(op,FlightOperationPhase::go_around,message);
}
inline void ground_command(OperationCommand& command, const FlightOperation& op, const Aircraft& a,const FlightModelParameters& model) {
    const double speed = norm(velocity_enu(a));
    const double desired = heading(op.runway) - std::atan2(op.cross_track_m,std::max(20.,speed*2));
    const double error = wrap(desired-a.heading_rad);
    const double yaw = 1.2*error-.15*a.body_rates_rps.z;
    command.ground.steering_deg = std::clamp(std::atan((model.nose_x_m-model.main_x_m)*yaw/std::max(speed,2.))/rad,-20.,20.);
    command.flight.aileron_deg = std::clamp((-.65*a.roll_rad-.12*a.body_rates_rps.x)/rad,-20.,20.);
    command.flight.rudder_deg = std::clamp((.12*error-.06*a.body_rates_rps.z)/rad,-10.,10.);
}
// Reuse the flight controller. A point ahead on the runway line supplies course;
// the altitude target includes descent feedforward through its existing 0.15/s gain.
inline std::optional<Controls> track(FlightOperation& op, const Aircraft& a, const Environment& env,
                                    double dt, double height, double climb, double speed) {
    op.target_enu_m = point(op.runway,op.along_m+100,0,height);
    const double normal_height = dot(a.position_enu_m-op.runway.threshold_enu_m,op.runway.normal_enu);
    const double vertical = climb*op.runway.normal_enu.z
        + dot(velocity_enu(a),op.runway.direction_enu)*op.runway.direction_enu.z;
    const double altitude_error = (height-normal_height)*op.runway.normal_enu.z;
    auto guidance_target = op.target_enu_m;
    guidance_target.z = a.position_enu_m.z + altitude_error + vertical/(.15* (1/model_parameters(env).controller_time_scale));
    op.navigation.waypoints = {{guidance_target,speed}};
    op.navigation.final_orbit=false;
    op.navigation.active_index = 0; op.navigation.loitering = false;
    op.navigation.initialized = true;
    op.navigation.leg_start_enu_m = point(op.runway,op.along_m-1000,0,height);
    return guide(op.navigation,a,dt,env);
}
}

// Controller limits and path dimensions are development choices, not measured
// takeoff/landing performance. No path obstacle survey is performed by this core.
inline std::optional<FlightOperation> start_takeoff(const Aircraft& a, const Runway& runway,
    const Environment& env, GroundPlane plane, double target_agl_m=50, double speed_mps=19) {
    using namespace operations_detail;
    if (!model_parameters(env).operating.automation_supported || !ready(a,runway,env,plane) || !std::isfinite(target_agl_m) || target_agl_m < 20 || target_agl_m > 300
        || !std::isfinite(speed_mps) || speed_mps < model_parameters(env).route_min_mps || speed_mps > model_parameters(env).route_max_mps) return std::nullopt;
    FlightOperation op;
    op.kind = FlightOperationKind::takeoff; op.runway = runway;
    op.target_agl_m = target_agl_m; op.target_speed_mps = speed_mps;
    telemetry(op,a);
    const auto ground = ground_data(a,plane,env);
    if (env.engine_power_fraction < .9 || env.elevator_jammed || env.aileron_jammed || env.rudder_jammed
        || !ground.on_ground || !gear_down_locked(a.gear) || ground.minimum_wheel_clearance_m < -.02
        || ground.minimum_wheel_clearance_m > .03 || op.along_m < 0
        || runway.length_m-op.along_m < 300 || std::abs(op.cross_track_m) > runway.width_m*.25
        || std::abs(wrap(a.heading_rad-heading(runway))) > 10*rad || norm(velocity_enu(a)) > 10) return std::nullopt;
    op.target_enu_m = point(runway,runway.length_m+400,0,target_agl_m);
    phase(op,FlightOperationPhase::takeoff_roll,"Accelerating on runway");
    return op;
}

inline std::optional<FlightOperation> start_landing(const Aircraft& a, const Runway& runway,
    const Environment& env, GroundPlane plane, double approach_speed_mps=18) {
    using namespace operations_detail;
    if (!model_parameters(env).operating.automation_supported || !ready(a,runway,env,plane) || !std::isfinite(approach_speed_mps)
        || approach_speed_mps < model_parameters(env).landing_min_mps || approach_speed_mps > model_parameters(env).landing_max_mps || ground_data(a,plane,env).on_ground
        || airspeed(a,env) < model_parameters(env).landing_min_mps*.75 || airspeed(a,env) > model_parameters(env).route_max_mps*1.5 || env.engine_power_fraction < .9
        || env.elevator_jammed || env.aileron_jammed || env.rudder_jammed) return std::nullopt;
    FlightOperation op;
    op.kind = FlightOperationKind::landing; op.runway = runway;
    op.target_agl_m=80; op.target_speed_mps=approach_speed_mps;
    telemetry(op,a);
    const double height=dot(a.position_enu_m-runway.threshold_enu_m,runway.normal_enu);
    if (height<10 || height>5000 || std::hypot(op.along_m,op.cross_track_m)>40000) return std::nullopt;
    prepare_return(op,a,env);
    if (!level_trim(a.position_enu_m,a.heading_rad,op.reference_speed_mps,env,a.time_s,a.flap_deg)) return std::nullopt;
    const bool configured=a.flap_deg>=model_parameters(env).maximum_flap_deg-.5;
    if (op.along_m < -100 && op.along_m > -op.final_distance_m && std::abs(op.cross_track_m)<40
        && std::abs(wrap(a.heading_rad-heading(runway)))<20*rad && height<150
        && airspeed(a,env)<=approach_speed_mps+3 && configured) {
        op.reference_speed_mps=approach_speed_mps;
        phase(op,FlightOperationPhase::final,"Tracking configured final approach");
    } else phase(op,FlightOperationPhase::returning,"Returning at current configuration; descending to approach orbit");

    return op;
}

// Landing-specific continuous orbit/line references; the ordinary mission route controller is unchanged.
inline std::optional<OperationCommand> update_landing(FlightOperation& op,const Aircraft& a,
    const Environment& env,GroundPlane plane,double dt) {
    using namespace operations_detail;
    using Phase=FlightOperationPhase;
    const auto& model=model_parameters(env);
    OperationCommand result;
    result.gear_down=true;
    const auto ground=ground_data(a,plane,env);
    if(a.ground.crashed || !plane.valid || !valid_ground_plane(plane)) {
        phase(op,Phase::aborted,"Landing aborted: aircraft crash or missing terrain");
        result.ground={0,1,true}; return result;
    }
    const auto velocity=velocity_enu(a);
    const double speed=airspeed(a,env),ground_speed=norm(velocity);
    const double height=dot(a.position_enu_m-op.runway.threshold_enu_m,op.runway.normal_enu);
    const double course=std::atan2(velocity.x,velocity.y), runway_course=heading(op.runway);
    const double course_error=wrap(course-runway_course);
    const bool approach=op.phase==Phase::final || op.phase==Phase::flare;
    if(approach && ground.on_ground) {
        phase(op,Phase::rollout,"Touchdown; braking and lowering nose gear");
    }
    if(op.phase==Phase::rollout) {
        ground_command(result,op,a,model);
        result.flap_deg=model.maximum_flap_deg;
        result.flight.elevator_deg=ground_speed>3 ? std::clamp(
            -(model.pitch_alpha*(-model.reference_pitch_rad)+model.flap_pitch_per_rad*a.flap_deg*rad)/model.pitch_elevator/rad
            +(a.pitch_rad+.4*model.controller_time_scale*a.body_rates_rps.y)/rad,-20.,20.) : 0;
        result.ground.brake=.7;
        if(ground_speed<.15 && ground.on_ground) {
            result.ground={0,1,true}; phase(op,Phase::complete,"Landed and parked");
        }
        return result;
    }
    if(approach && ((height<15 && (std::abs(op.cross_track_m)>op.runway.width_m*.35
        || std::abs(course_error)>20*rad || speed<model.landing_min_mps*.875
        || speed>model.landing_max_mps*1.09 || std::abs(a.roll_rad)>12*rad || !gear_down_locked(a.gear)))
        || op.along_m>op.runway.length_m*.65)) go_around(op,"Unstable approach; climbing on existing configuration");
    const auto radial=a.position_enu_m-op.return_center_enu_m;
    const double radius=std::hypot(radial.x,radial.y);
    double desired_course=std::atan2(radial.x,radial.y)+op.turn_direction*(3.14159265358979323846/2
        +std::atan(2*(radius-op.return_radius_m)/op.return_radius_m));
    double feed_bank=op.turn_direction*std::atan(ground_speed*ground_speed/(gravity_mps2*op.return_radius_m));
    double target_speed=op.reference_speed_mps,target_height=op.entry_height_m,climb=0;
    double flap_command=a.flap_target_deg;
    const double deploy_speed=std::max(op.target_speed_mps*1.25,model.landing_max_mps);
    if(op.phase==Phase::returning) {
        if(radius>op.return_radius_m*1.5) target_height=std::max(op.entry_height_m,op.return_floor_height_m);
        target_speed=std::clamp(a.flap_deg>.5 ? deploy_speed
            : std::max(speed,model.operating.initial_speed_mps),model.route_min_mps,model.route_max_mps);
        if(a.flap_deg<.1 || (speed>=deploy_speed && level_trim(a.position_enu_m,a.heading_rad,speed,env,a.time_s,0))) flap_command=0;
        if(std::abs(height-op.entry_height_m)<15 && radius<op.return_radius_m*1.3)
            phase(op,Phase::configuring,"Approach orbit acquired; decelerating before flap extension");
    }
    if(op.phase==Phase::configuring) {
        target_speed=deploy_speed;
        flap_command=speed<=deploy_speed+2 ? model.maximum_flap_deg : 0;
        if(a.flap_deg>=model.maximum_flap_deg-.5) target_speed=op.target_speed_mps;
        if(std::abs(height-op.entry_height_m)<10 && std::abs(speed-op.target_speed_mps)<2
            && a.flap_deg>=model.maximum_flap_deg-.5 && std::abs(op.along_m+op.final_distance_m)<std::max(80.,speed*2)
            && std::abs(op.cross_track_m)<std::max(40.,op.return_radius_m*.08)
            && std::abs(course_error)<12*rad) phase(op,Phase::aligning,"Approach gate captured; tracking runway extended centerline");
    }
    if(op.phase==Phase::go_around) {
        flap_command=std::max(a.flap_target_deg,a.flap_deg);
        target_speed=std::max(op.target_speed_mps,model.landing_max_mps);
        target_height=op.entry_height_m+20;
        if(height>op.entry_height_m && speed>model.route_min_mps*.95) {
            if(op.go_arounds>=2) phase(op,Phase::hold,"Landing suspended after two go-arounds; holding approach orbit");
            else phase(op,Phase::configuring,"Go-around climb complete; reconfiguring on approach orbit");
        }
    }
    if(op.phase==Phase::hold) {
        flap_command=model.maximum_flap_deg;target_speed=op.target_speed_mps;
    }
    if(op.phase==Phase::aligning || op.phase==Phase::final || op.phase==Phase::flare) {
        desired_course=runway_course-std::clamp(std::atan2(op.cross_track_m,std::max(400.,speed*10)),-20*rad,20*rad);
        feed_bank=0; flap_command=model.maximum_flap_deg; target_speed=op.target_speed_mps;
        if(op.phase==Phase::aligning) {
            if(op.along_m>-op.final_distance_m+200 && std::abs(op.cross_track_m)<op.runway.width_m*.3 && std::abs(course_error)<10*rad)
                phase(op,Phase::final,"Aligned final approach; descending on configured glide path");
            else if(op.along_m> -600) go_around(op,"Centerline not captured before final gate; going around");
        }
        if(op.phase==Phase::final || op.phase==Phase::flare) {
            const double slope=std::tan(3*rad);
            target_height=std::min(op.entry_height_m,model.cg_height_m+std::max(0.,80-op.along_m)*slope);
            climb=target_height<op.entry_height_m ? -slope*std::max(0.,dot(velocity,op.runway.direction_enu)) : 0;
            if(op.phase==Phase::final && height<model.cg_height_m+2.82*model.controller_time_scale && op.along_m>=-100)
                phase(op,Phase::flare,"Flare; controlling descent to wheel contact");
            if(op.phase==Phase::flare) {
                target_height=height;
                climb=-std::clamp(.4*(height-model.cg_height_m),.25,1.);
                target_speed=model.landing_min_mps;
            }
        }
    }
    // References change in simulation time, independently of render/playback speed.
    op.reference_speed_mps+=std::clamp(target_speed-op.reference_speed_mps,-.5*dt,.5*dt);
    double error=wrap(desired_course-course);
    if(std::abs(error)>135*rad) op.behind_turn=true;
    if(std::abs(error)<90*rad) op.behind_turn=false;
    if(op.behind_turn) error=op.turn_direction*std::abs(error);
    const double bank=std::clamp(feed_bank+error,-25*rad,25*rad);
    op.reference_bank_rad+=std::clamp(bank-op.reference_bank_rad,-3*rad*dt,3*rad*dt);
    const auto target=point(op.runway,op.along_m+std::max(400.,speed*10),0,target_height);
    op.target_enu_m=target;
    op.navigation.waypoints={{target,op.reference_speed_mps}};
    op.navigation.active_index=0;op.navigation.loitering=false;
    GuidanceReference reference{desired_course,op.reference_bank_rad,target.z,climb*op.runway.normal_enu.z};
    auto controls=guide(op.navigation,a,dt,env,&reference);
    if(!controls) { op.message="No supported trim for current speed/configuration reference";return std::nullopt; }
    result.flight=*controls;result.flap_deg=flap_command;
    return result;
}

inline std::optional<OperationCommand> update_flight_operation(FlightOperation& op, const Aircraft& a,
    const Environment& env, GroundPlane plane, double dt) {
    using namespace operations_detail;
    using Phase = FlightOperationPhase;
    if (!operation_active(op) || !valid_runway(op.runway) || !fixed_wing_detail::valid_aircraft(a)
        || !fixed_wing_detail::valid_environment(env) || !std::isfinite(dt) || dt <= 0 || dt > .05
        || !std::isfinite(op.phase_time_s) || !std::isfinite(op.total_time_s)) return std::nullopt;
    telemetry(op,a);
    op.phase_time_s += dt; op.total_time_s += dt;
    if(op.kind==FlightOperationKind::landing) return update_landing(op,a,env,plane,dt);
    OperationCommand result;
    const auto ground = ground_data(a,plane,env);
    const auto velocity = velocity_enu(a);
    const auto& model=model_parameters(env);
    const double speed_scale=model.route_min_mps/16.;
    const double speed = airspeed(a,env), ground_speed = norm(velocity);
    const double height = dot(a.position_enu_m-op.runway.threshold_enu_m,op.runway.normal_enu);
    result.flap_deg=op.kind==FlightOperationKind::landing && op.phase!=Phase::hold ? model.maximum_flap_deg : 0;
    result.gear_down=op.kind==FlightOperationKind::landing ? true : a.gear.target_down;
    // Keep gear down through liftoff. Retraction begins only above 10 m AGL
    // with positive climb; the current target then persists into departure hold.
    if (op.kind==FlightOperationKind::takeoff && !ground.on_ground
        && ground.available && ground.clearance_m>=10 && dot(velocity,plane.normal_enu)>0)
        result.gear_down=model.retractable_gear ? false : true;
    if (a.ground.crashed || !plane.valid || !valid_ground_plane(plane)) {
        phase(op,Phase::aborted,a.ground.crashed ? "Aircraft crashed" : "Ground surface unavailable");
        result.ground = {0,1,true};
        return result;
    }
    if (op.phase == Phase::takeoff_roll || op.phase == Phase::rotate) {
        const double remaining = op.runway.length_m-op.along_m;
        if (env.engine_power_fraction < .5 || std::abs(op.cross_track_m) > op.runway.width_m*.4
            || (ground.on_ground && remaining < ground_speed*ground_speed/(2*.4*gravity_mps2)+30)
            || op.phase_time_s > 30*speed_scale || (op.total_time_s > 10 && speed < model.route_min_mps*.5))
            phase(op,Phase::rejected_takeoff,"Rejected takeoff: acceleration, alignment or runway remaining");
        else if (op.phase == Phase::takeoff_roll && speed >= model.route_min_mps*.94)
            phase(op,Phase::rotate,"Rotating for liftoff");
        else if (!ground.on_ground && height > 1 && speed >= model.route_min_mps) {
            op.navigation = {};
    op.navigation.loiter_radius_m=minimum_loiter_radius_m(op.target_speed_mps)*1.5;
            phase(op,Phase::climb,"Climbing along runway departure");
        }
    }
    if (op.phase == Phase::takeoff_roll || op.phase == Phase::rotate) {
        ground_command(result,op,a,model_parameters(env));
        result.flight.throttle = 1;
        const double pitch = std::atan2(op.runway.direction_enu.z,std::hypot(op.runway.direction_enu.x,op.runway.direction_enu.y))
            + (op.phase == Phase::rotate ? 7*rad : 0);
        result.flight.elevator_deg = std::clamp(((a.pitch_rad-pitch)+.15*a.body_rates_rps.y)/rad,-20.,20.);
        return result;
    }
    if (op.phase == Phase::rejected_takeoff || op.phase == Phase::rollout) {
        ground_command(result,op,a,model_parameters(env));
        result.flight.elevator_deg = ground_speed > 3 ? std::clamp(
            -(model.pitch_alpha*(-model.reference_pitch_rad)+model.flap_pitch_per_rad*a.flap_deg*rad)/model.pitch_elevator/rad
            + (a.pitch_rad+ .4*model.controller_time_scale*a.body_rates_rps.y)/rad,-20.,20.) : 0;
        result.ground.brake = .7;
        if (ground_speed < .15 && ground.on_ground) {
            result.ground = {0,1,true};
            phase(op,op.phase == Phase::rollout ? Phase::complete : Phase::aborted,
                op.phase == Phase::rollout ? "Landed and parked" : "Rejected takeoff stopped");
        }
        return result;
    }
    if (op.phase == Phase::climb) {
        if (height >= op.target_agl_m-2 && speed >= op.target_speed_mps-2) {
            op.navigation = {};
    op.navigation.loiter_radius_m=minimum_loiter_radius_m(op.target_speed_mps)*1.5;
            op.target_enu_m = point(op.runway,op.runway.length_m+400,0,op.target_agl_m);
            op.navigation.waypoints = {{op.target_enu_m,op.target_speed_mps}};
            phase(op,Phase::hold,"Takeoff complete; flying departure hold");
        } else {
            auto command = track(op,a,env,dt,std::min(op.target_agl_m,height+(speed < op.target_speed_mps*.95 ? 5 : 20)),0,op.target_speed_mps);
            if (!command) return std::nullopt;
            result.flight = *command;
            return result;
        }
    }
    if (op.phase==Phase::hold) {
        auto command=guide(op.navigation,a,dt,env);
        if (!command) return std::nullopt;
        result.flight=*command;
        return result;
    }
    return std::nullopt;
}

} // namespace flight_core
