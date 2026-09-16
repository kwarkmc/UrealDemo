#include "FlightScene.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool AFlightScene::ReloadAircraftProfile()
{
    return !State.Running && LoadAircraftProfile(true);
}

bool AFlightScene::LoadAircraftProfile(bool Reset)
{
    auto Fail = [this](const TCHAR* Error) { State.AirframeError = Error; return false; };
    const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("aircraft"));
    FString ActiveText, Text, Filename, Id;
    TSharedPtr<FJsonObject> Active, Json;
    const FString ActivePath = FPaths::Combine(Directory, TEXT("active.json"));
    const int64 ActiveSize = IFileManager::Get().FileSize(*ActivePath);
    if (ActiveSize <= 0 || ActiveSize > 1024 || !FFileHelper::LoadFileToString(ActiveText, *ActivePath)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ActiveText), Active) || !Active.IsValid()
        || !Active->TryGetStringField(TEXT("profile"), Filename) || Filename != FPaths::GetCleanFilename(Filename)
        || Filename.Contains(TEXT("..")) || Filename.Contains(TEXT(":")) || Filename.Contains(TEXT("\\"))
        || Filename.Contains(TEXT("/")) || FPaths::GetExtension(Filename).ToLower() != TEXT("json"))
        return Fail(TEXT("aircraft/active.json must select a JSON basename using profile"));
    const FString Path = FPaths::Combine(Directory, Filename);
    const int64 Size = IFileManager::Get().FileSize(*Path);
    if (Size <= 0 || Size > 65536 || !FFileHelper::LoadFileToString(Text, *Path)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
        return Fail(TEXT("Aircraft profile must be valid JSON up to 64 KB"));
    double Version = 0;
    const TSharedPtr<FJsonObject>* Parameters = nullptr;
    if (!Json->TryGetNumberField(TEXT("schema_version"), Version) || Version != 1
        || !Json->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty() || Id.Len() > 80
        || !Json->TryGetObjectField(TEXT("parameters"), Parameters) || !Parameters || !Parameters->IsValid())
        return Fail(TEXT("Aircraft profile requires schema_version 1, id and parameters"));
    flight_core::FlightModelParameters Candidate;
    int Count = 0;
    for (const auto& Field : flight_core::parameter_fields)
    {
        double Value = 0;
        const auto Item = (*Parameters)->TryGetField(UTF8_TO_TCHAR(Field.name));
        if (!Item.IsValid() || Item->Type != EJson::Number || !Item->TryGetNumber(Value) || !FMath::IsFinite(Value))
            return Fail(TEXT("Aircraft parameters require every documented numeric field"));
        Candidate.*(Field.member) = Value;
        ++Count;
    }
    if ((*Parameters)->Values.Num() != Count || !flight_core::valid_model_parameters(Candidate))
        return Fail(TEXT("Unknown aircraft parameter or invalid geometry, inertia, coefficients or response limits"));
    flight_core::Environment Environment;
    Environment.model = &Candidate;
    Environment.wind_enu_mps = FlightEnvironment.wind_enu_mps;
    Environment.origin_altitude_m = OriginHeight;
    const auto Position = flight_core::geodetic_to_enu({OriginLatitude, OriginLongitude, 2600},
        {OriginLatitude, OriginLongitude, OriginHeight});
    const auto Initial = Position ? flight_core::level_trim(*Position, 0, 19, Environment) : std::nullopt;
    if (!Initial) return Fail(TEXT("Aircraft profile has no supported 19 m/s initial level trim"));
    const FTCHARToUTF8 Encoded(*Text);
    uint8 Digest[20];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    ModelParameters = Candidate;
    FlightEnvironment = Environment;
    FlightEnvironment.model = &ModelParameters;
    State.AirframeId = Id;
    State.AirframeProfile = TEXT("aircraft/") + Filename;
    State.AirframeFingerprint = TEXT("sha1:") + BytesToHex(Digest, 20).ToLower();
    State.AirframeError.Empty();
    if (Reset)
    {
        Dynamics = Initial->aircraft;
        Inputs = Initial->controls;
        GroundInputs = {};
        Navigation = {};
        Operation = {};
        SelectedRunway = -1;
        State.Fault.Empty();
        State.DistanceTravelledM = 0;
        SetRunning(false);
        return SyncState();
    }
    return true;
}

