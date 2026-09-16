#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Navigation.h"
#include "FlightOperations.h"
#include "TerrainSurface.h"
#include "WargameVisual.h"
#include "FlightScene.generated.h"

class UCameraComponent;
class UProceduralMeshComponent;
class UTexture2D;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

struct FGroundRunway
{
    FString Name;
    flight_core::Geodetic Origin;
    flight_core::Vec3 Center, Along, Right, Normal;
    double HeadingDeg = 0, LengthM = 0, WidthM = 0;
};

struct FWargameAnimation
{
    TWeakObjectPtr<AActor> Actor;
    FVector From, To;
    TWeakObjectPtr<UProceduralMeshComponent> Trail;
};

USTRUCT(BlueprintType)
struct FWargameWeaponState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Id;
    UPROPERTY(BlueprintReadOnly) FString Label;
    UPROPERTY(BlueprintReadOnly) FString Mesh;
    UPROPERTY(BlueprintReadOnly) double AnimationDurationS = 2;
    UPROPERTY(BlueprintReadOnly) int32 TriangleCount = 0;
};

USTRUCT(BlueprintType)
struct FWargameShotState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Id;
    UPROPERTY(BlueprintReadOnly) FString WeaponId;
    UPROPERTY(BlueprintReadOnly) FString TargetId;
    UPROPERTY(BlueprintReadOnly) FString Phase = TEXT("animating");
    UPROPERTY(BlueprintReadOnly) FString Outcome = TEXT("pending");
    UPROPERTY(BlueprintReadOnly) double ElapsedS = 0;
    UPROPERTY(BlueprintReadOnly) double DurationS = 2;
    UPROPERTY(BlueprintReadOnly) double ResolvedTimeS = 0;
};

USTRUCT(BlueprintType)
struct FFlightWaypoint
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double LatitudeDeg = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double LongitudeDeg = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double EllipsoidHeightM = 2600;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) double SpeedMps = 19;
};

USTRUCT(BlueprintType)
struct FSimTargetState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Id;
    UPROPERTY(BlueprintReadOnly) FString Kind;
    UPROPERTY(BlueprintReadOnly) double LatitudeDeg = 0;
    UPROPERTY(BlueprintReadOnly) double LongitudeDeg = 0;
    UPROPERTY(BlueprintReadOnly) double EllipsoidHeightM = 0;
    UPROPERTY(BlueprintReadOnly) double HeadingDeg = 0;
    UPROPERTY(BlueprintReadOnly) bool Hit = false;
    UPROPERTY(BlueprintReadOnly) int32 HitCount = 0;
    UPROPERTY(BlueprintReadOnly) FString LastHitShotId;
};

USTRUCT(BlueprintType)
struct FEoirFrame
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString ImageBase64;
    UPROPERTY(BlueprintReadOnly) int32 Width = 1280;
    UPROPERTY(BlueprintReadOnly) int32 Height = 960;
    UPROPERTY(BlueprintReadOnly) FString Mode;
    UPROPERTY(BlueprintReadOnly) int64 Sequence = 0;
    UPROPERTY(BlueprintReadOnly) double SimulationTimeS = 0;
    UPROPERTY(BlueprintReadOnly) FString Error;
};

