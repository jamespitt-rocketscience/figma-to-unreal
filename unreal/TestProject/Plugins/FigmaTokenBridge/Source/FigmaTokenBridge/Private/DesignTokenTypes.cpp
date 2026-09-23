// Copyright Rocket Science.

#include "DesignTokenTypes.h"
#include "DesignTokenLibrary.h"
#include "DesignTokens.h"

bool FDesignTokenColour::ParseSrgbHex(const FString& Hex, float Alpha, FLinearColor& OutColour)
{
	FString Clean = Hex.TrimStartAndEnd();
	if (Clean.StartsWith(TEXT("#")))
	{
		Clean.RightChopInline(1, EAllowShrinking::No);
	}

	if (Clean.Len() != 6)
	{
		return false;
	}

	for (const TCHAR Char : Clean)
	{
		if (!FChar::IsHexDigit(Char))
		{
			return false;
		}
	}

	const uint32 Packed = FParse::HexNumber(*Clean);

	const float R = static_cast<float>((Packed >> 16) & 0xFF) / 255.0f;
	const float G = static_cast<float>((Packed >> 8) & 0xFF) / 255.0f;
	const float B = static_cast<float>(Packed & 0xFF) / 255.0f;

	// Only the colour channels get the transfer function. Alpha is already linear.
	OutColour = FLinearColor(
		SrgbChannelToLinear(R),
		SrgbChannelToLinear(G),
		SrgbChannelToLinear(B),
		Alpha);

	return true;
}

bool FDesignTokenColourRef::ResolveToken(FDesignColourToken& OutToken) const
{
	if (!IsSet())
	{
		return false;
	}
	return UDesignTokenLibrary::GetDesignTokenInfo(Token, OutToken);
}

FLinearColor FDesignTokenColourRef::Resolve(const FLinearColor& Fallback) const
{
	if (!IsSet())
	{
		return Fallback;
	}

	// Deliberately routed through the library rather than the asset, so an unknown
	// or deprecated token produces the same fallback and the same one-shot log
	// warning it would at runtime. The editor should not be quieter than the game.
	return UDesignTokenLibrary::GetDesignColourAnyTier(Token, Fallback);
}

FSlateColor FDesignTokenColourRef::ResolveSlate(const FLinearColor& Fallback) const
{
	return FSlateColor(Resolve(Fallback));
}
