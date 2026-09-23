// Copyright Rocket Science.

#pragma once

#include "CoreMinimal.h"
#include "FigmaPublication.h"

struct FFigmaFetchResult
{
	bool bSuccess = false;
	FFigmaPublication Publication;
	TArray<FString> Errors;
	TArray<FString> Warnings;
};

/**
 * Fetches this project's publish from the paired Figma file.
 *
 * One request per call, and never on a timer: a View or Collab seat gets 20
 * file reads a month.
 */
class FFigmaTokenFetcher
{
public:
	/** Why a pull cannot happen right now, or empty if it can. */
	static FString WhyCannotPull();

	/** OnComplete runs on the game thread. */
	static void FetchPublication(TFunction<void(const FFigmaFetchResult&)> OnComplete);
};
