#include "FlightScene.h"

bool AFlightScene::LocalRunway(int32 Index, flight_core::Runway& Out) const
{
    if (!Runways.IsValidIndex(Index)) return false;
    const auto& Strip = Runways[Index];
    const flight_core::Geodetic RenderOrigin{RenderOriginLatitude, RenderOriginLongitude, RenderOriginHeight};
    const flight_core::Geodetic Origin{OriginLatitude, OriginLongitude, OriginHeight};
    auto Local = [&](flight_core::Vec3 Position)
    {
        const auto Geo = flight_core::enu_to_geodetic(Position, RenderOrigin);
        return Geo ? flight_core::geodetic_to_enu(*Geo, Origin) : std::nullopt;
    };
    const auto Threshold = Strip.Center - Strip.Along * (Strip.LengthM * .5);
    const auto Start = Local(Threshold), Forward = Local(Threshold + Strip.Along * 100), Up = Local(Threshold + Strip.Normal * 100);
    if (!Start || !Forward || !Up || std::hypot(Start->x, Start->y) + Strip.LengthM + 2000 > 50000) return false;
    auto Unit = [](flight_core::Vec3 V) { return V * (1 / std::hypot(V.x, V.y, V.z)); };
    Out = {*Start, Unit(*Forward - *Start), Unit(*Up - *Start), Strip.LengthM, Strip.WidthM};
    return true;
}

bool AFlightScene::StartAutomaticTakeoff(int32 RunwayIndex, double ClimbHeightM, double SpeedMps)
{
    flight_core::Runway Strip;
    flight_core::GroundPlane Plane;
    if (!State.Fault.IsEmpty() || !State.TerrainLoaded || !LocalRunway(RunwayIndex, Strip)
        || !FMath::IsFinite(ClimbHeightM) || ClimbHeightM < 20 || ClimbHeightM > 150
        || !FMath::IsFinite(SpeedMps) || SpeedMps < 16 || SpeedMps > 24
        || !QueryGround(Dynamics.position_enu_m, {OriginLatitude, OriginLongitude, OriginHeight}, Plane)) return false;
    const auto Candidate = flight_core::start_takeoff(Dynamics, Strip, FlightEnvironment, Plane, ClimbHeightM, SpeedMps);
    if (!Candidate) return false;
    Operation = *Candidate;
    Navigation = {};
    SelectedRunway = RunwayIndex;
    return SyncState();
}

bool AFlightScene::StartAutomaticLanding(int32 RunwayIndex, double ApproachSpeedMps)
{
    flight_core::Runway Strip;
    flight_core::GroundPlane Plane;
    if (!State.Fault.IsEmpty() || !State.TerrainLoaded || !LocalRunway(RunwayIndex, Strip)
        || !FMath::IsFinite(ApproachSpeedMps) || ApproachSpeedMps < 16 || ApproachSpeedMps > 22
        || !QueryGround(Dynamics.position_enu_m, {OriginLatitude, OriginLongitude, OriginHeight}, Plane)) return false;
    const auto Candidate = flight_core::start_landing(Dynamics, Strip, FlightEnvironment, Plane, ApproachSpeedMps);
    if (!Candidate) return false;
    // Arming changes the target immediately; paused aircraft remain at their current extension.
    if (!flight_core::request_gear(Dynamics, true)) return false;
    Operation = *Candidate;
    Navigation = {};
    SelectedRunway = RunwayIndex;
    return SyncState();
}

void AFlightScene::CancelFlightOperation()
{
    Operation = {};
    SelectedRunway = -1;
    SyncState();
}

bool AFlightScene::PrepareLandingApproach(int32 RunwayIndex)
{
    flight_core::Runway Strip;
    if (!State.TerrainLoaded || !LocalRunway(RunwayIndex, Strip)) return false;
    const auto Position = Strip.threshold_enu_m - Strip.direction_enu * 600 + Strip.normal_enu * 60;
    const auto Geo = flight_core::enu_to_geodetic(Position, {OriginLatitude, OriginLongitude, OriginHeight});
    const auto Trim = flight_core::level_trim(Position, FMath::DegreesToRadians(Runways[RunwayIndex].HeadingDeg), 18, FlightEnvironment);
    if (!Geo || !Trim) return false;
    // This is the explicit test-position command, not part of the flight controller.
    return SetInitialCondition(Geo->latitude_deg, Geo->longitude_deg, Geo->ellipsoid_height_m,
        Runways[RunwayIndex].HeadingDeg, FMath::RadiansToDegrees(Trim->aircraft.pitch_rad), 0, 18);
}
