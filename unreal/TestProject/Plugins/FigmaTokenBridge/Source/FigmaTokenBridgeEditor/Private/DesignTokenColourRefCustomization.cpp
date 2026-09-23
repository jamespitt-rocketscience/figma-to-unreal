// Copyright Rocket Science.

#include "DesignTokenColourRefCustomization.h"

#include "DesignTokenTypes.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FigmaTokenBridge"

TSharedRef<IPropertyTypeCustomization> FDesignTokenColourRefCustomization::MakeInstance()
{
	return MakeShareable(new FDesignTokenColourRefCustomization());
}

FName FDesignTokenColourRefCustomization::CurrentToken() const
{
	FName Value = NAME_None;
	if (TokenHandle.IsValid())
	{
		TokenHandle->GetValue(Value);
	}
	return Value;
}

FLinearColor FDesignTokenColourRefCustomization::CurrentColour() const
{
	FDesignTokenColourRef Ref;
	Ref.Token = CurrentToken();

	if (!Ref.IsSet())
	{
		// Nothing chosen is not an error, so it should not look like one. A flat
		// mid grey reads as "no opinion" where magenta would read as "broken".
		return FLinearColor(0.15f, 0.15f, 0.15f, 1.f);
	}

	return Ref.Resolve();
}

FText FDesignTokenColourRefCustomization::ValueSummary() const
{
	FDesignTokenColourRef Ref;
	Ref.Token = CurrentToken();

	if (!Ref.IsSet())
	{
		return LOCTEXT("NoToken", "No token — this property keeps its hand-set value.");
	}

	FDesignColourToken Info;
	if (!Ref.ResolveToken(Info))
	{
		return LOCTEXT("UnknownToken", "Not in the current palette.");
	}

	return FText::Format(
		LOCTEXT("ValueSummaryFmt", "{0} · alpha {1} · {2} tier"),
		FText::FromString(Info.SrgbHex),
		FText::AsNumber(Info.Colour.A),
		FText::FromString(UEnum::GetDisplayValueAsText(Info.Tier).ToString()));
}

FText FDesignTokenColourRefCustomization::AliasChain() const
{
	FDesignTokenColourRef Ref;
	Ref.Token = CurrentToken();

	FDesignColourToken Info;
	if (!Ref.IsSet() || !Ref.ResolveToken(Info))
	{
		return FText::GetEmpty();
	}

	// The Figma names, not the normalised ones: this row exists so a developer
	// can say the same thing to a designer that the designer sees in Figma.
	TArray<FString> Hops;
	Hops.Add(Info.FigmaName);

	if (!Info.AliasOf.IsNone())
	{
		FDesignColourToken Parent;
		FDesignTokenColourRef ParentRef;
		ParentRef.Token = Info.AliasOf;
		Hops.Add(ParentRef.ResolveToken(Parent) ? Parent.FigmaName : Info.AliasOf.ToString());
	}

	if (!Info.ResolvesTo.IsNone() && Info.ResolvesTo != Info.AliasOf)
	{
		FDesignColourToken Root;
		FDesignTokenColourRef RootRef;
		RootRef.Token = Info.ResolvesTo;
		const FString RootName = RootRef.ResolveToken(Root) ? Root.FigmaName : Info.ResolvesTo.ToString();
		if (!Hops.Contains(RootName))
		{
			Hops.Add(RootName);
		}
	}

	if (Hops.Num() < 2)
	{
		return FText::GetEmpty();
	}

	return FText::FromString(FString::Join(Hops, TEXT("  →  ")));
}

FText FDesignTokenColourRefCustomization::Problem() const
{
	FDesignTokenColourRef Ref;
	Ref.Token = CurrentToken();

	if (!Ref.IsSet())
	{
		return FText::GetEmpty();
	}

	FDesignColourToken Info;
	if (!Ref.ResolveToken(Info))
	{
		return FText::Format(
			LOCTEXT("MissingTokenFmt",
				"'{0}' is not in the current palette, so this resolves to magenta. It may have been removed in Figma, or the palette may need re-syncing."),
			FText::FromName(Ref.Token));
	}

	if (Info.bDeprecated)
	{
		return FText::Format(
			LOCTEXT("DeprecatedTokenFmt",
				"'{0}' no longer exists in Figma. It still resolves to its last known value, but pick a replacement before it is removed for good."),
			FText::FromName(Ref.Token));
	}

	if (!Info.bPublished)
	{
		return FText::Format(
			LOCTEXT("UnpublishedTokenFmt",
				"'{0}' is imported but not published to this project, so it is not offered in the dropdown. It works, but the designer did not intend it for use here."),
			FText::FromName(Ref.Token));
	}

	return FText::GetEmpty();
}

EVisibility FDesignTokenColourRefCustomization::ProblemVisibility() const
{
	return Problem().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

void FDesignTokenColourRefCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TokenHandle = PropertyHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FDesignTokenColourRef, Token));

	if (!TokenHandle.IsValid())
	{
		// Should be impossible, but falling back to the default row beats an
		// empty property that gives the user nothing to work with.
		HeaderRow.NameContent()[ PropertyHandle->CreatePropertyNameWidget() ];
		return;
	}

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(280.f)
	[
		SNew(SHorizontalBox)

		// The swatch. Alpha is drawn split, because several tokens differ only
		// in opacity (bg-blade is 80%, bg-abnormal 40%) and a combined block
		// makes those two look like different colours rather than the same one.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(SBox)
			.WidthOverride(26.f)
			.HeightOverride(16.f)
			[
				SNew(SColorBlock)
				.Color(this, &FDesignTokenColourRefCustomization::CurrentColour)
				.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Separate)
				.ShowBackgroundForAlpha(true)
				.CornerRadius(FVector4(2.f, 2.f, 2.f, 2.f))
				.UseSRGB(true)
			]
		]

		// The dropdown itself. Built from the inner FName handle so the
		// GetOptions metadata on it drives the list — the options come from the
		// live token asset, so the moment a re-sync lands the list is correct.
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		[
			TokenHandle->CreatePropertyValueWidget()
		]
	];
}

void FDesignTokenColourRefCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// The Token child is already shown in the header; re-adding it here would
	// give two dropdowns for one value.

	ChildBuilder.AddCustomRow(LOCTEXT("ResolvedFilter", "Resolved value"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("ResolvedLabel", "Resolves to"))
		.Font(CustomizationUtils.GetRegularFont())
	]
	.ValueContent()
	.MinDesiredWidth(280.f)
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(this, &FDesignTokenColourRefCustomization::ValueSummary)
			.Font(CustomizationUtils.GetRegularFont())
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 2.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(this, &FDesignTokenColourRefCustomization::AliasChain)
			.Font(CustomizationUtils.GetRegularFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.AutoWrapText(true)
		]
	];

	ChildBuilder.AddCustomRow(LOCTEXT("ProblemFilter", "Design token warning"))
	.Visibility(TAttribute<EVisibility>(this, &FDesignTokenColourRefCustomization::ProblemVisibility))
	.WholeRowContent()
	[
		SNew(STextBlock)
		.Text(this, &FDesignTokenColourRefCustomization::Problem)
		.Font(CustomizationUtils.GetRegularFont())
		.ColorAndOpacity(FLinearColor(1.f, 0.65f, 0.f, 1.f))
		.AutoWrapText(true)
	];
}

#undef LOCTEXT_NAMESPACE
