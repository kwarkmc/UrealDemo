#include "Navigation.h"

#include <cstdio>
#include <limits>

using namespace flight_core;

// Development regressions, not aircraft acceptance or formal reliability tests.
struct RunResult {
    bool finite_run = true;
    bool reached_final = false;
    bool sequential = true;
    double max_radius_error_m = 0.0;
    double max_height_error_m = 0.0;
    double max_speed_error_mps = 0.0;
    double signed_turn_rad = 0.0;
    std::size_t samples = 0;
    Aircraft aircraft;
};

RunResult simulate(Navigation navigation, Vec3 initial_position, double heading,
                   double duration_s = 360.0, const Environment& environment = {}) {
    RunResult result;
    auto initial = level_trim(initial_position, heading, 19.0, environment);
    if (!initial) { result.finite_run = false; return result; }
    result.aircraft = initial->aircraft;
    double reached_at = -1.0;
    double previous_radial = 0.0;
    constexpr double dt = 0.01;
    for (int i = 0; i < static_cast<int>(duration_s / dt); ++i) {
        const auto previous_index = navigation.active_index;
        auto controls = guide(navigation, result.aircraft, dt, environment);
        if (!controls) { result.finite_run = false; break; }
        auto advanced = step_aircraft(result.aircraft, *controls, dt, environment);
        if (!advanced) { result.finite_run = false; break; }
        result.aircraft = *advanced;
        result.sequential = result.sequential && navigation.active_index >= previous_index
            && navigation.active_index <= previous_index + 1;
        if (navigation.loitering && reached_at < 0.0) {
            reached_at = result.aircraft.time_s;
            result.reached_final = true;
        }
        // Allow entry transients to settle, then observe many seconds of actual
        // 6DOF flight instead of checking the guidance command in isolation.
        if (navigation.loitering && result.aircraft.time_s > reached_at + 120.0) {
            const auto& target = navigation.waypoints.back();
            const Vec3 radial = result.aircraft.position_enu_m - target.position_enu_m;
            const double angle = std::atan2(radial.x, radial.y);
            if (result.samples > 0)
                result.signed_turn_rad += std::remainder(angle - previous_radial,
                    2.0 * 3.14159265358979323846);
            previous_radial = angle;
            ++result.samples;
            result.max_radius_error_m = std::max(result.max_radius_error_m,
                std::abs(std::hypot(radial.x, radial.y) - navigation.loiter_radius_m));
            result.max_height_error_m = std::max(result.max_height_error_m, std::abs(radial.z));
            result.max_speed_error_mps = std::max(result.max_speed_error_mps,
                std::abs(airspeed(result.aircraft, environment) - target.speed_mps));
        }
    }
    return result;
}

