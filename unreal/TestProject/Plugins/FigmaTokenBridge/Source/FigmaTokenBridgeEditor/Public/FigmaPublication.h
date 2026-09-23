// Copyright Rocket Science.

#pragma once

#include "CoreMinimal.h"

/**
 * One link's publish, as a designer stored it in the Figma file.
 *
 * DocumentJson is exactly the tokens.json the plugin would have offered for
 * download, so it goes through the importer and both handshake checks
 * unchanged.
 */
struct FIGMATOKENBRIDGEEDITOR_API FFigmaPublication
{
	FString LinkId;
	FString LinkName;
	FString ProjectId;
	FString ProjectName;
	FString FileKey;
	FString FileName;
	FString PublishId;
	FString PublishedAt;
	FString PublishedBy;
	FString ExportedAt;
	int32 PublishedCount = INDEX_NONE;

	FString DocumentJson;
};

/**
 * Reads a publish back out of a GET /v1/files/:key?plugin_data=shared response.
 *
 * The mirror of planPublication() in figma-plugin/code.js. The layout, chunking
 * and checksum are explained there; if either side changes, change both, and the
 * checksum vectors in both test suites will fail until they agree.
 *
 * Pure: no HTTP, no assets. See FFigmaTokenFetcher for the network half.
 */
class FIGMATOKENBRIDGEEDITOR_API FFigmaPublicationReader
{
public:
	static const TCHAR* Namespace;
	static const TCHAR* Schema;

	/** The request that returns the document node's shared plugin data. */
	static FString BuildFileUrl(const FString& FileKey);

	/** FNV-1a 32-bit over UTF-16 code units, as 8 lower-case hex digits. */
	static FString Fnv1a32(const FString& Text);

	/**
	 * Find the publish aimed at ProjectId and reassemble its tokens.json.
	 *
	 * Fails, with an error a developer can act on, when nothing is published,
	 * nothing targets this project, or the chunks do not add up. When several
	 * links target this project the most recent publish wins, with a warning.
	 */
	static bool ReadFromFileResponse(
		const FString& ResponseJson,
		const FString& ProjectId,
		FFigmaPublication& OutPublication,
		TArray<FString>& OutErrors,
		TArray<FString>& OutWarnings);

	/**
	 * Indent compact JSON the way JSON.stringify(value, null, 2) does, so a pulled
	 * tokens.json diffs cleanly against one downloaded from the plugin. It
	 * re-spaces the text and never re-parses it, so numbers are left exactly as
	 * written; a JSON round trip in Unreal would print 0.8 as 0.80000000000000004.
	 */
	static FString PrettyPrint(const FString& CompactJson);
};
