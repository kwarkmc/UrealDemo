#pragma once

#include <cmath>
#include <iterator>

namespace flight_core {

struct FlightModelParameters {
    double reference_speed_mps=19.0, reference_pitch_rad=.021425292448534562;

    // Thor Table 1 geometry and inertia. The xz entries in I are -ixz.
    double mass_kg = 1.959, wing_area_m2 = .31, span_m = 1.27, chord_m = .25;
    double ix = .089, iy = .144, iz = .162, ixz = .014;

    // Dimensionless reconstruction of Eq.25/26 at V0=19, rho0=1.225.
    // Force derivatives: multiply by m/(q0*S), angle derivatives use
    // d/dalpha = -w0*d/du + u0*d/dw; rate derivatives use 2V0/c or 2V0/b.
    // Moment derivatives use Ix/(q0*S*b), Iy/(q0*S*c), Iz/(q0*S*b).
    double lift_ref = .2778231283194157, lift_alpha = 5.661853719148891;
    double lift_elevator = .1032147709712520, lift_pitch_rate = 9.800924979561476;
    double pitch_alpha = -.8609954332170188, pitch_speed = .01029582430006314;
    double pitch_rate = -21.13922594504695, pitch_elevator = -1.189648679058791;
    double side_beta = -.3475305775960639, side_roll_rate = .04528038436775074;
    double side_yaw_rate = .6718348048933765, side_aileron = .05887442350722793;
    double side_rudder = -.08516785536482487;
    double roll_beta = -.03923884969144804, roll_rate = -.3814675728169931;
    double roll_yaw_rate = .1238928364000659, roll_aileron = .1422127147493596;
    double roll_rudder = -.006665901510897373;
    double yaw_beta = .04596563435876604, yaw_roll_rate = .04788666935013784;
    double yaw_rate = -.1720579166185185, yaw_aileron = -.0320456770711721;
    double yaw_rudder = .04916648015216999;

