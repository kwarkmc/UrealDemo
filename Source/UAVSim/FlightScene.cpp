#include "FlightScene.h"
#include "FlightCore.h"
#include "PropellerGeometry.h"
#include "LandingGearVisual.h"
#include "KismetProceduralMeshLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
FVector ChaseCameraOffset(double SpanM)
{
    const double DistanceCm = FMath::Max(600.0, SpanM * 300.0);
    return FVector(-DistanceCm, 0, DistanceCm * .3);
}

// ECEF basis of an ENU frame. Reflect both source and destination Y at the Unreal boundary.
FMatrix RenderFrame(double Latitude, double Longitude, double RenderLatitude, double RenderLongitude)
{
    auto Basis = [](double Lat, double Lon)
    {
        Lat = FMath::DegreesToRadians(Lat);
        Lon = FMath::DegreesToRadians(Lon);
        const double S = FMath::Sin(Lat), C = FMath::Cos(Lat), SL = FMath::Sin(Lon), CL = FMath::Cos(Lon);
        return FMatrix(FVector(-SL, CL, 0), FVector(-S * CL, -S * SL, C),
                       FVector(C * CL, C * SL, S), FVector::ZeroVector);
    };
    const auto Local = Basis(Latitude, Longitude), World = Basis(RenderLatitude, RenderLongitude);
    auto ToWorld = [&World](FVector V)
    {
        return FVector(V | World.GetScaledAxis(EAxis::X), -(V | World.GetScaledAxis(EAxis::Y)),
                       V | World.GetScaledAxis(EAxis::Z));
    };
    return FMatrix(ToWorld(Local.GetScaledAxis(EAxis::X)), -ToWorld(Local.GetScaledAxis(EAxis::Y)),
                   ToWorld(Local.GetScaledAxis(EAxis::Z)), FVector::ZeroVector);
}
}

AFlightScene::AFlightScene()
{
    FlightEnvironment.model = &ModelParameters;
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    Terrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Terrain"));
    Terrain->SetupAttachment(RootComponent);
    Terrain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Aircraft = CreateDefaultSubobject<USceneComponent>(TEXT("Aircraft"));
    Aircraft->SetupAttachment(RootComponent);
    AirframeVisual = CreateDefaultSubobject<USceneComponent>(TEXT("AirframeVisual"));
    AirframeVisual->SetupAttachment(Aircraft);
    // Match the physical 1.27 m span; the old silhouette used 6 m display dimensions.
    AirframeVisual->SetRelativeScale3D(FVector(1.27 / 6));
    ImportedAircraftMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ImportedAircraftMesh"));
    ImportedAircraftMesh->SetupAttachment(Aircraft);
    ImportedAircraftMesh->SetMobility(EComponentMobility::Movable);
    ImportedAircraftMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ImportedAircraftMesh->SetVisibility(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto Part = [this](const TCHAR* Name, USceneComponent* Parent, UStaticMesh* Mesh, FVector Location, FVector Scale)
    {
        auto* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Component->SetupAttachment(Parent == Aircraft.Get() ? AirframeVisual.Get() : Parent);
        Component->SetStaticMesh(Mesh);
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetRelativeLocation(Location);
        Component->SetRelativeScale3D(Scale);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    };
    // Generic silhouette, scaled above to the model's span. No measured Thor CAD.
    // Unreal aircraft-local +X forward, +Y right, +Z up; raw/core coordinates remain ENU.
    Part(TEXT("Fuselage"), Aircraft, Sphere.Object, FVector::ZeroVector, FVector(4, .45, .45));
    Part(TEXT("WingLeadingEdge"), Aircraft, Cube.Object, FVector(12.5, 0, 10), FVector(.60, 6, .10));
    Part(TEXT("WingFixedTrailingEdge"), Aircraft, Cube.Object, FVector(-30, 0, 10), FVector(.25, 2.4, .10));
    Part(TEXT("TailplaneLeadingEdge"), Aircraft, Cube.Object, FVector(-137.5, 0, 20), FVector(.40, 2.2, .08));
    Part(TEXT("FinLeadingEdge"), Aircraft, Cube.Object, FVector(-137.5, 0, 65), FVector(.45, .08, 1.1));
    auto Surface = [this, &Part](const TCHAR* HingeName, const TCHAR* MeshName, FVector Hinge, FVector Scale)
    {
        auto* Pivot = CreateDefaultSubobject<USceneComponent>(HingeName);
        Pivot->SetupAttachment(AirframeVisual);
        Pivot->SetMobility(EComponentMobility::Movable);
        Pivot->SetRelativeLocation(Hinge);
        // The cube's leading face meets the hinge; its trailing edge lies along local -X.
        Part(MeshName, Pivot, Cube.Object, FVector(-Scale.X * 50, 0, 0), Scale);
        return Pivot;
    };
    LeftAileron = Surface(TEXT("LeftAileronHinge"), TEXT("LeftAileronSurface"), FVector(-17.5, -210, 10), FVector(.25, 1.8, .10));
    RightAileron = Surface(TEXT("RightAileronHinge"), TEXT("RightAileronSurface"), FVector(-17.5, 210, 10), FVector(.25, 1.8, .10));
    LeftElevator = Surface(TEXT("LeftElevatorHinge"), TEXT("LeftElevatorSurface"), FVector(-157.5, -67.5, 20), FVector(.25, .85, .08));
    RightElevator = Surface(TEXT("RightElevatorHinge"), TEXT("RightElevatorSurface"), FVector(-157.5, 67.5, 20), FVector(.25, .85, .08));
    Rudder = Surface(TEXT("RudderHinge"), TEXT("RudderSurface"), FVector(-160, 0, 65), FVector(.25, .08, 1.1));
    LeftFlap = Surface(TEXT("LeftFlapHinge"), TEXT("LeftFlapSurface"), FVector::ZeroVector, FVector(.2, 1, .05));
    RightFlap = Surface(TEXT("RightFlapHinge"), TEXT("RightFlapSurface"), FVector::ZeroVector, FVector(.2, 1, .05));
    Propeller=CreateDefaultSubobject<USceneComponent>(TEXT("Propeller"));
    Propeller->SetupAttachment(AirframeVisual);
    PropellerBlades=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PropellerBlades"));
    PropellerBlur=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PropellerBlur"));
    PropellerSpinner=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PropellerSpinner"));
    for(auto* Mesh:{PropellerBlades.Get(),PropellerBlur.Get(),PropellerSpinner.Get()}) {
        Mesh->SetupAttachment(Propeller);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(false);
    }
    // Development folding geometry. Fully extended wheel bottoms match GroundContact.h (cm here).
    auto Gear = [this, &Part](const TCHAR* HingeName, const TCHAR* PivotName, const TCHAR* MeshName, FVector Position)
    {
        auto* Hinge = CreateDefaultSubobject<USceneComponent>(HingeName);
        Hinge->SetupAttachment(Aircraft);
        Hinge->SetMobility(EComponentMobility::Movable);
        Hinge->SetRelativeLocation(Position);
        auto* Pivot = CreateDefaultSubobject<USceneComponent>(PivotName);
        Pivot->SetupAttachment(Hinge);
        Pivot->SetMobility(EComponentMobility::Movable);
        const FString StrutName = FString(PivotName) + TEXT("Strut");
        Part(*StrutName, Pivot, Cube.Object, FVector(0, 0, -5.5), FVector(.012, .012, .11));
        Part(MeshName, Pivot, Sphere.Object, FVector(0, 0, -11), FVector(.06, .025, .06));
        if (FString(PivotName) == TEXT("NoseWheelPivot")) NoseWheel = Pivot;
        return Hinge;
    };
    NoseGearHinge = Gear(TEXT("NoseGearHinge"), TEXT("NoseWheelPivot"), TEXT("NoseWheel"), FVector(35, 0, -4));
    LeftMainGearHinge = Gear(TEXT("LeftMainGearHinge"), TEXT("LeftMainWheelPivot"), TEXT("LeftMainWheel"), FVector(-12, -22, -4));
    RightMainGearHinge = Gear(TEXT("RightMainGearHinge"), TEXT("RightMainWheelPivot"), TEXT("RightMainWheel"), FVector(-12, 22, -4));
    LandingPath=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("LandingReferencePath"));
    LandingPath->SetupAttachment(RootComponent);
    LandingPath->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    LandingPath->SetVisibility(false);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
    Camera->SetupAttachment(Aircraft);
    Camera->SetRelativeLocation(FVector(-600, 0, 180));
    Camera->SetRelativeRotation(FRotator(-17, 0, 0));
    Camera->FieldOfView = 75;
    SensorCamera = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("EoirCamera"));
    SensorCamera->SetupAttachment(Aircraft);
    SensorCamera->SetRelativeLocation(FVector(20, 0, -12));
    SensorCamera->SetRelativeRotation(FRotator(-45, 0, 0));
    SensorCamera->bCaptureEveryFrame = false;
    SensorCamera->bCaptureOnMovement = false;
    SensorCamera->bAlwaysPersistRenderingState = true;
    SensorCamera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    SensorCamera->FOVAngle = 30;
    SensorCamera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
    SensorCamera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
    SensorCamera->PostProcessSettings.AutoExposureMinBrightness = 1;
    SensorCamera->PostProcessSettings.AutoExposureMaxBrightness = 1;
    SensorCamera->ShowFlags.SetMotionBlur(false);
    SensorCamera->ShowFlags.SetTemporalAA(false);
    auto* Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
    Sun->SetupAttachment(RootComponent);
    Sun->SetMobility(EComponentMobility::Movable);
    Sun->SetRelativeRotation(FRotator(-40, -35, 0));
    Sun->SetIntensity(10);
    Sun->bAtmosphereSunLight = true;
    auto* Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
    Atmosphere->SetupAttachment(RootComponent);
    auto* Sky = CreateDefaultSubobject<USkyLightComponent>(TEXT("Sky"));
    Sky->SetupAttachment(RootComponent);
    Sky->SetMobility(EComponentMobility::Movable);
    Sky->SetRealTimeCaptureEnabled(true);
    Sky->SetIntensity(1);
}

