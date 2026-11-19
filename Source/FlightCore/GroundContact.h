#pragma once

#include "FlightCore.h"
#include "AircraftParameters.h"
#include <algorithm>
#include <array>

namespace flight_core {

struct WheelSurface {Vec3 point_enu_m{},normal_enu{0,0,1};bool valid=false;};

struct GroundPlane {
    Vec3 point_enu_m{};
    Vec3 normal_enu{0,0,1}; // Unit normal, pointing out of the surface.
    bool valid=false;      // Missing terrain leaves the existing free-flight model unchanged.
    bool per_wheel=false;
    std::array<WheelSurface,3> wheels{};
};
inline WheelSurface wheel_surface(const GroundPlane& plane,std::size_t wheel) {
    return plane.per_wheel ? plane.wheels[wheel] : WheelSurface{plane.point_enu_m,plane.normal_enu,plane.valid};
}

struct GroundControls {
    double steering_deg=0; // Nose wheel, positive right, +/-30 degrees.
    double brake=0;        // 0..1, all wheels; development proxy, not measured hardware.
    bool parking_brake=false;
};

struct GearState {
    bool target_down=true; // Command is separate from the physical deployment fraction.
    double extension=1.0;  // 0 stowed, 1 deployed; all three wheels move together.
};
// Development assumptions, NOT measured Thor retractable-gear data. The
// existing aerodynamic reference includes gear down; stowing reduces its Cd.
inline constexpr double gear_travel_time_s=3.0, gear_drag_coefficient=.012;
inline bool valid_gear(const GearState& gear) {
    return std::isfinite(gear.extension) && gear.extension>=0 && gear.extension<=1;
}
inline bool gear_down_locked(const GearState& gear) { return gear.target_down && gear.extension>=1-1e-12; }
inline bool gear_up_locked(const GearState& gear) { return !gear.target_down && gear.extension<=1e-12; }
inline GearState gear_after(GearState gear,double dt_s) {
    const double travel=dt_s/gear_travel_time_s;
    gear.extension=gear.target_down ? std::min(1.0,gear.extension+travel) : std::max(0.0,gear.extension-travel);
    if (gear.extension<1e-12) gear.extension=0;
    if (gear.extension>1-1e-12) gear.extension=1;
    return gear;
}

struct GroundState {
    bool available=false, on_ground=false;
    int wheels_on_ground=0;
    double normal_force_n=0; // Sum of normal reaction impulses / substep, NOT peak strut force.
    double clearance_m=0;   // CG signed normal distance from the sampled plane.
    double minimum_wheel_clearance_m=0;
    double impact_speed_mps=0; // Peak normal closing speed of the most recent touchdown.
    bool crashed=false, body_strike=false, gear_overload=false;
    std::array<double,3> wheel_normal_force_n{},wheel_clearance_m{},wheel_lateral_slip_mps{};
    bool per_wheel=false;
};

// All landing-gear geometry and contact coefficients below are DECLARED
// development assumptions for the 1.27 m span proxy. No Thor gear data exist here.
inline constexpr std::array<Vec3,3> ground_wheel_points_body_m{{
    {.35,0,.18}, {-.12,-.22,.18}, {-.12,.22,.18}}}; // Nose, left main, right main; FRD.
inline constexpr std::array<Vec3,6> ground_body_points_body_m{{
    {0,0,.05}, {0,0,-.05}, {.425,0,.04}, {-.425,0,.04}, {0,-.635,-.021}, {0,.635,-.021}}};

inline std::array<Vec3,3> wheel_points(const FlightModelParameters& p) {
    return {{{p.nose_x_m,0,p.cg_height_m}, {p.main_x_m,-p.half_track_m,p.cg_height_m},
        {p.main_x_m,p.half_track_m,p.cg_height_m}}};
}
inline std::array<Vec3,6> body_points(const FlightModelParameters& p) {
    return {{{0,0,p.body_half_height_m}, {0,0,-p.body_half_height_m},
        {p.body_half_length_m,0,p.body_half_height_m*.8}, {-p.body_half_length_m,0,p.body_half_height_m*.8},
        {0,-p.span_m*.5,-p.body_half_height_m*.42}, {0,p.span_m*.5,-p.body_half_height_m*.42}}};
}

struct GroundParameters {
    double rolling_friction=.025, brake_friction=.70, lateral_friction=.80;
    double impact_limit_mps=4.0; // Development hard-touchdown criterion, NOT a certified limit.
    double contact_substep_s=.002;
    int solver_iterations=12;
};
inline constexpr GroundParameters ground_parameters{};

inline bool valid_ground_controls(GroundControls c) {
    return std::isfinite(c.steering_deg) && std::abs(c.steering_deg)<=30
        && std::isfinite(c.brake) && c.brake>=0 && c.brake<=1;
}
inline bool valid_ground_plane(GroundPlane p) {
    if (!p.valid) return true;
    const double length=std::hypot(p.normal_enu.x,std::hypot(p.normal_enu.y,p.normal_enu.z));
    if(!finite(p.point_enu_m) || !finite(p.normal_enu) || std::abs(length-1)>=1e-6 || p.normal_enu.z<=0) return false;
    if(p.per_wheel) for(const auto& surface:p.wheels) {
        const auto n=surface.normal_enu;
        if(!surface.valid || !finite(surface.point_enu_m) || !finite(n) || n.z<=0 || std::abs(std::hypot(n.x,n.y,n.z)-1)>=1e-6) return false;
    }
    return true;
}

} // namespace flight_core
