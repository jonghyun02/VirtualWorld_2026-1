using UnrealBuildTool;

public class HandTrackingDemoTarget : TargetRules
{
	public HandTrackingDemoTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "HandTrackingDemo" });
	}
}
