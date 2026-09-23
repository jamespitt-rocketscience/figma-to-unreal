// Copyright Rocket Science.

#include "DesignTokens.h"

bool UDesignTokens::FindColour(FName Token, FLinearColor& OutColour) const
{
	if (const FDesignColourToken* Found = Colours.Find(Token))
	{
		OutColour = Found->Colour;
		return true;
	}
	OutColour = FLinearColor(1.f, 0.f, 1.f, 1.f);
	return false;
}

bool UDesignTokens::FindToken(FName Token, FDesignColourToken& OutToken) const
{
	if (const FDesignColourToken* Found = Colours.Find(Token))
	{
		OutToken = *Found;
		return true;
	}
	return false;
}

TArray<FName> UDesignTokens::GetTokenNames(EDesignTokenTier Tier) const
{
	TArray<FName> Names;
	Names.Reserve(Colours.Num());

	for (const TPair<FName, FDesignColourToken>& Pair : Colours)
	{
		// Other is overloaded to mean "no filter" — the only tier a caller is
		// unlikely to want on its own, and it keeps the Blueprint node to one pin.
		if (Tier == EDesignTokenTier::Other || Pair.Value.Tier == Tier)
		{
			Names.Add(Pair.Key);
		}
	}

	Names.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});

	return Names;
}

TArray<FName> UDesignTokens::GetPublishedTokenNames() const
{
	TArray<FName> Names;
	Names.Reserve(Colours.Num());

	for (const TPair<FName, FDesignColourToken>& Pair : Colours)
	{
		if (Pair.Value.bPublished && !Pair.Value.bDeprecated)
		{
			Names.Add(Pair.Key);
		}
	}

	Names.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});

	return Names;
}

TArray<FName> UDesignTokens::GetTokensResolvingTo(FName PrimitiveToken) const
{
	TArray<FName> Names;

	for (const TPair<FName, FDesignColourToken>& Pair : Colours)
	{
		if (Pair.Value.ResolvesTo == PrimitiveToken && Pair.Key != PrimitiveToken)
		{
			Names.Add(Pair.Key);
		}
	}

	Names.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});

	return Names;
}
