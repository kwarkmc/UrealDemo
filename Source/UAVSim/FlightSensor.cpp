#include "FlightScene.h"
#include "Gimbal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Base64.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"

namespace
{
bool ValidTargetId(const FString& Id)
{
    if (Id.IsEmpty() || Id.Len() > 48) return false;
    for (TCHAR C : Id)
        if (!((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z')
            || (C >= '0' && C <= '9') || C == '_' || C == '-')) return false;
    return true;
}
FVector DisplayVector(flight_core::Vec3 V) { return FVector(V.x, -V.y, V.z); }
}

bool AFlightScene::SetSensorSettings(bool Enabled, const FString& Mode, double FovDeg)
{
    if ((Mode != TEXT("ccd") && Mode != TEXT("ir_white") && Mode != TEXT("ir_black"))
        || !FMath::IsFinite(FovDeg) || FovDeg < 1 || FovDeg > 90)
    {
        State.SensorError = TEXT("Mode must be ccd/ir_white/ir_black and horizontal FOV must be 1..90 degrees");
        return false;
    }
    if (Enabled && State.GimbalMode == TEXT("protect"))
    {
        State.SensorError = TEXT("Select another gimbal mode before enabling capture in protection mode");
        return false;
    }
    State.SensorEnabled = Enabled;
    State.SensorMode = Mode;
    State.SensorFovDeg = FovDeg;
    State.SensorError.Empty();
    SyncSensor();
    return true;
}

bool AFlightScene::SetGimbal(double AzimuthDeg, double ElevationDeg)
{
    if (!flight_core::gimbal_direction(AzimuthDeg, ElevationDeg))
    {
        State.SensorError = TEXT("Gimbal azimuth must be -180..180 and elevation -90..30 degrees");
        return false;
    }
    State.GimbalTargetId.Empty();
    State.GimbalMode = TEXT("body_fixed");
    State.GimbalPointLatitudeDeg = 0;
    State.GimbalPointLongitudeDeg = 0;
    State.GimbalPointEllipsoidHeightM = 0;
    State.GimbalTrackingStatus = TEXT("manual");
    State.GimbalAzimuthDeg = AzimuthDeg;
    State.GimbalElevationDeg = ElevationDeg;
    State.SensorError.Empty();
    SyncSensor();
    return true;
}

bool AFlightScene::TrackSensorTarget(const FString& TargetId)
{
    if (!TargetId.IsEmpty() && !TargetActors.Contains(TargetId))
    {
        State.SensorError = TEXT("Tracking target does not exist");
        return false;
    }
    if (!SetGimbal(State.GimbalAzimuthDeg, State.GimbalElevationDeg)) return false;
    State.GimbalMode = TargetId.IsEmpty() ? TEXT("body_fixed") : TEXT("auto");
    State.GimbalTargetId = TargetId;
    State.GimbalTrackingStatus = TargetId.IsEmpty() ? TEXT("manual") : TEXT("tracking");
    State.SensorError.Empty();
    SyncSensor();
    return true;
}

TOptional<FVector> AFlightScene::SensorPointWorld(double Latitude, double Longitude, double Height) const
{
    if (!FMath::IsFinite(Height) || Height < -500 || Height > 20000) return {};
    const auto Point = flight_core::geodetic_to_enu({Latitude, Longitude, Height},
        {RenderOriginLatitude, RenderOriginLongitude, RenderOriginHeight});
    // All three axes matter: the antipode can project near zero horizontal ENU.
    if (!Point || std::hypot(Point->x, std::hypot(Point->y, Point->z)) > 1e6) return {};
    return RootComponent->GetComponentTransform().TransformPosition(DisplayVector(*Point) * 100);
}

bool AFlightScene::PointSensorGeodetic(double LatitudeDeg, double LongitudeDeg, double EllipsoidHeightM)
{
    if (!SensorPointWorld(LatitudeDeg, LongitudeDeg, EllipsoidHeightM))
    {
        State.SensorError = TEXT("Point requires WGS84 coordinates, ellipsoid height -500..20000 m and a location within 1000 km of the render origin");
        return false;
    }
    if (!SetGimbal(State.GimbalAzimuthDeg, State.GimbalElevationDeg)) return false;
    State.GimbalMode = TEXT("geo");
    State.GimbalPointLatitudeDeg = LatitudeDeg;
    State.GimbalPointLongitudeDeg = LongitudeDeg;
    State.GimbalPointEllipsoidHeightM = EllipsoidHeightM;
    SyncSensor();
    return true;
}

bool AFlightScene::ProtectSensor()
{
    // Virtual park posture; no particular sensor's protective housing is modeled.
    if (!SetGimbal(180, 30)) return false;
    State.GimbalMode = TEXT("protect");
    State.SensorEnabled = false;
    SyncSensor();
    return true;
}

void AFlightScene::SyncSensor()
{
    if (!SensorCamera) return;
    State.GimbalLimited = false;
    State.GimbalTargetInFov = false;
    State.GimbalErrorDeg = 0;
    TOptional<FVector> Aim;
    if (State.GimbalMode == TEXT("protect"))
    {
        State.GimbalAzimuthDeg = 180;
        State.GimbalElevationDeg = 30;
        State.SensorEnabled = false;
        State.GimbalTrackingStatus = TEXT("protected");
    }
    else if (State.GimbalMode == TEXT("auto"))
    {
        const auto* Found = TargetActors.Find(State.GimbalTargetId);
        if (Found && IsValid(Found->Get()))
        {
            // Aim 1.5 m above the prop's ground origin along its surface normal.
            // This uses simulator truth, never image detection or weapon guidance.
            Aim = (*Found)->GetActorTransform().TransformPosition(FVector(0, 0, 150));
        }
        else
        {
            State.GimbalTargetId.Empty();
            State.GimbalMode = TEXT("body_fixed");
            State.GimbalTrackingStatus = TEXT("target_missing");
        }
    }
    else if (State.GimbalMode == TEXT("geo"))
    {
        Aim = SensorPointWorld(State.GimbalPointLatitudeDeg, State.GimbalPointLongitudeDeg,
            State.GimbalPointEllipsoidHeightM);
        if (!Aim)
        {
            State.GimbalMode = TEXT("body_fixed");
            State.GimbalPointLatitudeDeg = 0;
            State.GimbalPointLongitudeDeg = 0;
            State.GimbalPointEllipsoidHeightM = 0;
            State.GimbalTrackingStatus = TEXT("target_missing");
        }
    }
    if (Aim)
    {
        const FVector Body = Aircraft->GetComponentTransform().InverseTransformVectorNoScale(
            *Aim - SensorCamera->GetComponentLocation());
        const flight_core::Vec3 Ray{Body.X, Body.Y, -Body.Z};
        if (const auto Angles = flight_core::gimbal_angles(Ray))
        {
            State.GimbalAzimuthDeg = Angles->azimuth_deg;
            State.GimbalElevationDeg = Angles->elevation_deg;
            State.GimbalLimited = Angles->limited;
            State.GimbalErrorDeg = Angles->error_deg;
            State.GimbalTargetInFov = flight_core::target_in_fov(Ray, Angles->azimuth_deg,
                Angles->elevation_deg, State.SensorFovDeg);
            State.GimbalTrackingStatus = Angles->limited ? TEXT("limited")
                : State.GimbalMode == TEXT("geo") ? TEXT("pointing") : TEXT("tracking");
        }
        else State.GimbalTrackingStatus = TEXT("target_missing");
    }
    SensorCamera->FOVAngle = State.SensorFovDeg;
    SensorCamera->SetRelativeRotation(FRotator(State.GimbalElevationDeg, State.GimbalAzimuthDeg, 0));
}

FEoirFrame AFlightScene::CaptureSensorFrame()
{
    FEoirFrame Result;
    Result.Mode = State.SensorMode;
    Result.Sequence = State.SensorFrameSequence;
    Result.SimulationTimeS = State.SimulationTimeS;
    auto Fail = [&](const TCHAR* Message)
    {
        Result.Error = Message;
        State.SensorError = Message;
        return Result;
    };
    if (!State.SensorEnabled) return Fail(TEXT("EOIR camera is disabled"));
    if (!GetWorld() || !GetWorld()->IsGameWorld() || !State.TerrainLoaded)
        return Fail(TEXT("EOIR requires the running game world and loaded terrain"));
    if (!SensorRenderTarget)
    {
        // Configure at runtime as the saved starter map can override constructor defaults.
        // Keep the sky visible while disabling cinematic blur and height fog.
        SensorCamera->ShowFlags.SetMotionBlur(false);
        SensorCamera->ShowFlags.SetDepthOfField(false);
        SensorCamera->ShowFlags.SetTemporalAA(false);
        SensorCamera->ShowFlags.SetAtmosphere(true);
        SensorCamera->ShowFlags.SetFog(false);
        auto& Post = SensorCamera->PostProcessSettings;
        Post.bOverride_AutoExposureMethod = true;
        Post.AutoExposureMethod = AEM_Manual;
        Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
        Post.AutoExposureApplyPhysicalCameraExposure = false;
        Post.bOverride_AutoExposureBias = true;
        Post.AutoExposureBias = 0;
        Post.bOverride_VignetteIntensity = true;
        Post.VignetteIntensity = 0;
        Post.bOverride_SceneFringeIntensity = true;
        Post.SceneFringeIntensity = 0;
        SensorRenderTarget = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
        SensorRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
        SensorRenderTarget->ClearColor = FLinearColor::Black;
        SensorRenderTarget->InitAutoFormat(Result.Width, Result.Height);
        SensorRenderTarget->UpdateResourceImmediate(true);
        SensorCamera->TextureTarget = SensorRenderTarget;
    }
    // Hide only our aircraft geometry; terrain and independent target actors stay visible.
    SensorCamera->ClearHiddenComponents();
    TArray<UPrimitiveComponent*> Components;
    GetComponents(Components);
    for (auto* Component : Components)
        if (Component->IsAttachedTo(Aircraft)) SensorCamera->HideComponent(Component);
    SyncSensor();
    SensorCamera->CaptureScene();
    auto* Resource = SensorRenderTarget->GameThread_GetRenderTargetResource();
    TArray<FColor> Pixels;
    if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != Result.Width * Result.Height)
        return Fail(TEXT("EOIR render target readback failed"));
    if (State.SensorMode != TEXT("ccd"))
        for (auto& Pixel : Pixels)
        {
            // Display luminance only. Neither temperature nor thermal emission is modeled.
            uint8 Luma = (77u * Pixel.R + 150u * Pixel.G + 29u * Pixel.B + 128u) >> 8;
            if (State.SensorMode == TEXT("ir_black")) Luma = 255 - Luma;
            Pixel = FColor(Luma, Luma, Luma, 255);
        }
    auto* Module = FModuleManager::LoadModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
    if (!Module) return Fail(TEXT("JPEG encoder is unavailable"));
    const auto Encoder = Module->CreateImageWrapper(EImageFormat::JPEG);
    if (!Encoder || !Encoder->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor),
        Result.Width, Result.Height, ERGBFormat::BGRA, 8)) return Fail(TEXT("EOIR JPEG input failed"));
    const auto Jpeg = Encoder->GetCompressed(92);
    if (Jpeg.IsEmpty()) return Fail(TEXT("EOIR JPEG encoding failed"));
    Result.ImageBase64 = FBase64::Encode(Jpeg.GetData(), Jpeg.Num());
    Result.Sequence = ++State.SensorFrameSequence;
    State.SensorError.Empty();
    return Result;
}

