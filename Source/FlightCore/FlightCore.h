#pragma once

#include <cmath>
#include <optional>

namespace flight_core {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Geodetic {
    double latitude_deg = 0.0;          // North positive, [-90, 90].
    double longitude_deg = 0.0;         // East positive, [-180, 180].
    double ellipsoid_height_m = 0.0;    // WGS84 ellipsoid, NOT mean sea level.
};

struct State {
    Vec3 position_m;                    // Local ENU: x East, y North, z Up.
    Vec3 velocity_mps;
};

inline bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline bool valid(Geodetic p) {
    return std::isfinite(p.latitude_deg) && std::abs(p.latitude_deg) <= 90.0
        && std::isfinite(p.longitude_deg) && std::abs(p.longitude_deg) <= 180.0
        && std::isfinite(p.ellipsoid_height_m);
}

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 v, double s) { return {v.x * s, v.y * s, v.z * s}; }

// All conversion/step functions return nullopt for invalid inputs or nonfinite results.
// ECEF axes: x at lat/lon 0/0, y at 0/90E, z towards the North Pole; metres.
inline std::optional<Vec3> geodetic_to_ecef(Geodetic p) {
    if (!valid(p)) return std::nullopt;

    // WGS84 defining constants: https://earth-info.nga.mil/?action=wgs84&dir=wgs84
    constexpr double a = 6378137.0;
    constexpr double f = 1.0 / 298.257223563;
    constexpr double e2 = f * (2.0 - f);
    constexpr double rad = 3.14159265358979323846 / 180.0;
    const double lat = p.latitude_deg * rad;
    const double lon = p.longitude_deg * rad;
    const double sin_lat = std::sin(lat);
    const double cos_lat = std::cos(lat);
    const double n = a / std::sqrt(1.0 - e2 * sin_lat * sin_lat);

    // ESA Navipedia, equations (1)-(3):
    // https://gssc.esa.int/navipedia/index.php/Ellipsoidal_and_Cartesian_Coordinates_Conversion
    const Vec3 result{
        (n + p.ellipsoid_height_m) * cos_lat * std::cos(lon),
        (n + p.ellipsoid_height_m) * cos_lat * std::sin(lon),
        (n * (1.0 - e2) + p.ellipsoid_height_m) * sin_lat
    };
    return finite(result) ? std::optional<Vec3>{result} : std::nullopt;
}

// ENU is the tangent frame anchored at origin; heights must share the WGS84 datum.
inline std::optional<Vec3> ecef_to_enu(Vec3 ecef_m, Geodetic origin) {
    const auto origin_ecef = geodetic_to_ecef(origin);
    if (!finite(ecef_m) || !origin_ecef) return std::nullopt;

    constexpr double rad = 3.14159265358979323846 / 180.0;
    const double slat = std::sin(origin.latitude_deg * rad);
    const double clat = std::cos(origin.latitude_deg * rad);
    const double slon = std::sin(origin.longitude_deg * rad);
    const double clon = std::cos(origin.longitude_deg * rad);
    const Vec3 d = ecef_m - *origin_ecef;

    // ESA Navipedia, rotation matrix (6), applied to the origin-relative vector:
    // https://gssc.esa.int/navipedia/index.php/Transformations_between_ECEF_and_ENU_coordinates
    const Vec3 result{
        -slon * d.x + clon * d.y,
        -slat * clon * d.x - slat * slon * d.y + clat * d.z,
        clat * clon * d.x + clat * slon * d.y + slat * d.z
    };
    return finite(result) ? std::optional<Vec3>{result} : std::nullopt;
}

inline std::optional<Vec3> geodetic_to_enu(Geodetic position, Geodetic origin) {
    const auto ecef = geodetic_to_ecef(position);
    return ecef ? ecef_to_enu(*ecef, origin) : std::nullopt;
}

// Inverse ENU rotation, followed by the WGS84 fixed-point latitude solution.
inline std::optional<Geodetic> enu_to_geodetic(Vec3 enu, Geodetic origin) {
    const auto anchor = geodetic_to_ecef(origin);
    if (!anchor || !finite(enu)) return std::nullopt;
    constexpr double rad = 3.14159265358979323846 / 180.0;
    constexpr double a = 6378137.0, f = 1.0 / 298.257223563, e2 = f * (2 - f);
    const double slat = std::sin(origin.latitude_deg * rad), clat = std::cos(origin.latitude_deg * rad);
    const double slon = std::sin(origin.longitude_deg * rad), clon = std::cos(origin.longitude_deg * rad);
    const Vec3 ecef = *anchor + Vec3{
        -slon * enu.x - slat * clon * enu.y + clat * clon * enu.z,
        clon * enu.x - slat * slon * enu.y + clat * slon * enu.z,
        clat * enu.y + slat * enu.z};
    if (!finite(ecef)) return std::nullopt;
    const double rho = std::hypot(ecef.x, ecef.y);
    if (rho < 1e-8) return Geodetic{std::copysign(90.0, ecef.z), 0, std::abs(ecef.z) - a * (1 - f)};
    double lat = std::atan2(ecef.z, rho * (1 - e2));
    for (int i = 0; i < 12; ++i) {
        const double s = std::sin(lat), n = a / std::sqrt(1 - e2 * s * s);
        lat = std::atan2(ecef.z + e2 * n * s, rho);
    }
    const double s = std::sin(lat), c = std::cos(lat), n = a / std::sqrt(1 - e2 * s * s);
    const double h = rho * c + ecef.z * s - n * (1 - e2 * s * s);
    const Geodetic result{lat / rad, std::atan2(ecef.y, ecef.x) / rad, h};
    return valid(result) ? std::optional<Geodetic>{result} : std::nullopt;
}

// Point-mass demonstration, NOT a fixed-wing flight model.
// Caller supplies constant net force (including gravity) in ENU newtons and mass in kg.
// dt_s > 0 is explicit; a fixed-step caller repeats the same dt independently of rendering.
// Newton F=ma, exact constant-acceleration solution: p'=p+v*dt+a*dt^2/2, v'=v+a*dt.
// ponytail: fixed local inertial approximation; add rotation/aerodynamics/Earth rotation only with agreed models.
inline std::optional<State> step(const State& state, Vec3 net_force_n,
                                 double mass_kg, double dt_s) {
    if (!finite(state.position_m) || !finite(state.velocity_mps) || !finite(net_force_n)
        || !std::isfinite(mass_kg) || mass_kg <= 0.0
        || !std::isfinite(dt_s) || dt_s <= 0.0) return std::nullopt;

    const Vec3 acceleration{net_force_n.x / mass_kg,
                            net_force_n.y / mass_kg,
                            net_force_n.z / mass_kg};
    const Vec3 delta_v = acceleration * dt_s;
    const State result{
        state.position_m + (state.velocity_mps + delta_v * 0.5) * dt_s,
        state.velocity_mps + delta_v
    };
    if (!finite(result.position_m) || !finite(result.velocity_mps)) return std::nullopt;
    return result;
}

} // namespace flight_core
