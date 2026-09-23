// Copyright Rocket Science.

#include "DesignTokenWidgets.h"

#define LOCTEXT_NAMESPACE "FigmaTokenBridge"

namespace
{
	/**
	 * Every widget below shares this shape: an unset token leaves the stock
	 * property alone, so a token-aware widget with no token behaves exactly like
	 * the widget it derives from. That matters because it makes reparenting an
	 * existing Border to a TokenBorder a safe, reversible step rather than a
	 * commitment.
	 */
	const FText DesignSystemPalette = LOCTEXT("PaletteCategory", "Design System");
}

// ---------------------------------------------------------------------------

void UTokenBorder::SynchronizeProperties()
{
	// Resolve before Super, so the value is already in place when the base class
	// pushes properties onto the underlying Slate widget.
	if (BrushColourToken.IsSet())
	{
		SetBrushColor(BrushColourToken.Resolve());
	}

	if (ContentColourToken.IsSet())
	{
		SetContentColorAndOpacity(ContentColourToken.Resolve());
	}

	Super::SynchronizeProperties();
}

#if WITH_EDITOR
const FText UTokenBorder::GetPaletteCategory()
{
	return DesignSystemPalette;
}
#endif

// ---------------------------------------------------------------------------

void UTokenImage::SynchronizeProperties()
{
	if (ColourToken.IsSet())
	{
		SetColorAndOpacity(ColourToken.Resolve());
	}

	Super::SynchronizeProperties();
}

#if WITH_EDITOR
const FText UTokenImage::GetPaletteCategory()
{
	return DesignSystemPalette;
}
#endif

// ---------------------------------------------------------------------------

void UTokenTextBlock::SynchronizeProperties()
{
	if (ColourToken.IsSet())
	{
		// UTextBlock takes an FSlateColor here, unlike UImage and UBorder which
		// take a raw FLinearColor. Getting this wrong is a compile error rather
		// than a silent one, which is the only reason it is worth a comment.
		SetColorAndOpacity(ColourToken.ResolveSlate());
	}

	Super::SynchronizeProperties();
}

#if WITH_EDITOR
const FText UTokenTextBlock::GetPaletteCategory()
{
	return DesignSystemPalette;
}
#endif

#undef LOCTEXT_NAMESPACE
