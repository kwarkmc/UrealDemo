#include "FlightScene.h"
#include "LandingGearVisual.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
using flight_core::Vec3;
using flight_core::Geodetic;
double Dot(Vec3 A, Vec3 B) { return A.x * B.x + A.y * B.y + A.z * B.z; }
FVector Display(Vec3 V) { return FVector(V.x, -V.y, V.z) * 100; }

// Vector rotation only; unlike a position, a normal has no origin translation.
Vec3 RotateEnu(Vec3 V, Geodetic From, Geodetic To)
{
    auto Basis = [](Geodetic P)
    {
        const double Lat = FMath::DegreesToRadians(P.latitude_deg), Lon = FMath::DegreesToRadians(P.longitude_deg);
        const double S = std::sin(Lat), C = std::cos(Lat), SL = std::sin(Lon), CL = std::cos(Lon);
        return std::array<Vec3, 3>{{{-SL, CL, 0}, {-S * CL, -S * SL, C}, {C * CL, C * SL, S}}};
    };
    const auto A = Basis(From), B = Basis(To);
    const auto Ecef = A[0] * V.x + A[1] * V.y + A[2] * V.z;
    return {Dot(Ecef, B[0]), Dot(Ecef, B[1]), Dot(Ecef, B[2])};
}

std::optional<flight_core::TerrainHit> RunwayHit(const FGroundRunway& Strip, double X, double Y)
{
    if (Strip.Normal.z <= .1) return std::nullopt;
    const Vec3 Point{X, Y, Strip.Center.z
        - (Strip.Normal.x * (X - Strip.Center.x) + Strip.Normal.y * (Y - Strip.Center.y)) / Strip.Normal.z};
    const Vec3 Delta = Point - Strip.Center;
    if (std::abs(Dot(Delta, Strip.Along)) > Strip.LengthM * .5
        || std::abs(Dot(Delta, Strip.Right)) > Strip.WidthM * .5) return std::nullopt;
    return flight_core::TerrainHit{Point, Strip.Normal};
}
}

bool AFlightScene::QueryGround(Vec3 Position, Geodetic Origin, flight_core::GroundPlane& Plane,
    FString* SurfaceName, double* EllipsoidHeight) const
{
    Plane = {};
    const Geodetic RenderOrigin{RenderOriginLatitude, RenderOriginLongitude, RenderOriginHeight};
    const auto Location = flight_core::enu_to_geodetic(Position, Origin);
    if (!Location) return false;
    auto Probe = *Location;
    Probe.ellipsoid_height_m = 0;
    std::optional<flight_core::TerrainHit> Hit;
    std::optional<Geodetic> GroundGeo;
    FString Name;
    // Find terrain beneath this latitude/longitude rather than underneath the
    // country-wide render frame's vertical ray, which leans relative to local Up.
    for (int Iteration = 0; Iteration < 4; ++Iteration)
    {
        const auto Render = flight_core::geodetic_to_enu(Probe, RenderOrigin);
        if (!Render) return false;
        Hit = GroundTerrain.query(Render->x, Render->y);
        Name = TEXT("terrain");
        const auto Shoulder = GroundRunwayShoulders.query(Render->x, Render->y);
        if (Shoulder && (!Hit || Shoulder->point_enu_m.z >= Hit->point_enu_m.z))
        {
            Hit = Shoulder;
            Name = TEXT("development runway shoulder");
        }
        for (const auto& Strip : Runways)
        {
            const auto Candidate = RunwayHit(Strip, Render->x, Render->y);
            if (Candidate && (!Hit || Candidate->point_enu_m.z >= Hit->point_enu_m.z))
            {
                Hit = Candidate;
                Name = Strip.Name;
            }
        }
        if (!Hit) return false;
        GroundGeo = flight_core::enu_to_geodetic(Hit->point_enu_m, RenderOrigin);
        if (!GroundGeo) return false;
        Probe.ellipsoid_height_m = GroundGeo->ellipsoid_height_m;
    }
    const auto Local = flight_core::geodetic_to_enu(*GroundGeo, Origin);
    if (!Local) return false;
    Plane = {*Local, RotateEnu(Hit->normal_enu, RenderOrigin, Origin), true};
    if (SurfaceName) *SurfaceName = Name;
    if (EllipsoidHeight) *EllipsoidHeight = GroundGeo->ellipsoid_height_m;
    return flight_core::valid_ground_plane(Plane);
}