int main() {
    int failures = 0;
    const auto expect = [&failures](bool passed, const char* name) {
        if (!passed) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
    };
    Navigation single;
    single.waypoints = {{{0.0, 800.0, 20.0}, 20.0}};
    expect(valid_route(single), "single waypoint with requested altitude, speed and orbit radius");
    Navigation invalid = single;
    invalid.waypoints.clear();
    expect(!valid_route(invalid), "empty route rejected");
    invalid = single;
    invalid.waypoints.resize(65, single.waypoints.front());
    expect(!valid_route(invalid), "route beyond 64 waypoints rejected");
    invalid = single;
    invalid.waypoints[0].speed_mps = 24.0;
    invalid.loiter_radius_m = 100.0;
    expect(!valid_route(invalid), "orbit requiring more than 30 degree bank rejected");
    invalid = single;
    invalid.waypoints[0].position_enu_m.x = std::numeric_limits<double>::quiet_NaN();
    expect(!valid_route(invalid), "nonfinite position rejected");
    invalid = single;
    invalid.waypoints[0].speed_mps = 15.0;
    expect(!valid_route(invalid), "speed outside local development envelope rejected");

    const auto verify = [&expect](const RunResult& run, bool clockwise, const char* name) {
        std::printf("%s: finite=%d arrived=%d samples=%zu radius_error=%.3fm height_error=%.3fm speed_error=%.3fm/s turn=%.3frad\n",
            name, run.finite_run, run.reached_final, run.samples, run.max_radius_error_m,
            run.max_height_error_m, run.max_speed_error_mps, run.signed_turn_rad);
        expect(run.finite_run && run.reached_final && run.sequential && run.samples >= 3000,
            "route completed sequentially through actual 6DOF integration");
        expect(run.max_radius_error_m <= 15.0, "settled orbit radius within 15 m development tolerance");
        expect(run.max_height_error_m <= 3.0, "settled target altitude within 3 m development tolerance");
        expect(run.max_speed_error_mps <= 0.5, "settled target airspeed within 0.5 m/s development tolerance");
        expect(clockwise ? run.signed_turn_rad > 3.0 : run.signed_turn_rad < -3.0,
            "final orbit progresses in requested direction");
    };
    const auto clockwise = simulate(single, {}, 0.0);
    verify(clockwise, true, "single clockwise");
    single.clockwise = false;
    const auto counterclockwise = simulate(single, {}, 0.0);
    verify(counterclockwise, false, "single counterclockwise");

    Navigation multiple;
    multiple.waypoints = {{{0.0, 600.0, 20.0}, 18.0},
                          {{600.0, 600.0, 40.0}, 22.0},
                          {{600.0, 0.0, 10.0}, 19.0}};
    verify(simulate(multiple, {}, 0.0, 480.0), true, "three waypoints");

    Navigation short_climb;
    short_climb.waypoints = {{{0.0, 30.0, 100.0}, 19.0},
                             {{30.0, 30.0, 100.0}, 19.0},
                             {{30.0, -300.0, 100.0}, 19.0}};
    verify(simulate(short_climb, {}, 0.0, 480.0), true, "close waypoint climb and short turning leg");

    Navigation fast_tight;
    fast_tight.waypoints = {{{0.0, 800.0, 20.0}, 24.0}};
    fast_tight.loiter_radius_m = 110.0;
    verify(simulate(fast_tight, {}, 0.0), true, "24 m/s and requested 110 m orbit");

    Navigation high_altitude;
    high_altitude.waypoints = {{{0.0, 800.0, 2620.0}, 20.0}};
    verify(simulate(high_altitude, {0.0, 0.0, 2600.0}, 0.0), true, "ISA altitude 2600 m");
    Environment crosswind;
    crosswind.wind_enu_mps = {3.0, 0.0, 0.0};
    verify(simulate(high_altitude, {0.0, 0.0, 2600.0}, 0.0, 360.0, crosswind), true,
        "ISA altitude 2600 m with constant 3 m/s eastward wind");

    Navigation recovery;
    recovery.waypoints = {{{0.0, 1500.0, 2600.0}, 19.0}};
    recovery.climb_integral_rad = .18;
    recovery.pitch_integral_rad = -.15;
    recovery.speed_integral = -.4;
    Aircraft stalled;
    stalled.position_enu_m = {0.0, 0.0, 2600.0};
    constexpr double rad = 3.14159265358979323846 / 180.0;
    stalled.pitch_rad = 18.0 * rad;
    stalled.roll_rad = 20.0 * rad;
    stalled.body_velocity_mps = Vec3{9.0 * std::cos(18.0 * rad), 0.0, 9.0 * std::sin(18.0 * rad)}
        + enu_to_body(stalled, crosswind.wind_enu_mps);
    stalled.separation = .95;
    auto recovery_controls = guide(recovery, stalled, .01, crosswind);
    expect(recovery_controls && recovery.recovering && recovery_controls->throttle == 1.0
        && recovery_controls->elevator_deg > 0.0 && recovery_controls->aileron_deg < 0.0
        && recovery.climb_integral_rad == 0.0 && recovery.pitch_integral_rad == 0.0
        && recovery.speed_integral == 0.0, "stall recovery overrides climb, unloads, levels wings and clears integrals");
    bool finite_recovery = true;
    double recovered_at = -1.0, minimum_height = stalled.position_enu_m.z;
    for (int i = 0; i < 4000; ++i) {
        recovery_controls = guide(recovery, stalled, .01, crosswind);
        const auto next = recovery_controls ? step_aircraft(stalled, *recovery_controls, .01, crosswind) : std::nullopt;
        if (!next) { finite_recovery = false; break; }
        stalled = *next;
        minimum_height = std::min(minimum_height, stalled.position_enu_m.z);
        if (!recovery.recovering && recovered_at < 0.0) recovered_at = stalled.time_s;
    }
    const auto recovered_air = air_data(stalled, crosswind);
    std::printf("stall recovery at 2600m with wind: finite=%d recovered_at=%.2fs height_loss=%.2fm alpha=%.2fdeg speed=%.2fm/s separation=%.3f\n",
        finite_recovery, recovered_at, 2600.0-minimum_height, recovered_air.alpha_rad/rad,
        recovered_air.speed_mps, recovered_air.separation);
    expect(finite_recovery && recovered_at > 0.0 && !recovery.recovering && !recovered_air.stall_warning
        && recovered_air.speed_mps >= 16.0, "stall recovery returns to attached flight through actual nonlinear integration");

    const auto repeated = simulate(single, {}, 0.0);
    expect(repeated.aircraft.position_enu_m.x == counterclockwise.aircraft.position_enu_m.x
        && repeated.aircraft.position_enu_m.y == counterclockwise.aircraft.position_enu_m.y
        && repeated.aircraft.position_enu_m.z == counterclockwise.aircraft.position_enu_m.z
        && repeated.aircraft.time_s == counterclockwise.aircraft.time_s,
        "fixed-step autopilot repeats exactly from identical initial state and route");
    if (failures) return 1;
    std::printf("PASS: route validation, single/multiple waypoint capture, persistent CW/CCW orbit and deterministic 6DOF control\n");
    return 0;
}