bool AFlightScene::LoadTerrain()
{
    SetRunning(false);
    TArray<TObjectPtr<UProceduralMeshComponent>> NewTiles;
    TArray<TObjectPtr<UTexture2D>> NewTextures;
    flight_core::TerrainSurface NewGroundTerrain;
    flight_core::TerrainSurface NewShoulders;
    TArray<FGroundRunway> NewRunways;
    FBox Extents(ForceInit);
    auto Fail = [this, &NewTiles](const TCHAR* Reason)
    {
        for (const auto& Component : NewTiles) Component->DestroyComponent();
        State.TerrainError = Reason; // Preserve the current map and flight fault on an unsuccessful reload.
        UE_LOG(LogTemp, Error, TEXT("UAVSim terrain: %s"), Reason);
        return false;
    };
    auto ReadJson = [](const FString& Path, int64 MaxBytes, TSharedPtr<FJsonObject>& Json)
    {
        FString Text;
        const int64 Size = IFileManager::Get().FileSize(*Path);
        return Size > 0 && Size <= MaxBytes && FFileHelper::LoadFileToString(Text, *Path)
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid();
    };
    auto Filename = [](const FString& Value)
    {
        if (Value.IsEmpty() || Value.Len() > 100 || Value.StartsWith(TEXT(".")) || Value.EndsWith(TEXT("."))) return false;
        for (const TCHAR Character : Value)
            if (!FChar::IsAlnum(Character) && Character != TEXT('_') && Character != TEXT('-') && Character != TEXT('.')) return false;
        return true;
    };
    auto NumberField = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, double& Value)
    {
        const auto Field = Object->TryGetField(Name);
        return Field.IsValid() && Field->Type == EJson::Number && Field->TryGetNumber(Value) && FMath::IsFinite(Value);
    };
    const FString MapsDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("maps"));
    const FString ActivePath = FPaths::Combine(MapsDirectory, TEXT("active.json"));
    const bool Legacy = !FPaths::FileExists(ActivePath);
    FString Manifest, ManifestPath;
    if (!Legacy)
    {
        TSharedPtr<FJsonObject> Active;
        if (!ReadJson(ActivePath, 4096, Active) || !Active->TryGetStringField(TEXT("manifest"), Manifest)
            || Manifest.Len() > 256 || !FPaths::IsRelative(Manifest))
            return Fail(TEXT("maps/active.json must select a relative manifest path beneath maps"));
        TArray<FString> Segments;
        Manifest.ParseIntoArray(Segments, TEXT("/"), false);
        if (Segments.IsEmpty()) return Fail(TEXT("Active manifest path is empty"));
        for (const FString& Segment : Segments)
            if (!Filename(Segment)) return Fail(TEXT("Active manifest path contains an invalid or traversing segment"));
        if (!Manifest.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase)) return Fail(TEXT("Active manifest must be JSON"));
        ManifestPath = FPaths::Combine(MapsDirectory, Manifest);
    }
    else
    {
        const bool OldCountry = FPaths::FileExists(FPaths::Combine(FPaths::ProjectDir(), TEXT("data/korea/metadata.json")));
        Manifest = OldCountry ? TEXT("data/korea/metadata.json") : TEXT("data/jeju/metadata.json");
        ManifestPath = FPaths::Combine(FPaths::ProjectDir(), Manifest);
    }
    const FString Directory = FPaths::GetPath(ManifestPath);
    TSharedPtr<FJsonObject> Metadata;
    if (!ReadJson(ManifestPath, 2 * 1024 * 1024, Metadata)) return Fail(TEXT("Terrain manifest is missing, invalid, or larger than 2 MB"));
    double Schema = 0;
    if (!NumberField(Metadata, TEXT("schema_version"), Schema) || (Schema != 2 && !(Legacy && Schema == 1)))
        return Fail(TEXT("Active terrain manifests require schema_version 2"));
    const bool Tiled = Schema == 2;
    FString Dataset, Name, Attribution;
    if (!Legacy)
    {
        if (!Metadata->TryGetStringField(TEXT("id"), Dataset) || !Filename(Dataset)
            || !Metadata->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() || Name.Len() > 200
            || !Metadata->TryGetStringField(TEXT("attribution"), Attribution) || Attribution.Len() > 4096)
            return Fail(TEXT("Manifest requires a simple id, a name, and an attribution string"));
    }
    else
    {
        Dataset = Tiled ? TEXT("korea") : TEXT("jeju");
        Metadata->TryGetStringField(TEXT("name"), Name);
        Attribution = Tiled
            ? TEXT("EOxCloudless https://cloudless.eox.at by EOX IT Services GmbH; modified Copernicus Sentinel data 2016 & 2017, Dokdo 2025; Copernicus DEM GLO-30")
            : TEXT("Copernicus DEM GLO-30 and Sentinel-2; see THIRD_PARTY_NOTICES.md");
    }
    const TSharedPtr<FJsonObject>* Origin = nullptr;
    double Latitude = 0, Longitude = 0, Elevation = 0;
    if (!Metadata->TryGetObjectField(TEXT("origin"), Origin)
        || !NumberField(*Origin, TEXT("latitude_deg"), Latitude)
        || !NumberField(*Origin, TEXT("longitude_deg"), Longitude)
        || !NumberField(*Origin, TEXT("ellipsoid_height_m"), Elevation)
        || !flight_core::valid({Latitude, Longitude, Elevation}))
        return Fail(TEXT("Invalid WGS84 render origin"));
    TArray<TSharedPtr<FJsonValue>> Tiles;
    FVector4 Bounds = TerrainBounds;
    if (Tiled)
    {
        const TArray<TSharedPtr<FJsonValue>>* ManifestTiles = nullptr;
        if (!Metadata->TryGetArrayField(TEXT("tiles"), ManifestTiles)
            || ManifestTiles->Num() < 1 || ManifestTiles->Num() > 64)
            return Fail(TEXT("Expected 1..64 terrain tiles"));
        const TArray<TSharedPtr<FJsonValue>>* BoundsValues = nullptr;
        if (!Metadata->TryGetArrayField(TEXT("bounds_deg"), BoundsValues) || BoundsValues->Num() != 4)
            return Fail(TEXT("bounds_deg must contain west, south, east, north"));
        for (int32 Axis = 0; Axis < 4; ++Axis)
            if (!(*BoundsValues)[Axis].IsValid() || (*BoundsValues)[Axis]->Type != EJson::Number
                || !(*BoundsValues)[Axis]->TryGetNumber(Bounds[Axis])
                || !FMath::IsFinite(Bounds[Axis]))
                return Fail(TEXT("Terrain bounds must be finite numbers"));
        if (Bounds.X < -180 || Bounds.Z > 180 || Bounds.Y < -90 || Bounds.W > 90
            || Bounds.X >= Bounds.Z || Bounds.Y >= Bounds.W)
            return Fail(TEXT("Terrain bounds must be ordered WGS84 longitude/latitude limits"));
        if (Longitude < Bounds.X || Longitude > Bounds.Z || Latitude < Bounds.Y || Latitude > Bounds.W)
            return Fail(TEXT("Render origin must lie inside the terrain bounds"));
        Tiles = *ManifestTiles;
    }
    else Tiles.Add(MakeShared<FJsonValueObject>(Metadata));
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Satellite.M_Satellite"));
    if (!Material) return Fail(TEXT("Missing /Game/Materials/M_Satellite"));
    auto* ImageModule = FModuleManager::LoadModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
    if (!ImageModule) return Fail(TEXT("ImageWrapper module is unavailable"));
    TSet<FString> Directories;
    // ponytail: all bounded base tiles reside in memory; add distance streaming when higher detail requires it.
    for (const auto& TileValue : Tiles)
    {
        const TSharedPtr<FJsonObject>* Tile = nullptr;
        if (!TileValue.IsValid() || !TileValue->TryGetObject(Tile) || !Tile || !Tile->IsValid())
            return Fail(TEXT("Tile must be an object"));
        double GridWidth = 0, GridHeight = 0;
        if (!NumberField(*Tile, TEXT("width"), GridWidth) || !NumberField(*Tile, TEXT("height"), GridHeight)
            || GridWidth < 2 || GridHeight < 2 || GridWidth > 513 || GridHeight > 513
            || GridWidth != static_cast<int32>(GridWidth) || GridHeight != static_cast<int32>(GridHeight))
            return Fail(TEXT("Tile grid dimensions must be 2..513"));
        const int32 Width = static_cast<int32>(GridWidth), Height = static_cast<int32>(GridHeight);
        int32 SkipCells[4] = {0, 0, 0, 0};
        if ((*Tile)->HasField(TEXT("skip_cells")))
        {
            const TArray<TSharedPtr<FJsonValue>>* SkipValues = nullptr;
            if (!(*Tile)->TryGetArrayField(TEXT("skip_cells"), SkipValues) || SkipValues->Num() != 4)
                return Fail(TEXT("skip_cells must contain four half-open cell indices"));
            for (int32 Axis = 0; Axis < 4; ++Axis)
            {
                double Value = 0;
                if (!(*SkipValues)[Axis].IsValid() || (*SkipValues)[Axis]->Type != EJson::Number || !(*SkipValues)[Axis]->TryGetNumber(Value)
                    || !FMath::IsFinite(Value) || Value < 0 || Value > (Axis % 2 == 0 ? Width - 1 : Height - 1)
                    || Value != static_cast<int32>(Value))
                    return Fail(TEXT("skip_cells indices must be integers within the tile cell grid"));
                SkipCells[Axis] = static_cast<int32>(Value);
            }
            if (SkipCells[0] >= SkipCells[2] || SkipCells[1] >= SkipCells[3])
                return Fail(TEXT("skip_cells must be a nonempty ordered rectangle"));
        }
        FString TileDirectory = Directory;
        FVector Center = FVector::ZeroVector;
        if (Tiled)
        {
            FString Relative, TileId;
            const TArray<TSharedPtr<FJsonValue>>* Offset = nullptr;
            if (!(*Tile)->TryGetStringField(TEXT("id"), TileId) || !Filename(TileId)
                || !(*Tile)->TryGetStringField(TEXT("directory"), Relative) || Relative != TEXT("tiles/") + TileId
                || Directories.Contains(Relative.ToLower()))
                return Fail(TEXT("Tiles require unique ids and matching tiles/<id> directories"));
            Directories.Add(Relative.ToLower());
            TileDirectory = FPaths::Combine(Directory, Relative);
            if (!(*Tile)->TryGetArrayField(TEXT("center_enu_m"), Offset) || Offset->Num() != 3)
                return Fail(TEXT("Tile center_enu_m must have three numbers"));
            for (int32 Axis = 0; Axis < 3; ++Axis)
                if (!(*Offset)[Axis].IsValid() || (*Offset)[Axis]->Type != EJson::Number || !(*Offset)[Axis]->TryGetNumber(Center[Axis])
                    || !FMath::IsFinite(Center[Axis]) || FMath::Abs(Center[Axis]) > 2e6)
                    return Fail(TEXT("Invalid tile center"));
        }
        FString VertexFile = TEXT("vertices.raw"), ImageFile = TEXT("satellite.png");
        if ((!Legacy || (*Tile)->HasField(TEXT("vertex_file"))) && !(*Tile)->TryGetStringField(TEXT("vertex_file"), VertexFile))
            return Fail(TEXT("Tile requires vertex_file"));
        if ((!Legacy || (*Tile)->HasField(TEXT("image_file"))) && !(*Tile)->TryGetStringField(TEXT("image_file"), ImageFile))
            return Fail(TEXT("Tile requires image_file"));
        const FString ImageExtension = FPaths::GetExtension(ImageFile).ToLower();
        if (!Filename(VertexFile) || FPaths::GetExtension(VertexFile).ToLower() != TEXT("raw")
            || !Filename(ImageFile) || (ImageExtension != TEXT("png") && ImageExtension != TEXT("jpg") && ImageExtension != TEXT("jpeg")))
            return Fail(TEXT("Tile filenames must be basenames: .raw vertices and .png/.jpg/.jpeg image"));
        const FVector DisplayCenter = FVector(Center.X, -Center.Y, Center.Z) * 100.0;
        TArray<uint8> Bytes;
        const FString VertexPath = FPaths::Combine(TileDirectory, VertexFile);
        const int64 ExpectedBytes = int64(Width) * Height * 3 * sizeof(float);
        if (IFileManager::Get().FileSize(*VertexPath) != ExpectedBytes
            || !FFileHelper::LoadFileToArray(Bytes, *VertexPath) || Bytes.Num() != ExpectedBytes)
            return Fail(TEXT("vertices.raw must contain width*height ENU float32 triples"));
        TArray<FVector> Vertices, Normals;
        TArray<FVector2D> UV;
        TArray<int32> Indices;
        TArray<FProcMeshTangent> Tangents;
        Vertices.Reserve(Width * Height);
        UV.Reserve(Width * Height);
        for (int32 Row = 0; Row < Height; ++Row)
            for (int32 Column = 0; Column < Width; ++Column)
            {
                float ENU[3]; // Prepared little-endian float32 relative to the tile center, Windows x64.
                FMemory::Memcpy(ENU, Bytes.GetData() + (Row * Width + Column) * sizeof(ENU), sizeof(ENU));
                const FVector V(ENU[0] * 100.0, -ENU[1] * 100.0, ENU[2] * 100.0);
                if (V.ContainsNaN() || V.GetAbsMax() > 2e7) return Fail(TEXT("Nonfinite or out-of-range tile vertex"));
                const FVector DisplayVertex = V + DisplayCenter;
                if (std::hypot(DisplayVertex.X, DisplayVertex.Y) > 1e8)
                    return Fail(TEXT("Terrain vertices exceed the 1000 km horizontal render radius"));
                Vertices.Add(V); // Display only: UE X=East, Y=South, Z=Up in cm; raw/core coordinates stay ENU.
                Extents += DisplayVertex;
                UV.Add(FVector2D(double(Column) / (Width - 1), double(Row) / (Height - 1)));
            }
        Indices.Reserve((Width - 1) * (Height - 1) * 6);
        for (int32 Row = 0; Row < Height - 1; ++Row)
            for (int32 Column = 0; Column < Width - 1; ++Column)
            {
                if (Column >= SkipCells[0] && Column < SkipCells[2] && Row >= SkipCells[1] && Row < SkipCells[3]) continue;
                const int32 A = Row * Width + Column, B = A + 1, C = A + Width, D = C + 1;
                // Rows progress North to South; reverse winding after reflecting ENU's North axis.
                Indices.Append({A, C, B, B, C, D});
            }
        if (Indices.IsEmpty()) return Fail(TEXT("skip_cells may not remove an entire terrain tile"));
        std::vector<flight_core::Vec3> GroundVertices;
        GroundVertices.reserve(Vertices.Num());
        for (const auto& V : Vertices)
            GroundVertices.push_back({V.X / 100 + Center.X, -V.Y / 100 + Center.Y, V.Z / 100 + Center.Z});
        const std::vector<uint32_t> GroundIndices(Indices.GetData(), Indices.GetData() + Indices.Num());
        if (!NewGroundTerrain.add_tile(Width, Height, std::move(GroundVertices), GroundIndices))
            return Fail(TEXT("Terrain triangle indexing could not build a collision surface"));
        Normals.Reserve(Vertices.Num());
        Tangents.Reserve(Vertices.Num());
        for (int32 Row = 0; Row < Height; ++Row)
            for (int32 Column = 0; Column < Width; ++Column)
            {
                const FVector East = Vertices[Row * Width + FMath::Min(Column + 1, Width - 1)]
                    - Vertices[Row * Width + FMath::Max(Column - 1, 0)];
                const FVector South = Vertices[FMath::Min(Row + 1, Height - 1) * Width + Column]
                    - Vertices[FMath::Max(Row - 1, 0) * Width + Column];
                const FVector Normal = FVector::CrossProduct(East, South).GetSafeNormal();
                if (Normal.Z <= 0) return Fail(TEXT("Grid must run West-to-East and North-to-South"));
                Normals.Add(Normal);
                Tangents.Emplace(East.GetSafeNormal(), false); // Cross(Up, East) is display South, matching UV V.
            }
        const FString TexturePath = FPaths::Combine(TileDirectory, ImageFile);
        const int64 TextureBytes = IFileManager::Get().FileSize(*TexturePath);
        if (TextureBytes < 4 || TextureBytes > 20 * 1024 * 1024
            || !FFileHelper::LoadFileToArray(Bytes, *TexturePath) || Bytes.Num() != TextureBytes)
            return Fail(TEXT("Tile image is missing or exceeds 20 MB"));
        const auto Format = ImageModule->DetectImageFormat(Bytes.GetData(), Bytes.Num());
        if ((ImageExtension == TEXT("png") && Format != EImageFormat::PNG)
            || (ImageExtension != TEXT("png") && Format != EImageFormat::JPEG))
            return Fail(TEXT("Tile image bytes must match its PNG/JPEG extension"));
        // Inspect encoded image dimensions before the engine allocates decoded pixel storage.
        const auto ImageInfo = ImageModule->CreateImageWrapper(Format);
        if (!ImageInfo.IsValid() || !ImageInfo->SetCompressed(Bytes.GetData(), Bytes.Num())
            || ImageInfo->GetWidth() < 1 || ImageInfo->GetWidth() > 2048 || ImageInfo->GetHeight() < 1 || ImageInfo->GetHeight() > 2048)
            return Fail(TEXT("Satellite texture dimensions must be 1..2048"));
        auto* Texture = FImageUtils::ImportBufferAsTexture2D(Bytes);
        if (!Texture) return Fail(TEXT("Could not decode tile image"));
        Texture->Filter = TF_Trilinear;
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;
        Texture->UpdateResource(); // Adjacent tiles must not wrap their opposite image edge into the border.
        auto* Instance = UMaterialInstanceDynamic::Create(Material, this);
        Instance->SetTextureParameterValue(TEXT("Satellite"), Texture);
        auto* Component = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
        Component->SetupAttachment(RootComponent);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetVisibility(false);
        AddInstanceComponent(Component);
        Component->RegisterComponent();
        NewTiles.Add(Component);
        Component->SetRelativeLocation(DisplayCenter);
        Component->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, UV, {}, Tangents, false, false);
        Component->SetMaterial(0, Instance);
        NewTextures.Add(Texture);
    }
    FString RunwayError;
    if (!PrepareRunways(Directory, NewGroundTerrain, NewShoulders, {Latitude, Longitude, Elevation}, NewRunways, NewTiles, RunwayError))
        return Fail(*RunwayError);
    GroundTerrain = std::move(NewGroundTerrain);
    GroundRunwayShoulders = std::move(NewShoulders);
    Runways = MoveTemp(NewRunways);
    Terrain->ClearAllMeshSections();
    Terrain->SetMaterial(0, nullptr);
    for (const auto& Component : TerrainTiles) Component->DestroyComponent();
    TerrainTiles = MoveTemp(NewTiles);
    SatelliteTextures = MoveTemp(NewTextures);
    for (const auto& Component : TerrainTiles) Component->SetVisibility(true);
    RenderOriginLatitude = Latitude;
    RenderOriginLongitude = Longitude;
    RenderOriginHeight = Elevation;
    TiledTerrain = Tiled;
    if (Tiled) TerrainBounds = Bounds;
    TerrainViewCenter = Extents.GetCenter();
    TerrainViewSpanM = FMath::Max(Extents.GetSize().X, Extents.GetSize().Y) / 100.0;
    const auto PreviousOrigin = flight_core::geodetic_to_enu({OriginLatitude, OriginLongitude, 2600}, {Latitude, Longitude, Elevation});
    const bool KeepOrigin = PreviousOrigin && std::hypot(PreviousOrigin->x, PreviousOrigin->y) <= 1e6 && (Tiled
        ? OriginLongitude >= Bounds.X && OriginLongitude <= Bounds.Z && OriginLatitude >= Bounds.Y && OriginLatitude <= Bounds.W
        : std::hypot(PreviousOrigin->x, PreviousOrigin->y) <= 50000);
    if (!KeepOrigin)
    {
        OriginLatitude = Latitude;
        OriginLongitude = Longitude;
        OriginHeight = 0;
    }
    State.TerrainLoaded = true;
    State.TerrainTileCount = SatelliteTextures.Num();
    State.TerrainDataset = Dataset;
    State.TerrainManifest = Manifest;
    State.TerrainAttribution = Attribution;
    State.TerrainError.Empty();
    ClearGroundTargets();
    ResetFlight();
    SetViewMode(State.ViewMode);
    UE_LOG(LogTemp, Display, TEXT("UAVSim: loaded %d georeferenced terrain tiles (%s)"), State.TerrainTileCount, *State.TerrainDataset);
    return true;
}

