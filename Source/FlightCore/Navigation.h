#pragma once

#include "FixedWing.h"

#include <algorithm>
#include <vector>

namespace flight_core {

struct Waypoint {
    Vec3 position_enu_m;
    double speed_mps = 19.0; // True airspeed; geometric guidance follows ground velocity.
};

struct Navigation {
    std::vector<Waypoint> waypoints;
    std::size_t active_index = 0;
    bool loitering = false;
    double loiter_radius_m = 150.0;
    bool clockwise = true;

    // Per-run controller memory. Replace the entire Navigation when loading a route.
    bool initialized = false;
    Vec3 leg_start_enu_m;
    double climb_integral_rad = 0.0;
    double pitch_integral_rad = 0.0;
    double speed_integral = 0.0;
    bool recovering = false;
    // Only pitch/controls are reused, never the cached aircraft position or velocity.
    std::optional<LevelTrim> cached_trim;
    double cached_trim_speed_mps = 0.0;
    double cached_origin_altitude_m = 0.0;
    double cached_wind_up_mps = 0.0;
    const FlightModelParameters* cached_model = nullptr;
};

inline double minimum_loiter_radius_m(double speed_mps) {
    // Coordinated, level turn: tan(bank) = V^2/(g R), with a 30 degree bank limit.
    return std::max(100.0, speed_mps * speed_mps / (9.80665 * std::tan(0.5235987755982988)));
}

inline bool valid_route(const Navigation& navigation) {
    if (navigation.waypoints.empty() || navigation.waypoints.size() > 64
        || navigation.active_index >= navigation.waypoints.size()
        || !std::isfinite(navigation.loiter_radius_m)
        || navigation.loiter_radius_m > 5000.0
        || (navigation.loitering && navigation.active_index + 1 != navigation.waypoints.size())) return false;
    for (const auto& waypoint : navigation.waypoints) {
        if (!finite(waypoint.position_enu_m) || !std::isfinite(waypoint.speed_mps)
            || waypoint.speed_mps < 16.0 || waypoint.speed_mps > 24.0) return false;
    }
    return navigation.loiter_radius_m >= minimum_loiter_radius_m(navigation.waypoints.back().speed_mps);
}

// ENU geometry; course/heading = clockwise from North, radians. Controls alone are
// returned: this function never moves or reorients the aircraft. All gains below
// are development controller choices, NOT identified Thor coefficients.
// ponytail: no terrain avoidance, wind estimation, actuator limits beyond angles,
// or optimal route planning. Legs must leave room for a fixed-wing aircraft to turn.
inline std::optional<Controls> guide(Navigation& navigation, const Aircraft& aircraft,
                                     double dt_s, const Environment& environment = {}) {
    if (!valid_route(navigation) || !finite(aircraft.position_enu_m)
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
    if (!navigation.loitering && std::hypot(error.x, error.y) <= 30.0
        && std::abs(error.z) <= 15.0 && std::abs(destination.speed_mps - speed) <= 2.0) {
        if (navigation.active_index + 1 < navigation.waypoints.size()) {
            navigation.leg_start_enu_m = destination.position_enu_m;
            ++navigation.active_index;
        } else {
            navigation.loitering = true;
        }
    }

    const auto& target = navigation.waypoints[navigation.active_index];
    const Vec3 displacement = target.position_enu_m - aircraft.position_enu_m;
    double desired_course = aircraft.heading_rad;
    double bank_feedforward = 0.0;
    if (navigation.loitering) {
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
    } else {
        const double lookahead = std::max(60.0, speed * 3.0);
        const double runout = std::max(200.0, speed * 10.0);
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
        if (along > length + runout) {
            // A missed capture must not trap the aircraft circling a point inside
            // its turn radius. Fly past, then establish a fresh return approach.
            navigation.leg_start_enu_m = aircraft.position_enu_m;
            leg = target.position_enu_m - navigation.leg_start_enu_m;
            length = std::hypot(leg.x, leg.y);
            along = 0.0;
        }
        const double fraction = std::clamp((along + lookahead) / length,
                                           0.0, 1.0 + (runout + lookahead) / length);
        const Vec3 aim = navigation.leg_start_enu_m + leg * fraction;
        desired_course = std::atan2(aim.x - aircraft.position_enu_m.x,
                                    aim.y - aircraft.position_enu_m.y);
    }

    const double course = std::hypot(velocity.x, velocity.y) > 1.0
        ? std::atan2(velocity.x, velocity.y) : aircraft.heading_rad;
    const double bank_command = std::clamp(bank_feedforward
        + 1.0 * wrap(desired_course - course), -bank_limit_rad, bank_limit_rad);
    // Avoid solving the same attached equilibrium on every physics step. Heading
    // and horizontal wind do not change trim pitch or inputs; ISA height does.
    if (!navigation.cached_trim || navigation.cached_trim_speed_mps != target.speed_mps
        || std::abs(aircraft.position_enu_m.z - navigation.cached_trim->aircraft.position_enu_m.z) >= .5
        || navigation.cached_origin_altitude_m != environment.origin_altitude_m
        || navigation.cached_wind_up_mps != environment.wind_enu_mps.z || navigation.cached_model != environment.model) {
        // Nominal feedforward remains defined during faults/gusts; feedback sees actual airflow and applied controls.
        auto nominal = environment;
        nominal.engine_power_fraction = 1;
        nominal.elevator_jammed = nominal.aileron_jammed = nominal.rudder_jammed = false;
        nominal.gust_duration_s = 0;
        navigation.cached_trim = level_trim(aircraft.position_enu_m, aircraft.heading_rad, target.speed_mps, nominal);
        if (!navigation.cached_trim) return std::nullopt;
        navigation.cached_trim_speed_mps = target.speed_mps;
        navigation.cached_origin_altitude_m = environment.origin_altitude_m;
        navigation.cached_wind_up_mps = environment.wind_enu_mps.z;
        navigation.cached_model = environment.model;
    }
    const auto& trim = navigation.cached_trim;
    const double climb_command = std::clamp(0.15 * displacement.z, -2.0, 2.0);
    const double climb_error = climb_command - velocity.z;
    navigation.climb_integral_rad = std::clamp(navigation.climb_integral_rad
        + 0.025 * climb_error * dt_s, -0.18, 0.18);
    const double pitch_command = std::clamp(trim->aircraft.pitch_rad
        + 0.10 * climb_error + navigation.climb_integral_rad, -0.17, 0.22);
    const double pitch_error = pitch_command - aircraft.pitch_rad;
    navigation.pitch_integral_rad = std::clamp(navigation.pitch_integral_rad
        + 0.15 * pitch_error * dt_s, -0.15, 0.15);
    const double speed_error = target.speed_mps - speed;
    navigation.speed_integral = std::clamp(navigation.speed_integral
        + 0.025 * speed_error * dt_s, -0.4, 0.4);

    Controls controls;
    controls.elevator_deg = std::clamp(trim->controls.elevator_deg + (-1.0 * pitch_error + 0.12 * aircraft.body_rates_rps.y
        - navigation.pitch_integral_rad) * 180.0 / pi, -25.0, 25.0);
    controls.aileron_deg = std::clamp((0.65 * (bank_command - aircraft.roll_rad)
        - 0.12 * aircraft.body_rates_rps.x) * 180.0 / pi, -25.0, 25.0);
    controls.rudder_deg = 0.0;
    controls.throttle = std::clamp(trim->controls.throttle + 0.10 * speed_error + navigation.speed_integral, 0.0, 1.0);
    return valid_controls(controls) ? std::optional<Controls>{controls} : std::nullopt;
}

} // namespace flight_core
