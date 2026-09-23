// Copyright Rocket Science.

#include "DesignTokenImporter.h"

#include "DesignTokens.h"
#include "DesignTokenLibrary.h"
#include "DesignTokenSettings.h"
#include "FigmaPublication.h"
#include "FigmaTokenBridgeLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "FigmaTokenBridge"

namespace
{
	const TCHAR* SupportedSchema = TEXT("figma-token-bridge/1");

	EDesignTokenTier TierFromString(const FString& In)
	{
		if (In == TEXT("Global"))    return EDesignTokenTier::Global;
		if (In == TEXT("Semantic"))  return EDesignTokenTier::Semantic;
		if (In == TEXT("Component")) return EDesignTokenTier::Component;
		return EDesignTokenTier::Other;
	}

	/** Colour equality that ignores float noise below what 8-bit sRGB can express. */
	bool ColoursMatch(const FDesignColourToken& A, const FDesignColourToken& B)
	{
		return A.SrgbHex.Equals(B.SrgbHex, ESearchCase::IgnoreCase)
			&& FMath::IsNearlyEqual(A.Colour.A, B.Colour.A, 1.e-4f)
			&& A.Tier == B.Tier
			&& A.AliasOf == B.AliasOf
			&& A.ResolvesTo == B.ResolvesTo
			&& A.FigmaName == B.FigmaName
			// A change to what the designer published is a real change: it moves
			// the token in or out of the Blueprint picker.
			&& A.bPublished == B.bPublished;
	}
}

FString FDesignTokenImportResult::Summary() const
{
	if (!bSuccess)
	{
		return FString::Printf(TEXT("Import failed: %s"),
			Errors.Num() ? *Errors[0] : TEXT("unknown error"));
	}

	return FString::Printf(
		TEXT("%d tokens (%d added, %d updated, %d unchanged, %d renamed, %d deprecated), %d warning(s)"),
		TotalImported(), Added, Updated, Unchanged, Renamed, Deprecated, Warnings.Num());
}