bool AFlightScene::ApplyPosition(double Latitude, double Longitude, double Height, double Heading, double Pitch, double Roll)
{
    if (!FMath::IsFinite(Heading) || !FMath::IsFinite(Pitch) || !FMath::IsFinite(Roll)
        || FMath::Abs(Heading) > 360 || FMath::Abs(Pitch) > 90 || FMath::Abs(Roll) > 180
        || Height < -500 - 1e-6 || Height > 20000 + 1e-6) return false; // ENU inverse roundoff only.
    const auto ENU = flight_core::geodetic_to_enu({Latitude, Longitude, Height},
        {RenderOriginLatitude, RenderOriginLongitude, RenderOriginHeight});
    if (!ENU || std::hypot(ENU->x, ENU->y) > 1e6) return false;
    // ENU -> Unreal East/South/Up avoids mirrored imagery. Rotate the local tangent attitude too.
    const FQuat Frame(RenderFrame(OriginLatitude, OriginLongitude, RenderOriginLatitude, RenderOriginLongitude));
    Aircraft->SetRelativeLocationAndRotation(FVector(ENU->x, -ENU->y, ENU->z) * 100.0,
        Frame * FRotator(Pitch, Heading - 90, Roll).Quaternion());
    State.LatitudeDeg = Latitude;
    State.LongitudeDeg = Longitude;
    State.EllipsoidHeightM = Height;
    State.HeadingDeg = FMath::Fmod(Heading + 360, 360);
    State.PitchDeg = Pitch;
    State.RollDeg = Roll;
    return true;
}