bool AFlightScene::PrepareRunways(const FString& Directory, const flight_core::TerrainSurface& Surface,
    flight_core::TerrainSurface& Shoulders, Geodetic RenderOrigin, TArray<FGroundRunway>& Prepared,
    TArray<TObjectPtr<UProceduralMeshComponent>>& Meshes, FString& Error)
{
    const FString Path = FPaths::Combine(Directory, TEXT("runways.json"));
    if (!FPaths::FileExists(Path)) return true;
    auto Fail = [&Error](const TCHAR* Message) { Error = Message; return false; };
    FString Text;
    TSharedPtr<FJsonObject> Json;
    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
    const int64 Size = IFileManager::Get().FileSize(*Path);
    if (Size <= 0 || Size > 65536 || !FFileHelper::LoadFileToString(Text, *Path)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()
        || !Json->TryGetArrayField(TEXT("runways"), List) || List->Num() > 16)
        return Fail(TEXT("runways.json requires up to 16 development runway definitions (max 64 KB)"));
    auto Number = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double& Value)
    {
        const auto Field = Object->TryGetField(Key);
        return Field && Field->Type == EJson::Number && Field->TryGetNumber(Value) && FMath::IsFinite(Value);
    };
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!Material) return Fail(TEXT("Runway material is unavailable"));
    for (const auto& Value : *List)
    {
        const TSharedPtr<FJsonObject>* Definition = nullptr;
        FGroundRunway Strip;
        if (!Value || !Value->TryGetObject(Definition) || !Definition || !Definition->IsValid()
            || !(*Definition)->TryGetStringField(TEXT("name"), Strip.Name) || Strip.Name.IsEmpty() || Strip.Name.Len() > 120
            || !Number(*Definition, TEXT("latitude_deg"), Strip.Origin.latitude_deg)
            || !Number(*Definition, TEXT("longitude_deg"), Strip.Origin.longitude_deg)
            || !Number(*Definition, TEXT("heading_deg"), Strip.HeadingDeg)
            || !Number(*Definition, TEXT("length_m"), Strip.LengthM)
            || !Number(*Definition, TEXT("width_m"), Strip.WidthM)
            || !flight_core::valid(Strip.Origin) || std::abs(Strip.HeadingDeg) > 360
            || Strip.LengthM < 100 || Strip.LengthM > 3000 || Strip.WidthM < 10 || Strip.WidthM > 100)
            return Fail(TEXT("Invalid runway name, WGS84 coordinate, heading, length 100..3000m or width 10..100m"));
        const auto Base = flight_core::geodetic_to_enu(Strip.Origin, RenderOrigin);
        if (!Base || std::hypot(Base->x, Base->y) > 1e6) return Fail(TEXT("Runway is outside the render domain"));
        const double Heading = FMath::DegreesToRadians(Strip.HeadingDeg);
        Strip.Along = RotateEnu({std::sin(Heading), std::cos(Heading), 0}, Strip.Origin, RenderOrigin);
        Strip.Right = RotateEnu({std::cos(Heading), -std::sin(Heading), 0}, Strip.Origin, RenderOrigin);
        Strip.Normal = RotateEnu({0, 0, 1}, Strip.Origin, RenderOrigin);
        // The strip is an explicitly artificial flat platform. Place it above
        // the loaded mesh, in the runway's local tangent frame, not global Z.
        double Top = -20000;
        const int Steps = FMath::CeilToInt(Strip.LengthM / 5.0);
        for (int I = 0; I <= Steps; ++I)
            for (double Across : {-Strip.WidthM * .5, 0.0, Strip.WidthM * .5})
            {
                const Vec3 At = *Base + Strip.Along * (Strip.LengthM * (double(I) / Steps - .5)) + Strip.Right * Across;
                const auto Hit = Surface.query(At.x, At.y);
                if (!Hit) return Fail(TEXT("Runway footprint crosses missing terrain"));
                const double Height = Dot(Hit->point_enu_m - *Base, Strip.Normal);
                Top = FMath::Max(Top, Height);
            }
        Strip.Origin.ellipsoid_height_m = Top + .25;
        if ((*Definition)->HasField(TEXT("ellipsoid_height_m")))
        {
            if (!Number(*Definition, TEXT("ellipsoid_height_m"), Strip.Origin.ellipsoid_height_m)
                || Strip.Origin.ellipsoid_height_m < Top + .1 || Strip.Origin.ellipsoid_height_m > 20000)
                return Fail(TEXT("Explicit runway height must be above its terrain footprint and below 20000m"));
        }
        Strip.Center = *Base + Strip.Normal * Strip.Origin.ellipsoid_height_m;
        auto* Mesh = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
        Mesh->SetupAttachment(RootComponent);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // Same explicit plane in the independent core.
        Mesh->SetVisibility(false);
        AddInstanceComponent(Mesh);
        Mesh->RegisterComponent();
        Meshes.Add(Mesh);
        Mesh->SetRelativeLocation(Display(Strip.Center));
        auto Section = [&](int Index, const TArray<FVector4>& Rectangles, FLinearColor Color, double Raise)
        {
            TArray<FVector> Vertices, Normals;
            TArray<int32> Indices;
            TArray<FVector2D> UV;
            for (const auto& Rect : Rectangles)
            {
                const int32 A = Vertices.Num();
                for (const auto& Corner : {FVector2D(Rect.X, Rect.Y), FVector2D(Rect.Z, Rect.Y),
                                           FVector2D(Rect.Z, Rect.W), FVector2D(Rect.X, Rect.W)})
                {
                    Vertices.Add(Display(Strip.Along * Corner.X + Strip.Right * Corner.Y + Strip.Normal * Raise));
                    Normals.Add(Display(Strip.Normal).GetSafeNormal());
                    UV.Add(FVector2D::ZeroVector);
                }
                // Unreal's visible top face uses clockwise winding; shading normals remain Up.
                Indices.Append({A, A + 2, A + 1, A, A + 3, A + 2});
            }
            Mesh->CreateMeshSection_LinearColor(Index, Vertices, Indices, Normals, UV, {}, {}, false);
            auto* Instance = UMaterialInstanceDynamic::Create(Material, this);
            Instance->SetVectorParameterValue(TEXT("Color"), Color);
            Mesh->SetMaterial(Index, Instance);
        };
        const double L = Strip.LengthM * .5, W = Strip.WidthM * .5;
        Section(0, {FVector4(-L, -W, L, W)}, FLinearColor(.035f, .04f, .045f), 0);
        TArray<FVector4> Paint{FVector4(-L, -W + .4, L, -W + .65), FVector4(-L, W - .65, L, W - .4)};
        for (double X = -L + 20; X < L - 20; X += 25) Paint.Add(FVector4(X, -.2, X + 12, .2));
        Paint.Add(FVector4(-L + 8, -W + 2, -L + 10, W - 2));
        Paint.Add(FVector4(L - 10, -W + 2, L - 8, W - 2));
        Section(1, Paint, FLinearColor(.9f, .9f, .85f), .003);
        // Four trapezoids share their outer corners, closing the entire perimeter.
        // These artificial shoulders use the same triangles for display and contact.
        constexpr double ShoulderWidth = 20.0, SegmentLength = 20.0;
        const std::array<FVector2D, 4> Inner{{{-L, -W}, {L, -W}, {L, W}, {-L, W}}};
        const std::array<FVector2D, 4> Outer{{{-L - ShoulderWidth, -W - ShoulderWidth},
            {L + ShoulderWidth, -W - ShoulderWidth}, {L + ShoulderWidth, W + ShoulderWidth},
            {-L - ShoulderWidth, W + ShoulderWidth}}};
        auto* ShoulderMaterial = UMaterialInstanceDynamic::Create(Material, this);
        ShoulderMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(.22f, .20f, .16f));
        for (int Side = 0; Side < 4; ++Side)
        {
            const int Next = (Side + 1) % 4;
            const int Segments = FMath::CeilToInt((Outer[Next] - Outer[Side]).Size() / SegmentLength);
            TArray<FVector> Vertices, Normals;
            TArray<FVector2D> UV;
            TArray<int32> Indices;
            std::vector<Vec3> CoreVertices;
            std::vector<std::uint32_t> CoreIndices;
            for (int Row = 0; Row <= Segments; ++Row)
            {
                const double T = double(Row) / Segments;
                const FVector2D Inside = Inner[Side] * (1 - T) + Inner[Next] * T;
                const FVector2D Outside = Outer[Side] * (1 - T) + Outer[Next] * T;
                const Vec3 InnerPoint = Strip.Center + Strip.Along * Inside.X + Strip.Right * Inside.Y;
                const Vec3 Nominal = Strip.Center + Strip.Along * Outside.X + Strip.Right * Outside.Y;
                Vec3 Probe = Nominal;
                for (int Iteration = 0; Iteration < 4; ++Iteration)
                {
                    const auto Hit = Surface.query(Probe.x, Probe.y);
                    if (!Hit) return Fail(TEXT("Runway shoulder crosses missing terrain"));
                    Probe = Nominal + Strip.Normal * Dot(Hit->point_enu_m - Nominal, Strip.Normal);
                }
                // Finish on the exact rendered terrain height at the final XY.
                const auto Ground = Surface.query(Probe.x, Probe.y);
                if (!Ground) return Fail(TEXT("Runway shoulder outer edge crosses missing terrain"));
                for (const Vec3 Point : {InnerPoint, Ground->point_enu_m})
                {
                    CoreVertices.push_back(Point);
                    Vertices.Add(Display(Point - Strip.Center));
                    Normals.Add(FVector::ZeroVector);
                }
                UV.Add(FVector2D(T, 0));
                UV.Add(FVector2D(T, 1));
            }
            auto Triangle = [&](int32 A, int32 B, int32 C)
            {
                FVector Normal = FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]);
                if (FVector::DotProduct(Normal, Display(Strip.Normal)) < 0) { Swap(B, C); Normal *= -1; }
                Indices.Append({A, C, B}); // Clockwise front face, same as the terrain grid.
                CoreIndices.insert(CoreIndices.end(), {std::uint32_t(A), std::uint32_t(B), std::uint32_t(C)});
                Normals[A] += Normal; Normals[B] += Normal; Normals[C] += Normal;
            };
            for (int Row = 0; Row < Segments; ++Row)
            {
                const int32 A = Row * 2, B = A + 1, C = A + 2, D = A + 3;
                Triangle(A, B, C);
                Triangle(B, D, C);
            }
            if (!Shoulders.add_tile(2, Segments + 1, std::move(CoreVertices), CoreIndices))
                return Fail(TEXT("Runway shoulder triangles could not be indexed"));
            for (auto& Normal : Normals) Normal = Normal.GetSafeNormal();
            Mesh->CreateMeshSection_LinearColor(2 + Side, Vertices, Indices, Normals, UV, {}, {}, false);
            Mesh->SetMaterial(2 + Side, ShoulderMaterial);
        }
        Prepared.Add(Strip);
    }
    return true;
}