bool AFlightScene::GroundTargetTransform(double Latitude, double Longitude, double Heading,
    FTransform& Transform, double& Height)
{
    const flight_core::Geodetic Location{Latitude, Longitude, 0};
    const flight_core::Geodetic Origin{RenderOriginLatitude, RenderOriginLongitude, RenderOriginHeight};
    const auto Probe = flight_core::geodetic_to_enu(Location, Origin);
    flight_core::GroundPlane Plane;
    if (!flight_core::valid(Location) || !FMath::IsFinite(Heading) || Heading < 0 || Heading > 360
        || !State.TerrainLoaded || !Probe || std::hypot(Probe->x, Probe->y) > 1e6
        || !QueryGround(*Probe, Origin, Plane, nullptr, &Height))
    {
        State.SensorError = TEXT("Target pose requires finite coordinates, heading 0..360 and loaded ground at that location");
        return false;
    }
    const double Angle = FMath::DegreesToRadians(Heading);
    const auto AheadGeo = flight_core::enu_to_geodetic({std::sin(Angle) * 10, std::cos(Angle) * 10, 0}, Location);
    const auto Ahead = AheadGeo ? flight_core::geodetic_to_enu(*AheadGeo, Origin) : std::nullopt;
    if (!Ahead) { State.SensorError = TEXT("Target heading transform failed"); return false; }
    const FVector Up = DisplayVector(Plane.normal_enu).GetSafeNormal();
    const FVector Forward = FVector::VectorPlaneProject(DisplayVector(*Ahead - *Probe), Up).GetSafeNormal();
    const FTransform Local(FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat(), DisplayVector(Plane.point_enu_m) * 100);
    Transform = Local * RootComponent->GetComponentTransform();
    return true;
}