bool AFlightScene::SetAircraftGeodetic(double LatitudeDeg, double LongitudeDeg, double EllipsoidHeightM,
                                      double HeadingDeg, double PitchDeg, double RollDeg)
{
    return SetInitialCondition(LatitudeDeg, LongitudeDeg, EllipsoidHeightM, HeadingDeg, PitchDeg, RollDeg, State.SpeedMps);
}

bool AFlightScene::SetInitialCondition(double LatitudeDeg, double LongitudeDeg, double EllipsoidHeightM,
                                      double HeadingDeg, double PitchDeg, double RollDeg, double SpeedMps)
{
    return InitializeFlight(LatitudeDeg, LongitudeDeg, EllipsoidHeightM, HeadingDeg,
        PitchDeg, RollDeg, SpeedMps, {});
}

bool AFlightScene::SetAerodynamicInitialCondition(double LatitudeDeg, double LongitudeDeg, double EllipsoidHeightM,
    double HeadingDeg, double PitchDeg, double RollDeg, double SpeedMps, double AngleOfAttackDeg, double SideslipDeg)
{
    if (!FMath::IsFinite(AngleOfAttackDeg) || FMath::Abs(AngleOfAttackDeg) > 180
        || !FMath::IsFinite(SideslipDeg) || FMath::Abs(SideslipDeg) > 89) return false;
    return InitializeFlight(LatitudeDeg, LongitudeDeg, EllipsoidHeightM, HeadingDeg,
        PitchDeg, RollDeg, SpeedMps, FVector2D(AngleOfAttackDeg, SideslipDeg));
}

bool AFlightScene::SetWind(double WindEastMps, double WindNorthMps, double WindUpMps)
{
    const flight_core::Vec3 Wind{WindEastMps, WindNorthMps, WindUpMps};
    if (!flight_core::finite(Wind) || FMath::Abs(WindEastMps) > 30
        || FMath::Abs(WindNorthMps) > 30 || FMath::Abs(WindUpMps) > 30) return false;
    // A changed wind changes relative air velocity, not inertial aircraft velocity.
    FlightEnvironment.wind_enu_mps = Wind;
    return SyncState();
}

