// Copyright Rocket Science.

#pragma once

#include "CoreMinimal.h"
#include "DesignTokenTypes.h"

class UDesignTokens;

enum class EDesignTokenChangeKind : uint8 { Added, Updated, Renamed, Deprecated };

/** One token an import changed, with its colour before and after, for reports. */
struct FIGMATOKENBRIDGEEDITOR_API FDesignTokenChange
{
	FName Name;
	EDesignTokenChangeKind Kind = EDesignTokenChangeKind::Updated;
	FString Before;   // "" when Added; e.g. "#00D86C" or "#1C1B1A 80%"
	FString After;    // "" when Deprecated
	FName OldName;    // set when Renamed
};

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

	/** False when the content was already imported, so the asset was left untouched. */
	bool bWroteAsset = false;

	/** False when tokens.json already held this content, or no path was given. */
	bool bWroteTokensFile = false;

	/** Every token this import added, updated, renamed or newly deprecated. */
	TArray<FDesignTokenChange> Changes;

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

	/** FDesignTokenImporter::ContentFingerprint of the text this was parsed from. */
	FString Fingerprint;

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

	/**
	 * Identity of an export's content: lower-case hex SHA-1 of the condensed JSON
	 * with source.exportedAt and source.exportedBy removed, so a re-export of the
	 * same tokens, pretty-printed or not, has the same fingerprint. Empty for
	 * text that is not JSON.
	 */
	static FString ContentFingerprint(const FString& JsonText);

	/** "#RRGGBB", upper case, plus " NN%" when the token is not opaque. */
	static FString FormatSwatch(const FDesignColourToken& Token);

	/** "added", "updated", "renamed" or "deprecated", as the push response spells it. */
	static const TCHAR* ChangeKindName(EDesignTokenChangeKind Kind);

	/** Parse the file at JsonPath, then create or update the configured asset. */
	static FDesignTokenImportResult ImportFromFile(const FString& JsonPath);

	/** Import using the path from project settings. */
	static FDesignTokenImportResult ImportFromSettings();

	/**
	 * Import a tokens.json fetched from Figma, then write it to SaveToPath so it
	 * can be reviewed and committed. The file is written only after the document
	 * passes both handshake checks, so a publish aimed at another project never
	 * replaces this project's tokens.json. An empty SaveToPath skips the write.
	 */
	static FDesignTokenImportResult ImportFromText(const FString& JsonText, const FString& SaveToPath);

	/**
	 * Create or update the generated asset from an already-parsed document.
	 * Diffs against what is already there so an unchanged sync is a no-op, which
	 * matters once this runs against Perforce.
	 */
	static FDesignTokenImportResult WriteAsset(const FDesignTokenDocument& Doc);
};