bool FDesignTokenImporter::ParseDocument(const FString& JsonText, FDesignTokenDocument& OutDoc, FDesignTokenImportResult& InOutResult)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);

	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		InOutResult.Errors.Add(TEXT("tokens.json is not valid JSON."));
		return false;
	}

	// --- schema gate -------------------------------------------------------
	Root->TryGetStringField(TEXT("schema"), OutDoc.Schema);
	if (OutDoc.Schema != SupportedSchema)
	{
		InOutResult.Errors.Add(FString::Printf(
			TEXT("Unsupported schema '%s'. This importer understands '%s'. Update the plugin or re-export."),
			*OutDoc.Schema, SupportedSchema));
		return false;
	}

	// --- provenance --------------------------------------------------------
	const TSharedPtr<FJsonObject>* Source = nullptr;
	if (Root->TryGetObjectField(TEXT("source"), Source) && Source)
	{
		(*Source)->TryGetStringField(TEXT("fileKey"), OutDoc.FileKey);
		(*Source)->TryGetStringField(TEXT("fileName"), OutDoc.FileName);
		(*Source)->TryGetStringField(TEXT("readMode"), OutDoc.ReadMode);
		(*Source)->TryGetStringField(TEXT("defaultModeId"), OutDoc.ModeId);
		(*Source)->TryGetStringField(TEXT("collectionId"), OutDoc.CollectionId);

		// Phase 0 imports one mode. The contract is mode-keyed so light/dark can
		// be added later without a schema break, but resolving more than one mode
		// needs a theme concept in Unreal, which is phase 3.
		const TArray<TSharedPtr<FJsonValue>>* Modes = nullptr;
		if ((*Source)->TryGetArrayField(TEXT("modes"), Modes) && Modes && Modes->Num() > 1)
		{
			InOutResult.Warnings.Add(FString::Printf(
				TEXT("This export declares %d modes. Only '%s' is imported — multi-mode theming is not implemented yet."),
				Modes->Num(), *OutDoc.ModeId));
		}
		(*Source)->TryGetStringField(TEXT("exportedAt"), OutDoc.ExportedAt);
	}
	else
	{
		InOutResult.Errors.Add(TEXT("tokens.json has no 'source' block, so the Figma file it came from cannot be verified."));
		return false;
	}

	// --- the link interlock ------------------------------------------------
	// Guards against importing one design system's tokens into another project.
	const UDesignTokenSettings* Settings = UDesignTokenSettings::Get();
	if (Settings && Settings->bRequireFileKeyMatch && !Settings->FigmaFileKey.IsEmpty())
	{
		if (!OutDoc.FileKey.Equals(Settings->FigmaFileKey, ESearchCase::CaseSensitive))
		{
			InOutResult.Errors.Add(FString::Printf(
				TEXT("This project is linked to Figma file '%s' but tokens.json came from '%s'. ")
				TEXT("Re-export from the linked file, or change the link in Project Settings > Plugins > Figma Token Bridge."),
				*Settings->FigmaFileKey, *OutDoc.FileKey));
			return false;
		}
	}
	else if (Settings && Settings->FigmaFileKey.IsEmpty())
	{
		InOutResult.Warnings.Add(FString::Printf(
			TEXT("No Figma file key is set for this project, so any tokens.json will be accepted. Link it to '%s' to enable the check."),
			*OutDoc.FileKey));
	}

	// --- the designer's half of the handshake -------------------------------
	// The Figma link names the project this export is for. Checking it means a
	// designer's link, not a developer's config, decides where tokens land — and
	// neither side can be silently wrong about it.
	const TSharedPtr<FJsonObject>* Target = nullptr;
	if (Root->TryGetObjectField(TEXT("target"), Target) && Target && (*Target).IsValid())
	{
		(*Target)->TryGetStringField(TEXT("projectId"), OutDoc.TargetProjectId);
		(*Target)->TryGetStringField(TEXT("projectName"), OutDoc.TargetProjectName);
		(*Target)->TryGetStringField(TEXT("linkName"), OutDoc.LinkName);
		(*Target)->TryGetStringField(TEXT("linkedBy"), OutDoc.LinkedBy);

		const TArray<TSharedPtr<FJsonValue>>* Published = nullptr;
		if ((*Target)->TryGetArrayField(TEXT("published"), Published) && Published)
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Published)
			{
				FString Selection;
				if (Entry.IsValid() && Entry->TryGetString(Selection))
				{
					OutDoc.PublishedSelection.Add(Selection);
				}
			}
		}
	}

	if (Settings && Settings->bRequireProjectIdMatch)
	{
		const FString Mine = UDesignTokenSettings::NormaliseProjectId(UDesignTokenSettings::GetOrCreateProjectId());
		const FString Theirs = UDesignTokenSettings::NormaliseProjectId(OutDoc.TargetProjectId);

		if (Theirs.IsEmpty())
		{
			InOutResult.Errors.Add(FString::Printf(
				TEXT("This export names no target project. Create a link in the Figma plugin and give it this project's id: %s"),
				*UDesignTokenSettings::GetOrCreateProjectId()));
			return false;
		}

		if (Mine != Theirs)
		{
			InOutResult.Errors.Add(FString::Printf(
				TEXT("This export was authored for project '%s' (%s), but this project is %s. ")
				TEXT("Either export from the link that targets this project, or update the link in Figma."),
				OutDoc.TargetProjectName.IsEmpty() ? TEXT("unnamed") : *OutDoc.TargetProjectName,
				*OutDoc.TargetProjectId,
				*UDesignTokenSettings::GetOrCreateProjectId()));
			return false;
		}
	}

	if (OutDoc.ReadMode == TEXT("consumer"))
	{
		InOutResult.Warnings.Add(
			TEXT("Exported from a file that subscribes to the design system rather than owning it, so only variables "
			     "bound on the canvas were captured. Export from the library file for an authoritative set."));
	}

	// --- colours -----------------------------------------------------------
	const TArray<TSharedPtr<FJsonValue>>* Colours = nullptr;
	if (!Root->TryGetArrayField(TEXT("colours"), Colours) || !Colours)
	{
		InOutResult.Errors.Add(TEXT("tokens.json has no 'colours' array."));
		return false;
	}

	// First pass: read every token and remember key -> name so aliases, which are
	// expressed as Figma keys, can be rewritten into token names people can read.
	TMap<FString, FName> KeyToName;
	struct FPendingAlias { FName Token; FString AliasKey; FString ResolvesKey; };
	TArray<FPendingAlias> Pending;

	for (const TSharedPtr<FJsonValue>& Value : *Colours)
	{
		const TSharedPtr<FJsonObject> Obj = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Obj.IsValid())
		{
			InOutResult.Warnings.Add(TEXT("Skipped a malformed entry in 'colours'."));
			continue;
		}

		FString Name, Key, FigmaName, TierText, Group;
		Obj->TryGetStringField(TEXT("name"), Name);
		Obj->TryGetStringField(TEXT("key"), Key);
		Obj->TryGetStringField(TEXT("figmaName"), FigmaName);
		Obj->TryGetStringField(TEXT("tier"), TierText);
		Obj->TryGetStringField(TEXT("group"), Group);

		if (Name.IsEmpty() || Key.IsEmpty())
		{
			InOutResult.Errors.Add(FString::Printf(TEXT("Token '%s' is missing a name or key."),
				Name.IsEmpty() ? *FigmaName : *Name));
			continue;
		}

		const TSharedPtr<FJsonObject>* ValuesObj = nullptr;
		if (!Obj->TryGetObjectField(TEXT("values"), ValuesObj) || !ValuesObj)
		{
			InOutResult.Errors.Add(FString::Printf(TEXT("Token '%s' has no resolved values."), *Name));
			continue;
		}

		// Prefer the declared default mode; fall back to the only entry present
		// so a document that omits source.defaultModeId still imports.
		TSharedPtr<FJsonObject> ModeValue;
		if (!OutDoc.ModeId.IsEmpty())
		{
			const TSharedPtr<FJsonObject>* Found = nullptr;
			if ((*ValuesObj)->TryGetObjectField(OutDoc.ModeId, Found) && Found)
			{
				ModeValue = *Found;
			}
		}
		if (!ModeValue.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*ValuesObj)->Values)
			{
				if (Entry.Value.IsValid())
				{
					ModeValue = Entry.Value->AsObject();
					if (ModeValue.IsValid())
					{
						break;
					}
				}
			}
		}

		if (!ModeValue.IsValid())
		{
			InOutResult.Errors.Add(FString::Printf(
				TEXT("Token '%s' has no value for mode '%s'."), *Name, *OutDoc.ModeId));
			continue;
		}

		FString Hex;
		double Alpha = 1.0;
		ModeValue->TryGetStringField(TEXT("srgbHex"), Hex);
		ModeValue->TryGetNumberField(TEXT("alpha"), Alpha);

		FDesignColourToken Token;
		if (!FDesignTokenColour::ParseSrgbHex(Hex, static_cast<float>(Alpha), Token.Colour))
		{
			InOutResult.Errors.Add(FString::Printf(TEXT("Token '%s' has an unparseable colour '%s'."), *Name, *Hex));
			continue;
		}

		Token.SrgbHex = Hex;
		Token.Tier = TierFromString(TierText);
		Token.Group = Group;
		Token.FigmaName = FigmaName;
		Token.FigmaKey = Key;
		Token.bDeprecated = false;

		// Absent in exports written before links existed; those fall back to the
		// Component tier in the Blueprint picker.
		bool bPublished = false;
		Obj->TryGetBoolField(TEXT("published"), bPublished);
		Token.bPublished = bPublished;

		const FName TokenName(*Name);

		if (OutDoc.Colours.Contains(TokenName))
		{
			InOutResult.Errors.Add(FString::Printf(TEXT("Duplicate token name '%s'."), *Name));
			continue;
		}

		OutDoc.Colours.Add(TokenName, Token);
		KeyToName.Add(Key, TokenName);

		FString AliasKey, ResolvesKey;
		Obj->TryGetStringField(TEXT("aliasOf"), AliasKey);
		Obj->TryGetStringField(TEXT("resolvesTo"), ResolvesKey);
		Pending.Add({ TokenName, AliasKey, ResolvesKey });
	}

	// Second pass: rewrite alias keys as token names.
	for (const FPendingAlias& P : Pending)
	{
		FDesignColourToken& Token = OutDoc.Colours[P.Token];

		if (!P.AliasKey.IsEmpty())
		{
			if (const FName* Resolved = KeyToName.Find(P.AliasKey))
			{
				Token.AliasOf = *Resolved;
			}
			else
			{
				// Real in consumer mode: a parent that nothing on the canvas binds.
				InOutResult.Warnings.Add(FString::Printf(
					TEXT("Token '%s' aliases key %s, which is not in this export. The resolved value is still correct."),
					*P.Token.ToString(), *P.AliasKey));
			}
		}

		if (!P.ResolvesKey.IsEmpty())
		{
			if (const FName* Resolved = KeyToName.Find(P.ResolvesKey))
			{
				Token.ResolvesTo = *Resolved;
			}
		}
	}

	if (OutDoc.Colours.Num() == 0)
	{
		InOutResult.Errors.Add(TEXT("No usable colour tokens were found."));
		return false;
	}

	return InOutResult.Errors.Num() == 0;
}