bool AFlightScene::InitializeFlight(double LatitudeDeg, double LongitudeDeg, double EllipsoidHeightM,
    double HeadingDeg, double PitchDeg, double RollDeg, double SpeedMps, const TOptional<FVector2D>& AirAngles)
{
    const auto Position = flight_core::geodetic_to_enu({LatitudeDeg, LongitudeDeg, EllipsoidHeightM},
                                                      {LatitudeDeg, LongitudeDeg, 0});
    const auto& Limits = ModelParameters.operating;
    if (!Position || !FMath::IsFinite(SpeedMps) || SpeedMps < Limits.minimum_speed_mps || SpeedMps > Limits.maximum_speed_mps
        || !FMath::IsFinite(PitchDeg) || FMath::Abs(PitchDeg) > 89
        || !FMath::IsFinite(RollDeg) || FMath::Abs(RollDeg) > 180
        || !FMath::IsFinite(HeadingDeg) || FMath::Abs(HeadingDeg) > 360
        || EllipsoidHeightM < Limits.minimum_height_m || EllipsoidHeightM > Limits.maximum_height_m) return false;
    if (AirAngles.IsSet() && (std::abs(AirAngles->X)>Limits.maximum_alpha_deg
        || std::abs(AirAngles->Y)>Limits.maximum_beta_deg)) return false;
    const auto RenderPosition = flight_core::geodetic_to_enu({LatitudeDeg, LongitudeDeg, EllipsoidHeightM},
        {RenderOriginLatitude, RenderOriginLongitude, RenderOriginHeight});
    if (!RenderPosition || std::hypot(RenderPosition->x, RenderPosition->y) > 1e6) return false;
    if (TiledTerrain)
    {
        if (LongitudeDeg < TerrainBounds.X || LongitudeDeg > TerrainBounds.Z
            || LatitudeDeg < TerrainBounds.Y || LatitudeDeg > TerrainBounds.W) return false;
    }
    else
    {
        const auto Local = flight_core::geodetic_to_enu({LatitudeDeg, LongitudeDeg, EllipsoidHeightM},
            {RenderOriginLatitude, RenderOriginLongitude, RenderOriginHeight});
        if (!Local || std::hypot(Local->x, Local->y) > 50000) return false;
    }
    auto Environment = FlightEnvironment;
    Environment.origin_altitude_m = 0;
    auto Initial = flight_core::trim_aircraft(*Position, FMath::DegreesToRadians(HeadingDeg), SpeedMps, Environment);
    if (!Initial) return false;
    const auto Trim = flight_core::level_trim(*Position, FMath::DegreesToRadians(HeadingDeg), SpeedMps, Environment, Dynamics.time_s);
    const double Alpha = AirAngles.IsSet() ? FMath::DegreesToRadians(AirAngles->X)
        : flight_core::air_data(*Initial, Environment).alpha_rad;
    const double Beta = AirAngles.IsSet() ? FMath::DegreesToRadians(AirAngles->Y) : 0;
    Initial->pitch_rad = FMath::DegreesToRadians(PitchDeg);
    Initial->roll_rad = FMath::DegreesToRadians(RollDeg);
    Initial->body_velocity_mps = flight_core::Vec3{
        SpeedMps * std::cos(Beta) * std::cos(Alpha), SpeedMps * std::sin(Beta),
        SpeedMps * std::cos(Beta) * std::sin(Alpha)}
        + flight_core::enu_to_body(*Initial, flight_core::wind_at(Environment, Dynamics.time_s));
    // The initial condition represents an established flow at the supplied AoA.
    Initial->separation = flight_core::equilibrium_separation(Alpha, Environment);
    Initial->time_s = Dynamics.time_s;
    if (!flight_core::fixed_wing_detail::valid({*Initial, flight_core::fixed_wing_detail::orientation(*Initial)}, Environment)) return false;
    flight_core::GroundPlane Plane;
    if (QueryGround(Initial->position_enu_m, {LatitudeDeg, LongitudeDeg, 0}, Plane))
    {
        if (!Limits.ground_supported) {
            if (flight_core::fixed_wing_detail::dot(Initial->position_enu_m-Plane.point_enu_m, Plane.normal_enu)<=ModelParameters.span_m) return false;
        } else {
            const auto Ground = flight_core::ground_data(*Initial, Plane, FlightEnvironment);
            if (Ground.minimum_wheel_clearance_m < -1e-5 || Ground.body_strike) return false;
        }
    }
    OriginLatitude = LatitudeDeg;
    OriginLongitude = LongitudeDeg;
    OriginHeight = 0;
    FlightEnvironment = Environment;
    Dynamics = *Initial;
    Inputs = Trim ? Trim->controls : flight_core::Controls{};
    Dynamics.actual_controls = Inputs;
    Dynamics.actuator_initialized = true;
    GroundInputs = {};
    Navigation = {};
    Operation = {};
    SelectedRunway = -1;
    State.Fault.Empty();
    State.DistanceTravelledM = 0;
    SetRunning(false);
    return SyncState();
}

bool AFlightScene::SetRoute(const TArray<FFlightWaypoint>& Waypoints, double LoiterRadiusM, bool Clockwise)
{
    if (!ModelParameters.operating.navigation_supported) return false;
    if (!State.Fault.IsEmpty() || State.GroundOnGround || Waypoints.Num() < 1 || Waypoints.Num() > 64) return false;
    flight_core::Navigation Route;
    Route.loiter_radius_m = LoiterRadiusM;
    Route.clockwise = Clockwise;
    for (const auto& Point : Waypoints)
    {
        const auto Position = flight_core::geodetic_to_enu({Point.LatitudeDeg, Point.LongitudeDeg, Point.EllipsoidHeightM},
                                                          {OriginLatitude, OriginLongitude, OriginHeight});
        if (!Position || Point.EllipsoidHeightM < ModelParameters.operating.minimum_height_m || Point.EllipsoidHeightM > ModelParameters.operating.maximum_height_m
            || std::hypot(Position->x, Position->y) > 50000) return false;
        Route.waypoints.push_back({*Position, Point.SpeedMps});
    }
    if (!flight_core::valid_route(Route, ModelParameters)) return false;
    // Keep the complete final circle inside the local scene coordinate envelope.
    const auto& Center = Route.waypoints.back().position_enu_m;
    if (std::hypot(Center.x, Center.y) + LoiterRadiusM > 50000) return false;
    Navigation = std::move(Route);
    Operation = {};
    SelectedRunway = -1;
    return SyncState();
}

void AFlightScene::CancelNavigation()
{
    Navigation = {};
    Operation = {};
    SelectedRunway = -1;
    SyncState();
}

bool AFlightScene::SetControls(double ElevatorDeg, double AileronDeg, double RudderDeg, double Throttle)
{
    const flight_core::Controls Candidate{ElevatorDeg, AileronDeg, RudderDeg, Throttle};
    if (!flight_core::valid_controls(Candidate)) return false;
    Inputs = Candidate;
    CancelNavigation();
    return true;
}

void AFlightScene::ApplyControlVisuals()
{
    if (GeometryFingerprint!=State.AirframeFingerprint) ConfigureBuiltinGeometry();
    const auto Actual = flight_core::applied_controls(Dynamics, Inputs, FlightEnvironment);
    const double ElevatorDeg = Actual.elevator_deg, AileronDeg = Actual.aileron_deg, RudderDeg = Actual.rudder_deg;
    // UE +pitch lowers trailing (-X) edges. Negative yaw moves the rudder trailing edge right (+Y).
    LeftElevator->SetRelativeRotation(FRotator(ElevatorDeg, 0, 0));
    RightElevator->SetRelativeRotation(FRotator(ElevatorDeg, 0, 0));
    LeftAileron->SetRelativeRotation(FRotator(AileronDeg, 0, 0));
    RightAileron->SetRelativeRotation(FRotator(-AileronDeg, 0, 0));
    Rudder->SetRelativeRotation(FRotator(0, -RudderDeg, 0));
    LeftFlap->SetRelativeRotation(FRotator(Dynamics.flap_deg,0,0));
    RightFlap->SetRelativeRotation(FRotator(Dynamics.flap_deg,0,0));
    if (Dynamics.time_s<VisualTimeS) PropellerAngleDeg=0;
    const double Elapsed=FMath::Max(0.0,Dynamics.time_s-VisualTimeS);
    VisualTimeS=Dynamics.time_s;
    // shortcut: propeller phase is display animation, not an RPM/torque model.
    const double VisualRps=flight_core::dynamic_propeller(ModelParameters) ? Dynamics.propeller_omega_rps/(2*PI)
        : 20*Actual.throttle*FlightEnvironment.engine_power_fraction;
    PropellerAngleDeg=FMath::Fmod(PropellerAngleDeg+Elapsed*360*VisualRps*ModelParameters.propeller_spin_direction,360.0);
    WheelAngleDeg=FMath::RadiansToDegrees(Dynamics.wheel_rotation_rad);
    Propeller->SetRelativeRotation(FRotator(0,0,PropellerAngleDeg));
    const double Blend=propeller_visual::blur_fraction(VisualRps);
    if(PropellerBladeMaterial) PropellerBladeMaterial->SetScalarParameterValue(TEXT("Opacity"),1-Blend);
    if(PropellerBlurMaterial) PropellerBlurMaterial->SetScalarParameterValue(TEXT("Opacity"),.28*Blend);
    PropellerBlades->SetVisibility(Blend<.999 && State.VisualSource==TEXT("builtin"));
    PropellerBlur->SetVisibility(Blend>.001 && State.VisualSource==TEXT("builtin"));
    TInlineComponentArray<UStaticMeshComponent*> Meshes(this);
    for (auto* Mesh:Meshes) if (Mesh->GetName()==TEXT("NoseWheel") || Mesh->GetName()==TEXT("LeftMainWheel") || Mesh->GetName()==TEXT("RightMainWheel"))
        Mesh->SetRelativeRotation(FRotator(WheelAngleDeg,0,0));
    if (State.VisualSource == TEXT("builtin")) State.VisualSpanM=ModelParameters.span_m;
    if (State.ViewMode == TEXT("aircraft")) Camera->SetRelativeLocation(ChaseCameraOffset(State.VisualSpanM));
    State.ElevatorDeg = Inputs.elevator_deg;
    State.AileronDeg = Inputs.aileron_deg;
    State.RudderDeg = Inputs.rudder_deg;
    State.Throttle = Inputs.throttle;
}

