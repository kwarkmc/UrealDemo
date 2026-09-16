#include "WargameVisual.h"
#include "StlMesh.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool Number(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, double& Value)
{
    const auto Field = Json->TryGetField(Key);
    return Field.IsValid() && Field->Type == EJson::Number && Field->TryGetNumber(Value) && FMath::IsFinite(Value);
}

bool Vector(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, FVector& Value)
{
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Json->TryGetArrayField(Key, Items) || Items->Num() != 3) return false;
    for (int32 I = 0; I < 3; ++I)
        if (!(*Items)[I].IsValid() || (*Items)[I]->Type != EJson::Number
            || !(*Items)[I]->TryGetNumber(Value[I]) || !FMath::IsFinite(Value[I])) return false;
    return true;
}

bool Axis(const FString& Name, FVector& Value)
{
    const bool Negative = Name.StartsWith(TEXT("-"));
    const FString Label = Negative || Name.StartsWith(TEXT("+")) ? Name.Mid(1) : Name;
    if (Label != TEXT("x") && Label != TEXT("y") && Label != TEXT("z")) return false;
    Value = FVector::ZeroVector;
    Value[Label == TEXT("x") ? 0 : Label == TEXT("y") ? 1 : 2] = Negative ? -1 : 1;
    return true;
}

bool ValidId(const FString& Id)
{
    if (Id.IsEmpty() || Id.Len() > 48) return false;
    for (TCHAR C : Id)
        if (!((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z')
            || (C >= '0' && C <= '9') || C == '_' || C == '-')) return false;
    return true;
}

// Same path boundary as FlightVisual: ordinary files only, including all parents
// within weapons. UE's Windows IsSymlink also rejects junctions/reparse points.
bool AssetPath(const FString& Directory, const FString& Relative, FString& Out)
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
        for (TCHAR C : Part) if (C < 32) return false;
        Out = FPaths::Combine(Out, Part);
        if (Files.IsSymlink(*Out) != ESymlinkResult::NonSymlink) return false;
    }
    return true;
}

bool ReadAsset(const FString& Path, int64 Limit, TArray<uint8>& Bytes)
{
    TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path));
    if (!File) return false;
    const int64 Size = File->Size();
    if (Size <= 0 || Size > Limit) return false;
    Bytes.SetNumUninitialized(static_cast<int32>(Size));
    return File->Read(Bytes.GetData(), Size) && File->Size() == Size;
}
}

