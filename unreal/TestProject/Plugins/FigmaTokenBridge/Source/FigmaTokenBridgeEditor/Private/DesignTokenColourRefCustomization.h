// Copyright Rocket Science.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"
#include "Misc/Attribute.h"

/**
 * Details-panel drawing for FDesignTokenColourRef.
 *
 * Written once, for the struct rather than for any particular widget, so every
 * token-aware property added later gets this UI without further editor code.
 *
 * The header row is a swatch plus the token dropdown. The swatch matters more
 * than it looks: without it the property is a string, and a designer has no way
 * to tell "component.chat.bg_bubble" from "component.blades.bg_blade" at a
 * glance. Expanding the row shows the resolved value and the alias chain, which
 * is what answers "why is this colour this colour".
 */
class FDesignTokenColourRefCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	/** Handle to the inner Token FName, which carries the GetOptions metadata. */
	TSharedPtr<IPropertyHandle> TokenHandle;

	FName CurrentToken() const;

	/** Live colour for the swatch. Magenta when the token is unknown. */
	FLinearColor CurrentColour() const;

	/** "#00d86c · alpha 1.00" or an explanation of why there is no value. */
	FText ValueSummary() const;

	/** "Semantic/accent-green → Global/green-400", or empty for a primitive. */
	FText AliasChain() const;

	/** Non-empty only when something is wrong and worth saying loudly. */
	FText Problem() const;
	EVisibility ProblemVisibility() const;
};
