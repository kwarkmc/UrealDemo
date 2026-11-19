#pragma once

#include "FixedWing.h"

#include <algorithm>
#include <vector>

namespace flight_core {

struct Waypoint {
    Vec3 position_enu_m;
    double speed_mps = 19.0; // True airspeed; geometric guidance follows ground velocity.
};

struct GuidanceReference {
    double course_rad=0, bank_rad=0, height_m=0, climb_mps=0;
};

struct Navigation {
    std::vector<Waypoint> waypoints;
    std::size_t active_index = 0;
    bool loitering = false;
    double loiter_radius_m = 150.0;
    bool clockwise = true;
    bool final_orbit = true; // FlightOperations supplies straight-line references instead.

    // Per-run controller memory. Replace the entire Navigation when loading a route.
    bool initialized = false;
    Vec3 leg_start_enu_m;
    double climb_integral_rad = 0.0;
    double pitch_integral_rad = 0.0;
    double speed_integral = 0.0;
    bool recovering = false;
    bool reacquiring=false, opposite_turn=false;
    double turn_sign=1, rudder_integral_rps=0;
    double desired_course_rad=0, desired_bank_rad=0, desired_speed_mps=0, desired_height_m=0, desired_climb_mps=0;
    // Only pitch/controls are reused, never the cached aircraft position or velocity.
    std::optional<LevelTrim> cached_trim;
    double cached_trim_speed_mps = 0.0;
    double cached_origin_altitude_m = 0.0;
    double cached_wind_up_mps = 0.0;
    double cached_flap_deg = -1;
    const FlightModelParameters* cached_model = nullptr;
};

inline double minimum_loiter_radius_m(double speed_mps) {
    // Coordinated, level turn: tan(bank) = V^2/(g R), with a 30 degree bank limit.
    return std::max(100.0, speed_mps * speed_mps / (9.80665 * std::tan(0.5235987755982988)));
}

inline bool valid_route(const Navigation& navigation,const FlightModelParameters& model=flight_model_parameters) {
    if (navigation.waypoints.empty() || navigation.waypoints.size() > 64
        || navigation.active_index >= navigation.waypoints.size()
        || !std::isfinite(navigation.loiter_radius_m)
        || navigation.loiter_radius_m > 5000.0
        || (navigation.loitering && navigation.active_index + 1 != navigation.waypoints.size())) return false;
    for (const auto& waypoint : navigation.waypoints) {
        if (!finite(waypoint.position_enu_m) || !std::isfinite(waypoint.speed_mps)
            || waypoint.speed_mps < model.route_min_mps || waypoint.speed_mps > model.route_max_mps) return false;
    }
    return navigation.loiter_radius_m >= minimum_loiter_radius_m(navigation.waypoints.back().speed_mps);
}

// ENU geometry; course/heading = clockwise from North, radians. Controls alone are
// returned: this function never moves or reorients the aircraft. All gains below
// are development controller choices, NOT identified Thor coefficients.
// ponytail: no terrain avoidance, wind estimation, actuator limits beyond angles,
// or optimal route planning. Legs must leave room for a fixed-wing aircraft to turn.
inline std::optional<Controls> guide(Navigation& navigation, const Aircraft& aircraft,
                                     double dt_s, const Environment& environment = {}, const GuidanceReference* reference=nullptr) {
    if (reference && (!std::isfinite(reference->course_rad) || !std::isfinite(reference->bank_rad)
        || std::abs(reference->bank_rad)>.523598775598299 || !std::isfinite(reference->height_m)
        || !std::isfinite(reference->climb_mps))) return std::nullopt;
    if (!model_parameters(environment).operating.navigation_supported || !valid_route(navigation,model_parameters(environment)) || !finite(aircraft.position_enu_m)
        || !finite(aircraft.body_velocity_mps) || !finite(aircraft.body_rates_rps)
        || !std::isfinite(aircraft.roll_rad) || !std::isfinite(aircraft.pitch_rad)
        || !std::isfinite(aircraft.heading_rad)
        || !std::isfinite(aircraft.time_s) || aircraft.time_s < 0.0
        || !std::isfinite(aircraft.separation) || aircraft.separation < 0.0 || aircraft.separation > 1.0
        || !finite(environment.wind_enu_mps) || !std::isfinite(environment.origin_altitude_m)
        || !std::isfinite(dt_s) || dt_s <= 0.0 || dt_s > 0.05
        || (navigation.initialized && !finite(navigation.leg_start_enu_m))
        || !std::isfinite(navigation.climb_integral_rad)
        || !std::isfinite(navigation.pitch_integral_rad)
        || !std::isfinite(navigation.rudder_integral_rps) || std::abs(navigation.rudder_integral_rps)>.100001
        || !std::isfinite(navigation.speed_integral)) return std::nullopt;

    constexpr double pi = 3.14159265358979323846;
    constexpr double g = 9.80665;
    constexpr double bank_limit_rad = pi / 6.0;
    const auto wrap = [pi](double angle) { return std::remainder(angle, 2.0 * pi); };
    const double speed = airspeed(aircraft, environment);
    const Vec3 velocity = velocity_enu(aircraft);
    if (!std::isfinite(speed) || speed < 1.0 || !finite(velocity)) return std::nullopt;
    const auto aero = air_data(aircraft, environment);
    if (!std::isfinite(aero.alpha_rad) || !std::isfinite(aero.density_kg_m3)) return std::nullopt;

    // Recovery takes priority over route altitude. Hysteresis waits for both
    // reattachment and airspeed; integral memory cannot keep commanding a climb.
    const double alpha_limit = model_parameters(environment).stall_onset_rad;
    if (aero.stalled || std::abs(aero.alpha_rad) >= alpha_limit) navigation.recovering = true;
    if (navigation.recovering && std::abs(aero.alpha_rad) < .6 * alpha_limit
        && aero.separation < .15 && speed >= .9 * navigation.waypoints[navigation.active_index].speed_mps)
        navigation.recovering = false;
    if (navigation.recovering) {
        navigation.climb_integral_rad = navigation.pitch_integral_rad = navigation.speed_integral = 0.0;
        navigation.rudder_integral_rps=0;
        Controls recovery;
        recovery.elevator_deg = std::clamp((.75 * (aero.alpha_rad - 4.0 * pi / 180.0)
            + .18 * aircraft.body_rates_rps.y) * 180.0 / pi, -25.0, 25.0);
        recovery.aileron_deg = std::clamp((-.65 * aircraft.roll_rad
            - .12 * aircraft.body_rates_rps.x) * 180.0 / pi, -25.0, 25.0);
        recovery.throttle = 1.0;
        return valid_controls(recovery) ? std::optional<Controls>{recovery} : std::nullopt;
    }

    if (!navigation.initialized) {
        navigation.leg_start_enu_m = aircraft.position_enu_m;
        navigation.initialized = true;
    }
    const auto& destination = navigation.waypoints[navigation.active_index];
    const Vec3 error = destination.position_enu_m - aircraft.position_enu_m;
    // Capture is geometric and requires altitude/speed convergence; overshoot does
    // not skip a waypoint. The next leg starts at the previous waypoint coordinate.
    if (!reference && navigation.active_index+1<navigation.waypoints.size() && !navigation.loitering
        && std::hypot(error.x, error.y) <= std::max(30.0,minimum_loiter_radius_m(destination.speed_mps)*.25)
        && std::abs(error.z) <= 15.0 && std::abs(destination.speed_mps - speed) <= 2.0) {
        if (navigation.active_index + 1 < navigation.waypoints.size()) {
            navigation.leg_start_enu_m = destination.position_enu_m;
            ++navigation.active_index;
            navigation.reacquiring=false;
        } else {
            navigation.loitering = true;
        }
    }

    const auto& target = navigation.waypoints[navigation.active_index];
    const Vec3 displacement = target.position_enu_m - aircraft.position_enu_m;
    double desired_course = aircraft.heading_rad;
    double bank_feedforward = 0.0;
    if (navigation.final_orbit && navigation.active_index+1==navigation.waypoints.size()) {
        const double radial_course = std::atan2(-displacement.x, -displacement.y);
        const double radius = std::hypot(displacement.x, displacement.y);
        const double direction = navigation.clockwise ? 1.0 : -1.0;
        // Tangent vector plus inward/outward radial correction. The radial term
        // vanishes on the requested circle; centripetal acceleration supplies bank.
        desired_course = radial_course + direction * (pi / 2.0
            + std::atan(2.0 * (radius - navigation.loiter_radius_m) / navigation.loiter_radius_m));
        // Turn a ground-referenced circle; constant wind changes the speed around it.
        const double ground_speed = std::hypot(velocity.x, velocity.y);
        bank_feedforward = direction * std::atan(ground_speed * ground_speed / (g * navigation.loiter_radius_m));
        const double current_course=std::atan2(velocity.x,velocity.y);
        if (!reference && std::abs(radius-navigation.loiter_radius_m)<=std::max(30.,navigation.loiter_radius_m*.1)
            && std::abs(wrap(desired_course-current_course))<pi/9 && std::abs(displacement.z)<15
            && std::abs(target.speed_mps-speed)<2) navigation.loitering=true;
    } else {
        const double radius=minimum_loiter_radius_m(target.speed_mps);
        const double lookahead = std::max(60.0, radius*.8);
        const double runout = std::max(200.0, radius*2);
        Vec3 leg = target.position_enu_m - navigation.leg_start_enu_m;
        double length = std::hypot(leg.x, leg.y);
        if (length < 1.0) {
            // A point directly above us still needs a flyable approach line.
            navigation.leg_start_enu_m = target.position_enu_m
                - Vec3{std::sin(aircraft.heading_rad), std::cos(aircraft.heading_rad), 0.0} * runout;
            leg = target.position_enu_m - navigation.leg_start_enu_m;
            length = std::hypot(leg.x, leg.y);
        }
        Vec3 relative = aircraft.position_enu_m - navigation.leg_start_enu_m;
        double along = (relative.x * leg.x + relative.y * leg.y) / length;
        if (along>length+runout) navigation.reacquiring=true;
        if(navigation.reacquiring) {
            const Vec3 entry=target.position_enu_m-leg*(4*radius/length);
            const auto offset=entry-aircraft.position_enu_m;
            desired_course=std::atan2(offset.x,offset.y);
            if(std::hypot(offset.x,offset.y)<radius)
                navigation.reacquiring=false;
        } else {
            const double cross_track=(relative.x*leg.y-relative.y*leg.x)/length;
            desired_course=std::atan2(leg.x,leg.y)-std::atan2(cross_track,lookahead);
        }
    }

    const double course = std::hypot(velocity.x, velocity.y) > 1.0
        ? std::atan2(velocity.x, velocity.y) : aircraft.heading_rad;
    double course_error=wrap(desired_course-course);
    if(!navigation.opposite_turn && std::abs(course_error)>3*pi/4) {
        navigation.opposite_turn=true;navigation.turn_sign=std::copysign(1.,course_error);
    }
    if(std::abs(course_error)<pi/2) navigation.opposite_turn=false;
    if(navigation.opposite_turn) course_error=navigation.turn_sign*std::abs(course_error);
    const double bank_command = reference ? reference->bank_rad : std::clamp(bank_feedforward
        + course_error, -bank_limit_rad, bank_limit_rad);
    // Avoid solving the same attached equilibrium on every physics step. Heading
    // and horizontal wind do not change trim pitch or inputs; ISA height does.
    if (!navigation.cached_trim || std::abs(navigation.cached_trim_speed_mps-target.speed_mps) >= (reference ? .1 : 1e-12)
        || std::abs(aircraft.position_enu_m.z - navigation.cached_trim->aircraft.position_enu_m.z) >= .5
        || navigation.cached_origin_altitude_m != environment.origin_altitude_m
        || navigation.cached_wind_up_mps != environment.wind_enu_mps.z || navigation.cached_model != environment.model
        || std::abs(navigation.cached_flap_deg-aircraft.flap_deg) >= (reference ? .1 : 1e-12)) {
        // Nominal feedforward remains defined during faults/gusts; feedback sees actual airflow and applied controls.
        auto nominal = environment;
        nominal.engine_power_fraction = 1;
        nominal.elevator_jammed = nominal.aileron_jammed = nominal.rudder_jammed = false;
        nominal.gust_duration_s = 0;
        navigation.cached_trim = level_trim(aircraft.position_enu_m, aircraft.heading_rad, target.speed_mps, nominal,aircraft.time_s,aircraft.flap_deg);
        if (!navigation.cached_trim) return std::nullopt;
        navigation.cached_trim_speed_mps = target.speed_mps;
        navigation.cached_flap_deg = aircraft.flap_deg;
        navigation.cached_origin_altitude_m = environment.origin_altitude_m;
        navigation.cached_wind_up_mps = environment.wind_enu_mps.z;
        navigation.cached_model = environment.model;
    }
    const auto& trim = navigation.cached_trim;
    const double response=model_parameters(environment).controller_time_scale;
    const double height_error=reference ? reference->height_m-aircraft.position_enu_m.z : displacement.z;
    const double climb_command = std::clamp(0.15 / response * height_error
        +(reference ? reference->climb_mps : 0), -2.0*response, 2.0*response);
    navigation.desired_course_rad=reference ? reference->course_rad : desired_course;
    navigation.desired_bank_rad=bank_command;
    navigation.desired_speed_mps=target.speed_mps;
    navigation.desired_height_m=reference ? reference->height_m : target.position_enu_m.z;
    navigation.desired_climb_mps=climb_command;
    const double climb_error = climb_command - velocity.z;
    navigation.climb_integral_rad = std::clamp(navigation.climb_integral_rad
        + 0.025 / response * climb_error * dt_s, -0.18, 0.18);
    const double pitch_command = std::clamp(trim->aircraft.pitch_rad
        + 0.10 / response * climb_error + navigation.climb_integral_rad, -0.17, 0.22);
    const double pitch_error = pitch_command - aircraft.pitch_rad;
    navigation.pitch_integral_rad = std::clamp(navigation.pitch_integral_rad
        + 0.15 / response * pitch_error * dt_s, -0.15, 0.15);
    const double speed_error = target.speed_mps - speed;
    navigation.speed_integral = std::clamp(navigation.speed_integral
        + 0.025 / response * speed_error * dt_s, -0.4, 0.4);

    Controls controls;
    controls.elevator_deg = std::clamp(trim->controls.elevator_deg + (-1.0 * pitch_error + 0.12 * response * aircraft.body_rates_rps.y
        - navigation.pitch_integral_rad) * 180.0 / pi, -25.0, 25.0);
    controls.aileron_deg = std::clamp(trim->controls.aileron_deg+(0.65 * (bank_command - aircraft.roll_rad)
        - 0.12 * response * aircraft.body_rates_rps.x) * 180.0 / pi, -25.0, 25.0);
    const auto& model=model_parameters(environment);
    const double steady_course_rate=g*std::tan(aircraft.roll_rad)/std::max(speed,model.route_min_mps);
    const double yaw_target=steady_course_rate*std::cos(aircraft.pitch_rad)*std::cos(aircraft.roll_rad)
        +.5/response*aero.beta_rad+navigation.rudder_integral_rps;
    const double phat=aircraft.body_rates_rps.x*model.span_m/(2*speed),rhat=aircraft.body_rates_rps.z*model.span_m/(2*speed);
    const double baseline=model.yaw_beta*aero.beta_rad+model.yaw_roll_rate*phat+model.yaw_rate*rhat
        +model.yaw_aileron*applied_controls(aircraft).aileron_deg*pi/180;
    const double authority=aero.dynamic_pressure_pa*model.wing_area_m2*model.span_m*model.yaw_rudder/model.iz;
    if(std::abs(authority)>1e-6 && std::abs(model.yaw_rudder)>1e-6) {
        const double request=(2/response*(yaw_target-aircraft.body_rates_rps.z)/authority-baseline/model.yaw_rudder)*180/pi;
        controls.rudder_deg=std::clamp(request,-20.,20.);
        if(std::abs(request)<20 && !environment.rudder_jammed)
            navigation.rudder_integral_rps=std::clamp(navigation.rudder_integral_rps+.1/(response*response)*aero.beta_rad*dt_s,-.1,.1);
    }
    controls.throttle = std::clamp(trim->controls.throttle + 0.10 / response * speed_error + navigation.speed_integral, 0.0, 1.0);
    return valid_controls(controls) ? std::optional<Controls>{controls} : std::nullopt;
}

} // namespace flight_core
