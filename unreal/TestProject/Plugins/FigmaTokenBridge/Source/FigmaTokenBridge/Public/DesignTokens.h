// Copyright Rocket Science.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DesignTokenTypes.h"
#include "DesignTokens.generated.h"

/**
 * The generated design token palette. One asset per Figma link.
 *
 * This is the whole point of the bridge: widgets reference this asset rather
 * than holding baked colours, so a re-sync updates one file and every widget
 * follows. It is generated — it lives under the path configured in
 * UDesignTokenSettings::GeneratedPackagePath and must not be hand-edited.
 * The importer owns every property here.
 */
UCLASS(BlueprintType)
class FIGMATOKENBRIDGE_API UDesignTokens : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Keyed by normalised token name, e.g. "component.blades.accent_goals". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Design Tokens")
	TMap<FName, FDesignColourToken> Colours;

	// --- provenance: which Figma file and export produced this asset ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source")
	FString SchemaVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source")
	FString FigmaFileKey;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source")
	FString FigmaFileName;

	/** "local" if exported from the library file, "consumer" if from a subscriber. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source")
	FString ReadMode;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source")
	FString ModeId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source")
	FString ExportedAt;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Source")
	FString ImportedAt;

	// --- the link the designer authored in Figma ---

	/** Name the designer gave this link, e.g. "Clinical Sim — main UI". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Link")
	FString LinkName;

	/** Unreal project this export was authored for. Verified on import. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Link")
	FString TargetProjectId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Link")
	FString TargetProjectName;

	/** Tiers and groups the designer published, e.g. "Component/Blades". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Link")
	TArray<FString> PublishedSelection;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Link")
	FString LinkedBy;

	/** Look up a token. False when the name is unknown. */
	UFUNCTION(BlueprintPure, Category = "Design Tokens")
	bool FindColour(FName Token, FLinearColor& OutColour) const;

	/** Full token record, including provenance and alias information. */
	UFUNCTION(BlueprintPure, Category = "Design Tokens")
	bool FindToken(FName Token, FDesignColourToken& OutToken) const;

	/** Token names in a tier, sorted. Pass Other to mean "every tier". */
	UFUNCTION(BlueprintPure, Category = "Design Tokens")
	TArray<FName> GetTokenNames(EDesignTokenTier Tier) const;

	/**
	 * Token names the designer published to this project, sorted. This is the
	 * surface UI work is meant to use, and what the Blueprint picker offers.
	 */
	UFUNCTION(BlueprintPure, Category = "Design Tokens")
	TArray<FName> GetPublishedTokenNames() const;

	/** Every token whose alias chain passes through, or ends at, the given token. */
	UFUNCTION(BlueprintPure, Category = "Design Tokens")
	TArray<FName> GetTokensResolvingTo(FName PrimitiveToken) const;
};