bool AFlightScene::SetGroundControls(double SteeringDeg, double Brake, bool ParkingBrake)
{
    if (!ModelParameters.operating.ground_supported) return false;
    const flight_core::GroundControls Candidate{SteeringDeg, Brake, ParkingBrake};
    if (!flight_core::valid_ground_controls(Candidate) || State.Crashed) return false;
    GroundInputs = Candidate;
    CancelNavigation();
    return true;
}

bool AFlightScene::SetFlaps(double Degrees)
{
    if (flight_core::operation_active(Operation) || !State.Fault.IsEmpty()
        || !flight_core::request_flaps(Dynamics, Degrees, FlightEnvironment)) return false;
    CancelNavigation();
    return SyncState();
}

bool AFlightScene::SetLandingGear(bool Down)
{
    if (!ModelParameters.operating.ground_supported) return false;
    if (!State.TerrainLoaded || !State.Fault.IsEmpty() || flight_core::operation_active(Operation)) return false;
    flight_core::GroundPlane Plane;
    QueryGround(Dynamics.position_enu_m, {OriginLatitude, OriginLongitude, OriginHeight}, Plane);
    if (!Down && flight_core::ground_data(Dynamics, Plane, FlightEnvironment).on_ground) return false;
    if (!flight_core::request_gear(Dynamics, Down, FlightEnvironment)) return false;
    return SyncState();
}

