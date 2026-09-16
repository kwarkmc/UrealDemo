#pragma once

#include "FlightCore.h"
#include <algorithm>
#include <array>

namespace flight_core {

struct GroundPlane {
    Vec3 point_enu_m{};
    Vec3 normal_enu{0,0,1}; // Unit normal, pointing out of the surface.
    bool valid=false;      // Missing terrain leaves the existing free-flight model unchanged.
};

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
};

// All landing-gear geometry and contact coefficients below are DECLARED
// development assumptions for the 1.27 m span proxy. No Thor gear data exist here.
inline constexpr std::array<Vec3,3> ground_wheel_points_body_m{{
    {.35,0,.18}, {-.12,-.22,.18}, {-.12,.22,.18}}}; // Nose, left main, right main; FRD.
inline constexpr std::array<Vec3,6> ground_body_points_body_m{{
    {0,0,.05}, {0,0,-.05}, {.425,0,.04}, {-.425,0,.04}, {0,-.635,-.021}, {0,.635,-.021}}};

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
    return finite(p.point_enu_m) && finite(p.normal_enu) && std::abs(length-1)<1e-6 && p.normal_enu.z>0;
}

} // namespace flight_core