    // Development assumptions, NOT measured Thor full-envelope data.
    // Drag polar reconstructs the former implicit 4 m/s^2 cruise thrust and
    // its local alpha derivative. Its unusually high baseline is uncalibrated.
    double drag_zero = .1125420898833130, induced_drag = .02268479187223978;
    double stall_onset_rad = 12.0 * 3.14159265358979323846 / 180.0;
    double fully_separated_rad = 20.0 * 3.14159265358979323846 / 180.0;
    double separation_time_s = .12, reattachment_time_s = .60;
    double plate_lift = 1.0, plate_drag = 2.0, plate_pitch = -.35;
    double separated_control_fraction = .15, separated_damping_fraction = .40;
    double sideslip_drag = .60;
    double maximum_thrust_mps2 = 8.0, constant_power_above_mps = 30.0;
    double engine_tau_s=.30, servo_tau_s=.08, servo_rate_deg_s=120.0;
};

inline constexpr FlightModelParameters flight_model_parameters{};

struct ParameterField {
    const char* name;
    double FlightModelParameters::* member;
    double minimum, maximum; // Numerical input bounds, NOT flight qualification limits.
};
inline constexpr ParameterField parameter_fields[]{
    {"reference_speed_mps", &FlightModelParameters::reference_speed_mps, 1, 100},
    {"reference_pitch_rad", &FlightModelParameters::reference_pitch_rad, -0.2, 0.2},
    {"mass_kg", &FlightModelParameters::mass_kg, 0.1, 10000},
    {"wing_area_m2", &FlightModelParameters::wing_area_m2, 0.001, 1000},
    {"span_m", &FlightModelParameters::span_m, 0.01, 100},
    {"chord_m", &FlightModelParameters::chord_m, 0.01, 100},
    {"ix", &FlightModelParameters::ix, 1e-05, 1000000},
    {"iy", &FlightModelParameters::iy, 1e-05, 1000000},
    {"iz", &FlightModelParameters::iz, 1e-05, 1000000},
    {"ixz", &FlightModelParameters::ixz, -1000, 1000},
    {"lift_ref", &FlightModelParameters::lift_ref, -1000, 1000},
    {"lift_alpha", &FlightModelParameters::lift_alpha, -1000, 1000},
    {"lift_elevator", &FlightModelParameters::lift_elevator, -1000, 1000},
    {"lift_pitch_rate", &FlightModelParameters::lift_pitch_rate, -1000, 1000},
    {"pitch_alpha", &FlightModelParameters::pitch_alpha, -1000, 1000},
    {"pitch_speed", &FlightModelParameters::pitch_speed, -1000, 1000},
    {"pitch_rate", &FlightModelParameters::pitch_rate, -1000, 1000},
    {"pitch_elevator", &FlightModelParameters::pitch_elevator, -1000, 1000},
    {"side_beta", &FlightModelParameters::side_beta, -1000, 1000},
    {"side_roll_rate", &FlightModelParameters::side_roll_rate, -1000, 1000},
    {"side_yaw_rate", &FlightModelParameters::side_yaw_rate, -1000, 1000},
    {"side_aileron", &FlightModelParameters::side_aileron, -1000, 1000},
    {"side_rudder", &FlightModelParameters::side_rudder, -1000, 1000},
    {"roll_beta", &FlightModelParameters::roll_beta, -1000, 1000},
    {"roll_rate", &FlightModelParameters::roll_rate, -1000, 1000},
    {"roll_yaw_rate", &FlightModelParameters::roll_yaw_rate, -1000, 1000},
    {"roll_aileron", &FlightModelParameters::roll_aileron, -1000, 1000},
    {"roll_rudder", &FlightModelParameters::roll_rudder, -1000, 1000},
    {"yaw_beta", &FlightModelParameters::yaw_beta, -1000, 1000},
    {"yaw_roll_rate", &FlightModelParameters::yaw_roll_rate, -1000, 1000},
    {"yaw_rate", &FlightModelParameters::yaw_rate, -1000, 1000},
    {"yaw_aileron", &FlightModelParameters::yaw_aileron, -1000, 1000},
    {"yaw_rudder", &FlightModelParameters::yaw_rudder, -1000, 1000},
    {"drag_zero", &FlightModelParameters::drag_zero, 0, 100},
    {"induced_drag", &FlightModelParameters::induced_drag, 0, 100},
    {"stall_onset_rad", &FlightModelParameters::stall_onset_rad, 0.01, 1},
    {"fully_separated_rad", &FlightModelParameters::fully_separated_rad, 0.02, 1.5},
    {"separation_time_s", &FlightModelParameters::separation_time_s, 0.001, 10},
    {"reattachment_time_s", &FlightModelParameters::reattachment_time_s, 0.001, 10},
    {"plate_lift", &FlightModelParameters::plate_lift, 0, 100},
    {"plate_drag", &FlightModelParameters::plate_drag, 0, 100},
    {"plate_pitch", &FlightModelParameters::plate_pitch, -1000, 1000},
    {"separated_control_fraction", &FlightModelParameters::separated_control_fraction, 0, 1},
    {"separated_damping_fraction", &FlightModelParameters::separated_damping_fraction, 0, 1},
    {"sideslip_drag", &FlightModelParameters::sideslip_drag, 0, 100},
    {"maximum_thrust_mps2", &FlightModelParameters::maximum_thrust_mps2, 0.001, 100},
    {"constant_power_above_mps", &FlightModelParameters::constant_power_above_mps, 1, 200},
    {"engine_tau_s", &FlightModelParameters::engine_tau_s, 0.001, 10},
    {"servo_tau_s", &FlightModelParameters::servo_tau_s, 0.001, 10},
    {"servo_rate_deg_s", &FlightModelParameters::servo_rate_deg_s, 1, 1000},
};

inline bool valid_model_parameters(const FlightModelParameters& p) {
    for (const auto& field:parameter_fields) {
        const double value=p.*(field.member);
        if (!std::isfinite(value) || value<field.minimum || value>field.maximum) return false;
    }
    // Positive inertia alone is insufficient: principal moments must also obey
    // the triangle inequalities. J=trace(I)/2*Identity-I must be semidefinite.
    const double half_trace=(p.ix+p.iy+p.iz)*.5;
    return p.ix*p.iz-p.ixz*p.ixz>1e-10
        && half_trace>=p.ix && half_trace>=p.iy && half_trace>=p.iz
        && (half_trace-p.ix)*(half_trace-p.iz)-p.ixz*p.ixz>=0
        && p.fully_separated_rad>p.stall_onset_rad && std::abs(p.pitch_elevator)>1e-6;
}

} // namespace flight_core