bool AFlightScene::SetGust(double GustEastMps, double GustNorthMps, double GustUpMps, double DelayS, double DurationS)
{
    const flight_core::Vec3 Peak{GustEastMps, GustNorthMps, GustUpMps};
    if (!flight_core::finite(Peak) || std::abs(Peak.x) > 15 || std::abs(Peak.y) > 15 || std::abs(Peak.z) > 15
        || !FMath::IsFinite(DelayS) || DelayS < 0 || DelayS > 600
        || !FMath::IsFinite(DurationS) || DurationS < .1 || DurationS > 120) return false;
    FlightEnvironment.gust_peak_enu_mps = Peak;
    FlightEnvironment.gust_start_time_s = Dynamics.time_s + DelayS;
    FlightEnvironment.gust_duration_s = DurationS;
    return SyncState();
}

bool AFlightScene::SetFailures(double EnginePowerFraction, bool ElevatorJammed, bool AileronJammed, bool RudderJammed)
{
    if (!FMath::IsFinite(EnginePowerFraction) || EnginePowerFraction < 0 || EnginePowerFraction > 1) return false;
    const auto Actual = flight_core::applied_controls(Dynamics, Inputs, FlightEnvironment);
    // Capture on the false -> true transition, preserving the same mechanical stop on repeated requests.
    if (ElevatorJammed && !FlightEnvironment.elevator_jammed) FlightEnvironment.elevator_jam_deg = Actual.elevator_deg;
    if (AileronJammed && !FlightEnvironment.aileron_jammed) FlightEnvironment.aileron_jam_deg = Actual.aileron_deg;
    if (RudderJammed && !FlightEnvironment.rudder_jammed) FlightEnvironment.rudder_jam_deg = Actual.rudder_deg;
    FlightEnvironment.elevator_jammed = ElevatorJammed;
    FlightEnvironment.aileron_jammed = AileronJammed;
    FlightEnvironment.rudder_jammed = RudderJammed;
    FlightEnvironment.engine_power_fraction = EnginePowerFraction;
    Navigation.cached_trim.reset();
    return SyncState();
}

void AFlightScene::SyncSystemsState()
{
    const auto Actual = flight_core::applied_controls(Dynamics, Inputs, FlightEnvironment);
    State.AppliedElevatorDeg = Actual.elevator_deg;
    State.AppliedAileronDeg = Actual.aileron_deg;
    State.AppliedRudderDeg = Actual.rudder_deg;
    State.AppliedThrottle = Actual.throttle;
    State.ThrustN = flight_core::air_data(Dynamics, FlightEnvironment, Inputs).thrust_n;
    const auto Wind = flight_core::wind_at(FlightEnvironment, Dynamics.time_s);
    State.EffectiveWindEastMps = Wind.x;
    State.EffectiveWindNorthMps = Wind.y;
    State.EffectiveWindUpMps = Wind.z;
    State.GustEastMps = FlightEnvironment.gust_peak_enu_mps.x;
    State.GustNorthMps = FlightEnvironment.gust_peak_enu_mps.y;
    State.GustUpMps = FlightEnvironment.gust_peak_enu_mps.z;
    State.GustStartS = FlightEnvironment.gust_start_time_s;
    State.GustDurationS = FlightEnvironment.gust_duration_s;
    State.EnginePowerFraction = FlightEnvironment.engine_power_fraction;
    State.ElevatorJammed = FlightEnvironment.elevator_jammed;
    State.AileronJammed = FlightEnvironment.aileron_jammed;
    State.RudderJammed = FlightEnvironment.rudder_jammed;
}
