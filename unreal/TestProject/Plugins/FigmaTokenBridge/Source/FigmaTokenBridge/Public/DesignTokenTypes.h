// Copyright Rocket Science.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "DesignTokenTypes.generated.h"

/**
 * Which tier of the design system a token belongs to. Mirrors the Figma naming
 * convention, where the first path segment is the tier:
 *
 *   Global/gray-900          raw value, the only tier that holds literals
 *   Semantic/bg-primary      aliases a Global primitive, names an intent
 *   Component/Blades/bg-blade  aliases a Semantic token, names a usage
 *
 * Only Component tokens are meant to be referenced from UI work — the source
 * Figma board is titled "Only choose color variables from here" for that reason.
 * The Blueprint token picker honours it by listing Component tokens only.
 */
UENUM(BlueprintType)
enum class EDesignTokenTier : uint8
{
	Global    UMETA(DisplayName = "Global (primitive)"),
	Semantic  UMETA(DisplayName = "Semantic (intent)"),
	Component UMETA(DisplayName = "Component (usage)"),
	Other     UMETA(DisplayName = "Other")
};

/** One colour token, resolved to linear at import time. */
USTRUCT(BlueprintType)
struct FIGMATOKENBRIDGE_API FDesignColourToken
{
	GENERATED_BODY()

	/** Linear colour, ready to hand to Slate/UMG. Converted from sRGB at import. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Value")
	FLinearColor Colour = FLinearColor::White;

	/** The sRGB hex the designer sees in Figma. Kept for display and diffing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Value")
	FString SrgbHex;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Classification")
	EDesignTokenTier Tier = EDesignTokenTier::Other;

	/** Group between tier and leaf, e.g. "Icons - Secondary". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Classification")
	FString Group;

	/** Original Figma name, e.g. "Component/Blades/accent-goals". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Provenance")
	FString FigmaName;

	/**
	 * Figma's stable variable key. This is the identity used to match tokens
	 * across syncs — names change, keys do not, and keys survive the design
	 * system moving to a different Figma file.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Provenance")
	FString FigmaKey;

	/** Immediate alias parent, as a token name. NAME_None for primitives. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Provenance")
	FName AliasOf;

	/** Terminal primitive the alias chain lands on. Self for primitives. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Provenance")
	FName ResolvesTo;

	/**
	 * Set when a token present in a previous sync is absent from the current
	 * export. It keeps resolving to its last known value rather than vanishing,
	 * so a designer's tidy-up cannot turn shipped UI magenta. Phase 1 wires the
	 * editor warning that names the assets still referencing it.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lifecycle")
	bool bDeprecated = false;

	/**
	 * True when the designer published this token to this project in the Figma
	 * link. Unpublished tokens are still imported — the alias graph needs them to
	 * resolve Component tokens down to a literal — but they are kept out of the
	 * Blueprint token picker, so the surface a developer sees is the surface the
	 * designer intended.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lifecycle")
	bool bPublished = false;
};

/**
 * A reference to a design token, stored by name.
 *
 * This is the thing that makes the bridge worth having in the UMG editor. A
 * widget property of this type stores the token NAME, never the resolved colour,
 * so the value is looked up fresh every time the widget synchronises. Re-sync the
 * palette and every widget referencing it follows, with nothing regenerated.
 *
 * Compare with typing RGBA into Brush Color: that is a copy, it forgets where it
 * came from, and no amount of re-syncing will ever update it.
 *
 * The editor draws this as a swatch plus a searchable dropdown of published
 * tokens, with the alias chain underneath — see FDesignTokenColourRefCustomization.
 */
USTRUCT(BlueprintType)
struct FIGMATOKENBRIDGE_API FDesignTokenColourRef
{
	GENERATED_BODY()

	/**
	 * Published token name, e.g. "component.blades.accent_goals".
	 *
	 * The dropdown is populated from the live token asset, so it lists exactly what
	 * the designer published to this project and nothing else. Leave it as None to
	 * hand the property back to whatever the widget would do normally.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design Tokens",
		meta = (GetOptions = "/Script/FigmaTokenBridge.DesignTokenLibrary.GetComponentTokenNames"))
	FName Token;

	bool IsSet() const { return !Token.IsNone() && !Token.ToString().IsEmpty(); }

	/**
	 * Current colour for this token, or Fallback when it is unset or unknown.
	 * Magenta by default, for the same reason as everywhere else in the plugin: a
	 * silent black reads as a design decision, magenta gets noticed and fixed.
	 */
	FLinearColor Resolve(const FLinearColor& Fallback = FLinearColor(1.f, 0.f, 1.f, 1.f)) const;

	/** As Resolve, but for the Slate-typed properties such as UTextBlock::ColorAndOpacity. */
	FSlateColor ResolveSlate(const FLinearColor& Fallback = FLinearColor(1.f, 0.f, 1.f, 1.f)) const;

	/** Full record behind the token, for the editor to show provenance. False when unknown. */
	bool ResolveToken(FDesignColourToken& OutToken) const;
};

/**
 * sRGB to linear conversion.
 *
 * Figma hands out sRGB hex. FLinearColor is linear. Copying the bytes across is
 * the single most likely way to ship something visibly wrong, because the result
 * looks like a deliberate choice rather than a bug:
 *
 *   #00d86c correct  (0.000000, 0.686685, 0.149960)
 *   #00d86c naive    (0.000000, 0.847059, 0.423529)   <- 19% off in green, 2.8x in blue
 *
 * Alpha is already linear and must never get the transfer function.
 */
struct FIGMATOKENBRIDGE_API FDesignTokenColour
{
	/** Piecewise sRGB electro-optical transfer function, on a 0-1 channel. */
	static float SrgbChannelToLinear(float Channel)
	{
		return (Channel <= 0.04045f)
			? (Channel / 12.92f)
			: FMath::Pow((Channel + 0.055f) / 1.055f, 2.4f);
	}

	/**
	 * Parse "#rrggbb" (or "rrggbb") into a linear colour, applying Alpha
	 * straight through. Returns false on a malformed string.
	 */
	static bool ParseSrgbHex(const FString& Hex, float Alpha, FLinearColor& OutColour);
};
