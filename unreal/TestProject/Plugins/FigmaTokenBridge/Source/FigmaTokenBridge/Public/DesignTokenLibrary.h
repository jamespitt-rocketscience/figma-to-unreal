// Copyright Rocket Science.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Styling/SlateColor.h"
#include "DesignTokenTypes.h"
#include "DesignTokenLibrary.generated.h"

class UDesignTokens;

/**
 * The Blueprint-facing surface of the bridge.
 *
 * The token argument uses GetOptions metadata, so designers and developers pick
 * from a searchable dropdown of real token names on the node itself rather than
 * typing an FName that compiles fine and fails at runtime. The list regenerates
 * from the asset, so it is correct the moment a sync lands.
 *
 * The dropdown deliberately offers Component tokens only, mirroring the source
 * board's instruction to choose from that tier. GetDesignColourAnyTier exists
 * for the rare case that genuinely needs a Semantic or Global value.
 */
UCLASS()
class FIGMATOKENBRIDGE_API UDesignTokenLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The active token asset, resolved through project settings. May be null. */
	UFUNCTION(BlueprintPure, Category = "Design Tokens")
	static UDesignTokens* GetDesignTokens();

	/**
	 * Look up a Component-tier colour.
	 *
	 * A missing token returns Fallback, which defaults to magenta because a
	 * silent black or white reads as a design choice while magenta gets noticed
	 * and fixed. A warning is logged once per unknown token per session.
	 */
	UFUNCTION(BlueprintPure, Category = "Design Tokens", meta = (GetOptions = "GetComponentTokenNames"))
	static FLinearColor GetDesignColour(FName Token, FLinearColor Fallback = FLinearColor(1.f, 0.f, 1.f, 1.f));

	/** As GetDesignColour, but the dropdown offers every tier. */
	UFUNCTION(BlueprintPure, Category = "Design Tokens", meta = (GetOptions = "GetAllTokenNames"))
	static FLinearColor GetDesignColourAnyTier(FName Token, FLinearColor Fallback = FLinearColor(1.f, 0.f, 1.f, 1.f));

	/** Convenience for Slate/UMG properties that want an FSlateColor. */
	UFUNCTION(BlueprintPure, Category = "Design Tokens", meta = (GetOptions = "GetComponentTokenNames"))
	static FSlateColor GetDesignSlateColour(FName Token);

	/** True when the token exists in the active asset. */
	UFUNCTION(BlueprintPure, Category = "Design Tokens", meta = (GetOptions = "GetAllTokenNames"))
	static bool HasDesignToken(FName Token);

	/** Full record for a token, including which primitive drives it. */
	UFUNCTION(BlueprintPure, Category = "Design Tokens", meta = (GetOptions = "GetAllTokenNames"))
	static bool GetDesignTokenInfo(FName Token, FDesignColourToken& OutToken);

	/**
	 * Drop the cached token asset so the next lookup re-resolves through project
	 * settings.
	 *
	 * A re-sync normally mutates the existing asset in place, so the cache stays
	 * correct on its own. This is for the cases where it cannot: Active Tokens
	 * being repointed at a different asset, or the generated asset being written
	 * under a new name. Without it the editor keeps serving the old palette and
	 * the only symptom is colours that refuse to change.
	 */
	UFUNCTION(BlueprintCallable, Category = "Design Tokens")
	static void InvalidateDesignTokenCache();

	// --- dropdown providers; not intended to be called directly ---

	UFUNCTION()
	static TArray<FString> GetComponentTokenNames();

	UFUNCTION()
	static TArray<FString> GetAllTokenNames();
};