bool AFlightScene::SetGroundInitialCondition(double LatitudeDeg, double LongitudeDeg, double HeadingDeg, double GroundSpeedMps)
{
    if (!ModelParameters.operating.ground_supported) return false;
    if (!State.TerrainLoaded || !flight_core::valid({LatitudeDeg, LongitudeDeg, 0})
        || !FMath::IsFinite(HeadingDeg) || std::abs(HeadingDeg) > 360
        || !FMath::IsFinite(GroundSpeedMps) || GroundSpeedMps < 0 || GroundSpeedMps > 15) return false;
    const Geodetic Origin{LatitudeDeg, LongitudeDeg, 0};
    flight_core::GroundPlane Plane;
    if (!QueryGround({0, 0, 0}, Origin, Plane)) return false;
    auto Environment = FlightEnvironment;
    Environment.origin_altitude_m = 0;
    const double SurfaceZ = Dot(Plane.point_enu_m, Plane.normal_enu) / Plane.normal_enu.z;
    const auto Initial = flight_core::ground_initial_condition({0, 0, SurfaceZ}, FMath::DegreesToRadians(HeadingDeg),
        GroundSpeedMps, Plane, Environment);
    if (!Initial) return false;
    OriginLatitude = LatitudeDeg;
    OriginLongitude = LongitudeDeg;
    OriginHeight = 0;
    FlightEnvironment = Environment;
    Dynamics = *Initial;
    Inputs = {0, 0, 0, 0};
    GroundInputs = {0, 0, GroundSpeedMps == 0};
    Navigation = {};
    Operation = {};
    SelectedRunway = -1;
    State.Fault.Empty();
    State.DistanceTravelledM = 0;
    SetRunning(false);
    return SyncState();
}

