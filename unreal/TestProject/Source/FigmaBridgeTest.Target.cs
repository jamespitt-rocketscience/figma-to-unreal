// Copyright Rocket Science.

using UnrealBuildTool;

public class FigmaBridgeTestTarget : TargetRules
{
	public FigmaBridgeTestTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;

		// V7 is 5.8's default and promotes return-type, dangling and unreachable-code
		// warnings to errors. Kept deliberately: this project exists to catch problems
		// in the plugin, so the strictest setting the engine offers is the right one.
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

		ExtraModuleNames.Add("FigmaBridgeTest");
	}
}
