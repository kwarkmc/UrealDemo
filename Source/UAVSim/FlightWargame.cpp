#include "FlightScene.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
UProceduralMeshComponent* AddVisualTrail(AActor* Actor, UProceduralMeshComponent* Body,
    UMaterialInterface* FallbackMaterial, double RearX)
{
    auto* Trail = NewObject<UProceduralMeshComponent>(Actor, TEXT("VisualTrail"));
    Trail->SetupAttachment(Body);
    Trail->SetMobility(EComponentMobility::Movable);
    Trail->SetRelativeLocation(FVector(RearX, 0, 0));
    Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Trail->SetCastShadow(false);
    Trail->SetVisibility(false);
    Actor->AddInstanceComponent(Trail);
    Trail->RegisterComponent();
    auto* Material = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent.M_SimpleUnlitTranslucent"));
    if (!Material) Material = FallbackMaterial;
    // Two short tapered streaks make movement legible. This is a display effect,
    // not measured exhaust, smoke, propulsion or any form of trajectory model.
    for (int32 Section = 0; Section < 2; ++Section)
    {
        TArray<FVector> Vertices, Normals;
        TArray<int32> Indices;
        auto Triangle = [&](FVector A, FVector B, FVector C)
        {
            const int32 Index = Vertices.Num();
            Vertices.Append({A, B, C});
            const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
            Normals.Append({Normal, Normal, Normal});
            Indices.Append({Index, Index + 2, Index + 1});
        };
        const double Radius = Section == 0 ? 1.0 : .32;
        for (int32 Side = 0; Side < 12; ++Side)
        {
            const double A = Side * 2 * PI / 12, B = (Side + 1) * 2 * PI / 12;
            const FVector P(0, Radius * FMath::Cos(A), Radius * FMath::Sin(A));
            const FVector Q(0, Radius * FMath::Cos(B), Radius * FMath::Sin(B));
            Triangle(FVector(-1, 0, 0), Q, P);
            Triangle(FVector::ZeroVector, P, Q);
        }
        Trail->CreateMeshSection_LinearColor(Section, Vertices, Indices, Normals, {}, {}, {}, false);
        auto* Instance = UMaterialInstanceDynamic::Create(Material, Actor);
        Instance->SetVectorParameterValue(TEXT("Color"), Section == 0
            ? FLinearColor(1.f, .55f, .20f, .22f) : FLinearColor(3.f, 2.7f, 2.0f, .9f));
        Trail->SetMaterial(Section, Instance);
    }
    return Trail;
}
}

bool AFlightScene::UpdateWargameView()
{
    if (!Camera) return false;
    for (int32 Index = State.WargameShots.Num() - 1; Index >= 0; --Index)
    {
        const auto& Event = State.WargameShots[Index];
        if (Event.Phase != TEXT("animating")) continue;
        const auto* Animation = WargameAnimations.Find(Event.Id);
        auto* Actor = Animation ? Animation->Actor.Get() : nullptr;
        if (!IsValid(Actor)) continue;
        // Camera presentation only; no attachment, event or flight-state mutation.
        const FVector Location = Actor->GetActorTransform().TransformPosition(FVector(-650, 250, 180));
        Camera->SetWorldLocation(Location);
        Camera->SetWorldRotation((Actor->GetActorLocation() - Location).Rotation());
        return true;
    }
    return false;
}

// Abstract scene events only. Positions below drive a fixed cosmetic animation;
// neither the animation nor its endpoint decides a hit or simulates weapon guidance.
bool AFlightScene::ReloadWargameWeapons()
{
    if (State.Running || !WargameAnimations.IsEmpty())
    {
        State.WargameError = TEXT("Pause flight and finish or reset active animations before reloading visual assets");
        return false;
    }
    TArray<FWargameVisual> Prepared;
    if (!LoadWargameVisuals(Prepared, State.WargameError)) return false;
    TArray<FWargameWeaponState> Metadata;
    for (const auto& Visual : Prepared)
    {
        FWargameWeaponState Info;
        Info.Id = Visual.Id;
        Info.Label = Visual.Label;
        Info.Mesh = Visual.Mesh;
        Info.AnimationDurationS = Visual.AnimationDurationS;
        Info.TriangleCount = Visual.Indices.Num() / 3;
        Metadata.Add(Info);
    }
    WargameVisuals = MoveTemp(Prepared);
    State.WargameWeapons = MoveTemp(Metadata);
    State.WargameError.Empty();
    return true;
}