FDesignTokenImportResult FDesignTokenImporter::WriteAsset(const FDesignTokenDocument& Doc)
{
	FDesignTokenImportResult Result;

	const UDesignTokenSettings* Settings = UDesignTokenSettings::Get();
	if (!Settings)
	{
		Result.Errors.Add(TEXT("Figma Token Bridge settings are unavailable."));
		return Result;
	}

	const FString PackagePath = Settings->GetTokensAssetPackagePath();
	const FString AssetName = FPackageName::GetShortName(PackagePath);

	UPackage* Package = LoadPackage(nullptr, *PackagePath, LOAD_NoWarn | LOAD_Quiet);
	UDesignTokens* Asset = Package ? FindObject<UDesignTokens>(Package, *AssetName) : nullptr;

	// Capture prior state so the diff can report adds, updates, renames and drops.
	TMap<FName, FDesignColourToken> Previous;
	TMap<FString, FName> PreviousByKey;
	if (Asset)
	{
		Previous = Asset->Colours;
		for (const TPair<FName, FDesignColourToken>& Pair : Previous)
		{
			if (!Pair.Value.FigmaKey.IsEmpty())
			{
				PreviousByKey.Add(Pair.Value.FigmaKey, Pair.Key);
			}
		}
	}
	else
	{
		if (!Package)
		{
			Package = CreatePackage(*PackagePath);
		}
		if (!Package)
		{
			Result.Errors.Add(FString::Printf(TEXT("Could not create package '%s'."), *PackagePath));
			return Result;
		}

		Asset = NewObject<UDesignTokens>(Package, UDesignTokens::StaticClass(), *AssetName,
			RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Asset);
	}

	// --- build the new token map -------------------------------------------
	TMap<FName, FDesignColourToken> NewColours = Doc.Colours;

	for (const TPair<FName, FDesignColourToken>& Pair : NewColours)
	{
		const FDesignColourToken* Before = Previous.Find(Pair.Key);

		if (!Before)
		{
			// Identity is the Figma key, so the same key under a new name is a
			// rename rather than a delete plus an add.
			if (const FName* OldName = PreviousByKey.Find(Pair.Value.FigmaKey))
			{
				Result.Renamed++;
				Result.Warnings.Add(FString::Printf(TEXT("Token renamed: '%s' -> '%s'. Update any references."),
					*OldName->ToString(), *Pair.Key.ToString()));
			}
			else
			{
				Result.Added++;
			}
		}
		else if (ColoursMatch(*Before, Pair.Value))
		{
			Result.Unchanged++;
		}
		else
		{
			Result.Updated++;
		}
	}

	// Anything previously present and now absent is deprecated, never dropped.
	// It keeps resolving to its last known value so a tidy-up in Figma cannot
	// silently turn shipped UI magenta.
	for (const TPair<FName, FDesignColourToken>& Pair : Previous)
	{
		const bool bStillPresentByName = NewColours.Contains(Pair.Key);
		const bool bStillPresentByKey = !Pair.Value.FigmaKey.IsEmpty()
			&& Doc.Colours.Num() > 0
			&& [&]()
			{
				for (const TPair<FName, FDesignColourToken>& Incoming : Doc.Colours)
				{
					if (Incoming.Value.FigmaKey == Pair.Value.FigmaKey)
					{
						return true;
					}
				}
				return false;
			}();

		if (bStillPresentByName || bStillPresentByKey)
		{
			continue;
		}

		FDesignColourToken Kept = Pair.Value;
		if (!Kept.bDeprecated)
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("Token '%s' is gone from Figma. Kept and marked deprecated — remove it deliberately once nothing references it."),
				*Pair.Key.ToString()));
		}
		Kept.bDeprecated = true;
		NewColours.Add(Pair.Key, Kept);
		Result.Deprecated++;
	}

	// --- commit ------------------------------------------------------------
	Asset->Colours = NewColours;
	Asset->SchemaVersion = Doc.Schema;
	Asset->FigmaFileKey = Doc.FileKey;
	Asset->FigmaFileName = Doc.FileName;
	Asset->ReadMode = Doc.ReadMode;
	Asset->ModeId = Doc.ModeId;
	Asset->ExportedAt = Doc.ExportedAt;
	Asset->ImportedAt = FDateTime::UtcNow().ToIso8601();
	Asset->LinkName = Doc.LinkName;
	Asset->TargetProjectId = Doc.TargetProjectId;
	Asset->TargetProjectName = Doc.TargetProjectName;
	Asset->PublishedSelection = Doc.PublishedSelection;
	Asset->LinkedBy = Doc.LinkedBy;

	Asset->MarkPackageDirty();

	const bool bNothingChanged = Result.Added == 0 && Result.Updated == 0
		&& Result.Renamed == 0 && Result.Deprecated == 0;

	if (bNothingChanged)
	{
		// Phase 1 will skip the Perforce checkout entirely in this case; for now
		// we still save so a first run always produces the asset on disk.
		UE_LOG(LogFigmaTokens, Log, TEXT("Tokens are already up to date (%d unchanged)."), Result.Unchanged);
	}

	const FString FileName = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	if (!UPackage::SavePackage(Package, Asset, *FileName, SaveArgs))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to save '%s'."), *FileName));
		return Result;
	}

	// First run convenience: point the runtime lookup at what we just generated,
	// otherwise every Blueprint call returns the fallback and looks broken.
	if (Settings->ActiveTokens.IsNull())
	{
		UDesignTokenSettings* Mutable = GetMutableDefault<UDesignTokenSettings>();
		Mutable->ActiveTokens = FSoftObjectPath(Asset);
		Mutable->TryUpdateDefaultConfigFile();
		UE_LOG(LogFigmaTokens, Log, TEXT("Set Active Tokens to '%s'."), *PackagePath);
	}

	// Anything holding the previous palette — open widget previews included —
	// should resolve against what was just written, not what it cached earlier.
	UDesignTokenLibrary::InvalidateDesignTokenCache();

	Result.Asset = Asset;
	Result.bSuccess = true;
	return Result;
}

