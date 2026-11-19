#include "FlightScene.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

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
    if (!ModelParameters.operating.automation_supported) return false;
    flight_core::Runway Strip;
    flight_core::GroundPlane Plane;
    if (!State.Fault.IsEmpty() || !State.TerrainLoaded || !LocalRunway(RunwayIndex, Strip)
        || !FMath::IsFinite(ClimbHeightM) || ClimbHeightM < 20 || ClimbHeightM > 150
        || !FMath::IsFinite(SpeedMps) || SpeedMps < ModelParameters.route_min_mps || SpeedMps > ModelParameters.route_max_mps
        || !QueryGround(Dynamics.position_enu_m, {OriginLatitude, OriginLongitude, OriginHeight}, Plane)) return false;
    const auto Candidate = flight_core::start_takeoff(Dynamics, Strip, FlightEnvironment, Plane, ClimbHeightM, SpeedMps);
    if (!Candidate) return false;
    State.OperationRejection.Empty();
    Operation = *Candidate;
    Navigation = {};
    SelectedRunway = RunwayIndex;
    return SyncState();
}

bool AFlightScene::StartAutomaticLanding(int32 RunwayIndex, double ApproachSpeedMps)
{
    auto Reject=[this](const TCHAR* Message) { State.OperationRejection=Message;return false; };
    if (!ModelParameters.operating.automation_supported) return Reject(TEXT("profile_unsupported: automatic landing unavailable"));
    flight_core::Runway Strip;
    flight_core::GroundPlane Plane;
    if (!State.Fault.IsEmpty()) return Reject(TEXT("flight_fault: reset or resolve flight fault"));
    if (!State.TerrainLoaded || !LocalRunway(RunwayIndex,Strip)
        || !QueryGround(Dynamics.position_enu_m,{OriginLatitude,OriginLongitude,OriginHeight},Plane))
        return Reject(TEXT("terrain_unavailable: runway or flight terrain is unavailable"));
    if (!FMath::IsFinite(ApproachSpeedMps) || ApproachSpeedMps<ModelParameters.landing_min_mps || ApproachSpeedMps>ModelParameters.landing_max_mps)
        return Reject(TEXT("approach_speed_unsupported: target TAS outside profile limits"));
    auto Candidate=flight_core::start_landing(Dynamics,Strip,FlightEnvironment,Plane,ApproachSpeedMps);
    if (!Candidate) return Reject(TEXT("landing_not_ready: airborne state, local distance/height, wind, faults or current trim unsupported"));
    if (!PrepareLandingCorridor(*Candidate)) return false;
    if (!flight_core::request_gear(Dynamics,true,FlightEnvironment)) return Reject(TEXT("gear_rejected: landing gear cannot be deployed"));
    State.OperationRejection.Empty();
    Operation=*Candidate;Navigation={};SelectedRunway=RunwayIndex;
    Operation.navigation.desired_course_rad=Dynamics.heading_rad;
    Operation.navigation.desired_bank_rad=Operation.reference_bank_rad;
    Operation.navigation.desired_speed_mps=Operation.reference_speed_mps;
    Operation.navigation.desired_height_m=Operation.return_center_enu_m.z;
    ShowLandingPath(Operation);
    return SyncState();
}

