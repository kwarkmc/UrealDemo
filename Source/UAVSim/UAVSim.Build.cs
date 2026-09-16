using UnrealBuildTool;
using System.IO;

public class UAVSim : ModuleRules
{
    public UAVSim(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "ProceduralMeshComponent" });
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "ImageWrapper", "RenderCore", "RHI" });
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "FlightCore"));
    }
}