USTRUCT(BlueprintType)
struct FAircraftState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) double LatitudeDeg = 0;
    UPROPERTY(BlueprintReadOnly) double LongitudeDeg = 0;
    UPROPERTY(BlueprintReadOnly) double EllipsoidHeightM = 0;
    UPROPERTY(BlueprintReadOnly) double HeadingDeg = 0;
    UPROPERTY(BlueprintReadOnly) double PitchDeg = 0;
    UPROPERTY(BlueprintReadOnly) double RollDeg = 0;
    UPROPERTY(BlueprintReadOnly) double ElevatorDeg = 0;
    UPROPERTY(BlueprintReadOnly) double AileronDeg = 0;
    UPROPERTY(BlueprintReadOnly) double RudderDeg = 0;
    UPROPERTY(BlueprintReadOnly) double Throttle = 0;
    UPROPERTY(BlueprintReadOnly) double SimulationTimeS = 0;
    UPROPERTY(BlueprintReadOnly) bool Running = false;
    UPROPERTY(BlueprintReadOnly) double TimeScale = 1;
    UPROPERTY(BlueprintReadOnly) double EffectiveTimeScale = 0;
    UPROPERTY(BlueprintReadOnly) double DistanceTravelledM = 0;
    UPROPERTY(BlueprintReadOnly) bool TerrainLoaded = false;
    UPROPERTY(BlueprintReadOnly) int32 TerrainTileCount = 0;
    UPROPERTY(BlueprintReadOnly) FString TerrainDataset;
    UPROPERTY(BlueprintReadOnly) FString TerrainManifest;
    UPROPERTY(BlueprintReadOnly) FString TerrainAttribution;
    UPROPERTY(BlueprintReadOnly) FString TerrainError;
    UPROPERTY(BlueprintReadOnly) FString ViewMode = TEXT("aircraft");
    UPROPERTY(BlueprintReadOnly) int64 ProcessId = 0;
    UPROPERTY(BlueprintReadOnly) double SpeedMps = 19;
    UPROPERTY(BlueprintReadOnly) double GroundSpeedMps = 19;
    UPROPERTY(BlueprintReadOnly) double AngleOfAttackDeg = 0;
    UPROPERTY(BlueprintReadOnly) double SideslipDeg = 0;
    UPROPERTY(BlueprintReadOnly) double AirDensityKgM3 = 0;
    UPROPERTY(BlueprintReadOnly) double DynamicPressurePa = 0;
    UPROPERTY(BlueprintReadOnly) double LiftCoefficient = 0;
    UPROPERTY(BlueprintReadOnly) double DragCoefficient = 0;
    UPROPERTY(BlueprintReadOnly) double FlowSeparation = 0;
    UPROPERTY(BlueprintReadOnly) bool StallWarning = false;
    UPROPERTY(BlueprintReadOnly) bool Stalled = false;
    UPROPERTY(BlueprintReadOnly) double LoadFactor = 0;
    UPROPERTY(BlueprintReadOnly) double WindEastMps = 0;
    UPROPERTY(BlueprintReadOnly) double WindNorthMps = 0;
    UPROPERTY(BlueprintReadOnly) double WindUpMps = 0;
    UPROPERTY(BlueprintReadOnly) bool GroundAvailable = false;
    UPROPERTY(BlueprintReadOnly) bool GroundOnGround = false;
    UPROPERTY(BlueprintReadOnly) int32 WheelsOnGround = 0;
    UPROPERTY(BlueprintReadOnly) double HeightAglM = 0;
    UPROPERTY(BlueprintReadOnly) double TerrainHeightM = 0;
    UPROPERTY(BlueprintReadOnly) double GroundNormalForceN = 0;
    UPROPERTY(BlueprintReadOnly) double ImpactSpeedMps = 0;
    UPROPERTY(BlueprintReadOnly) bool Crashed = false;
    UPROPERTY(BlueprintReadOnly) double SteeringDeg = 0;
    UPROPERTY(BlueprintReadOnly) double Brake = 0;
    UPROPERTY(BlueprintReadOnly) bool ParkingBrake = false;
    UPROPERTY(BlueprintReadOnly) bool GearCommandDown = true;
    UPROPERTY(BlueprintReadOnly) double GearExtension = 1;
    UPROPERTY(BlueprintReadOnly) bool GearDownLocked = true;
    UPROPERTY(BlueprintReadOnly) bool GearUpLocked = false;
    UPROPERTY(BlueprintReadOnly) bool GearMoving = false;
    UPROPERTY(BlueprintReadOnly) FString GroundSurfaceName;
    UPROPERTY(BlueprintReadOnly) double VerticalSpeedMps = 0;
    UPROPERTY(BlueprintReadOnly) double RollRateDegS = 0;
    UPROPERTY(BlueprintReadOnly) double PitchRateDegS = 0;
    UPROPERTY(BlueprintReadOnly) double YawRateDegS = 0;
    UPROPERTY(BlueprintReadOnly) FString Model = TEXT("thor_nonlinear_6dof_v2");
    UPROPERTY(BlueprintReadOnly) FString NavigationMode = TEXT("manual");
    UPROPERTY(BlueprintReadOnly) bool StallRecovery = false;
    UPROPERTY(BlueprintReadOnly) int32 ActiveWaypointIndex = -1;
    UPROPERTY(BlueprintReadOnly) int32 WaypointCount = 0;
    UPROPERTY(BlueprintReadOnly) double DistanceToWaypointM = 0;
    UPROPERTY(BlueprintReadOnly) double LoiterRadiusM = 0;
    UPROPERTY(BlueprintReadOnly) FString Fault;
    UPROPERTY(BlueprintReadOnly) FString OperationKind = TEXT("none");
    UPROPERTY(BlueprintReadOnly) FString OperationPhase = TEXT("idle");
    UPROPERTY(BlueprintReadOnly) int32 OperationRunwayIndex = -1;
    UPROPERTY(BlueprintReadOnly) FString OperationRunwayName;
    UPROPERTY(BlueprintReadOnly) bool OperationCompleted = false;
    UPROPERTY(BlueprintReadOnly) FString OperationMessage;
    UPROPERTY(BlueprintReadOnly) double OperationCrossTrackM = 0;
    UPROPERTY(BlueprintReadOnly) double OperationAlongTrackM = 0;
    UPROPERTY(BlueprintReadOnly) double OperationTargetHeightM = 0;
    UPROPERTY(BlueprintReadOnly) double AppliedElevatorDeg = 0;
    UPROPERTY(BlueprintReadOnly) double AppliedAileronDeg = 0;
    UPROPERTY(BlueprintReadOnly) double AppliedRudderDeg = 0;
    UPROPERTY(BlueprintReadOnly) double AppliedThrottle = 0;
    UPROPERTY(BlueprintReadOnly) double ThrustN = 0;
    UPROPERTY(BlueprintReadOnly) FString AirframeId;
    UPROPERTY(BlueprintReadOnly) FString AirframeProfile;
    UPROPERTY(BlueprintReadOnly) FString AirframeFingerprint;
    UPROPERTY(BlueprintReadOnly) FString AirframeError;
    UPROPERTY(BlueprintReadOnly) double GustEastMps = 0;
    UPROPERTY(BlueprintReadOnly) double GustNorthMps = 0;
    UPROPERTY(BlueprintReadOnly) double GustUpMps = 0;
    UPROPERTY(BlueprintReadOnly) double GustStartS = 0;
    UPROPERTY(BlueprintReadOnly) double GustDurationS = 0;
    UPROPERTY(BlueprintReadOnly) double EffectiveWindEastMps = 0;
    UPROPERTY(BlueprintReadOnly) double EffectiveWindNorthMps = 0;
    UPROPERTY(BlueprintReadOnly) double EffectiveWindUpMps = 0;
    UPROPERTY(BlueprintReadOnly) double EnginePowerFraction = 1;
    UPROPERTY(BlueprintReadOnly) bool ElevatorJammed = false;
    UPROPERTY(BlueprintReadOnly) bool AileronJammed = false;
    UPROPERTY(BlueprintReadOnly) bool RudderJammed = false;
    UPROPERTY(BlueprintReadOnly) FString VisualSource = TEXT("builtin");
    UPROPERTY(BlueprintReadOnly) FString VisualConfig = TEXT("aircraft/visual.json");
    UPROPERTY(BlueprintReadOnly) FString VisualMesh;
    UPROPERTY(BlueprintReadOnly) int32 VisualTriangleCount = 0;
    UPROPERTY(BlueprintReadOnly) double VisualSpanM = 1.27;
    UPROPERTY(BlueprintReadOnly) FString VisualFingerprint;
    UPROPERTY(BlueprintReadOnly) FString VisualError;
    UPROPERTY(BlueprintReadOnly) bool VisualShowBuiltinGear = true;
    UPROPERTY(BlueprintReadOnly) bool SensorEnabled = false;
    UPROPERTY(BlueprintReadOnly) FString SensorMode = TEXT("ccd");
    UPROPERTY(BlueprintReadOnly) double SensorFovDeg = 30;
    UPROPERTY(BlueprintReadOnly) FString GimbalMode = TEXT("body_fixed");
    UPROPERTY(BlueprintReadOnly) double GimbalPointLatitudeDeg = 0;
    UPROPERTY(BlueprintReadOnly) double GimbalPointLongitudeDeg = 0;
    UPROPERTY(BlueprintReadOnly) double GimbalPointEllipsoidHeightM = 0;
    UPROPERTY(BlueprintReadOnly) double GimbalAzimuthDeg = 0;
    UPROPERTY(BlueprintReadOnly) double GimbalElevationDeg = -45;
    UPROPERTY(BlueprintReadOnly) FString GimbalTargetId;
    UPROPERTY(BlueprintReadOnly) FString GimbalTrackingStatus = TEXT("manual");
    UPROPERTY(BlueprintReadOnly) bool GimbalLimited = false;
    UPROPERTY(BlueprintReadOnly) bool GimbalTargetInFov = false;
    UPROPERTY(BlueprintReadOnly) double GimbalErrorDeg = 0;
    UPROPERTY(BlueprintReadOnly) int64 SensorFrameSequence = 0;
    UPROPERTY(BlueprintReadOnly) FString SensorError;
    UPROPERTY(BlueprintReadOnly) TArray<FSimTargetState> Targets;
    UPROPERTY(BlueprintReadOnly) double WargameTimeS = 0;
    UPROPERTY(BlueprintReadOnly) FString WargameError;
    UPROPERTY(BlueprintReadOnly) TArray<FWargameWeaponState> WargameWeapons;
    UPROPERTY(BlueprintReadOnly) TArray<FWargameShotState> WargameShots;
};

