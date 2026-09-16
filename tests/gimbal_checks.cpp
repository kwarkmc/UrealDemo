#include "Gimbal.h"
#include "FixedWing.h"
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace flight_core;
namespace {
void require(bool ok,const char* message) {
    if (!ok) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
bool close(double a,double b,double tolerance=1e-10) { return std::abs(a-b)<tolerance; }
double distance(Vec3 a,Vec3 b) { return std::hypot(a.x-b.x,std::hypot(a.y-b.y,a.z-b.z)); }
}

int main() {
    const auto forward=gimbal_angles({5,0,0}), right=gimbal_angles({0,5,0});
    const auto left=gimbal_angles({0,-5,0}), behind=gimbal_angles({-5,0,0});
    require(forward && right && left && behind,"cardinal body rays are valid");
    require(close(forward->azimuth_deg,0) && close(forward->elevation_deg,0)
        && close(right->azimuth_deg,90) && close(left->azimuth_deg,-90)
        && close(std::abs(behind->azimuth_deg),180),"body cardinal directions use right-positive azimuth");
    require(!forward->limited && close(forward->error_deg,0),"forward look-at needs no limiting");
    const auto nadir=gimbal_angles({0,0,10}), zenith=gimbal_angles({0,0,-10});
    require(nadir && zenith && close(nadir->azimuth_deg,0) && close(nadir->elevation_deg,-90)
        && !nadir->limited && close(nadir->error_deg,0),"nadir uses zero azimuth and down-negative elevation");
    require(close(zenith->azimuth_deg,0) && close(zenith->elevation_deg,30)
        && zenith->limited && close(zenith->error_deg,60),"zenith is limited to 30 degrees with a 60-degree pointing error");
    const auto above=gimbal_angles({1,0,-std::sqrt(3.0)});
    require(above && above->limited && close(above->elevation_deg,30) && close(above->error_deg,30),
        "look-at reports the angular error introduced by the elevation stop");
    const auto diagonal=gimbal_direction(45,-45);
    require(diagonal && distance(*diagonal,{.5,.5,std::sqrt(.5)})<1e-14,
        "direction matches independently derived 45-degree Cartesian components");
    const auto roundtrip=gimbal_angles(*diagonal);
    require(roundtrip && close(roundtrip->azimuth_deg,45) && close(roundtrip->elevation_deg,-45)
        && !roundtrip->limited,"angle-to-ray roundtrip preserves an unrestricted direction");

    Aircraft aircraft;
    const auto north=gimbal_angles(enu_to_body(aircraft,{0,10,0}));
    const auto east=gimbal_angles(enu_to_body(aircraft,{10,0,0}));
    require(north && east && close(north->azimuth_deg,0) && close(east->azimuth_deg,90),
        "zero-attitude ENU targets enter the gimbal through the existing FRD boundary");
    aircraft.heading_rad=3.14159265358979323846/2;
    const auto rotated=gimbal_angles(enu_to_body(aircraft,{10,0,0}));
    require(rotated && close(rotated->azimuth_deg,0),"aircraft heading is removed before body-relative camera pointing");
    aircraft.heading_rad=0; aircraft.pitch_rad=3.14159265358979323846/6;
    const auto pitched=gimbal_angles(enu_to_body(aircraft,{0,10,0}));
    require(pitched && close(pitched->elevation_deg,-30),"nose-up aircraft sees the horizontal target below its body forward axis");

    require(target_in_fov({1,1,0},0,0,90,2) && target_in_fov({1,0,-.5},0,0,90,2),
        "rectangular field includes exact horizontal and vertical edges");
    require(!target_in_fov({1,1.01,0},0,0,90,2) && !target_in_fov({1,0,-.51},0,0,90,2),
        "field width and height are tested independently using aspect ratio");
    require(target_in_fov({1,.9,-.45},0,0,90,2),"rectangular image corners are retained instead of using a circular cone");
    require(!target_in_fov({-1,0,0},0,0,90) && !target_in_fov({0,1,0},0,0,90),
        "targets behind or perpendicular to the camera are outside the image");
    require(target_in_fov({0,1,0},90,0,60) && target_in_fov({0,0,1},0,-90,60),
        "field projection follows yawed and nadir camera orientations");
    const double enormous=std::numeric_limits<double>::max(), tiny=std::numeric_limits<double>::denorm_min();
    const auto large=gimbal_angles({enormous,enormous,0}), small=gimbal_angles({tiny,0,0});
    require(large && small && close(large->azimuth_deg,45) && close(small->azimuth_deg,0),
        "ray normalization handles finite extreme scales without overflow or division by zero");
    const double nan=std::numeric_limits<double>::quiet_NaN(), infinity=std::numeric_limits<double>::infinity();
    require(!gimbal_angles({}) && !gimbal_angles({nan,0,0}) && !gimbal_angles({0,infinity,0}),
        "zero-distance and nonfinite target rays are rejected");
    require(!gimbal_direction(nan,0) && !gimbal_direction(0,infinity)
        && !gimbal_direction(181,0) && !gimbal_direction(0,31),"invalid or unsupported camera angles are rejected");
    require(!target_in_fov({},0,0,60) && !target_in_fov({nan,0,0},0,0,60)
        && !target_in_fov({1,0,0},0,0,0) && !target_in_fov({1,0,0},0,0,180)
        && !target_in_fov({1,0,0},0,0,nan) && !target_in_fov({1,0,0},0,0,60,0)
        && !target_in_fov({1,0,0},0,0,60,infinity),"invalid projection inputs return false");
    std::cout<<"Gimbal checks passed: ideal look-at, elevation limits and rectangular virtual-camera FOV.\n";
}