bool AFlightScene::PrepareLandingCorridor(flight_core::FlightOperation& Candidate)
{
    using namespace flight_core;
    using namespace operations_detail;
    const Geodetic Origin{OriginLatitude,OriginLongitude,OriginHeight};
    const auto Base=Candidate;
    std::optional<FlightOperation> Best;
    for (double Side:{Base.turn_direction,-Base.turn_direction})
    {
        auto Trial=Base;Trial.turn_direction=Side;
        bool Valid=true;
        auto Height=[&](Vec3 At,double& HeightM) {
            GroundPlane Surface;
            if (std::hypot(At.x,At.y)>49000 || !QueryGround(At,Origin,Surface)) {Valid=false;return false;}
            HeightM=dot(Surface.point_enu_m-Trial.runway.threshold_enu_m,Trial.runway.normal_enu);
            return true;
        };
        for (int Iteration=0;Iteration<6 && Valid;++Iteration)
        {
            Trial.final_distance_m=std::max(600.,80+(Trial.entry_height_m-ModelParameters.cg_height_m)/std::tan(3*rad));
            Trial.return_center_enu_m=point(Trial.runway,-Trial.final_distance_m,Side*Trial.return_radius_m,Trial.entry_height_m);
            double Required=80;
            const int Count=FMath::CeilToInt(2*PI*Trial.return_radius_m/40);
            for (int I=0;I<=Count && Valid;++I) {
                const double Angle=2*PI*I/Count;
                const Vec3 At=Trial.return_center_enu_m+Trial.runway.direction_enu*(std::cos(Angle)*Trial.return_radius_m)
                    +right(Trial.runway)*(std::sin(Angle)*Trial.return_radius_m);
                double Ground=0;if(Height(At,Ground)) Required=std::max(Required,Ground+40);
            }
            if (Required<=Trial.entry_height_m+1) break;
            Trial.entry_height_m=Required;
            if (Trial.entry_height_m>500 || Iteration==5) Valid=false;
        }
        if(!Valid) continue;
        // Sample the nominal inbound corridor plus lateral margins and the final centerline.
        const auto Delta=Dynamics.position_enu_m-Trial.return_center_enu_m;
        const double Length=std::hypot(Delta.x,Delta.y);
        const Vec3 Entry=Trial.return_center_enu_m+Vec3{Delta.x,Delta.y,0}*(Trial.return_radius_m/std::max(Length,1.));
        const int Steps=std::max(1,int(FMath::CeilToInt(Length/40)));
        const auto Tangent=Entry-Dynamics.position_enu_m;
        const double Horizontal=std::max(1.,std::hypot(Tangent.x,Tangent.y));
        const Vec3 Across{-Tangent.y/Horizontal,Tangent.x/Horizontal,0};
        Trial.return_floor_height_m=Trial.entry_height_m;
        for(int I=0;I<=Steps && Valid;++I) for(double Offset:{-Trial.return_radius_m,0.,Trial.return_radius_m}) {
            const auto At=Dynamics.position_enu_m+Tangent*(double(I)/Steps)+Across*Offset;
            double Ground=0;if(Height(At,Ground)) Trial.return_floor_height_m=std::max(Trial.return_floor_height_m,Ground+40);
        }
        const double CurrentHeight=dot(Dynamics.position_enu_m-Trial.runway.threshold_enu_m,Trial.runway.normal_enu);
        // A configured direct final never flies the inbound/orbit altitude floor.
        if(Trial.phase!=FlightOperationPhase::final && CurrentHeight+5<Trial.return_floor_height_m) Valid=false;
        for(double Along=-Trial.final_distance_m;Along<=80 && Valid;Along+=40) {
            const double Desired=std::min(Trial.entry_height_m,ModelParameters.cg_height_m+std::max(0.,80-Along)*std::tan(3*rad));
            double Ground=0;
            if(Height(point(Trial.runway,Along,0,Desired),Ground) && Desired<Ground+std::min(10.,std::max(0.,-Along)/100)) Valid=false;
        }
        if(Valid && (!Best || Trial.entry_height_m<Best->entry_height_m)) Best=Trial;
    }
    if(!Best) {
        State.OperationRejection=TEXT("return_corridor_unavailable: sampled terrain coverage, clearance or current altitude cannot support the return/approach");
        return false;
    }
    Candidate=*Best;
    Candidate.target_agl_m=Candidate.entry_height_m;
    return true;
}

