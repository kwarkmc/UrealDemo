#pragma once

#include "FlightCore.h"
#include <algorithm>

namespace flight_core {

struct GimbalAngles {
    double azimuth_deg=0, elevation_deg=0;
    bool limited=false;
    double error_deg=0;
};

namespace gimbal_detail {
inline constexpr double rad=3.14159265358979323846/180;
inline std::optional<Vec3> unit_ray(Vec3 ray) {
    if (!finite(ray)) return std::nullopt;
    const double scale=std::max({std::abs(ray.x),std::abs(ray.y),std::abs(ray.z)});
    if (scale==0) return std::nullopt;
    ray={ray.x/scale,ray.y/scale,ray.z/scale};
    const double length=std::hypot(ray.x,std::hypot(ray.y,ray.z));
    return ray*(1/length);
}
inline double dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
}

// Ideal, instantaneous camera orientation in body forward/right/down axes.
// Azimuth is positive right, elevation positive up. These angle limits belong
// to this virtual camera; no actuator, image recognition or occlusion model.
inline std::optional<Vec3> gimbal_direction(double azimuth_deg,double elevation_deg) {
    if (!std::isfinite(azimuth_deg) || !std::isfinite(elevation_deg)
        || azimuth_deg < -180 || azimuth_deg > 180 || elevation_deg < -90 || elevation_deg > 30)
        return std::nullopt;
    const double azimuth=azimuth_deg*gimbal_detail::rad, elevation=elevation_deg*gimbal_detail::rad;
    const double horizontal=std::cos(elevation);
    return Vec3{horizontal*std::cos(azimuth),horizontal*std::sin(azimuth),-std::sin(elevation)};
}

inline std::optional<GimbalAngles> gimbal_angles(Vec3 direction_body_frd) {
    const auto target=gimbal_detail::unit_ray(direction_body_frd);
    if (!target) return std::nullopt;
    const double horizontal=std::hypot(target->x,target->y);
    GimbalAngles result;
    result.azimuth_deg=horizontal==0 ? 0 : std::atan2(target->y,target->x)/gimbal_detail::rad;
    const double requested_elevation=std::atan2(-target->z,horizontal)/gimbal_detail::rad;
    result.elevation_deg=std::clamp(requested_elevation,-90.0,30.0);
    result.limited=result.elevation_deg!=requested_elevation;
    const auto commanded=gimbal_direction(result.azimuth_deg,result.elevation_deg);
    if (!commanded) return std::nullopt;
    result.error_deg=std::acos(std::clamp(gimbal_detail::dot(*commanded,*target),-1.0,1.0))/gimbal_detail::rad;
    return result;
}

// Rectangular pinhole field of view. Aspect is width/height, so
// tan(vertical_FOV/2)=tan(horizontal_FOV/2)/aspect. Boundaries count as inside;
// points behind the camera and invalid inputs return false. No visibility claim.
inline bool target_in_fov(Vec3 direction_body_frd,double azimuth_deg,double elevation_deg,
                          double horizontal_fov_deg,double aspect=4.0/3.0) {
    const auto target=gimbal_detail::unit_ray(direction_body_frd);
    const auto forward=gimbal_direction(azimuth_deg,elevation_deg);
    if (!target || !forward || !std::isfinite(horizontal_fov_deg) || horizontal_fov_deg<=0
        || horizontal_fov_deg>=180 || !std::isfinite(aspect) || aspect<=0) return false;
    const double azimuth=azimuth_deg*gimbal_detail::rad, elevation=elevation_deg*gimbal_detail::rad;
    const Vec3 right{-std::sin(azimuth),std::cos(azimuth),0};
    const Vec3 up{-std::sin(elevation)*std::cos(azimuth),-std::sin(elevation)*std::sin(azimuth),-std::cos(elevation)};
    const double depth=gimbal_detail::dot(*target,*forward);
    if (depth<=0) return false;
    const double horizontal=std::tan(horizontal_fov_deg*gimbal_detail::rad*.5), vertical=horizontal/aspect;
    if (!std::isfinite(vertical)) return false;
    constexpr double roundoff=1e-12; // Unit-ray projection tolerance at an exact image edge.
    return std::abs(gimbal_detail::dot(*target,right))<=depth*horizontal+roundoff
        && std::abs(gimbal_detail::dot(*target,up))<=depth*vertical+roundoff;
}

} // namespace flight_core