bool AFlightScene::CreateGroundTarget(const FString& Id, const FString& Kind,
    double LatitudeDeg, double LongitudeDeg, double HeadingDeg)
{
    if (!ValidTargetId(Id) || TargetActors.Contains(Id) || TargetActors.Num() >= 64
        || (Kind != TEXT("tank") && Kind != TEXT("missile") && Kind != TEXT("marker")))
    {
        State.SensorError = TEXT("Target requires a unique ASCII id (1..48 letters/digits/_/-), kind tank/missile/marker, maximum 64 props");
        return false;
    }
    FTransform Transform;
    double Height = 0;
    if (!GroundTargetTransform(LatitudeDeg, LongitudeDeg, HeadingDeg, Transform, Height)) return false;
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!Cube || !Sphere || !Material) { State.SensorError = TEXT("Target primitive assets are unavailable"); return false; }
    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags |= RF_Transient;
    Spawn.Owner = this;
    auto* Prop = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), Transform, Spawn);
    if (!Prop) { State.SensorError = TEXT("Target actor creation failed"); return false; }
    auto* Root = NewObject<USceneComponent>(Prop);
    Prop->SetRootComponent(Root);
    Prop->AddInstanceComponent(Root);
    Root->RegisterComponent();
    Prop->SetActorTransform(Transform);
    auto* BodyMaterial = UMaterialInstanceDynamic::Create(Material, Prop);
    BodyMaterial->SetVectorParameterValue(TEXT("Color"), Kind == TEXT("tank")
        ? FLinearColor(.24f, .30f, .09f) : FLinearColor(.85f, .72f, .32f));
    auto* DarkMaterial = UMaterialInstanceDynamic::Create(Material, Prop);
    DarkMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(.035f, .04f, .045f));
    auto Part = [&](UStaticMesh* Mesh, FVector Position, FVector Scale, bool Dark = false)
    {
        auto* Component = NewObject<UStaticMeshComponent>(Prop);
        Component->SetupAttachment(Root);
        Component->SetStaticMesh(Mesh);
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetRelativeLocation(Position);
        Component->SetRelativeScale3D(Scale);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetMaterial(0, Dark ? DarkMaterial : BodyMaterial);
        if (Dark) Component->ComponentTags.Add(TEXT("DarkPropPart"));
        Prop->AddInstanceComponent(Component);
        Component->RegisterComponent();
    };
    // Coarse, non-firing scene props in metres (basic shapes are 100 cm wide).
    if (Kind == TEXT("tank"))
    {
        Part(Cube, FVector(0, 0, 105), FVector(6, 2.9, 1.2));
        for (double Side : {-1.0, 1.0}) Part(Cube, FVector(0, Side * 145, 45), FVector(6.5, .65, .9), true);
        Part(Sphere, FVector(-40, 0, 175), FVector(2.8, 2.4, 1.3));
        Part(Cube, FVector(245, 0, 180), FVector(3.6, .16, .16), true);
    }
    else if (Kind == TEXT("missile"))
    {
        Part(Cube, FVector(0, 0, 35), FVector(6.6, 2, .7), true);
        Part(Sphere, FVector(0, 0, 115), FVector(6.5, .75, .75));
        Part(Cube, FVector(-230, 0, 115), FVector(1, 2, .08));
        Part(Cube, FVector(-230, 0, 145), FVector(1, .08, 1.3));
    }
    else
    {
        Part(Cube, FVector(0, 0, 75), FVector(1.5, 1.5, 1.5));
        Part(Cube, FVector(0, 0, 180), FVector(.1, .1, 1.2), true);
    }
    TargetActors.Add(Id, Prop);
    FSimTargetState Info;
    Info.Id = Id;
    Info.Kind = Kind;
    Info.LatitudeDeg = LatitudeDeg;
    Info.LongitudeDeg = LongitudeDeg;
    Info.EllipsoidHeightM = Height;
    Info.HeadingDeg = FMath::Fmod(HeadingDeg, 360);
    State.Targets.Add(Info);
    State.SensorError.Empty();
    SyncSensor();
    return true;
}

