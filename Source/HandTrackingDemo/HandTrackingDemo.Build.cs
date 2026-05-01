using UnrealBuildTool;

public class HandTrackingDemo : ModuleRules
{
	public HandTrackingDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"HeadMountedDisplay",
			"EnhancedInput",
			"XRBase",
			"OculusXRInput",
			"OculusXRHMD"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"Json",
			"JsonUtilities"
		});
	}
}