void AFlightScene::ConfigureBuiltinGeometry()
{
    GeometryFingerprint=State.AirframeFingerprint;
    AirframeVisual->SetRelativeScale3D(FVector(1));
    const auto& P=ModelParameters;
    TInlineComponentArray<UStaticMeshComponent*> Meshes(this);
    auto Shape=[&](const TCHAR* Name,FVector Position,FVector Dimensions) {
        for (auto* Mesh:Meshes) if (Mesh->GetName()==Name) {
            Mesh->SetRelativeLocation(Position*100);
            Mesh->SetRelativeScale3D(Dimensions);
        }
    };
    const double C=P.chord_m, B=P.span_m, L=P.body_half_length_m, H=P.body_half_height_m, Z=.75*H;
    Shape(TEXT("Fuselage"),FVector::ZeroVector,FVector(2*L,.8*C,2*H));
    Shape(TEXT("WingLeadingEdge"),FVector(.125*C,0,Z),FVector(.75*C,B,.06*C));
    Shape(TEXT("WingFixedTrailingEdge"),FVector::ZeroVector,FVector(.001));
    auto Surface=[&](USceneComponent* Pivot,const TCHAR* Name,double X,double Y,double Height,FVector Dimensions) {
        Pivot->SetRelativeLocation(FVector(X,Y,Height)*100);
        Shape(Name,FVector(-Dimensions.X*.5,0,0),Dimensions);
    };
    Surface(LeftAileron,TEXT("LeftAileronSurface"),-.25*C,-.375*B,Z,FVector(.25*C,.25*B,.06*C));
    Surface(RightAileron,TEXT("RightAileronSurface"),-.25*C,.375*B,Z,FVector(.25*C,.25*B,.06*C));
    Surface(LeftFlap,TEXT("LeftFlapSurface"),-.25*C,-.125*B,Z,FVector(.25*C,.25*B,.06*C));
    Surface(RightFlap,TEXT("RightFlapSurface"),-.25*C,.125*B,Z,FVector(.25*C,.25*B,.06*C));
    Shape(TEXT("TailplaneLeadingEdge"),FVector(-.7*L,0,.5*H),FVector(.4*C,.32*B,.04*C));
    Surface(LeftElevator,TEXT("LeftElevatorSurface"),-.7*L-.2*C,-.08*B,.5*H,FVector(.2*C,.16*B,.04*C));
    Surface(RightElevator,TEXT("RightElevatorSurface"),-.7*L-.2*C,.08*B,.5*H,FVector(.2*C,.16*B,.04*C));
    Shape(TEXT("FinLeadingEdge"),FVector(-.7*L,0,H*1.5),FVector(.4*C,.04*C,2*H));
    Surface(Rudder,TEXT("RudderSurface"),-.7*L-.2*C,0,H*1.5,FVector(.2*C,.04*C,2*H));
    Propeller->SetRelativeLocation(FVector(L+.02*P.propeller_radius_m,0,-P.thrust_z_m)*100);
    // Remove legacy serialized stick blades/hub if a saved map still contains them.
    for(auto* Mesh:Meshes) if(Mesh->GetName().StartsWith(TEXT("PropellerBlade")) || Mesh->GetName()==TEXT("PropellerHub"))
        Mesh->SetVisibility(false);
    auto MakeMesh=[&](UProceduralMeshComponent* Component,const propeller_visual::Mesh& Data,bool Blade,bool Blur) {
        TArray<FVector> Vertices,Normals;TArray<int32> Indices;TArray<FVector2D> UV;
        TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;
        for(const auto& V:Data.vertices) {
            Vertices.Add(FVector(V.x,V.y,V.z)*100);UV.Add(FVector2D(V.y,V.z));
            Colors.Add(Blade ? FMath::Lerp(FLinearColor(.025f,.03f,.035f,1),FLinearColor(.95f,.65f,.06f,1),float(V.tip))
                : FLinearColor(.15f,.17f,.18f,float(V.alpha)));
        }
        for(int I:Data.indices) Indices.Add(I);
        UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Vertices,Indices,UV,Normals,Tangents);
        Component->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,Colors,Tangents,false);
        if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_PropellerVisual.M_PropellerVisual"))) {
            if(Blade || Blur) {
                auto* Instance=UMaterialInstanceDynamic::Create(Material,this);Component->SetMaterial(0,Instance);
                if(Blade) PropellerBladeMaterial=Instance;else PropellerBlurMaterial=Instance;
            }
        }
        if(!Blade && !Blur) if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
            Component->SetMaterial(0,Material);
    };
    MakeMesh(PropellerBlades,propeller_visual::blades(P.propeller_radius_m),true,false);
    MakeMesh(PropellerBlur,propeller_visual::blur(P.propeller_radius_m),false,true);
    MakeMesh(PropellerSpinner,propeller_visual::spinner(P.propeller_radius_m),false,false);
    for(int Index=0;Index<3;++Index) {
        const TCHAR* Name=Index==0 ? TEXT("NoseWheelPivotStrut") : Index==1 ? TEXT("LeftMainWheelPivotStrut") : TEXT("RightMainWheelPivotStrut");
        const auto Delta=landing_gear_visual::center(P,Index)-landing_gear_visual::root(P,Index);
        const FVector Direction(Delta.x,Delta.y,Delta.z);
        Shape(Name,Direction*.5,FVector(P.wheel_radius_m*.25,P.wheel_radius_m*.25,Direction.Size()));
        for(auto* Mesh:Meshes) if(Mesh->GetName()==Name)
            Mesh->SetRelativeRotation(FQuat::FindBetweenNormals(FVector::UpVector,Direction.GetSafeNormal()));
        Shape(Index==0 ? TEXT("NoseWheel") : Index==1 ? TEXT("LeftMainWheel") : TEXT("RightMainWheel"),Direction,
            FVector(2*P.wheel_radius_m,.8*P.wheel_radius_m,2*P.wheel_radius_m));
    }
}

bool AFlightScene::SetRunning(bool Running)
{
    if (Running && (!State.Fault.IsEmpty() || !State.TerrainLoaded)) return false;
    State.Running = Running;
    Accumulator = 0;
    LastPacingWallS = FPlatformTime::Seconds();
    RateWindowWallS = RateWindowSimulationS = 0;
    State.EffectiveTimeScale = 0;
    return true;
}

bool AFlightScene::SetTimeScale(double TimeScale)
{
    if (!FMath::IsFinite(TimeScale) || TimeScale < .25 || TimeScale > 20) return false;
    if (!SetRunning(State.Running)) return false;
    State.TimeScale = TimeScale;
    return true;
}

bool AFlightScene::SetViewMode(const FString& ViewMode)
{
    if (ViewMode != TEXT("aircraft") && ViewMode != TEXT("overview") && ViewMode != TEXT("projectile")) return false;
    const bool Overview = ViewMode == TEXT("overview");
    const bool Projectile = ViewMode == TEXT("projectile");
    if (Projectile && !UpdateWargameView()) return false;
    Camera->AttachToComponent(Overview || Projectile ? RootComponent.Get() : Aircraft.Get(), FAttachmentTransformRules::KeepRelativeTransform);
    // Frame the selected map's bounds; north is up and east is right.
    int32 ViewWidth = 1600, ViewHeight = 900;
    if (auto* Player = UGameplayStatics::GetPlayerController(this, 0)) Player->GetViewportSize(ViewWidth, ViewHeight);
    const double Aspect = ViewHeight > 0 ? double(ViewWidth) / ViewHeight : 16.0 / 9;
    const double MapHeightCm = FMath::Max(1000.0, TerrainViewSpanM) * 100 * FMath::Max(1.0, Aspect)
        / (2 * FMath::Tan(FMath::DegreesToRadians(65.0 / 2))) * 1.1;
    Camera->SetRelativeLocation(Overview ? TerrainViewCenter + FVector(0, 0, MapHeightCm)
                                       : ChaseCameraOffset(State.VisualSpanM));
    Camera->SetRelativeRotation(Overview ? FRotator(-90, -90, 0) : FRotator(-17, 0, 0));
    Camera->FieldOfView = Projectile ? 60 : Overview ? 65 : 75;
    if (Projectile) UpdateWargameView();
    Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
    Camera->PostProcessSettings.AutoExposureBias = Overview ? -1.5f : 0.f;
    // Only the main orbital view hides atmosphere. EOIR keeps its own show flags.
    if (auto* Player = UGameplayStatics::GetPlayerController(this, 0))
        if (auto* Local = Player->GetLocalPlayer(); Local && Local->ViewportClient)
            Local->ViewportClient->EngineShowFlags.SetAtmosphere(!Overview);
    State.ViewMode = ViewMode;
    return true;
}

bool AFlightScene::CaptureView()
{
    if (!GetWorld() || !GetWorld()->IsGameWorld() || FScreenshotRequest::IsScreenshotRequested()) return false;
    const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("artifacts"));
    if (!IFileManager::Get().MakeDirectory(*Directory, true)) return false;
    FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("terrain-view.png"))),
        true, false);
    return true;
}