// Unreal adapter for the independent nonlinear development flight model.
UCLASS()
class UAVSIM_API AFlightScene : public AActor
{
    GENERATED_BODY()
public:
    AFlightScene();
    virtual void Tick(float DeltaSeconds) override;
    // Load maps/active.json; use old data folders only when absent. Successful load resets and pauses flight.
    UFUNCTION(BlueprintCallable, CallInEditor, Category="UAVSim") bool LoadTerrain();
    // WGS84 ellipsoid metres; heading clockwise from North, pitch nose-up, roll right-wing-down.
    // External positioning pauses flight and anchors its local 50 km physics frame at this location.
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetAircraftGeodetic(
        double LatitudeDeg, double LongitudeDeg, double EllipsoidHeightM,
        double HeadingDeg, double PitchDeg, double RollDeg);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetInitialCondition(
        double LatitudeDeg, double LongitudeDeg, double EllipsoidHeightM,
        double HeadingDeg, double PitchDeg, double RollDeg, double SpeedMps);
    // Explicit air-relative velocity direction for stall and recovery experiments.
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetAerodynamicInitialCondition(
        double LatitudeDeg, double LongitudeDeg, double EllipsoidHeightM,
        double HeadingDeg, double PitchDeg, double RollDeg, double SpeedMps,
        double AngleOfAttackDeg, double SideslipDeg);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetWind(
        double WindEastMps, double WindNorthMps, double WindUpMps);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetGroundControls(
        double SteeringDeg, double Brake, bool ParkingBrake);
    // Command only; travel uses simulation time. Retraction on ground and manual changes during auto operations refuse.
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetLandingGear(bool Down);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetGroundInitialCondition(
        double LatitudeDeg, double LongitudeDeg, double HeadingDeg, double GroundSpeedMps);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool ResetOnGround();
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool StartAutomaticTakeoff(int32 RunwayIndex, double ClimbHeightM, double SpeedMps);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool StartAutomaticLanding(int32 RunwayIndex, double ApproachSpeedMps);
    UFUNCTION(BlueprintCallable, Category="UAVSim") void CancelFlightOperation();
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool PrepareLandingApproach(int32 RunwayIndex);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool ReloadAircraftProfile();
    // Reload visual.json and optional STL while paused. Flight state and physical parameters are preserved.
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool ReloadAircraftVisual();
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetGust(double GustEastMps, double GustNorthMps, double GustUpMps, double DelayS, double DurationS);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetFailures(double EnginePowerFraction, bool ElevatorJammed, bool AileronJammed, bool RudderJammed);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetRoute(
        const TArray<FFlightWaypoint>& Waypoints, double LoiterRadiusM, bool Clockwise);
    UFUNCTION(BlueprintCallable, Category="UAVSim") void CancelNavigation();
    UFUNCTION(BlueprintCallable, Category="UAVSim") FAircraftState GetAircraftState() const { return State; }
    // Atomic manual inputs: each angle [-25,25] deg relative to trim, throttle [0,1]. Cancels navigation.
    // Positive: elevator trailing edge down; aileron right-roll command (right up, left down);
    // rudder trailing edge right. Applied by the same dynamics step used by navigation.
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetControls(
        double ElevatorDeg, double AileronDeg, double RudderDeg, double Throttle);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetRunning(bool Running);
    // Real-time pacing only. Explicit AdvanceSimulation seconds are never scaled.
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetTimeScale(double TimeScale);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetViewMode(const FString& ViewMode);
    // Native render harness: next frame to artifacts/terrain-view.png, no desktop capture.
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool CaptureView();
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetSensorSettings(bool Enabled, const FString& Mode, double FovDeg);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool SetGimbal(double AzimuthDeg, double ElevationDeg);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool TrackSensorTarget(const FString& TargetId);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool PointSensorGeodetic(double LatitudeDeg, double LongitudeDeg, double EllipsoidHeightM);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool ProtectSensor();
    UFUNCTION(BlueprintCallable, Category="UAVSim") FEoirFrame CaptureSensorFrame();
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool CreateGroundTarget(const FString& Id, const FString& Kind,
        double LatitudeDeg, double LongitudeDeg, double HeadingDeg);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool MoveGroundTarget(const FString& Id,
        double LatitudeDeg, double LongitudeDeg, double HeadingDeg);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool RemoveGroundTarget(const FString& Id);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool ReloadWargameWeapons();
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool PlayWargameShot(const FString& WeaponId, const FString& TargetId);
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool ResolveWargameShot(const FString& ShotId, const FString& Outcome);
    UFUNCTION(BlueprintCallable, Category="UAVSim") void ResetWargame();
    UFUNCTION(BlueprintCallable, CallInEditor, Category="UAVSim") void ResetFlight();
    // Explicit seconds, 0 < dt <= 1; pause first. No wall clock used by this function.
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool AdvanceSimulation(double DeltaSeconds);
    // Graceful exit only for the expected standalone process. Editor and PIE always refuse.
    UFUNCTION(BlueprintCallable, Category="UAVSim") bool RequestSimulationExit(int64 ExpectedProcessId);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    bool InitializeFlight(double Latitude, double Longitude, double Height, double Heading,
        double Pitch, double Roll, double Speed, const TOptional<FVector2D>& AirAngles);
    bool ApplyPosition(double Latitude, double Longitude, double Height, double Heading, double Pitch, double Roll);
    bool StepFlight(double DeltaSeconds);
    bool SyncState();
    bool QueryGround(flight_core::Vec3 Position, flight_core::Geodetic Origin,
        flight_core::GroundPlane& Plane, FString* SurfaceName = nullptr, double* EllipsoidHeight = nullptr) const;
    bool PrepareRunways(const FString& Directory, const flight_core::TerrainSurface& Surface,
        flight_core::TerrainSurface& Shoulders, flight_core::Geodetic RenderOrigin, TArray<FGroundRunway>& Prepared,
        TArray<TObjectPtr<UProceduralMeshComponent>>& Meshes, FString& Error);
    void SyncGroundState();
    void SyncSystemsState();
    bool LoadAircraftProfile(bool Reset);
    bool LocalRunway(int32 Index, flight_core::Runway& Out) const;
    void ApplyControlVisuals();
    void SyncSensor();
    TOptional<FVector> SensorPointWorld(double Latitude, double Longitude, double Height) const;
    void ClearGroundTargets();
    void AdvanceWargame(double DeltaSeconds);
    bool UpdateWargameView();
    void CancelWargameTarget(const FString& TargetId);
    void ApplyTargetHitVisual(const FSimTargetState& Target);
    TArray<FWargameVisual> WargameVisuals;
    TMap<FString, FWargameAnimation> WargameAnimations;
    int64 NextWargameShot = 1;
    bool GroundTargetTransform(double Latitude, double Longitude, double Heading, FTransform& Transform, double& Height);
    flight_core::Aircraft Dynamics;
    flight_core::Controls Inputs;
    flight_core::Environment FlightEnvironment;
    flight_core::FlightModelParameters ModelParameters;
    flight_core::FlightOperation Operation;
    int32 SelectedRunway = -1;
    flight_core::GroundControls GroundInputs;
    flight_core::TerrainSurface GroundTerrain;
    flight_core::TerrainSurface GroundRunwayShoulders;
    TArray<FGroundRunway> Runways;
    flight_core::Navigation Navigation;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Terrain;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Aircraft;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> AirframeVisual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> ImportedAircraftMesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> NoseWheel;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> NoseGearHinge;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> LeftMainGearHinge;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RightMainGearHinge;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> LeftElevator;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RightElevator;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> LeftAileron;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RightAileron;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Rudder;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> SensorCamera;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> SensorRenderTarget;
    UPROPERTY(Transient) TMap<FString, TObjectPtr<AActor>> TargetActors;
    UPROPERTY(Transient) TArray<TObjectPtr<UProceduralMeshComponent>> TerrainTiles;
    UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> SatelliteTextures;
    UPROPERTY(Transient) FAircraftState State;
    double OriginLatitude = 33.43;
    double OriginLongitude = 126.52;
    double OriginHeight = 0;
    double RenderOriginLatitude = 33.43;
    double RenderOriginLongitude = 126.52;
    double RenderOriginHeight = 0;
    FVector4 TerrainBounds = FVector4(124, 33, 132, 39);
    bool TiledTerrain = false;
    FVector TerrainViewCenter = FVector::ZeroVector;
    double TerrainViewSpanM = 20000;
    double Accumulator = 0;
    double LastPacingWallS = 0;
    double RateWindowWallS = 0;
    double RateWindowSimulationS = 0;
};
