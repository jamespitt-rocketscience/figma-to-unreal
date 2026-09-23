// Copyright Rocket Science.

#include "DesignTokenSettings.h"
#include "Misc/Paths.h"

UDesignTokenSettings::UDesignTokenSettings()
{
	// Sensible default for the dev sandbox; overridden per project in settings.
	TokensFile.FilePath = TEXT("Tokens/tokens.json");
}

const UDesignTokenSettings* UDesignTokenSettings::Get()
{
	return GetDefault<UDesignTokenSettings>();
}

FString UDesignTokenSettings::NormaliseProjectId(const FString& In)
{
	return In.Replace(TEXT("-"), TEXT(""))
		.Replace(TEXT("{"), TEXT(""))
		.Replace(TEXT("}"), TEXT(""))
		.TrimStartAndEnd()
		.ToUpper();
}

FString UDesignTokenSettings::GetOrCreateProjectId()
{
	UDesignTokenSettings* Settings = GetMutableDefault<UDesignTokenSettings>();
	if (!Settings)
	{
		return FString();
	}

	// Only generate when genuinely absent. Regenerating would silently invalidate
	// every Figma link pointing at this project.
	if (NormaliseProjectId(Settings->ProjectId).Len() != 32)
	{
		Settings->ProjectId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);

		// Must be TryUpdateDefaultConfigFile, not SaveConfig. SaveConfig resolves
		// through GetConfigFilename -> GetClass()->GetConfigName() to the *Saved*
		// Game.ini, which is not under source control — so a teammate cloning this
		// project would find no id, generate a different one, and every Figma link
		// aimed at this project would start being refused. This writes
		// Config/DefaultGame.ini, which is committed with the project.
		Settings->TryUpdateDefaultConfigFile();
	}

	return Settings->ProjectId;
}

FString UDesignTokenSettings::GetTokensAssetPackagePath() const
{
	const FString Path = GeneratedPackagePath.IsEmpty()
		? TEXT("/Game/DesignSystem/Generated")
		: GeneratedPackagePath;

	const FString Name = TokensAssetName.IsEmpty()
		? TEXT("DA_DesignTokens")
		: TokensAssetName;

	// A trailing slash is the obvious thing to type into a settings text field,
	// and would otherwise produce a doubled separator that LoadPackage rejects.
	FString Trimmed = Path.TrimStartAndEnd();
	while (Trimmed.EndsWith(TEXT("/")) || Trimmed.EndsWith(TEXT("\\")))
	{
		Trimmed.LeftChopInline(1, EAllowShrinking::No);
	}

	return FString::Printf(TEXT("%s/%s"), *Trimmed, *Name.TrimStartAndEnd());
}

FString UDesignTokenSettings::GetAbsoluteTokensFilePath() const
{
	if (TokensFile.FilePath.IsEmpty())
	{
		return FString();
	}

	if (FPaths::IsRelative(TokensFile.FilePath))
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TokensFile.FilePath));
	}

	return FPaths::ConvertRelativePathToFull(TokensFile.FilePath);
}
