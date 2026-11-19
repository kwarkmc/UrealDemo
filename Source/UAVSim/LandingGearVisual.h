#pragma once
#include "FlightCore.h"
#include "AircraftParameters.h"

namespace landing_gear_visual {
// Display coordinates in metres: +X forward, +Y right, +Z up.
inline flight_core::Vec3 root(const flight_core::FlightModelParameters& p,int wheel) {
    return wheel==0 ? flight_core::Vec3{p.nose_x_m,0,-.8*p.body_half_height_m}
        : flight_core::Vec3{p.main_x_m,(wheel==1 ? -1 : 1)*.32*p.chord_m,-.3*p.body_half_height_m};
}
inline flight_core::Vec3 center(const flight_core::FlightModelParameters& p,int wheel) {
    return {wheel==0 ? p.nose_x_m : p.main_x_m,wheel==0 ? 0 : (wheel==1 ? -1 : 1)*p.half_track_m,
        -p.cg_height_m+p.wheel_radius_m};
}
inline flight_core::Vec3 stowed_direction(int wheel) {
    // shortcut: hypothetical nose-aft/main-forward bays, not measured retractable C208B hardware.
    return wheel==0 ? flight_core::Vec3{-1,0,.3} : flight_core::Vec3{1,wheel==1 ? .14 : -.14,.1};
}
}
