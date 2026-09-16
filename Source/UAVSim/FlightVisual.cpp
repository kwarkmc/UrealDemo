#include "FlightScene.h"
#include "StlMesh.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool VisualNumber(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, double& Value)
{
    const auto Field = Json->TryGetField(Key);
    return Field.IsValid() && Field->Type == EJson::Number && Field->TryGetNumber(Value) && FMath::IsFinite(Value);
}

bool VisualVector(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, FVector& Value)
{
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Json->TryGetArrayField(Key, Items) || Items->Num() != 3) return false;
    for (int32 I = 0; I < 3; ++I)
        if (!(*Items)[I].IsValid() || (*Items)[I]->Type != EJson::Number
            || !(*Items)[I]->TryGetNumber(Value[I]) || !FMath::IsFinite(Value[I])) return false;
    return true;
}

bool VisualAxis(const FString& Name, FVector& Axis)
{
    const bool Negative = Name.StartsWith(TEXT("-"));
    const FString Label = Negative ? Name.Mid(1) : Name;
    if (Label != TEXT("x") && Label != TEXT("y") && Label != TEXT("z")) return false;
    Axis = FVector::ZeroVector;
    Axis[Label == TEXT("x") ? 0 : Label == TEXT("y") ? 1 : 2] = Negative ? -1 : 1;
    return true;
}

// Resolve only ordinary files inside aircraft; reject links/junctions at every path component.
bool VisualPath(const FString& Directory, const FString& Relative, FString& Out)
{
    if (Relative.IsEmpty() || Relative.Len() > 256 || !FPaths::IsRelative(Relative)
        || Relative.Contains(TEXT("\\")) || Relative.Contains(TEXT(":"))) return false;
    auto& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (Files.IsSymlink(*Directory) != ESymlinkResult::NonSymlink) return false;
    TArray<FString> Parts;
    Relative.ParseIntoArray(Parts, TEXT("/"), false);
    Out = Directory;
    for (const auto& Part : Parts)
    {
        if (Part.IsEmpty() || Part == TEXT(".") || Part == TEXT("..")
            || Part.TrimStartAndEnd() != Part || Part.EndsWith(TEXT("."))) return false;
        for (TCHAR Character : Part) if (Character < 32) return false;
        Out = FPaths::Combine(Out, Part);
        if (Files.IsSymlink(*Out) != ESymlinkResult::NonSymlink) return false;
    }
    return true;
}
}

