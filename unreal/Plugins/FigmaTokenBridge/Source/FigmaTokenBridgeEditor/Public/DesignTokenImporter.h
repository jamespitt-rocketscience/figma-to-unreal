// Copyright Rocket Science.

#pragma once

#include "CoreMinimal.h"
#include "DesignTokenTypes.h"

class UDesignTokens;

/** Outcome of one import, suitable for logging or showing in a dialog. */
struct FIGMATOKENBRIDGEEDITOR_API FDesignTokenImportResult
{
	bool bSuccess = false;

	int32 Added = 0;
	int32 Updated = 0;
	int32 Unchanged = 0;
	int32 Deprecated = 0;
	int32 Renamed = 0;

	TArray<FString> Errors;
	TArray<FString> Warnings;

	UDesignTokens* Asset = nullptr;

	int32 TotalImported() const { return Added + Updated + Unchanged; }
	FString Summary() const;
};

/** Everything parsed out of a tokens.json, before any asset is touched. */
struct FIGMATOKENBRIDGEEDITOR_API FDesignTokenDocument
{
	FString Schema;
	FString FileKey;
	FString FileName;
	FString ReadMode;
	FString ModeId;
	FString CollectionId;
	FString ExportedAt;

	// The link the designer authored in Figma.
	FString LinkName;
	FString TargetProjectId;
	FString TargetProjectName;
	FString LinkedBy;
	TArray<FString> PublishedSelection;

	TMap<FName, FDesignColourToken> Colours;
};

/**
 * Reads a tokens.json and writes the generated UDesignTokens asset.
 *
 * Parsing is separated from asset writing so the interesting half can be tested
 * without an asset registry, a package, or a Figma file. See
 * Private/Tests/DesignTokenTests.cpp.
 */
class FIGMATOKENBRIDGEEDITOR_API FDesignTokenImporter
{
public:
	/**
	 * Parse and validate a tokens.json.
	 *
	 * Resolves alias keys into token names, converts sRGB to linear, and rejects
	 * documents with an unsupported schema, a malformed colour, or a broken alias
	 * reference. Pure: touches no assets.
	 */
	static bool ParseDocument(const FString& JsonText, FDesignTokenDocument& OutDoc, FDesignTokenImportResult& InOutResult);

	/** Parse the file at JsonPath, then create or update the configured asset. */
	static FDesignTokenImportResult ImportFromFile(const FString& JsonPath);

	/** Import using the path from project settings. */
	static FDesignTokenImportResult ImportFromSettings();

	/**
	 * Create or update the generated asset from an already-parsed document.
	 * Diffs against what is already there so an unchanged sync is a no-op, which
	 * matters once this runs against Perforce.
	 */
	static FDesignTokenImportResult WriteAsset(const FDesignTokenDocument& Doc);
};