FDesignTokenImportResult FDesignTokenImporter::ImportFromFile(const FString& JsonPath)
{
	FDesignTokenImportResult Result;

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *JsonPath))
	{
		Result.Errors.Add(FString::Printf(TEXT("Could not read '%s'."), *JsonPath));
		return Result;
	}

	FDesignTokenDocument Doc;
	if (!ParseDocument(JsonText, Doc, Result))
	{
		return Result;
	}

	FDesignTokenImportResult Written = WriteAsset(Doc);
	Written.Warnings.Append(Result.Warnings);
	return Written;
}

FDesignTokenImportResult FDesignTokenImporter::ImportFromSettings()
{
	FDesignTokenImportResult Result;

	const UDesignTokenSettings* Settings = UDesignTokenSettings::Get();
	const FString Path = Settings ? Settings->GetAbsoluteTokensFilePath() : FString();

	if (Path.IsEmpty())
	{
		Result.Errors.Add(TEXT("No tokens.json is configured. Set it in Project Settings > Plugins > Figma Token Bridge."));
		return Result;
	}

	return ImportFromFile(Path);
}

FDesignTokenImportResult FDesignTokenImporter::ImportFromText(const FString& JsonText, const FString& SaveToPath)
{
	FDesignTokenImportResult Result;

	FDesignTokenDocument Doc;
	if (!ParseDocument(JsonText, Doc, Result))
	{
		return Result;
	}

	if (!SaveToPath.IsEmpty())
	{
		// A failed write is not worth failing the sync over: the asset is what the
		// game uses. The file is for review, so say plainly that it is now stale.
		if (!FFileHelper::SaveStringToFile(FFigmaPublicationReader::PrettyPrint(JsonText), *SaveToPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			Result.Warnings.Add(FString::Printf(
				TEXT("Imported, but could not update '%s' (is it read-only or checked in?). It no longer matches the asset."),
				*SaveToPath));
		}
	}

	FDesignTokenImportResult Written = WriteAsset(Doc);
	Written.Warnings.Append(Result.Warnings);
	return Written;
}

#undef LOCTEXT_NAMESPACE
