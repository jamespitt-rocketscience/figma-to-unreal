// Copyright Rocket Science.

#pragma once

#include "CoreMinimal.h"
#include "DesignTokenImporter.h"

/** Formats an import's changes for the Message Log page. */
class FFigmaTokenBridgeReport
{
public:
	/**
	 * One line per change, e.g. "Updated  component.blades.accent_goals  #00D86C → #7B61FF".
	 * At most Max lines, then "…and N more": the first import of a large system
	 * changes hundreds of tokens, and nobody reads past the first screen.
	 */
	static TArray<FString> ChangeLines(const TArray<FDesignTokenChange>& Changes, int32 Max);
};
