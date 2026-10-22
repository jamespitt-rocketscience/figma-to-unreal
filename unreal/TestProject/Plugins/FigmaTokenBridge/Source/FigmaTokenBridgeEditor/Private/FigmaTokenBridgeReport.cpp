// Copyright Rocket Science.

#include "FigmaTokenBridgeReport.h"

TArray<FString> FFigmaTokenBridgeReport::ChangeLines(const TArray<FDesignTokenChange>& Changes, int32 Max)
{
	TArray<FString> Lines;
	for (const FDesignTokenChange& Change : Changes)
	{
		if (Lines.Num() >= Max)
		{
			Lines.Add(FString::Printf(TEXT("…and %d more"), Changes.Num() - Max));
			break;
		}

		const FString Name = Change.Name.ToString();
		switch (Change.Kind)
		{
		case EDesignTokenChangeKind::Added:
			Lines.Add(FString::Printf(TEXT("Added  %s  %s"), *Name, *Change.After));
			break;
		case EDesignTokenChangeKind::Renamed:
			Lines.Add(FString::Printf(TEXT("Renamed  %s → %s  %s"), *Change.OldName.ToString(), *Name, *Change.After));
			break;
		case EDesignTokenChangeKind::Deprecated:
			Lines.Add(FString::Printf(TEXT("Deprecated  %s  (was %s)"), *Name, *Change.Before));
			break;
		default:
			Lines.Add(FString::Printf(TEXT("Updated  %s  %s → %s"), *Name, *Change.Before, *Change.After));
			break;
		}
	}
	return Lines;
}
