#pragma once

#include "FlightCore.h"
#include "AircraftParameters.h"
#include <algorithm>
#include <limits>

namespace flight_core {

constexpr double gravity_mps2 = 9.80665;
constexpr double reference_speed_mps = 19.0;
constexpr double reference_pitch_rad = 0.021425292448534562;

struct Environment {
    Vec3 wind_enu_mps{}; // Constant wind; air velocity = ground velocity - wind.
    double origin_altitude_m = 0.0; // Approximate geometric altitude = origin + local ENU Up.
    const FlightModelParameters* model=&flight_model_parameters; // Owner keeps this immutable object alive.
    double engine_power_fraction=1.0; // Instantaneous available fraction of developed thrust.
    bool elevator_jammed=false, aileron_jammed=false, rudder_jammed=false;
    double elevator_jam_deg=0, aileron_jam_deg=0, rudder_jam_deg=0;
    double gust_start_time_s=0, gust_duration_s=0; // Duration zero disables the pulse.
    Vec3 gust_peak_enu_mps{};
};

inline const FlightModelParameters& model_parameters(const Environment& environment) {
    return environment.model ? *environment.model : flight_model_parameters;
}

// Time-domain 1-cosine pulse: the FAA AC 25.341-1 shape, with an explicit
// duration instead of flight-path distance. This is NOT a certification gust.
inline Vec3 wind_at(const Environment& environment,double time_s) {
    const double t=time_s-environment.gust_start_time_s;
    if (environment.gust_duration_s<=0 || t<=0 || t>=environment.gust_duration_s) return environment.wind_enu_mps;
    const double fraction=.5*(1-std::cos(2*3.14159265358979323846*t/environment.gust_duration_s));
    return environment.wind_enu_mps+environment.gust_peak_enu_mps*fraction;
}

// U.S. Standard Atmosphere 1976: geopotential layers 0..11 and 11..20 km.
// Local ENU altitude is an explicit approximation, not a geoid conversion.
inline double isa_density_kg_m3(double geometric_altitude_m) {
    if (!std::isfinite(geometric_altitude_m) || geometric_altitude_m < -1000.0-1e-6
        || geometric_altitude_m > 20000.0+1e-6) return std::numeric_limits<double>::quiet_NaN();
    geometric_altitude_m=std::clamp(geometric_altitude_m,-1000.0,20000.0); // ENU inverse roundoff only.
    constexpr double earth_radius_m = 6356766.0, gas_constant = 287.05287;
    constexpr double t0 = 288.15, p0 = 101325.0, lapse = .0065;
    const double h = earth_radius_m * geometric_altitude_m / (earth_radius_m + geometric_altitude_m);
    const double lower_h = std::min(h, 11000.0);
    const double temperature = t0 - lapse * lower_h;
    double pressure = p0 * std::pow(temperature / t0, gravity_mps2 / (gas_constant * lapse));
    if (h > 11000.0) pressure *= std::exp(-gravity_mps2 * (h - 11000.0) / (gas_constant * temperature));
    return pressure / (gas_constant * temperature);
}

inline double equilibrium_separation(double alpha_rad,Environment environment={}) {
    const auto& p = model_parameters(environment);
    const double t = std::clamp((std::abs(alpha_rad) - p.stall_onset_rad)
        / (p.fully_separated_rad - p.stall_onset_rad), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace flight_core
