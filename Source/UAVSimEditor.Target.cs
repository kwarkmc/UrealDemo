using UnrealBuildTool;

public class UAVSimEditorTarget : TargetRules
{
    public UAVSimEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.Add("UAVSim");
    }
}