bool AFlightScene::MoveGroundTarget(const FString& Id, double LatitudeDeg, double LongitudeDeg, double HeadingDeg)
{
    const auto* Found = TargetActors.Find(Id);
    if (!Found || !IsValid(Found->Get())) { State.SensorError = TEXT("Target does not exist"); return false; }
    FTransform Transform;
    double Height = 0;
    if (!GroundTargetTransform(LatitudeDeg, LongitudeDeg, HeadingDeg, Transform, Height)) return false;
    (*Found)->SetActorTransform(Transform);
    auto* Info = State.Targets.FindByPredicate([&](const FSimTargetState& Item) { return Item.Id == Id; });
    if (Info)
    {
        Info->LatitudeDeg = LatitudeDeg;
        Info->LongitudeDeg = LongitudeDeg;
        Info->EllipsoidHeightM = Height;
        Info->HeadingDeg = FMath::Fmod(HeadingDeg, 360);
    }
    State.SensorError.Empty();
    SyncSensor();
    return true;
}

bool AFlightScene::RemoveGroundTarget(const FString& Id)
{
    const auto* Found = TargetActors.Find(Id);
    if (!Found) { State.SensorError = TEXT("Target does not exist"); return false; }
    CancelWargameTarget(Id);
    if (IsValid(Found->Get())) (*Found)->Destroy();
    TargetActors.Remove(Id);
    State.Targets.RemoveAll([&](const FSimTargetState& Item) { return Item.Id == Id; });
    State.SensorError.Empty();
    SyncSensor();
    return true;
}

void AFlightScene::ClearGroundTargets()
{
    ResetWargame();
    for (const auto& Item : TargetActors)
        if (IsValid(Item.Value)) Item.Value->Destroy();
    TargetActors.Empty();
    State.Targets.Empty();
    SyncSensor();
}