bool AFlightScene::SyncState()
{
    const auto Position = flight_core::enu_to_geodetic(Dynamics.position_enu_m, {OriginLatitude, OriginLongitude, OriginHeight});
    if (!Position || !ApplyPosition(Position->latitude_deg, Position->longitude_deg, Position->ellipsoid_height_m,
            std::remainder(FMath::RadiansToDegrees(Dynamics.heading_rad), 360.0), FMath::RadiansToDegrees(Dynamics.pitch_rad),
            std::remainder(FMath::RadiansToDegrees(Dynamics.roll_rad), 360.0))) return false;
    State.SimulationTimeS = Dynamics.time_s;
    const auto Air = flight_core::air_data(Dynamics, FlightEnvironment, Inputs);
    const auto Velocity = flight_core::velocity_enu(Dynamics);
    State.SpeedMps = Air.speed_mps;
    State.GroundSpeedMps = std::hypot(Velocity.x, Velocity.y);
    State.VerticalSpeedMps = Velocity.z;
    State.AngleOfAttackDeg = FMath::RadiansToDegrees(Air.alpha_rad);
    State.SideslipDeg = FMath::RadiansToDegrees(Air.beta_rad);
    State.AirDensityKgM3 = Air.density_kg_m3;
    State.DynamicPressurePa = Air.dynamic_pressure_pa;
    State.LiftCoefficient = Air.lift_coefficient;
    State.DragCoefficient = Air.drag_coefficient;
    State.FlowSeparation = Air.separation;
    State.StallWarning = Air.stall_warning;
    State.Stalled = Air.stalled;
    State.LoadFactor = Air.load_factor;
    State.WindEastMps = FlightEnvironment.wind_enu_mps.x;
    State.WindNorthMps = FlightEnvironment.wind_enu_mps.y;
    State.WindUpMps = FlightEnvironment.wind_enu_mps.z;
    State.RollRateDegS = FMath::RadiansToDegrees(Dynamics.body_rates_rps.x);
    State.PitchRateDegS = FMath::RadiansToDegrees(Dynamics.body_rates_rps.y);
    State.YawRateDegS = FMath::RadiansToDegrees(Dynamics.body_rates_rps.z);
    State.WaypointCount = static_cast<int32>(Navigation.waypoints.size());
    State.ActiveWaypointIndex = State.WaypointCount ? static_cast<int32>(Navigation.active_index) : -1;
    State.NavigationMode = State.WaypointCount ? (Navigation.loitering ? TEXT("loiter") : TEXT("waypoint")) : TEXT("manual");
    State.StallRecovery = State.WaypointCount && Navigation.recovering;
    State.NavigationReacquiring=State.WaypointCount && Navigation.reacquiring;
    State.LoiterRadiusM = State.WaypointCount ? Navigation.loiter_radius_m : 0;
    State.DistanceToWaypointM = 0;
    if (State.WaypointCount)
    {
        const auto Delta = Navigation.waypoints[Navigation.active_index].position_enu_m - Dynamics.position_enu_m;
        State.DistanceToWaypointM = std::hypot(Delta.x, Delta.y);
    }
    ApplyControlVisuals();
    SyncGroundState();
    SyncSystemsState();
    SyncSensor();
    State.OperationKind = UTF8_TO_TCHAR(flight_core::operation_kind_name(Operation.kind));
    State.OperationPhase = UTF8_TO_TCHAR(flight_core::operation_phase_name(Operation.phase));
    State.OperationCompleted = flight_core::operation_completed(Operation);
    State.OperationRunwayIndex = SelectedRunway;
    State.OperationRunwayName = Runways.IsValidIndex(SelectedRunway) ? Runways[SelectedRunway].Name : FString();
    State.OperationMessage = UTF8_TO_TCHAR(Operation.message.c_str());
    State.OperationCrossTrackM = Operation.cross_track_m;
    State.OperationAlongTrackM = Operation.along_m;
    const auto& Guidance=Operation.kind==flight_core::FlightOperationKind::none ? Navigation : Operation.navigation;
    State.OperationDesiredCourseDeg=FMath::RadiansToDegrees(Guidance.desired_course_rad);
    State.OperationDesiredBankDeg=FMath::RadiansToDegrees(Guidance.desired_bank_rad);
    State.OperationDesiredSpeedMps=Guidance.desired_speed_mps;
    const auto TargetGeo=flight_core::enu_to_geodetic({Dynamics.position_enu_m.x,Dynamics.position_enu_m.y,Guidance.desired_height_m},
        {OriginLatitude,OriginLongitude,OriginHeight});
    State.OperationDesiredHeightM=TargetGeo ? TargetGeo->ellipsoid_height_m : 0;
    State.OperationDesiredClimbMps=Guidance.desired_climb_mps;
    State.OperationReturnRadiusM=Operation.return_radius_m;
    if(Operation.kind!=flight_core::FlightOperationKind::landing) State.OperationPath.Empty();
    LandingPath->SetVisibility(Operation.kind==flight_core::FlightOperationKind::landing && flight_core::operation_active(Operation));
    State.OperationTargetHeightM = Operation.kind == flight_core::FlightOperationKind::none ? 0 : Operation.target_agl_m;
    return true;
}

bool AFlightScene::StepFlight(double DeltaSeconds)
{
    if (!State.Fault.IsEmpty()) return false;
    auto Next = Dynamics;
    auto Route = Navigation;
    auto Commands = Inputs;
    auto NextOperation = Operation;
    auto Wheels = GroundInputs;
    bool Ended = false;
    double TravelledM = 0;
    const int32 Steps = FMath::Max(1, FMath::CeilToInt(DeltaSeconds / .01 - 1e-9));
    for (int32 Index = 0; Index < Steps; ++Index)
    {
        const double Dt = FMath::Min(.01, DeltaSeconds - Index * .01);
        flight_core::GroundPlane Plane;
        if(!QueryGround(Next.position_enu_m,{OriginLatitude,OriginLongitude,OriginHeight},Plane)
            || !SampleWheelGround(Next,{OriginLatitude,OriginLongitude,OriginHeight},Plane)) {
            State.Fault=TEXT("Terrain missing under aircraft or wheel; no replacement ground was assumed.");SetRunning(false);return false;
        }
        if (flight_core::operation_active(NextOperation))
        {
            const auto Command = flight_core::update_flight_operation(NextOperation, Next, FlightEnvironment, Plane, Dt);
            if (!Command)
            {
                State.Fault = TEXT("Automatic flight could not produce a valid control input. Cancel or initialize flight.");
                SetRunning(false);
                return false;
            }
            Commands = Command->flight;
            Wheels = Command->ground;
            if (Command->flap_deg && !flight_core::request_flaps(Next, *Command->flap_deg, FlightEnvironment)) return false;
            if (Command->gear_down && !flight_core::request_gear(Next, *Command->gear_down, FlightEnvironment))
            {
                State.Fault = TEXT("Automatic gear command refused by ground interlock. Reset or cancel flight.");
                SetRunning(false);
                return false;
            }
            if (!flight_core::operation_active(NextOperation))
            {
                Ended = true;
                SetRunning(false);
                break;
            }
        }
        else if (!Route.waypoints.empty())
        {
            const auto Command = flight_core::guide(Route, Next, Dt, FlightEnvironment);
            if (!Command) { State.Fault = TEXT("Navigation could not produce a valid control input."); SetRunning(false); return false; }
            Commands = *Command;
        }
        const auto Stepped = flight_core::step_aircraft(Next, Commands, Dt, FlightEnvironment, Plane, Wheels);
        const auto Geo = Stepped ? flight_core::enu_to_geodetic(Stepped->position_enu_m,
            {OriginLatitude, OriginLongitude, OriginHeight}) : std::nullopt;
        if (!Stepped || !Geo || Geo->ellipsoid_height_m < -500 - 1e-6 || Geo->ellipsoid_height_m > 20000 + 1e-6
            || std::hypot(Stepped->position_enu_m.x, Stepped->position_enu_m.y) > 50000)
        {
            State.Fault = TEXT("Flight state outside the local model/scene envelope. Reset the initial condition.");
            SetRunning(false);
            return false;
        }
        const auto Travel = Stepped->position_enu_m - Next.position_enu_m;
        TravelledM += std::hypot(Travel.x, Travel.y, Travel.z);
        Next = *Stepped;
        if (Next.ground.crashed)
        {
            State.Fault = Next.ground.body_strike ? TEXT("Ground collision: airframe strike. Reset to continue.")
                : TEXT("Ground collision: touchdown speed exceeded the development gear limit. Reset to continue.");
            SetRunning(false);
            Route = {};
            break;
        }
    }
    if (!Next.ground.crashed && !Ended) Next.time_s = Dynamics.time_s + DeltaSeconds;
    const double AdvancedTimeS = Next.time_s - Dynamics.time_s;
    Dynamics = Next;
    Navigation = std::move(Route);
    Inputs = Commands;
    GroundInputs = Wheels;
    Operation = std::move(NextOperation);
    State.DistanceTravelledM += TravelledM;
    if (!SyncState()) return false;
    AdvanceWargame(AdvancedTimeS);
    return true;
}

void AFlightScene::ResetFlight()
{
    SetRunning(false);
    const auto Position = flight_core::geodetic_to_enu({OriginLatitude, OriginLongitude, ModelParameters.operating.initial_height_m},
        {OriginLatitude, OriginLongitude, OriginHeight});
    FlightEnvironment.origin_altitude_m = OriginHeight;
    const auto Initial = Position ? flight_core::level_trim(*Position, 0, ModelParameters.operating.initial_speed_mps, FlightEnvironment) : std::nullopt;
    if (!Initial) { State.Fault = TEXT("Could not initialize the flight model."); return; }
    Dynamics = Initial->aircraft;
    Navigation = {};
    Inputs = Initial->controls;
    GroundInputs = {};
    Operation = {};
    SelectedRunway = -1;
    State.Fault.Empty();
    State.DistanceTravelledM = 0;
    SyncState();
}

bool AFlightScene::AdvanceSimulation(double DeltaSeconds)
{
    return !State.Running && FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0 && DeltaSeconds <= 1 && StepFlight(DeltaSeconds);
}

bool AFlightScene::RequestSimulationExit(int64 ExpectedProcessId)
{
    UWorld* World = GetWorld();
    if (ExpectedProcessId != FPlatformProcess::GetCurrentProcessId()
        || GIsEditor || !World || World->WorldType != EWorldType::Game) return false;
    // Return the Remote Control response before requesting the normal engine shutdown path.
    World->GetTimerManager().SetTimerForNextTick([] { FPlatformMisc::RequestExit(false); });
    return true;
}

