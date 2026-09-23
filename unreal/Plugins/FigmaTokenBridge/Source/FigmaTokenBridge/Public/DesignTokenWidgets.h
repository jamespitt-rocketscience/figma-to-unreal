// Copyright Rocket Science.

#pragma once

#include "CoreMinimal.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "DesignTokenTypes.h"
#include "DesignTokenWidgets.generated.h"

/**
 * Token-aware UMG widgets.
 *
 * Each of these is the stock widget plus one or more FDesignTokenColourRef
 * properties. The token name is what gets saved into the widget asset; the
 * colour is resolved in SynchronizeProperties, which UMG calls both when the
 * Designer rebuilds the preview and when the widget is constructed at runtime.
 *
 * Two consequences worth understanding before using them:
 *
 *   1. The Designer shows the real token colour while you author, because
 *      SynchronizeProperties runs there too. No compile step, no PreConstruct
 *      graph node, no property binding ticking every frame.
 *
 *   2. Once a token is set, the underlying colour property is machine-owned.
 *      Editing Brush Color by hand will appear to work and then be overwritten
 *      on the next synchronise. Clear the token first if you want manual
 *      control of that property.
 *
 * Widgets already placed in a layout do not need regenerating when the palette
 * changes — that is the entire point. Re-sync, and they resolve to the new value.
 */

// ---------------------------------------------------------------------------

/** A Border whose brush and content colours come from design tokens. */
UCLASS(meta = (DisplayName = "Token Border"))
class FIGMATOKENBRIDGE_API UTokenBorder : public UBorder
{
	GENERATED_BODY()

public:
	/** Drives Brush Color. Leave as None to keep the hand-set colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (DisplayPriority = "1"))
	FDesignTokenColourRef BrushColourToken;

	/** Drives Content Color and Opacity, which tints everything inside the border. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayPriority = "1"))
	FDesignTokenColourRef ContentColourToken;

	virtual void SynchronizeProperties() override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif
};

// ---------------------------------------------------------------------------

/** An Image whose tint comes from a design token. */
UCLASS(meta = (DisplayName = "Token Image"))
class FIGMATOKENBRIDGE_API UTokenImage : public UImage
{
	GENERATED_BODY()

public:
	/** Drives Color and Opacity. Leave as None to keep the hand-set tint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (DisplayPriority = "1"))
	FDesignTokenColourRef ColourToken;

	virtual void SynchronizeProperties() override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif
};

// ---------------------------------------------------------------------------

/** A Text Block whose colour comes from a design token. */
UCLASS(meta = (DisplayName = "Token Text Block"))
class FIGMATOKENBRIDGE_API UTokenTextBlock : public UTextBlock
{
	GENERATED_BODY()

public:
	/** Drives Color and Opacity. Leave as None to keep the hand-set colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (DisplayPriority = "1"))
	FDesignTokenColourRef ColourToken;

	virtual void SynchronizeProperties() override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif
};
