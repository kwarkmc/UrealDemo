#include "FlightCore.h"

#include <cstdio>
#include <limits>

using namespace flight_core;

bool near(Vec3 actual, Vec3 expected, double tolerance) {
    return finite(actual) && std::abs(actual.x - expected.x) <= tolerance
        && std::abs(actual.y - expected.y) <= tolerance
        && std::abs(actual.z - expected.z) <= tolerance;
}

bool near(std::optional<Vec3> actual, Vec3 expected, double tolerance) {
    return actual && near(*actual, expected, tolerance);
}

int main() {
    int failures = 0;
    const auto expect = [&failures](bool passed, const char* name) {
        if (!passed) {
            std::fprintf(stderr, "FAIL: %s\n", name);
            ++failures;
        }
    };

    // Numerical development tolerances, not terrain/aircraft acceptance criteria.
    // 1e-7 m allows roundoff from subtracting Earth-scale (~6.4e6 m) coordinates.
    constexpr double geo_tolerance_m = 1e-7;
    expect(near(geodetic_to_ecef({0, 0, 0}), {6378137, 0, 0}, geo_tolerance_m),
           "WGS84 equator / Greenwich semi-major axis");
    expect(near(geodetic_to_ecef({0, 90, 0}), {0, 6378137, 0}, geo_tolerance_m),
           "WGS84 equator / east longitude axis");
    expect(near(geodetic_to_ecef({90, 0, 0}), {0, 0, 6356752.314245179}, geo_tolerance_m),
           "WGS84 north-pole semi-minor axis");

    const Geodetic seoul{37.5665, 126.9780, 50.0}; // Coordinate example only.
    expect(near(geodetic_to_enu(seoul, seoul), {}, geo_tolerance_m), "ENU origin is zero");
    expect(near(geodetic_to_enu({37.5665, 126.9780, 150.0}, seoul),
                {0, 0, 100}, geo_tolerance_m), "ellipsoid-height change follows local Up");
    expect(near(ecef_to_enu({6378137, 1, 0}, {}), {1, 0, 0}, geo_tolerance_m),
           "equatorial ECEF +Y is East");
    expect(near(ecef_to_enu({6378137, 0, 1}, {}), {0, 1, 0}, geo_tolerance_m),
           "equatorial ECEF +Z is North");
    for (const Geodetic position : {Geodetic{33.43, 126.52, 2600}, Geodetic{33.5, 126.6, 2800},
                                   Geodetic{89.99, 179.99, -100}}) {
        const auto enu = geodetic_to_enu(position, seoul);
        const auto roundtrip = enu ? enu_to_geodetic(*enu, seoul) : std::nullopt;
        expect(roundtrip && std::abs(roundtrip->latitude_deg - position.latitude_deg) < 1e-9
            && std::abs(roundtrip->longitude_deg - position.longitude_deg) < 1e-9
            && std::abs(roundtrip->ellipsoid_height_m - position.ellipsoid_height_m) < 1e-6,
            "WGS84/ENU inverse roundtrip including near-pole and below ellipsoid");
    }

    const State initial{{10, 20, 30}, {1, 2, 3}};
    const auto coast = step(initial, {}, 2.0, 2.0);
    expect(coast && near(coast->position_m, {12, 24, 36}, 1e-12)
                 && near(coast->velocity_mps, {1, 2, 3}, 1e-12), "zero force, no hidden gravity");
    const auto accelerated = step(initial, {4, -8, 12}, 2.0, 2.0);
    expect(accelerated && near(accelerated->position_m, {16, 16, 48}, 1e-12)
                       && near(accelerated->velocity_mps, {5, -6, 15}, 1e-12),
           "constant net force, analytic position and velocity at t=2 s");
    std::optional<State> repeated = initial;
    for (int i = 0; i < 20 && repeated; ++i) repeated = step(*repeated, {4, -8, 12}, 2.0, 0.1);
    expect(repeated && near(repeated->position_m, {16, 16, 48}, 1e-12)
                    && near(repeated->velocity_mps, {5, -6, 15}, 1e-12),
           "twenty fixed steps reach the same analytic state");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    expect(!geodetic_to_ecef({91, 0, 0}) && !geodetic_to_ecef({0, -181, 0})
        && !geodetic_to_ecef({nan, 0, 0}) && !geodetic_to_ecef({0, 0, inf}),
           "reject out-of-range/nonfinite geodetic coordinates");
    expect(!ecef_to_enu({inf, 0, 0}, {}) && !ecef_to_enu({}, {0, nan, 0}),
           "reject invalid ECEF position or origin");
    expect(!step(initial, {}, 0, 1) && !step(initial, {}, -1, 1)
        && !step(initial, {}, inf, 1) && !step(initial, {}, 1, 0)
        && !step(initial, {}, 1, -1) && !step(initial, {}, 1, nan), "reject invalid mass/dt");
    expect(!step({{nan, 0, 0}, {}}, {}, 1, 1)
        && !step({{}, {0, inf, 0}}, {}, 1, 1)
        && !step(initial, {0, 0, nan}, 1, 1), "reject nonfinite state/force");
    expect(!step(initial, {std::numeric_limits<double>::max(), 0, 0}, 0.1, 1),
           "reject arithmetic overflow");

    if (failures != 0) return 1;
    std::puts("PASS: WGS84/ENU and point-mass development checks");
    return 0;
}
