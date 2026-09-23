// Copyright Rocket Science.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DesignTokenSettings.generated.h"

/**
 * The link between this Unreal project and a Figma file.
 *
 * The bridge is built to serve many projects, so nothing about a particular
 * Figma file is compiled in. Pairing happens here, once, per project:
 * Project Settings > Plugins > Figma Token Bridge. The settings land in
 * DefaultGame.ini and are committed with the project.
 *
 * FigmaFileKey is the safety interlock. Every tokens.json records the file it
 * came from, and the importer refuses a file whose key does not match the link,
 * so tokens from the wrong design system cannot be imported into the wrong
 * project by accident.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Figma Token Bridge"))
class FIGMATOKENBRIDGE_API UDesignTokenSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDesignTokenSettings();

	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Figma Token Bridge"); }

	// ------------------------------------------------------- this project's id

	/**
	 * This project's identity in the handshake. Generated once on first run and
	 * committed with the project. Copy it into the Figma plugin when a designer
	 * creates a link, so the export can name the project it is intended for.
	 *
	 * Editable only so a lost config can be restored by pasting the value the
	 * Figma link already refers to. Changing it otherwise breaks every existing
	 * link to this project.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Unreal Identity")
	FString ProjectId;

	/** Returns ProjectId, generating and saving one if it is not set yet. */
	static FString GetOrCreateProjectId();

	/** Strip hyphens and upper-case, so formatting differences never matter. */
	static FString NormaliseProjectId(const FString& In);

	// ---------------------------------------------------------------- the link

	/**
	 * Figma file key this project is paired with — the segment after /design/
	 * in the file URL. Leave empty to accept any file (not recommended).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Figma Link")
	FString FigmaFileKey;

	/** Human-readable name of the paired file. Display only. */
	UPROPERTY(Config, EditAnywhere, Category = "Figma Link")
	FString FigmaFileName;

	/**
	 * Refuse to import a tokens.json whose source.fileKey differs from
	 * FigmaFileKey. Turn this off only for a deliberate re-link, and expect the
	 * key-based token matching to report a large rename set when you do.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Figma Link")
	bool bRequireFileKeyMatch = true;

	/**
	 * Refuse to import a tokens.json whose target.projectId is not this project.
	 * This is the designer's half of the handshake: they name the project the
	 * export is for, and this checks that we are it. Leave on.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Figma Link")
	bool bRequireProjectIdMatch = true;

	/**
	 * On Sync, fetch the designer's latest publish straight from the Figma file
	 * rather than reading a tokens.json someone had to send over. The fetched
	 * export is still written to TokensFile, so it can be reviewed and committed.
	 *
	 * Needs FigmaFileKey here, plus a Figma access token in Editor Preferences >
	 * Plugins > Figma Token Bridge. That token is per user and never committed. A
	 * user without one falls back to the local TokensFile, with a warning.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Figma Link")
	bool bPullFromFigma = true;

	/**
	 * tokens.json produced by the Figma plugin, relative to the project root.
	 * When pulling from Figma, each sync overwrites it with what was fetched.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Figma Link", meta = (FilePathFilter = "json", RelativeToGameDir))
	FFilePath TokensFile;

	// ------------------------------------------------------- generated content

	/**
	 * Where generated assets are written. Treat as machine-owned: the importer
	 * assumes it may overwrite anything here, and hand edits will be lost.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Generated Assets")
	FString GeneratedPackagePath = TEXT("/Game/DesignSystem/Generated");

	UPROPERTY(Config, EditAnywhere, Category = "Generated Assets")
	FString TokensAssetName = TEXT("DA_DesignTokens");

	// ------------------------------------------------------------ runtime lookup

	/**
	 * The token asset gameplay code resolves through. Set automatically on first
	 * import; override to point at a different palette.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (AllowedClasses = "/Script/FigmaTokenBridge.DesignTokens"))
	FSoftObjectPath ActiveTokens;

	static const UDesignTokenSettings* Get();

	/** Full package path of the generated token asset, e.g. "/Game/.../DA_DesignTokens". */
	FString GetTokensAssetPackagePath() const;

	/** Absolute path to the configured tokens.json, or empty if unset. */
	FString GetAbsoluteTokensFilePath() const;
};
