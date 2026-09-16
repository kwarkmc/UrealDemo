#pragma once

#include "CoreMinimal.h"

// Decorative scene asset only: no weapon performance, trajectory or damage model.
struct FWargameVisual
{
    FString Id, Label, Mesh;
    double AnimationDurationS = 2;
    TArray<FVector> Vertices, Normals; // Local forward/right/up centimetres.
    TArray<int32> Indices;
    FLinearColor Color = FLinearColor::White;
};

// Reads weapons/catalog.json and its STL assets. Failure leaves Out unchanged.
bool LoadWargameVisuals(TArray<FWargameVisual>& Out, FString& Error);
