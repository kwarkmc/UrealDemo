#pragma once
#include "AircraftParameters.h"
#include <algorithm>

namespace flight_core {
inline bool dynamic_propeller(const FlightModelParameters& p) {return p.propeller_nominal_rpm>0 && p.shaft_power_w>0;}
inline double disk_shaft_power(double thrust,double speed,double density,const FlightModelParameters& p) {
    const double area=3.14159265358979323846*p.propeller_radius_m*p.propeller_radius_m;
    const double induced=.5*(std::sqrt(speed*speed+2*std::max(0.,thrust)/(density*area))-speed);
    return std::max(0.,thrust)*(speed+induced)/p.propeller_efficiency;
}
inline double disk_thrust(double shaft_power,double speed,double density,const FlightModelParameters& p) {
    if(shaft_power<=0) return 0;
    const double flow=2*density*3.14159265358979323846*p.propeller_radius_m*p.propeller_radius_m;
    const double power=shaft_power*p.propeller_efficiency;
    double induced=std::min(std::cbrt(power/flow),power/(flow*std::max(1.,speed*speed)));
    for(int i=0;i<12;++i) {
        const double v=speed+induced;
        induced=std::max(0.,induced-(flow*induced*v*v-power)/(flow*v*(speed+3*induced)));
    }
    return std::min(p.mass_kg*p.maximum_thrust_mps2,flow*induced*(speed+induced));
}
struct PropellerData {double drive_torque_nm=0,load_torque_nm=0,drive_power_w=0,load_power_w=0,thrust_n=0;};
inline PropellerData propeller_data(double omega,double speed,double density,double throttle,double power_fraction,const FlightModelParameters& p) {
    PropellerData out;if(!dynamic_propeller(p)) return out;
    const double nominal=p.propeller_nominal_rpm*3.14159265358979323846/30;
    const double lapse=std::clamp(density/1.225,0.,1.);
    // shortcut: declared quadratic fixed-load proxy, no identified blade map or governor; replace after same-configuration data exist.
    out.load_torque_nm=p.shaft_power_w*lapse*omega*omega/(nominal*nominal*nominal);
    out.drive_torque_nm=std::min(2*p.shaft_power_w/nominal,throttle*power_fraction*p.shaft_power_w*lapse/std::max(omega,.2*nominal));
    out.drive_power_w=out.drive_torque_nm*omega;out.load_power_w=out.load_torque_nm*omega;
    out.thrust_n=disk_thrust(out.load_power_w,speed,density,p);
    return out;
}
}
