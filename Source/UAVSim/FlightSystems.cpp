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
    if (!Json->TryGetNumberField(TEXT("schema_version"), Version) || (Version != 1 && Version != 2)
        || !Json->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty() || Id.Len() > 80
        || !Json->TryGetObjectField(TEXT("parameters"), Parameters) || !Parameters || !Parameters->IsValid())
        return Fail(TEXT("Aircraft profile requires schema_version 1 or 2, id and parameters"));
    flight_core::FlightModelParameters Candidate;
    FString SupportMessage = TEXT("Thor-compatible development model; unqualified flight performance");
    if (Version == 2)
    {
        const TSharedPtr<FJsonObject>* Operating = nullptr;
        if (!Json->TryGetObjectField(TEXT("operating"), Operating) || !Operating || !Operating->IsValid()
            || (*Operating)->Values.Num() != 11
            || !Json->TryGetStringField(TEXT("support_message"), SupportMessage) || SupportMessage.IsEmpty() || SupportMessage.Len() > 512)
            return Fail(TEXT("Version 2 requires operating (8 numbers, 3 booleans) and support_message"));
        auto& Limits = Candidate.operating;
        Limits.restricted = true;
        auto Number = [&Operating](const TCHAR* Key, double& Value) {
            const auto Item = (*Operating)->TryGetField(Key);
            return Item.IsValid() && Item->Type == EJson::Number && Item->TryGetNumber(Value) && FMath::IsFinite(Value);
        };
        auto Boolean = [&Operating](const TCHAR* Key, bool& Value) {
            const auto Item = (*Operating)->TryGetField(Key);
            return Item.IsValid() && Item->Type == EJson::Boolean && Item->TryGetBool(Value);
        };
        if (!Number(TEXT("initial_speed_mps"), Limits.initial_speed_mps)
            || !Number(TEXT("initial_height_m"), Limits.initial_height_m)
            || !Number(TEXT("minimum_speed_mps"), Limits.minimum_speed_mps)
            || !Number(TEXT("maximum_speed_mps"), Limits.maximum_speed_mps)
            || !Number(TEXT("minimum_height_m"), Limits.minimum_height_m)
            || !Number(TEXT("maximum_height_m"), Limits.maximum_height_m)
            || !Number(TEXT("maximum_alpha_deg"), Limits.maximum_alpha_deg)
            || !Number(TEXT("maximum_beta_deg"), Limits.maximum_beta_deg)
            || !Boolean(TEXT("ground_supported"), Limits.ground_supported)
            || !Boolean(TEXT("navigation_supported"), Limits.navigation_supported)
            || !Boolean(TEXT("automation_supported"), Limits.automation_supported))
            return Fail(TEXT("Version 2 operating fields have invalid types or values"));
    }
    const TSharedPtr<FJsonObject>* Configuration = nullptr;
    if (Json->HasField(TEXT("configuration")))
    {
        if (Version != 2 || !Json->TryGetObjectField(TEXT("configuration"), Configuration) || !Configuration || !Configuration->IsValid()
            || !(*Configuration)->TryGetField(TEXT("retractable_gear")).IsValid()
            || (*Configuration)->TryGetField(TEXT("retractable_gear"))->Type != EJson::Boolean
            || !(*Configuration)->TryGetBoolField(TEXT("retractable_gear"), Candidate.retractable_gear))
            return Fail(TEXT("Invalid v2 configuration fields"));
        for (const auto& Field : flight_core::configuration_fields)
        {
            double Value = 0;
            const auto Item = (*Configuration)->TryGetField(UTF8_TO_TCHAR(Field.name));
            if (!Item.IsValid() || Item->Type != EJson::Number || !Item->TryGetNumber(Value) || !FMath::IsFinite(Value))
                return Fail(TEXT("Configuration requires every documented numeric field"));
            Candidate.*(Field.member) = Value;
        }
        int Expected = static_cast<int>(std::size(flight_core::configuration_fields))+1;
        for (const auto& Field : flight_core::refinement_fields)
        {
            const auto Item = (*Configuration)->TryGetField(UTF8_TO_TCHAR(Field.name));
            if (!Item.IsValid()) continue;
            double Value=0;
            if (Item->Type!=EJson::Number || !Item->TryGetNumber(Value) || !FMath::IsFinite(Value))
                return Fail(TEXT("Invalid optional refinement number"));
            Candidate.*(Field.member)=Value;
            ++Expected;
        }
        if ((*Configuration)->Values.Num()!=Expected) return Fail(TEXT("Unknown configuration field"));
    }
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
    const auto Position = flight_core::geodetic_to_enu({OriginLatitude, OriginLongitude, Candidate.operating.initial_height_m},
        {OriginLatitude, OriginLongitude, OriginHeight});
    const auto Initial = Position ? flight_core::level_trim(*Position, 0, Candidate.operating.initial_speed_mps, Environment) : std::nullopt;
    if (!Initial) return Fail(TEXT("Aircraft profile has no supported level trim at its declared initial condition"));
    flight_core::GroundPlane Plane;
    if (!Candidate.operating.ground_supported && QueryGround(Initial->aircraft.position_enu_m,
        {OriginLatitude, OriginLongitude, OriginHeight}, Plane)
        && flight_core::fixed_wing_detail::dot(Initial->aircraft.position_enu_m-Plane.point_enu_m, Plane.normal_enu)<=Candidate.span_m)
        return Fail(TEXT("Initial condition intersects the unsupported ground-contact boundary"));
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
    State.AirframeSchemaVersion = static_cast<int32>(Version);
    State.AirframeSupportMessage = SupportMessage;
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
    const auto& Limits = ModelParameters.operating;
    State.AirframeInitialSpeedMps = Limits.initial_speed_mps;
    State.AirframeInitialHeightM = Limits.initial_height_m;
    State.AirframeMinimumSpeedMps = Limits.minimum_speed_mps;
    State.AirframeMaximumSpeedMps = Limits.maximum_speed_mps;
    State.AirframeMinimumHeightM = Limits.minimum_height_m;
    State.AirframeMaximumHeightM = Limits.maximum_height_m;
    State.AirframeMaximumAlphaDeg = Limits.maximum_alpha_deg;
    State.AirframeMaximumBetaDeg = Limits.maximum_beta_deg;
    State.AirframeGroundSupported = Limits.ground_supported;
    State.AirframeNavigationSupported = Limits.navigation_supported;
    State.AirframeAutomationSupported = Limits.automation_supported;
    State.AirframeRetractableGear = ModelParameters.retractable_gear;
    State.AirframeRouteMinimumSpeedMps = ModelParameters.route_min_mps;
    State.AirframeRouteMaximumSpeedMps = ModelParameters.route_max_mps;
    State.AirframeLandingMinimumSpeedMps = ModelParameters.landing_min_mps;
    State.AirframeLandingMaximumSpeedMps = ModelParameters.landing_max_mps;
    State.AirframeMaximumFlapDeg = ModelParameters.maximum_flap_deg;
    State.FlapCommandDeg = Dynamics.flap_target_deg;
    State.FlapDeg = Dynamics.flap_deg;
    const auto Actual = flight_core::applied_controls(Dynamics, Inputs, FlightEnvironment);
    State.AppliedElevatorDeg = Actual.elevator_deg;
    State.AppliedAileronDeg = Actual.aileron_deg;
    State.AppliedRudderDeg = Actual.rudder_deg;
    State.AppliedThrottle = Actual.throttle;
    State.ThrustN = flight_core::air_data(Dynamics, FlightEnvironment, Inputs).thrust_n;
    State.DynamicPropeller=flight_core::dynamic_propeller(ModelParameters);
    const auto Aero=flight_core::air_data(Dynamics,FlightEnvironment,Inputs);
    const auto Prop=flight_core::propeller_data(Dynamics.propeller_omega_rps,Aero.speed_mps,Aero.density_kg_m3,Actual.throttle,FlightEnvironment.engine_power_fraction,ModelParameters);
    State.PropellerRpm=Dynamics.propeller_omega_rps*30/PI;
    State.ShaftDrivePowerW=Prop.drive_power_w;State.ShaftLoadPowerW=Prop.load_power_w;
    State.ShaftDriveTorqueNm=Prop.drive_torque_nm;State.PropellerLoadTorqueNm=Prop.load_torque_nm;
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