bool AFlightScene::ReloadAircraftVisual()
{
    auto Fail = [this](const FString& Error) { State.VisualError = Error; return false; };
    if (State.Running) return Fail(TEXT("Pause flight before reloading its visual model"));
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("aircraft")));
    FString ConfigPath, Text, Mesh, MeshPath;
    if (!VisualPath(Directory, TEXT("visual.json"), ConfigPath))
        return Fail(TEXT("Visual configuration must be an ordinary file inside aircraft"));
    double Scale = 1;
    FVector Origin = FVector::ZeroVector, Color(.75, .78, .82), Forward(1, 0, 0), Up(0, 0, 1);
    bool ShowGear = true;
    if (IFileManager::Get().FileExists(*ConfigPath))
    {
        const int64 Size = IFileManager::Get().FileSize(*ConfigPath);
        TSharedPtr<FJsonObject> Json;
        if (Size <= 0 || Size > 8192 || !FFileHelper::LoadFileToString(Text, *ConfigPath)
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
            return Fail(TEXT("aircraft/visual.json must be a JSON object up to 8 KiB"));
        double Version = 0;
        FString ForwardName, UpName;
        if (Json->Values.Num() != 8 || !VisualNumber(Json, TEXT("schema_version"), Version) || Version != 1
            || !Json->HasTypedField<EJson::String>(TEXT("mesh"))
            || !Json->HasTypedField<EJson::String>(TEXT("forward_axis"))
            || !Json->HasTypedField<EJson::String>(TEXT("up_axis"))
            || !Json->HasTypedField<EJson::Boolean>(TEXT("show_builtin_gear"))
            || !Json->TryGetStringField(TEXT("mesh"), Mesh) || !VisualNumber(Json, TEXT("scale_to_m"), Scale)
            || Scale < 1e-6 || Scale > 1000 || !VisualVector(Json, TEXT("origin"), Origin) || Origin.GetAbsMax() > 1e9
            || !VisualVector(Json, TEXT("color"), Color) || Color.GetMin() < 0 || Color.GetMax() > 1
            || !Json->TryGetStringField(TEXT("forward_axis"), ForwardName) || !VisualAxis(ForwardName, Forward)
            || !Json->TryGetStringField(TEXT("up_axis"), UpName) || !VisualAxis(UpName, Up)
            || FMath::Abs(FVector::DotProduct(Forward, Up)) > .5
            || !Json->TryGetBoolField(TEXT("show_builtin_gear"), ShowGear))
            return Fail(TEXT("Visual config requires schema_version, mesh, scale_to_m, perpendicular forward_axis/up_axis, origin, color and show_builtin_gear"));
    }
    const bool Custom = !Mesh.IsEmpty();
    TArray<uint8> Bytes;
    TArray<FVector> Vertices, Normals;
    TArray<int32> Indices;
    FBox Bounds(ForceInit);
    if (Custom)
    {
        if (!VisualPath(Directory, Mesh, MeshPath) || FPaths::GetExtension(Mesh).ToLower() != TEXT("stl"))
            return Fail(TEXT("mesh must be a relative .stl file inside aircraft; parent traversal and links are not accepted"));
        const int64 Size = IFileManager::Get().FileSize(*MeshPath);
        if (Size <= 0 || Size > 32 * 1024 * 1024 || !FFileHelper::LoadFileToArray(Bytes, *MeshPath) || Bytes.Num() != Size)
            return Fail(TEXT("STL file is missing, unreadable or larger than 32 MiB"));
        const auto Parsed = flight_core::parse_stl(std::string_view(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num()));
        if (!Parsed) return Fail(UTF8_TO_TCHAR(Parsed.error.c_str()));
        const FVector Right = FVector::CrossProduct(Up, Forward);
        Vertices.Reserve(static_cast<int32>(Parsed.vertices.size()));
        Normals.Reserve(static_cast<int32>(Parsed.vertices.size()));
        Indices.Reserve(static_cast<int32>(Parsed.vertices.size()));
        for (const auto& Point : Parsed.vertices)
        {
            const FVector Local = (FVector(Point.x, Point.y, Point.z) - Origin) * Scale;
            const FVector V(FVector::DotProduct(Local, Forward), FVector::DotProduct(Local, Right), FVector::DotProduct(Local, Up));
            if (V.ContainsNaN() || V.GetAbsMax() > 100)
                return Fail(TEXT("Converted STL coordinates must be within 100 m of its CG; check units and origin"));
            Vertices.Add(V * 100); // Aircraft-local forward/right/up cm, independent of the world ENU transform.
            Bounds += V;
        }
        for (int32 I = 0; I < Vertices.Num(); I += 3)
        {
            const FVector Cross = FVector::CrossProduct(Vertices[I + 1] - Vertices[I], Vertices[I + 2] - Vertices[I]);
            if (Cross.ContainsNaN() || Cross.SizeSquared() <= 1e-16)
                return Fail(TEXT("STL contains a triangle too small after unit conversion"));
            const FVector Normal = Cross / Cross.Size();
            Normals.Append({Normal, Normal, Normal});
            Indices.Append({I, I + 2, I + 1}); // STL outward CCW to Unreal front-face winding.
        }
    }
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    auto* Instance = Material && Custom ? UMaterialInstanceDynamic::Create(Material, this) : nullptr;
    if (Custom && !Instance) return Fail(TEXT("Aircraft display material could not be created"));
    FSHA1 Hash;
    const FTCHARToUTF8 Encoded(*Text);
    Hash.Update(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
    if (!Bytes.IsEmpty()) Hash.Update(Bytes.GetData(), Bytes.Num());
    Hash.Final();
    uint8 Digest[20];
    Hash.GetHash(Digest);
    // All file and geometry checks finish before replacing the displayed section.
    if (Custom)
    {
        Instance->SetVectorParameterValue(TEXT("Color"), FLinearColor(Color.X, Color.Y, Color.Z));
        ImportedAircraftMesh->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, {}, {}, {}, false);
        ImportedAircraftMesh->SetMaterial(0, Instance);
    }
    else ImportedAircraftMesh->ClearAllMeshSections();
    ImportedAircraftMesh->SetVisibility(Custom);
    AirframeVisual->SetVisibility(!Custom, true);
    State.VisualSource = Custom ? TEXT("stl") : TEXT("builtin");
    State.VisualMesh = Mesh;
    State.VisualTriangleCount = Indices.Num() / 3;
    State.VisualSpanM = Custom ? Bounds.GetSize().Y : 1.27;
    State.VisualFingerprint = TEXT("sha1:") + BytesToHex(Digest, 20).ToLower();
    State.VisualShowBuiltinGear = ShowGear;
    State.VisualError.Empty();
    SyncGroundState();
    return true;
}