bool AFlightScene::ResetOnGround()
{
    if (Runways.IsEmpty()) return SetGroundInitialCondition(OriginLatitude, OriginLongitude, 0, 0);
    const auto& Strip = Runways[0];
    const Vec3 Start = Strip.Center - Strip.Along * (Strip.LengthM * .5 - 30);
    const auto Geo = flight_core::enu_to_geodetic(Start, {RenderOriginLatitude, RenderOriginLongitude, RenderOriginHeight});
    return Geo && SetGroundInitialCondition(Geo->latitude_deg, Geo->longitude_deg, Strip.HeadingDeg, 0);
}

void AFlightScene::SyncGroundState()
{
    flight_core::GroundPlane Plane;
    State.GroundSurfaceName.Empty();
    State.TerrainHeightM = 0;
    State.GroundAvailable = QueryGround(Dynamics.position_enu_m, {OriginLatitude, OriginLongitude, OriginHeight},
        Plane, &State.GroundSurfaceName, &State.TerrainHeightM);
    if(State.GroundAvailable) State.GroundAvailable=SampleWheelGround(Dynamics,{OriginLatitude,OriginLongitude,OriginHeight},Plane);
    const auto Ground = flight_core::ground_data(Dynamics, Plane, FlightEnvironment);
    State.GroundOnGround = Ground.on_ground;
    State.WheelsOnGround = Ground.wheels_on_ground;
    State.HeightAglM = State.GroundAvailable ? Ground.clearance_m : 0;
    State.GroundNormalForceN = Dynamics.ground.normal_force_n;
    State.ImpactSpeedMps = Dynamics.ground.impact_speed_mps;
    State.Crashed = Dynamics.ground.crashed;
    State.SteeringDeg = GroundInputs.steering_deg;
    State.AppliedSteeringDeg = Dynamics.actual_steering_deg;
    State.AppliedBrake = Dynamics.actual_brake;
    State.WheelSurfacesSampled=Plane.per_wheel;
    State.WheelNormalForceN.Empty();State.WheelClearanceM.Empty();State.WheelLateralSlipMps.Empty();
    for(int I=0;I<3;++I) {
        State.WheelNormalForceN.Add(Dynamics.ground.wheel_normal_force_n[I]);
        State.WheelClearanceM.Add(Ground.wheel_clearance_m[I]);
        State.WheelLateralSlipMps.Add(Dynamics.ground.wheel_lateral_slip_mps[I]);
    }
    State.Brake = GroundInputs.brake;
    State.ParkingBrake = GroundInputs.parking_brake;
    State.GearCommandDown = Dynamics.gear.target_down;
    State.GearExtension = Dynamics.gear.extension;
    State.GearDownLocked = flight_core::gear_down_locked(Dynamics.gear);
    State.GearUpLocked = flight_core::gear_up_locked(Dynamics.gear);
    State.GearMoving = Dynamics.gear.target_down ? !State.GearDownLocked : !State.GearUpLocked;
    USceneComponent* Hinges[] = {NoseGearHinge.Get(), LeftMainGearHinge.Get(), RightMainGearHinge.Get()};
    for (int Index=0; Index<3; ++Index) {
        const auto Root=landing_gear_visual::root(ModelParameters,Index);
        const auto Delta=landing_gear_visual::center(ModelParameters,Index)-Root;
        const auto Stowed=landing_gear_visual::stowed_direction(Index);
        Hinges[Index]->SetRelativeLocation(FVector(Root.x,Root.y,Root.z)*100);
        const FQuat Fold=FQuat::FindBetweenNormals(FVector(Delta.x,Delta.y,Delta.z).GetSafeNormal(),
            FVector(Stowed.x,Stowed.y,Stowed.z).GetSafeNormal());
        Hinges[Index]->SetRelativeRotation(FQuat::Slerp(FQuat::Identity,Fold,1-Dynamics.gear.extension));
    }
    // ponytail: no gear-bay mesh; hide fully stowed parts until measured geometry is available.
    for (USceneComponent* Hinge : {NoseGearHinge.Get(), LeftMainGearHinge.Get(), RightMainGearHinge.Get()})
        Hinge->SetHiddenInGame(State.GearUpLocked || !State.VisualShowBuiltinGear, true);
    if (NoseWheel) NoseWheel->SetRelativeRotation(FRotator(0, Dynamics.actual_steering_deg, 0));
}

bool AFlightScene::SampleWheelGround(const flight_core::Aircraft& Body,flight_core::Geodetic Origin,flight_core::GroundPlane& Plane) const
{
    using namespace flight_core;
    using namespace fixed_wing_detail;
    if(!Plane.valid) return false;
    if(dot(Body.position_enu_m-Plane.point_enu_m,Plane.normal_enu)>ModelParameters.cg_height_m+10) return true;
    Plane.per_wheel=true;
    const auto Points=wheel_points(ModelParameters);
    for(int I=0;I<3;++I) {
        const auto Position=Body.position_enu_m+enu(rotate(orientation(Body),Points[I]));
        GroundPlane Surface;
        if(!QueryGround(Position,Origin,Surface)) {Plane.valid=false;return false;}
        Plane.wheels[I]={Surface.point_enu_m,Surface.normal_enu,true};
    }
    return valid_ground_plane(Plane);
}
