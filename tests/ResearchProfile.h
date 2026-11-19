#pragma once
#include "AircraftParameters.h"
#include <fstream>
#include <regex>
#include <stdexcept>
using namespace flight_core;
// Test fixture reader only; product JSON parsing remains at the Unreal boundary.
inline FlightModelParameters research_profile(const char* path=RESEARCH_PROFILE) {
    std::ifstream file(path);
    const std::string text((std::istreambuf_iterator<char>(file)),{});
    if(text.empty()) throw std::runtime_error("Research profile is unreadable");
    FlightModelParameters p;
    for (const auto* fields:{&parameter_fields[0],&configuration_fields[0]}) {
        const auto count=fields==parameter_fields ? std::size(parameter_fields) : std::size(configuration_fields);
        for (std::size_t i=0;i<count;++i) {
            std::smatch match;
            if(!std::regex_search(text,match,std::regex(std::string("\"")+fields[i].name+"\"\\s*:\\s*([-+0-9.eE]+)"))) throw std::runtime_error("Missing profile number");
            p.*(fields[i].member)=std::stod(match[1]);
        }
    }
    for (const auto& field:refinement_fields) {
        std::smatch match;
        if(std::regex_search(text,match,std::regex(std::string("\"")+field.name+"\"\\s*:\\s*([-+0-9.eE]+)")))
            p.*(field.member)=std::stod(match[1]);
    }
    p.retractable_gear=std::regex_search(text,std::regex("\"retractable_gear\"\\s*:\\s*true"));
    p.operating={65,2600,0,110,-500,6000,180,89,true,true,true,true};
    return p;
}
