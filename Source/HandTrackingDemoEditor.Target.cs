using UnrealBuildTool;

public class HandTrackingDemoEditorTarget : TargetRules
{
	public HandTrackingDemoEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "HandTrackingDemo" });
	}
}