void AFlightScene::ShowLandingPath(const flight_core::FlightOperation& Candidate)
{
    using namespace flight_core;
    using namespace operations_detail;
    const Geodetic Origin{OriginLatitude,OriginLongitude,OriginHeight};
    const Geodetic Render{RenderOriginLatitude,RenderOriginLongitude,RenderOriginHeight};
    TArray<Vec3> Points;
    for(int I=0;I<=96;++I) {
        const double Angle=2*PI*I/96;
        Points.Add(Candidate.return_center_enu_m+Candidate.runway.direction_enu*(std::cos(Angle)*Candidate.return_radius_m)
            +right(Candidate.runway)*(std::sin(Angle)*Candidate.return_radius_m));
    }
    // A separate centerline ribbon represents the physical glide path, never the controller's feedforward point.
    for(int I=0;I<=40;++I) {
        const double Along=-Candidate.final_distance_m+(Candidate.final_distance_m+80)*I/40;
        const double Height=std::min(Candidate.entry_height_m,ModelParameters.cg_height_m+std::max(0.,80-Along)*std::tan(3*rad));
        Points.Add(point(Candidate.runway,Along,0,Height));
    }
    TArray<FVector> Vertices;TArray<int32> Indices;State.OperationPath.Empty();
    for(int I=0;I<Points.Num();++I) {
        const auto Geo=enu_to_geodetic(Points[I],Origin);
        const auto Position=Geo ? geodetic_to_enu(*Geo,Render) : std::nullopt;
        if(!Geo || !Position) continue;
        State.OperationPath.Add({Geo->latitude_deg,Geo->longitude_deg,Geo->ellipsoid_height_m,Candidate.target_speed_mps});
        const FVector At(Position->x*100,-Position->y*100,Position->z*100);
        Vertices.Add(At+FVector(0,-150,0));Vertices.Add(At+FVector(0,150,0));
        if(I>0 && I!=97) {const int A=Vertices.Num()-4;Indices.Append({A,A+2,A+1,A+1,A+2,A+3,A+1,A+2,A,A+3,A+2,A+1});}
    }
    LandingPath->ClearAllMeshSections();
    LandingPath->CreateMeshSection_LinearColor(0,Vertices,Indices,{},{},{},{},false);
    if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"))) {
        auto* Instance=UMaterialInstanceDynamic::Create(Material,this);
        Instance->SetVectorParameterValue(TEXT("Color"),FLinearColor(0,1,1));
        LandingPath->SetMaterial(0,Instance);
    }
}

void AFlightScene::CancelFlightOperation()
{
    Operation = {};
    SelectedRunway = -1;
    SyncState();
}

bool AFlightScene::PrepareLandingApproach(int32 RunwayIndex)
{
    if (!ModelParameters.operating.automation_supported) return false;
    flight_core::Runway Strip;
    if (!State.TerrainLoaded || !LocalRunway(RunwayIndex, Strip)) return false;
    const double Speed=State.AirframeSchemaVersion==1 ? 18 : (ModelParameters.landing_min_mps+ModelParameters.landing_max_mps)*.5;
    const double Flaps=ModelParameters.maximum_flap_deg;
    const auto Position=Strip.threshold_enu_m-Strip.direction_enu*1200+Strip.normal_enu*70;
    const auto Geo=flight_core::enu_to_geodetic(Position,{OriginLatitude,OriginLongitude,OriginHeight});
    const auto Trim=flight_core::level_trim(Position,FMath::DegreesToRadians(Runways[RunwayIndex].HeadingDeg),Speed,FlightEnvironment,0,Flaps);
    if(!Geo || !Trim) return false;
    // Explicit test initialization, unlike ordinary landing arming which never moves the aircraft.
    if(!SetInitialCondition(Geo->latitude_deg,Geo->longitude_deg,Geo->ellipsoid_height_m,
        Runways[RunwayIndex].HeadingDeg,FMath::RadiansToDegrees(Trim->aircraft.pitch_rad),0,std::max(Speed,ModelParameters.landing_max_mps))) return false;
    Dynamics.flap_deg=Dynamics.flap_target_deg=Flaps;
    Dynamics.body_velocity_mps=flight_core::Vec3{Speed*std::cos(Dynamics.pitch_rad),0,Speed*std::sin(Dynamics.pitch_rad)}
        +flight_core::enu_to_body(Dynamics,flight_core::wind_at(FlightEnvironment,0));
    Inputs=Trim->controls;Dynamics.actual_controls=Inputs;Dynamics.actuator_initialized=true;
    return SyncState();
}