bool AFlightScene::PlayWargameShot(const FString& WeaponId, const FString& TargetId)
{
    const auto* Visual = WargameVisuals.FindByPredicate([&](const FWargameVisual& Item) { return Item.Id == WeaponId; });
    const auto* Target = TargetActors.Find(TargetId);
    if (!Visual || !Target || !IsValid(Target->Get()) || !State.TerrainLoaded
        || !GetWorld() || !GetWorld()->IsGameWorld())
    {
        State.WargameError = TEXT("Visual asset and registered target must exist in the active game scene");
        return false;
    }
    if (WargameAnimations.Num() >= 16 || State.WargameShots.Num() >= 128)
    {
        State.WargameError = TEXT("Maximum 16 active animations and 128 records; reset the wargame records to clear them");
        return false;
    }
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!Material) { State.WargameError = TEXT("Visual animation material is unavailable"); return false; }
    // Both endpoints are snapshots. No seeker, prediction, collision test or homing.
    const FVector From = Aircraft->GetComponentTransform().TransformPosition(FVector(50, 0, -25));
    const FVector To = (*Target)->GetActorTransform().TransformPosition(FVector(0, 0, 150));
    const FRotator Orientation = (To - From).IsNearlyZero() ? FRotator::ZeroRotator : (To - From).Rotation();
    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags |= RF_Transient;
    Spawn.Owner = this;
    auto* Prop = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), From, Orientation, Spawn);
    if (!Prop) { State.WargameError = TEXT("Animation actor creation failed"); return false; }
    auto* Mesh = NewObject<UProceduralMeshComponent>(Prop);
    Prop->SetRootComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Prop->AddInstanceComponent(Mesh);
    Mesh->RegisterComponent();
    // ponytail: a procedural section per effect; share a static mesh if measured scene cost requires it.
    Mesh->CreateMeshSection_LinearColor(0, Visual->Vertices, Visual->Indices, Visual->Normals, {}, {}, {}, false);
    auto* Instance = UMaterialInstanceDynamic::Create(Material, Prop);
    Instance->SetVectorParameterValue(TEXT("Color"), Visual->Color);
    Mesh->SetMaterial(0, Instance);
    FBox Bounds(ForceInit);
    for (const FVector& Vertex : Visual->Vertices) Bounds += Vertex;
    auto* Trail = AddVisualTrail(Prop, Mesh, Material, Bounds.Min.X);
    Prop->SetActorLocationAndRotation(From, Orientation);
    FWargameShotState Event;
    Event.Id = FString::Printf(TEXT("shot_%lld"), NextWargameShot++);
    Event.WeaponId = WeaponId;
    Event.TargetId = TargetId;
    Event.DurationS = Visual->AnimationDurationS;
    WargameAnimations.Add(Event.Id, {Prop, From, To, Trail});
    State.WargameShots.Add(Event);
    State.WargameError.Empty();
    if (State.ViewMode == TEXT("projectile") && !UpdateWargameView()) SetViewMode(TEXT("aircraft"));
    return true;
}

void AFlightScene::AdvanceWargame(double DeltaSeconds)
{
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0) return;
    State.WargameTimeS += DeltaSeconds;
    for (auto& Event : State.WargameShots)
    {
        if (Event.Phase != TEXT("animating")) continue;
        const auto* Target = TargetActors.Find(Event.TargetId);
        auto* Animation = WargameAnimations.Find(Event.Id);
        if (!Target || !IsValid(Target->Get()) || !Animation || !Animation->Actor.IsValid())
        {
            if (Animation && Animation->Actor.IsValid()) Animation->Actor->Destroy();
            WargameAnimations.Remove(Event.Id);
            Event.Phase = TEXT("canceled");
            Event.Outcome = TEXT("canceled");
            Event.ResolvedTimeS = State.WargameTimeS;
            continue;
        }
        Event.ElapsedS = FMath::Min(Event.DurationS, Event.ElapsedS + DeltaSeconds);
        const double Progress = Event.ElapsedS / Event.DurationS;
        Animation->Actor->SetActorLocation(FMath::Lerp(Animation->From, Animation->To, Progress));
        if (auto* Trail = Animation->Trail.Get())
        {
            // Never extend the streak behind the launch snapshot. Limit its length
            // and width purely for readability; actor destruction cleans both sections.
            const double Length = FMath::Clamp((Animation->To - Animation->From).Size() * Progress
                + Trail->GetRelativeLocation().X, 0.0, 1200.0);
            Trail->SetRelativeScale3D(FVector(FMath::Max(.001, Length), 12, 12));
            Trail->SetVisibility(Length > 1);
        }
        if (Event.ElapsedS + 1e-9 >= Event.DurationS)
        {
            Event.ElapsedS = Event.DurationS;
            Animation->Actor->Destroy();
            WargameAnimations.Remove(Event.Id);
            Event.Phase = TEXT("awaiting_result"); // Reaching a visual endpoint is never evidence of a hit.
        }
    }
    if (State.ViewMode == TEXT("projectile") && !UpdateWargameView()) SetViewMode(TEXT("aircraft"));
}

