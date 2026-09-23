// Copyright Rocket Science.

using UnrealBuildTool;

public class FigmaBridgeTest : ModuleRules
{
	public FigmaBridgeTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",

			// Not needed to build, but it makes the bridge's Blueprint nodes and the
			// UDesignTokens asset type available to anything added to this project.
			"FigmaTokenBridge"
		});
	}
}
