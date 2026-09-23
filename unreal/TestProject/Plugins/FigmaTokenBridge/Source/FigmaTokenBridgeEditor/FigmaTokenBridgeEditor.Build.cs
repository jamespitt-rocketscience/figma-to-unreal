// Copyright Rocket Science.

using UnrealBuildTool;

public class FigmaTokenBridgeEditor : ModuleRules
{
	public FigmaTokenBridgeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"FigmaTokenBridge"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"ToolMenus",
			"AssetRegistry",
			"AssetTools",
			"Projects",
			"PropertyEditor",
			"Json",
			"JsonUtilities",
			"DeveloperSettings",
			"EditorFramework"
		});
	}
}
