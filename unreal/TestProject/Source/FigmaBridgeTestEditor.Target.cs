// Copyright Rocket Science.

using UnrealBuildTool;

public class FigmaBridgeTestEditorTarget : TargetRules
{
	public FigmaBridgeTestEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;

		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

		ExtraModuleNames.Add("FigmaBridgeTest");
	}
}