bool AFlightScene::ResolveWargameShot(const FString& ShotId, const FString& Outcome)
{
    auto* Event = State.WargameShots.FindByPredicate([&](const FWargameShotState& Item) { return Item.Id == ShotId; });
    if (!Event || Event->Phase != TEXT("awaiting_result") || (Outcome != TEXT("hit") && Outcome != TEXT("miss")))
    {
        State.WargameError = TEXT("Choose hit or miss for an existing event awaiting operator adjudication");
        return false;
    }
    const auto* Actor = TargetActors.Find(Event->TargetId);
    auto* Target = State.Targets.FindByPredicate([&](const FSimTargetState& Item) { return Item.Id == Event->TargetId; });
    if (!Actor || !IsValid(Actor->Get()) || !Target)
    {
        CancelWargameTarget(Event->TargetId);
        State.WargameError = TEXT("The event's target no longer exists");
        return false;
    }
    Event->Phase = TEXT("resolved");
    Event->Outcome = Outcome;
    Event->ResolvedTimeS = State.WargameTimeS;
    if (Outcome == TEXT("hit"))
    {
        Target->Hit = true;
        ++Target->HitCount;
        Target->LastHitShotId = Event->Id;
        ApplyTargetHitVisual(*Target);
    }
    State.WargameError.Empty();
    return true;
}

void AFlightScene::ApplyTargetHitVisual(const FSimTargetState& Target)
{
    const auto* Actor = TargetActors.Find(Target.Id);
    if (!Actor || !IsValid(Actor->Get())) return;
    TArray<UStaticMeshComponent*> Components;
    (*Actor)->GetComponents(Components);
    const FLinearColor Body = Target.Kind == TEXT("tank") ? FLinearColor(.24f, .30f, .09f) : FLinearColor(.85f, .72f, .32f);
    for (auto* Component : Components)
        if (auto* Material = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0)))
            Material->SetVectorParameterValue(TEXT("Color"), Target.Hit ? FLinearColor(1, .025f, .015f)
                : Component->ComponentHasTag(TEXT("DarkPropPart")) ? FLinearColor(.035f, .04f, .045f) : Body);
}

void AFlightScene::CancelWargameTarget(const FString& TargetId)
{
    for (auto& Event : State.WargameShots)
        if (Event.TargetId == TargetId && (Event.Phase == TEXT("animating") || Event.Phase == TEXT("awaiting_result")))
        {
            if (const auto* Animation = WargameAnimations.Find(Event.Id); Animation && Animation->Actor.IsValid())
                Animation->Actor->Destroy();
            WargameAnimations.Remove(Event.Id);
            Event.Phase = TEXT("canceled");
            Event.Outcome = TEXT("canceled");
            Event.ResolvedTimeS = State.WargameTimeS;
        }
    if (State.ViewMode == TEXT("projectile") && !UpdateWargameView()) SetViewMode(TEXT("aircraft"));
}

void AFlightScene::ResetWargame()
{
    for (const auto& Item : WargameAnimations)
        if (Item.Value.Actor.IsValid()) Item.Value.Actor->Destroy();
    WargameAnimations.Empty();
    State.WargameShots.Empty();
    State.WargameTimeS = 0;
    State.WargameError.Empty();
    for (auto& Target : State.Targets)
    {
        Target.Hit = false;
        Target.HitCount = 0;
        Target.LastHitShotId.Empty();
        ApplyTargetHitVisual(Target);
    }
    if (State.ViewMode == TEXT("projectile") && !UpdateWargameView()) SetViewMode(TEXT("aircraft"));
}
