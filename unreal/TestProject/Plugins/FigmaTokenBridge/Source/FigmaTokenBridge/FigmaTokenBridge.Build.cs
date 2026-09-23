// Copyright Rocket Science.

using UnrealBuildTool;

public class FigmaTokenBridge : ModuleRules
{
	public FigmaTokenBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"SlateCore",
			"Slate",
			"UMG",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Json",
			"JsonUtilities"
		});
	}
}
