// Copyright Rocket Science.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DesignTokenUserSettings.generated.h"

/**
 * One developer's own Figma access, kept apart from the project settings.
 *
 * UDesignTokenSettings lands in DefaultGame.ini and is committed, so a token
 * stored there would be handed to everyone with repository access. This class is
 * saved to Saved/Config/.../EditorPerProjectUserSettings.ini instead, which is
 * never committed. Here SaveConfig() writing to Saved/ is exactly what we want,
 * unlike the DefaultConfig trap in UDesignTokenSettings.
 *
 * Editor Preferences > Plugins > Figma Token Bridge.
 */
UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "Figma Token Bridge"))
class FIGMATOKENBRIDGEEDITOR_API UDesignTokenUserSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("Figma Token Bridge"); }

	/**
	 * A Figma personal access token with the file_content:read scope. Create one
	 * in Figma under Settings > Security > Personal access tokens.
	 *
	 * The FIGMA_ACCESS_TOKEN environment variable is used when this is empty,
	 * which suits build machines.
	 *
	 * Figma rate-limits file reads by seat type. Dev and Full seats allow about
	 * 20 a minute. View and Collab seats allow only 20 a MONTH, which is why sync
	 * fetches only when asked to.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Figma Access", meta = (PasswordField = true))
	FString FigmaAccessToken;

	/**
	 * Check Figma a few seconds after the editor opens, and offer to sync if a
	 * designer has published since the last import. Costs one file read per
	 * editor launch, so leave it off on a View or Collab seat.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Figma Access")
	bool bCheckForPublishedTokensOnStartup = false;

	static const UDesignTokenUserSettings* Get();

	/** FigmaAccessToken if set, otherwise FIGMA_ACCESS_TOKEN, otherwise empty. */
	FString GetAccessToken() const;
};
