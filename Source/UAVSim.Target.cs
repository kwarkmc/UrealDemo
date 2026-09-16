using UnrealBuildTool;

public class UAVSimTarget : TargetRules
{
    public UAVSimTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.Add("UAVSim");
    }
}
