// Copyright Rocket Science.

#include "DesignTokenLibrary.h"
#include "DesignTokens.h"
#include "DesignTokenSettings.h"
#include "FigmaTokenBridgeLog.h"

namespace
{
	/**
	 * Cached weak handle to the active asset. Cleared automatically if the asset
	 * is reloaded, which happens on every re-sync in the editor.
	 */
	TWeakObjectPtr<UDesignTokens> GCachedTokens;

	/** Unknown tokens are logged once each, so a tick-rate call cannot spam. */
	TSet<FName> GReportedMissing;

	UDesignTokens* ResolveTokens()
	{
		if (GCachedTokens.IsValid())
		{
			return GCachedTokens.Get();
		}

		const UDesignTokenSettings* Settings = UDesignTokenSettings::Get();
		if (!Settings || Settings->ActiveTokens.IsNull())
		{
			return nullptr;
		}

		UDesignTokens* Loaded = Cast<UDesignTokens>(Settings->ActiveTokens.TryLoad());
		GCachedTokens = Loaded;
		return Loaded;
	}

	FLinearColor LookUp(FName Token, const FLinearColor& Fallback, EDesignTokenTier RequiredTier)
	{
		UDesignTokens* Tokens = ResolveTokens();
		if (!Tokens)
		{
			if (!GReportedMissing.Contains(NAME_None))
			{
				GReportedMissing.Add(NAME_None);
				UE_LOG(LogFigmaTokens, Warning,
					TEXT("No design token asset is set. Project Settings > Plugins > Figma Token Bridge > Active Tokens."));
			}
			return Fallback;
		}

		FDesignColourToken Found;
		if (!Tokens->FindToken(Token, Found))
		{
			if (!GReportedMissing.Contains(Token))
			{
				GReportedMissing.Add(Token);
				UE_LOG(LogFigmaTokens, Warning, TEXT("Unknown design token '%s'. Using the fallback colour."),
					*Token.ToString());
			}
			return Fallback;
		}

		if (Found.bDeprecated && !GReportedMissing.Contains(Token))
		{
			GReportedMissing.Add(Token);
			UE_LOG(LogFigmaTokens, Warning,
				TEXT("Design token '%s' is deprecated — it is no longer present in Figma and is resolving to its last known value."),
				*Token.ToString());
		}

		if (RequiredTier == EDesignTokenTier::Component && Found.Tier != EDesignTokenTier::Component)
		{
			UE_LOG(LogFigmaTokens, Verbose,
				TEXT("Design token '%s' is %s tier, not Component. Prefer a Component token in UI work."),
				*Token.ToString(), *UEnum::GetDisplayValueAsText(Found.Tier).ToString());
		}

		return Found.Colour;
	}
}

void UDesignTokenLibrary::InvalidateDesignTokenCache()
{
	GCachedTokens.Reset();

	// Clearing the reported-missing set too, so that after a re-sync a token that
	// was missing and has now arrived stops being reported, and one that has just
	// gone missing is reported once rather than never.
	GReportedMissing.Empty();
}

UDesignTokens* UDesignTokenLibrary::GetDesignTokens()
{
	return ResolveTokens();
}

FLinearColor UDesignTokenLibrary::GetDesignColour(FName Token, FLinearColor Fallback)
{
	return LookUp(Token, Fallback, EDesignTokenTier::Component);
}

FLinearColor UDesignTokenLibrary::GetDesignColourAnyTier(FName Token, FLinearColor Fallback)
{
	return LookUp(Token, Fallback, EDesignTokenTier::Other);
}

FSlateColor UDesignTokenLibrary::GetDesignSlateColour(FName Token)
{
	return FSlateColor(LookUp(Token, FLinearColor(1.f, 0.f, 1.f, 1.f), EDesignTokenTier::Component));
}

bool UDesignTokenLibrary::HasDesignToken(FName Token)
{
	const UDesignTokens* Tokens = ResolveTokens();
	return Tokens && Tokens->Colours.Contains(Token);
}

bool UDesignTokenLibrary::GetDesignTokenInfo(FName Token, FDesignColourToken& OutToken)
{
	const UDesignTokens* Tokens = ResolveTokens();
	return Tokens && Tokens->FindToken(Token, OutToken);
}

TArray<FString> UDesignTokenLibrary::GetComponentTokenNames()
{
	TArray<FString> Out;

	if (const UDesignTokens* Tokens = ResolveTokens())
	{
		// Driven by what the designer published in the Figma link, not by tier.
		// Falling back to the Component tier keeps older exports working, which
		// were written before links existed.
		TArray<FName> Names = Tokens->GetPublishedTokenNames();
		if (Names.Num() == 0)
		{
			Names = Tokens->GetTokenNames(EDesignTokenTier::Component);
		}

		for (const FName& Name : Names)
		{
			Out.Add(Name.ToString());
		}
	}

	return Out;
}

TArray<FString> UDesignTokenLibrary::GetAllTokenNames()
{
	TArray<FString> Out;

	if (const UDesignTokens* Tokens = ResolveTokens())
	{
		for (const FName& Name : Tokens->GetTokenNames(EDesignTokenTier::Other))
		{
			Out.Add(Name.ToString());
		}
	}

	return Out;
}