void AFlightScene::BeginPlay()
{
    Super::BeginPlay();
    State.ProcessId = FPlatformProcess::GetCurrentProcessId();
    AirframeVisual->SetRelativeScale3D(FVector(1.27 / 6.0));
    const auto OldParts = Aircraft->GetAttachChildren();
    for (USceneComponent* Part : OldParts)
        if (Cast<UStaticMeshComponent>(Part))
            Part->AttachToComponent(AirframeVisual, FAttachmentTransformRules::KeepRelativeTransform);
    for (USceneComponent* Hinge : {LeftElevator.Get(), RightElevator.Get(), LeftAileron.Get(), RightAileron.Get(), Rudder.Get()})
        Hinge->AttachToComponent(AirframeVisual, FAttachmentTransformRules::KeepRelativeTransform);
    // Override old serialized component locations from the saved starter map.
    LeftAileron->SetRelativeLocation(FVector(-17.5, -210, 10));
    RightAileron->SetRelativeLocation(FVector(-17.5, 210, 10));
    LeftElevator->SetRelativeLocation(FVector(-157.5, -67.5, 20));
    RightElevator->SetRelativeLocation(FVector(-157.5, 67.5, 20));
    // The saved map may still carry the former fixed-wheel offsets and attachments.
    auto RestoreGear = [this](USceneComponent* Hinge, const TCHAR* PivotName, FVector Position)
    {
        Hinge->AttachToComponent(Aircraft, FAttachmentTransformRules::KeepRelativeTransform);
        Hinge->SetRelativeLocation(Position);
        if (auto* Pivot = Cast<USceneComponent>(GetDefaultSubobjectByName(FName(PivotName))))
        {
            Pivot->AttachToComponent(Hinge, FAttachmentTransformRules::KeepRelativeTransform);
            Pivot->SetRelativeLocation(FVector::ZeroVector);
            for (USceneComponent* Part : Pivot->GetAttachChildren())
                Part->SetRelativeLocation(FVector(0, 0, Part->GetName().EndsWith(TEXT("Strut")) ? -5.5 : -11));
        }
    };
    RestoreGear(NoseGearHinge, TEXT("NoseWheelPivot"), FVector(35, 0, -4));
    RestoreGear(LeftMainGearHinge, TEXT("LeftMainWheelPivot"), FVector(-12, -22, -4));
    RestoreGear(RightMainGearHinge, TEXT("RightMainWheelPivot"), FVector(-12, 22, -4));
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (Material)
    {
        auto* ControlMaterial = UMaterialInstanceDynamic::Create(Material, this);
        ControlMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, .23f, .025f));
        for (USceneComponent* Hinge : {LeftElevator.Get(), RightElevator.Get(), LeftAileron.Get(), RightAileron.Get(), Rudder.Get()})
            if (auto* Mesh = Cast<UStaticMeshComponent>(Hinge->GetChildComponent(0))) Mesh->SetMaterial(0, ControlMaterial);
        auto* TireMaterial = UMaterialInstanceDynamic::Create(Material, this);
        TireMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(.025f, .025f, .03f));
        TArray<UStaticMeshComponent*> Meshes;
        GetComponents(Meshes);
        for (auto* Mesh : Meshes)
            if (Mesh->GetName().EndsWith(TEXT("Wheel"))) Mesh->SetMaterial(0, TireMaterial);
    }
    // Publish the actor even if terrain loading fails, so its owning API can still stop it.
    if (GetWorld()->IsGameWorld())
    {
        auto Locator = MakeShared<FJsonObject>();
        Locator->SetStringField(TEXT("object_path"), GetPathName());
        Locator->SetStringField(TEXT("world_kind"), GetWorld()->WorldType == EWorldType::PIE ? TEXT("PIE") : TEXT("Game"));
        Locator->SetNumberField(TEXT("process_id"), FPlatformProcess::GetCurrentProcessId());
        FString LocatorText;
        FJsonSerializer::Serialize(Locator, TJsonWriterFactory<>::Create(&LocatorText));
        if (!FFileHelper::SaveStringToFile(LocatorText, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("uavsim-runtime.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            UE_LOG(LogTemp, Error, TEXT("UAVSim: could not write the runtime API locator"));
    }
    // Invalid optional visual data leave the built-in model available; physics startup proceeds.
    ReloadAircraftVisual();
    if (!LoadAircraftProfile(false))
    {
        State.Fault = State.AirframeError;
        SetRunning(false);
        return;
    }
    if (!LoadTerrain()) { SetRunning(false); return; }
    ReloadWargameWeapons();
    ResetFlight();
    SetViewMode(TEXT("aircraft"));
    if (auto* Player = UGameplayStatics::GetPlayerController(this, 0)) Player->SetViewTarget(this);
    SetRunning(true);
}

void AFlightScene::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearGroundTargets();
    const FString LocatorPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("uavsim-runtime.json"));
    if (IFileManager::Get().FileExists(*LocatorPath))
    {
        FString Text, ObjectPath;
        uint32 ProcessId = 0;
        TSharedPtr<FJsonObject> Locator;
        if (!FFileHelper::LoadFileToString(Text, *LocatorPath)
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Locator)
            || !Locator.IsValid() || !Locator->TryGetStringField(TEXT("object_path"), ObjectPath)
            || !Locator->TryGetNumberField(TEXT("process_id"), ProcessId))
        {
            UE_LOG(LogTemp, Warning, TEXT("UAVSim: could not read the runtime API locator during shutdown"));
        }
        else if (ObjectPath == GetPathName() && ProcessId == FPlatformProcess::GetCurrentProcessId()
            && !IFileManager::Get().Delete(*LocatorPath))
        {
            UE_LOG(LogTemp, Warning, TEXT("UAVSim: could not remove the runtime API locator"));
        }
    }
    Super::EndPlay(EndPlayReason);
}

void AFlightScene::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Now = FPlatformTime::Seconds();
    const double WallSeconds = LastPacingWallS > 0 ? FMath::Max(0.0, Now - LastPacingWallS) : 0;
    LastPacingWallS = Now;
    if (State.Running)
    {
        const double BeforeTime = Dynamics.time_s;
        // Same .01 s physics steps at every factor. Limit catch-up after a rendering stall.
        Accumulator += FMath::Min(WallSeconds, .25) * State.TimeScale;
        constexpr double Step = .01;
        while (Accumulator + 1e-12 >= Step)
        {
            if (!StepFlight(Step)) { SetRunning(false); break; }
            if (!State.Running) break;
            Accumulator = FMath::Max(0.0, Accumulator - Step);
        }
        if (State.Running)
        {
            RateWindowWallS += WallSeconds;
            RateWindowSimulationS += Dynamics.time_s - BeforeTime;
            if (RateWindowWallS >= .5)
            {
                State.EffectiveTimeScale = RateWindowSimulationS / RateWindowWallS;
                RateWindowWallS = RateWindowSimulationS = 0;
            }
        }
    }
    if (State.ViewMode == TEXT("projectile") && !UpdateWargameView()) SetViewMode(TEXT("aircraft"));
    if (GEngine) GEngine->AddOnScreenDebugMessage(uint64(GetUniqueID()), 0.0f,
        State.Stalled ? FColor::Red : State.StallWarning ? FColor::Yellow : FColor::Cyan,
        FString::Printf(TEXT("NONLINEAR 6DOF | %s | %.6f N  %.6f E | WGS84 %.1f m | TAS %.1f / GS %.1f m/s\nAoA %+.1f deg | beta %+.1f deg | separation %.0f%% | %.2f g | %s\n%s | WP %d/%d | %s\n%s | %d terrain tiles | %.2fx (actual %.2fx) | t=%.2fs | travelled %.1fm"),
            State.Running ? TEXT("RUNNING") : TEXT("PAUSED"), State.LatitudeDeg, State.LongitudeDeg,
            State.EllipsoidHeightM, State.SpeedMps, State.GroundSpeedMps, State.AngleOfAttackDeg,
            State.SideslipDeg, State.FlowSeparation * 100, State.LoadFactor,
            State.Stalled ? TEXT("STALL") : State.StallWarning ? TEXT("STALL WARNING") : TEXT("ATTACHED FLOW"),
            State.OperationKind != TEXT("none") ? *State.OperationPhase
                : State.StallRecovery ? TEXT("STALL RECOVERY") : *State.NavigationMode, State.ActiveWaypointIndex + 1,
            State.WaypointCount, *State.Fault, *State.ViewMode, State.TerrainTileCount,
            State.TimeScale, State.EffectiveTimeScale, State.SimulationTimeS, State.DistanceTravelledM));
    if (GEngine && !State.TerrainAttribution.IsEmpty())
        GEngine->AddOnScreenDebugMessage(uint64(GetUniqueID()) + 1, 0.0f, FColor::White,
            State.TerrainAttribution);
    if (GEngine)
        GEngine->AddOnScreenDebugMessage(uint64(GetUniqueID()) + 2, 0.0f, State.Crashed ? FColor::Red : FColor::White,
            FString::Printf(TEXT("%s | CG clearance %.2f m | wheels %d | brake %.0f%% %s | %s"),
                State.Crashed ? TEXT("CRASHED") : State.GroundOnGround ? TEXT("ON GROUND") : TEXT("AIRBORNE"),
                State.HeightAglM, State.WheelsOnGround, State.Brake * 100,
                State.ParkingBrake ? TEXT("PARKING") : TEXT(""), *State.GroundSurfaceName));
}
