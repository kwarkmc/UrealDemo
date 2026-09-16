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
    idle, takeoff_roll, rotate, climb, hold, join, final, flare, rollout,
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
};

struct OperationCommand {
    Controls flight{0,0,0,0};
    GroundControls ground;
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
        && !ground_data(a,plane).crashed && std::abs(dot(env.wind_enu_mps,right(runway))) <= 5
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
inline void join(FlightOperation& op, const Aircraft& a) {
    telemetry(op,a);
    const double side = op.cross_track_m < 0 ? -1 : 1;
    op.navigation = {};
    op.navigation.waypoints = {
        {point(op.runway,std::max(100.,op.along_m),side*400,80),op.target_speed_mps},
        {point(op.runway,-1200,side*400,80),op.target_speed_mps},
        {point(op.runway,-1200,0,80),op.target_speed_mps},
        {point(op.runway,-600,0,60),op.target_speed_mps}};
    phase(op,FlightOperationPhase::join,"Joining runway approach pattern");
}
inline void go_around(FlightOperation& op, const char* message) {
    ++op.go_arounds;
    op.navigation = {};
    op.target_enu_m = point(op.runway,std::max(op.along_m+500,op.runway.length_m+200),0,80);
    op.navigation.waypoints = {{op.target_enu_m,19}};
    phase(op,FlightOperationPhase::go_around,message);
}
inline void ground_command(OperationCommand& command, const FlightOperation& op, const Aircraft& a) {
    const double speed = norm(velocity_enu(a));
    const double desired = heading(op.runway) - std::atan2(op.cross_track_m,std::max(20.,speed*2));
    const double error = wrap(desired-a.heading_rad);
    const double yaw = 1.2*error-.15*a.body_rates_rps.z;
    command.ground.steering_deg = std::clamp(std::atan(.47*yaw/std::max(speed,2.))/rad,-20.,20.);
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
    guidance_target.z = a.position_enu_m.z + altitude_error + vertical/.15;
    op.navigation.waypoints = {{guidance_target,speed}};
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
    if (!ready(a,runway,env,plane) || !std::isfinite(target_agl_m) || target_agl_m < 20 || target_agl_m > 300
        || !std::isfinite(speed_mps) || speed_mps < 16 || speed_mps > 24) return std::nullopt;
    FlightOperation op;
    op.kind = FlightOperationKind::takeoff; op.runway = runway;
    op.target_agl_m = target_agl_m; op.target_speed_mps = speed_mps;
    telemetry(op,a);
    const auto ground = ground_data(a,plane);
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
    if (!ready(a,runway,env,plane) || !std::isfinite(approach_speed_mps)
        || approach_speed_mps < 16 || approach_speed_mps > 22 || ground_data(a,plane).on_ground
        || airspeed(a,env) < 12 || airspeed(a,env) > 35 || env.engine_power_fraction < .9
        || env.elevator_jammed || env.aileron_jammed || env.rudder_jammed) return std::nullopt;
    FlightOperation op;
    op.kind = FlightOperationKind::landing; op.runway = runway;
    op.target_agl_m = 60; op.target_speed_mps = approach_speed_mps;
    telemetry(op,a);
    const double height = dot(a.position_enu_m-runway.threshold_enu_m,runway.normal_enu);
    if (height < 10 || height > 1000 || std::hypot(op.along_m,op.cross_track_m) > 5000) return std::nullopt;
    op.target_enu_m = point(runway,-600,0,60);
    if (op.along_m < -100 && op.along_m > -1200 && std::abs(op.cross_track_m) < 40
        && std::abs(wrap(a.heading_rad-heading(runway))) < 20*rad && height < 150)
        phase(op,FlightOperationPhase::final,"Tracking runway final approach");
    else join(op,a);
    return op;
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
    OperationCommand result;
    const auto ground = ground_data(a,plane);
    const auto velocity = velocity_enu(a);
    const double speed = airspeed(a,env), ground_speed = norm(velocity);
    const double height = dot(a.position_enu_m-op.runway.threshold_enu_m,op.runway.normal_enu);
    const double along_speed = dot(velocity,op.runway.direction_enu);
    result.gear_down=op.kind==FlightOperationKind::landing ? true : a.gear.target_down;
    // Keep gear down through liftoff. Retraction begins only above 10 m AGL
    // with positive climb; the current target then persists into departure hold.
    if (op.kind==FlightOperationKind::takeoff && !ground.on_ground
        && ground.available && ground.clearance_m>=10 && dot(velocity,plane.normal_enu)>0)
        result.gear_down=false;
    if (a.ground.crashed || !plane.valid || !valid_ground_plane(plane)) {
        phase(op,Phase::aborted,a.ground.crashed ? "Aircraft crashed" : "Ground surface unavailable");
        result.ground = {0,1,true};
        return result;
    }
    if (op.phase == Phase::takeoff_roll || op.phase == Phase::rotate) {
        const double remaining = op.runway.length_m-op.along_m;
        if (env.engine_power_fraction < .5 || std::abs(op.cross_track_m) > op.runway.width_m*.4
            || (ground.on_ground && remaining < ground_speed*ground_speed/(2*.4*gravity_mps2)+30)
            || op.phase_time_s > 30 || (op.total_time_s > 10 && speed < 8))
            phase(op,Phase::rejected_takeoff,"Rejected takeoff: acceleration, alignment or runway remaining");
        else if (op.phase == Phase::takeoff_roll && speed >= 15)
            phase(op,Phase::rotate,"Rotating for liftoff");
        else if (!ground.on_ground && height > 1 && speed >= 16) {
            op.navigation = {};
            phase(op,Phase::climb,"Climbing along runway departure");
        }
    }
    if (op.phase == Phase::takeoff_roll || op.phase == Phase::rotate) {
        ground_command(result,op,a);
        result.flight.throttle = 1;
        const double pitch = std::atan2(op.runway.direction_enu.z,std::hypot(op.runway.direction_enu.x,op.runway.direction_enu.y))
            + (op.phase == Phase::rotate ? 7*rad : 0);
        result.flight.elevator_deg = std::clamp(((a.pitch_rad-pitch)+.15*a.body_rates_rps.y)/rad,-20.,20.);
        return result;
    }
    if (op.phase == Phase::rejected_takeoff || op.phase == Phase::rollout) {
        ground_command(result,op,a);
        result.flight.elevator_deg = ground_speed > 3 ? 2 : 0;
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
            op.target_enu_m = point(op.runway,op.runway.length_m+400,0,op.target_agl_m);
            op.navigation.waypoints = {{op.target_enu_m,op.target_speed_mps}};
            phase(op,Phase::hold,"Takeoff complete; flying departure hold");
        } else {
            auto command = track(op,a,env,dt,std::min(op.target_agl_m,height+(speed < 18 ? 5 : 20)),0,op.target_speed_mps);
            if (!command) return std::nullopt;
            result.flight = *command;
            return result;
        }
    }
    if (op.phase == Phase::hold || op.phase == Phase::join || op.phase == Phase::go_around) {
        if (op.phase == Phase::join && op.navigation.active_index+1 == op.navigation.waypoints.size()
            && op.along_m < -200 && std::abs(op.cross_track_m) < 25
            && std::abs(wrap(std::atan2(velocity.x,velocity.y)-heading(op.runway))) < 15*rad) {
            op.navigation = {};
            phase(op,Phase::final,"Tracking runway final approach");
        } else if (op.phase == Phase::go_around && height > 65 && speed > 17) {
            if (op.go_arounds >= 2) phase(op,Phase::hold,"Landing aborted after two go-arounds; holding");
            else join(op,a);
        }
        if (op.phase != Phase::final) {
            if (!valid_route(op.navigation)) return std::nullopt;
            op.target_enu_m = op.navigation.waypoints[op.navigation.active_index].position_enu_m;
            auto command = guide(op.navigation,a,dt,env);
            if (!command) return std::nullopt;
            result.flight = *command;
            return result;
        }
    }
    if (op.phase == Phase::final || op.phase == Phase::flare) {
        if (ground.on_ground) {
            if (op.along_m < 0 || op.along_m > op.runway.length_m || std::abs(op.cross_track_m) > op.runway.width_m*.5)
                phase(op,Phase::rejected_takeoff,"Touchdown outside runway; stopping");
            else phase(op,Phase::rollout,"Touchdown; braking along runway");
            result.ground.brake = .7;
            result.flight.elevator_deg = 2;
            return result;
        }
        const double slope = .085;
        const double path_height = std::min(60.,.18+std::max(0.,80-op.along_m)*slope);
        const double course_error = wrap(std::atan2(velocity.x,velocity.y)-heading(op.runway));
        if ((height < 15 && (std::abs(op.cross_track_m) > op.runway.width_m*.35
                || std::abs(course_error) > 20*rad || speed < 14 || speed > 24 || std::abs(a.roll_rad) > 12*rad
                || !gear_down_locked(a.gear)))
            || (op.along_m > -100 && height-path_height > 12) || op.along_m > op.runway.length_m*.65) {
            go_around(op,"Unstable final; going around");
            auto command = guide(op.navigation,a,dt,env);
            if (!command) return std::nullopt;
            result.flight = *command;
            return result;
        }
        if (op.phase == Phase::final && height < 3 && op.along_m >= 0)
            phase(op,Phase::flare,"Flare and touchdown");
        const bool flare = op.phase == Phase::flare;
        const double climb = flare ? -std::clamp(.4*(height-.18),.25,1.)
            : (path_height < 60 ? -slope*std::max(0.,along_speed) : 0);
        auto command = track(op,a,env,dt,flare ? height : path_height,climb,
            flare ? 16 : op.target_speed_mps);
        if (!command) return std::nullopt;
        result.flight = *command;
        return result;
    }
    return std::nullopt;
}

} // namespace flight_core