bool LoadWargameVisuals(TArray<FWargameVisual>& Out, FString& Error)
{
    auto Fail = [&](const FString& Message) { Error = Message; return false; };
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("weapons")));
    FString ConfigPath, Text;
    TArray<uint8> Bytes;
    if (!AssetPath(Directory, TEXT("catalog.json"), ConfigPath) || !ReadAsset(ConfigPath, 64 * 1024, Bytes))
        return Fail(TEXT("weapons/catalog.json must be an ordinary readable file of 1 byte to 64 KiB"));
    FFileHelper::BufferToString(Text, Bytes.GetData(), Bytes.Num());
    TSharedPtr<FJsonObject> Json;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
        return Fail(TEXT("weapons/catalog.json must contain a JSON object"));
    double Version = 0;
    const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
    if (Json->Values.Num() != 2 || !Number(Json, TEXT("schema_version"), Version) || Version != 1
        || !Json->TryGetArrayField(TEXT("weapons"), Entries) || Entries->IsEmpty() || Entries->Num() > 32)
        return Fail(TEXT("Catalog requires schema_version 1 and a weapons array of 1..32 visual assets"));

    TArray<FWargameVisual> Candidate;
    TSet<FString> Ids;
    int32 TotalTriangles = 0;
    for (const auto& Entry : *Entries)
    {
        if (!Entry.IsValid() || Entry->Type != EJson::Object)
            return Fail(TEXT("Each catalog entry must be a JSON object"));
        const auto Object = Entry->AsObject();
        FWargameVisual Visual;
        double Scale = 1;
        FVector Origin = FVector::ZeroVector, Color = FVector::ZeroVector, Forward(1, 0, 0), Up(0, 0, 1);
        FString ForwardName, UpName, MeshPath;
        if (Object->Values.Num() != 9
            || !Object->HasTypedField<EJson::String>(TEXT("id")) || !Object->TryGetStringField(TEXT("id"), Visual.Id)
            || !ValidId(Visual.Id) || Ids.Contains(Visual.Id)
            || !Object->HasTypedField<EJson::String>(TEXT("label")) || !Object->TryGetStringField(TEXT("label"), Visual.Label)
            || Visual.Label.TrimStartAndEnd().IsEmpty() || Visual.Label.Len() > 80
            || !Object->HasTypedField<EJson::String>(TEXT("mesh")) || !Object->TryGetStringField(TEXT("mesh"), Visual.Mesh)
            || !Number(Object, TEXT("scale_to_m"), Scale) || Scale < 1e-6 || Scale > 1000
            || !Number(Object, TEXT("animation_duration_s"), Visual.AnimationDurationS)
            || Visual.AnimationDurationS < .2 || Visual.AnimationDurationS > 30
            || !Vector(Object, TEXT("origin"), Origin) || !Vector(Object, TEXT("color"), Color)
            || Color.GetMin() < 0 || Color.GetMax() > 1
            || !Object->HasTypedField<EJson::String>(TEXT("forward_axis"))
            || !Object->TryGetStringField(TEXT("forward_axis"), ForwardName) || !Axis(ForwardName, Forward)
            || !Object->HasTypedField<EJson::String>(TEXT("up_axis"))
            || !Object->TryGetStringField(TEXT("up_axis"), UpName) || !Axis(UpName, Up)
            || FMath::Abs(FVector::DotProduct(Forward, Up)) > .5)
            return Fail(TEXT("Each visual requires unique ASCII id, label, mesh, scale_to_m, perpendicular forward_axis/up_axis, origin, color and animation_duration_s (.2..30)"));
        if (!AssetPath(Directory, Visual.Mesh, MeshPath) || FPaths::GetExtension(Visual.Mesh).ToLower() != TEXT("stl"))
            return Fail(Visual.Id + TEXT(": mesh must be a nonempty relative .stl inside weapons; traversal and links are rejected"));
        if (!ReadAsset(MeshPath, flight_core::stl_max_file_bytes, Bytes))
            return Fail(Visual.Id + TEXT(": STL is missing, unreadable or exceeds 32 MiB"));
        if (TotalTriangles >= 400000) return Fail(TEXT("Catalog exceeds the aggregate 400000-triangle budget"));
        const auto Parsed = flight_core::parse_stl(
            std::string_view(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num()),
            FMath::Min(200000, 400000 - TotalTriangles));
        if (!Parsed) return Fail(Visual.Id + TEXT(": ") + UTF8_TO_TCHAR(Parsed.error.c_str()));
        TotalTriangles += static_cast<int32>(Parsed.vertices.size() / 3);
        const FVector Right = FVector::CrossProduct(Up, Forward);
        const int32 VertexCount = static_cast<int32>(Parsed.vertices.size());
        Visual.Vertices.Reserve(VertexCount);
        Visual.Normals.Reserve(VertexCount);
        Visual.Indices.Reserve(VertexCount);
        for (const auto& Point : Parsed.vertices)
        {
            const FVector Local = (FVector(Point.x, Point.y, Point.z) - Origin) * Scale;
            const FVector V(FVector::DotProduct(Local, Forward), FVector::DotProduct(Local, Right), FVector::DotProduct(Local, Up));
            if (V.ContainsNaN() || V.GetAbsMax() > 100)
                return Fail(Visual.Id + TEXT(": converted coordinates must stay within 100 m of the configured origin"));
            Visual.Vertices.Add(V * 100); // Identical forward/right/up cm conversion to FlightVisual.
        }
        for (int32 I = 0; I < VertexCount; I += 3)
        {
            const FVector Cross = FVector::CrossProduct(Visual.Vertices[I + 1] - Visual.Vertices[I],
                                                        Visual.Vertices[I + 2] - Visual.Vertices[I]);
            if (Cross.ContainsNaN() || Cross.SizeSquared() <= 1e-16)
                return Fail(Visual.Id + TEXT(": STL contains a triangle too small after unit conversion"));
            const FVector Normal = Cross / Cross.Size();
            Visual.Normals.Append({Normal, Normal, Normal});
            Visual.Indices.Append({I, I + 2, I + 1}); // Same STL outward CCW to Unreal front-face winding.
        }
        Visual.Color = FLinearColor(Color.X, Color.Y, Color.Z);
        Ids.Add(Visual.Id);
        Candidate.Add(MoveTemp(Visual));
    }
    Out = MoveTemp(Candidate);
    Error.Empty();
    return true;
}
