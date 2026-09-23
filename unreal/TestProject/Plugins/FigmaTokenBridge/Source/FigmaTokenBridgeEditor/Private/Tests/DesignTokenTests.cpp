// Copyright Rocket Science.

#include "Misc/AutomationTest.h"

#include "DesignTokenImporter.h"
#include "DesignTokenSettings.h"
#include "DesignTokenTypes.h"
#include "DesignTokenLibrary.h"
#include "DesignTokens.h"
#include "FigmaPublication.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr float Tol = 1.e-5f;

	/**
	 * The Figma file this project is paired with.
	 *
	 * Must be read from settings for the same reason the project id is: the
	 * importer enforces both halves of the handshake, so a hardcoded value makes
	 * every test fail the moment someone legitimately pairs the project with a
	 * different Figma file — which is exactly what happened the first time this
	 * project was pointed at a real one. Falls back to a literal only when no
	 * pairing is configured, where the check is skipped anyway.
	 */
	FString TestFileKey()
	{
		const UDesignTokenSettings* Settings = UDesignTokenSettings::Get();
		return (Settings && !Settings->FigmaFileKey.IsEmpty())
			? Settings->FigmaFileKey
			: FString(TEXT("f7tYGbrdTli91GaFdgioLi"));
	}

	/**
	 * Tests build their own JSON around this project's real id and file key,
	 * because the importer verifies both and would otherwise refuse every document.
	 */
	FString MakeDoc(const FString& TargetProjectId, const FString& ColourEntries)
	{
		return FString::Printf(TEXT(R"JSON(
{
  "schema": "figma-token-bridge/1",
  "source": {
    "fileKey": "%s",
    "fileName": "FigmaUnrealTest",
    "readMode": "local",
    "defaultModeId": "3680:0",
    "modes": [ { "id": "3680:0", "name": "Value" } ],
    "exportedAt": "2026-08-19T00:00:00.000Z"
  },
  "target": {
    "projectId": "%s",
    "projectName": "TestProject",
    "linkName": "Automated test link",
    "published": [ "Component" ],
    "linkedBy": "automation"
  },
  "colours": [ %s ]
}
)JSON"), *TestFileKey(), *TargetProjectId, *ColourEntries);
	}

	const TCHAR* GoalsChain = TEXT(R"JSON(
    {
      "key": "5274de63adf74279ef2f9798292f1d251f387182",
      "name": "component.blades.accent_goals",
      "figmaName": "Component/Blades/accent-goals",
      "tier": "Component", "group": "Blades", "published": true,
      "aliasOf": "bb230bafa611a4d29d3a116b29cd981a356576a4",
      "resolvesTo": "2ca6d44b525b0c9e4aa7885be53c5f3c6c3f3259",
      "values": { "3680:0": { "srgbHex": "#00d86c", "alpha": 1 } },
      "own": null
    },
    {
      "key": "bb230bafa611a4d29d3a116b29cd981a356576a4",
      "name": "semantic.accent_green",
      "figmaName": "Semantic/accent-green",
      "tier": "Semantic", "group": "", "published": false,
      "aliasOf": "2ca6d44b525b0c9e4aa7885be53c5f3c6c3f3259",
      "resolvesTo": "2ca6d44b525b0c9e4aa7885be53c5f3c6c3f3259",
      "values": { "3680:0": { "srgbHex": "#00d86c", "alpha": 1 } },
      "own": null
    },
    {
      "key": "2ca6d44b525b0c9e4aa7885be53c5f3c6c3f3259",
      "name": "global.green_400",
      "figmaName": "Global/green-400",
      "tier": "Global", "group": "", "published": false,
      "aliasOf": null,
      "resolvesTo": "2ca6d44b525b0c9e4aa7885be53c5f3c6c3f3259",
      "values": { "3680:0": { "srgbHex": "#00d86c", "alpha": 1 } },
      "own": { "3680:0": { "srgbHex": "#00d86c", "alpha": 1 } }
    },
    {
      "key": "5dda8aa23cd0105fb8f8ff9c415a4d9ca2eb64f2",
      "name": "component.blades.bg_blade",
      "figmaName": "Component/Blades/bg-blade",
      "tier": "Component", "group": "Blades", "published": true,
      "aliasOf": "4a3f4a37b6a84f09854669a1518867b5f23df0d0",
      "resolvesTo": "42db26238db1bc8d494c9799324b01fbda2f5e6f",
      "values": { "3680:0": { "srgbHex": "#1c1b1a", "alpha": 0.8 } },
      "own": null
    })JSON");

}

// ---------------------------------------------------------------------------
// Colour space. The headline Phase 0 guarantee: if this passes, the whole
// colour path is probably right; if the pipeline ever regresses, this catches it.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignTokenColourTest,
	"FigmaTokenBridge.Colour.SrgbToLinear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesignTokenColourTest::RunTest(const FString& Parameters)
{
	FLinearColor Out;

	// The Goals accent. Naively copying the bytes would give
	// (0, 0.847059, 0.423529) — 19% out in green and 2.8x in blue.
	TestTrue(TEXT("#00d86c parses"), FDesignTokenColour::ParseSrgbHex(TEXT("#00d86c"), 1.0f, Out));
	TestNearlyEqual(TEXT("#00d86c red"),   Out.R, 0.000000f, Tol);
	TestNearlyEqual(TEXT("#00d86c green"), Out.G, 0.686685f, Tol);
	TestNearlyEqual(TEXT("#00d86c blue"),  Out.B, 0.149960f, Tol);
	TestNearlyEqual(TEXT("#00d86c alpha"), Out.A, 1.000000f, Tol);

	// Endpoints must be exact, not merely close.
	TestTrue(TEXT("white parses"), FDesignTokenColour::ParseSrgbHex(TEXT("#ffffff"), 1.0f, Out));
	TestNearlyEqual(TEXT("white is 1.0"), Out.R, 1.0f, Tol);

	TestTrue(TEXT("black parses"), FDesignTokenColour::ParseSrgbHex(TEXT("#000000"), 1.0f, Out));
	TestNearlyEqual(TEXT("black is 0.0"), Out.R, 0.0f, Tol);

	// 0x0a/255 = 0.0392 is below the 0.04045 knee, so the linear segment applies.
	TestTrue(TEXT("below-knee parses"), FDesignTokenColour::ParseSrgbHex(TEXT("#0a0a0a"), 1.0f, Out));
	TestNearlyEqual(TEXT("below-knee uses the linear segment"), Out.R, (10.0f / 255.0f) / 12.92f, Tol);

	// Alpha is already linear and must pass through untouched. If the transfer
	// function ever leaks onto it, 0.8 would arrive as roughly 0.6.
	TestTrue(TEXT("alpha token parses"), FDesignTokenColour::ParseSrgbHex(TEXT("#1c1b1a"), 0.8f, Out));
	TestNearlyEqual(TEXT("alpha is untransformed"), Out.A, 0.8f, Tol);

	// Leading hash is optional; anything else is rejected rather than guessed at.
	TestTrue(TEXT("bare hex parses"), FDesignTokenColour::ParseSrgbHex(TEXT("00d86c"), 1.0f, Out));
	TestFalse(TEXT("short hex rejected"), FDesignTokenColour::ParseSrgbHex(TEXT("#fff"), 1.0f, Out));
	TestFalse(TEXT("8-digit hex rejected"), FDesignTokenColour::ParseSrgbHex(TEXT("#00d86cff"), 1.0f, Out));
	TestFalse(TEXT("non-hex rejected"), FDesignTokenColour::ParseSrgbHex(TEXT("#00d8zz"), 1.0f, Out));
	TestFalse(TEXT("empty rejected"), FDesignTokenColour::ParseSrgbHex(TEXT(""), 1.0f, Out));

	return true;
}

// ---------------------------------------------------------------------------
// Document parsing
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignTokenParseTest,
	"FigmaTokenBridge.Import.ParseDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesignTokenParseTest::RunTest(const FString& Parameters)
{
	FDesignTokenDocument Doc;
	FDesignTokenImportResult Result;

	const FString MyId = UDesignTokenSettings::GetOrCreateProjectId();
	const bool bParsed = FDesignTokenImporter::ParseDocument(MakeDoc(MyId, GoalsChain), Doc, Result);

	TestTrue(TEXT("document parses"), bParsed);
	TestEqual(TEXT("no errors"), Result.Errors.Num(), 0);
	TestEqual(TEXT("token count"), Doc.Colours.Num(), 4);
	TestEqual(TEXT("file key carried through"), Doc.FileKey, TestFileKey());
	TestEqual(TEXT("read mode carried through"), Doc.ReadMode, FString(TEXT("local")));

	// The designer's link comes through intact.
	TestEqual(TEXT("link name carried through"), Doc.LinkName, FString(TEXT("Automated test link")));
	TestEqual(TEXT("target project name carried through"), Doc.TargetProjectName, FString(TEXT("TestProject")));
	TestEqual(TEXT("published selection carried through"), Doc.PublishedSelection.Num(), 1);
	if (Doc.PublishedSelection.Num() == 1)
	{
		TestEqual(TEXT("published selection is the Component tier"), Doc.PublishedSelection[0], FString(TEXT("Component")));
	}

	const FDesignColourToken* Goals = Doc.Colours.Find(TEXT("component.blades.accent_goals"));
	if (!Goals)
	{
		AddError(TEXT("component.blades.accent_goals is missing"));
		return false;
	}

	TestEqual(TEXT("tier parsed"), static_cast<int32>(Goals->Tier), static_cast<int32>(EDesignTokenTier::Component));
	TestEqual(TEXT("group parsed"), Goals->Group, FString(TEXT("Blades")));
	TestNearlyEqual(TEXT("colour converted to linear"), Goals->Colour.G, 0.686685f, Tol);

	// Alias keys should have been rewritten into readable token names.
	TestEqual(TEXT("alias rewritten to a token name"), Goals->AliasOf, FName(TEXT("semantic.accent_green")));
	TestEqual(TEXT("resolves to the primitive"), Goals->ResolvesTo, FName(TEXT("global.green_400")));

	// Publishing gates the picker, not the import. The tiers a Component token
	// depends on arrive unpublished but present, or nothing would resolve.
	TestTrue(TEXT("Component token is published"), Goals->bPublished);
	if (const FDesignColourToken* Semantic = Doc.Colours.Find(TEXT("semantic.accent_green")))
	{
		TestFalse(TEXT("Semantic token is imported but unpublished"), Semantic->bPublished);
	}
	else
	{
		AddError(TEXT("semantic.accent_green should still be imported even though it is unpublished"));
	}

	// A primitive resolves to itself and keeps its own literal.
	const FDesignColourToken* Primitive = Doc.Colours.Find(TEXT("global.green_400"));
	if (Primitive)
	{
		TestEqual(TEXT("primitive has no alias parent"), Primitive->AliasOf, FName(NAME_None));
		TestEqual(TEXT("primitive resolves to itself"), Primitive->ResolvesTo, FName(TEXT("global.green_400")));
	}

	// Alpha survives the alias chain.
	const FDesignColourToken* Blade = Doc.Colours.Find(TEXT("component.blades.bg_blade"));
	if (Blade)
	{
		TestNearlyEqual(TEXT("80% alpha preserved"), Blade->Colour.A, 0.8f, Tol);
	}

	// An alias target absent from the export warns but does not fail — normal in
	// consumer mode, where a parent nothing binds is not captured.
	TestTrue(TEXT("missing alias target warns rather than errors"),
		Result.Errors.Num() == 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignTokenRejectionTest,
	"FigmaTokenBridge.Import.RejectsBadInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesignTokenRejectionTest::RunTest(const FString& Parameters)
{
	{
		FDesignTokenDocument Doc;
		FDesignTokenImportResult Result;
		TestFalse(TEXT("rejects non-JSON"),
			FDesignTokenImporter::ParseDocument(TEXT("not json at all"), Doc, Result));
		TestTrue(TEXT("reports a parse error"), Result.Errors.Num() > 0);
	}

	{
		FDesignTokenDocument Doc;
		FDesignTokenImportResult Result;
		TestFalse(TEXT("rejects an unknown schema"),
			FDesignTokenImporter::ParseDocument(
				TEXT(R"JSON({"schema":"figma-token-bridge/99","source":{},"colours":[]})JSON"), Doc, Result));
		TestTrue(TEXT("names the schema in the error"),
			Result.Errors.Num() > 0 && Result.Errors[0].Contains(TEXT("schema")));
	}

	{
		// No source block means the Figma file cannot be verified, so refuse.
		FDesignTokenDocument Doc;
		FDesignTokenImportResult Result;
		TestFalse(TEXT("rejects a missing source block"),
			FDesignTokenImporter::ParseDocument(
				TEXT(R"JSON({"schema":"figma-token-bridge/1","colours":[]})JSON"), Doc, Result));
	}

	{
		// A malformed colour is an error, not a silently substituted default.
		FDesignTokenDocument Doc;
		FDesignTokenImportResult Result;
		const TCHAR* BadEntry = TEXT(R"JSON(
    { "key": "k1", "name": "global.broken", "figmaName": "Global/broken", "tier": "Global",
      "published": false,
      "values": { "3680:0": { "srgbHex": "not-a-colour", "alpha": 1 } } })JSON");

		TestFalse(TEXT("rejects an unparseable colour"),
			FDesignTokenImporter::ParseDocument(
				MakeDoc(UDesignTokenSettings::GetOrCreateProjectId(), BadEntry), Doc, Result));
		TestTrue(TEXT("names the offending token"),
			Result.Errors.Num() > 0 && Result.Errors[0].Contains(TEXT("global.broken")));
	}

	return true;
}

// ---------------------------------------------------------------------------
// The handshake. The designer names the project an export is for; this checks
// that we are it. Without this, tokens land wherever someone happens to import
// them, and the designer has no say in where their work goes.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignTokenHandshakeTest,
	"FigmaTokenBridge.Import.ProjectHandshake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesignTokenHandshakeTest::RunTest(const FString& Parameters)
{
	const FString MyId = UDesignTokenSettings::GetOrCreateProjectId();
	TestTrue(TEXT("this project has an id"), UDesignTokenSettings::NormaliseProjectId(MyId).Len() == 32);
	TestEqual(TEXT("the id is stable across calls"), UDesignTokenSettings::GetOrCreateProjectId(), MyId);

	// Formatting must not matter — a designer may paste with or without hyphens.
	TestEqual(TEXT("hyphens are ignored"),
		UDesignTokenSettings::NormaliseProjectId(TEXT("a1b2c3d4-e5f6-7890-abcd-ef1234567890")),
		UDesignTokenSettings::NormaliseProjectId(TEXT("A1B2C3D4E5F67890ABCDEF1234567890")));
	TestEqual(TEXT("braces are ignored"),
		UDesignTokenSettings::NormaliseProjectId(TEXT("{a1b2c3d4-e5f6-7890-abcd-ef1234567890}")),
		UDesignTokenSettings::NormaliseProjectId(TEXT("A1B2C3D4E5F67890ABCDEF1234567890")));

	// An export for a different project is refused, and the message names both
	// ids so whoever hit it can tell which end is wrong.
	{
		FDesignTokenDocument Doc;
		FDesignTokenImportResult Result;
		const FString OtherId = TEXT("FFFFFFFF-FFFF-4FFF-8FFF-FFFFFFFFFFFF");

		TestFalse(TEXT("refuses an export aimed at another project"),
			FDesignTokenImporter::ParseDocument(MakeDoc(OtherId, GoalsChain), Doc, Result));

		const bool bNamesBoth = Result.Errors.Num() > 0
			&& Result.Errors[0].Contains(OtherId)
			&& Result.Errors[0].Contains(MyId);
		TestTrue(TEXT("the error names both project ids"), bNamesBoth);
	}

	// An export with no link at all is refused, and the message hands over the id
	// the designer needs rather than making them hunt for it.
	{
		FDesignTokenDocument Doc;
		FDesignTokenImportResult Result;
		const FString NoTarget = FString::Printf(TEXT(R"JSON(
{
  "schema": "figma-token-bridge/1",
  "source": { "fileKey": "%s", "fileName": "x", "readMode": "local", "defaultModeId": "3680:0" },
  "colours": [ %s ]
}
)JSON"), *TestFileKey(), GoalsChain);

		TestFalse(TEXT("refuses an unlinked export"),
			FDesignTokenImporter::ParseDocument(NoTarget, Doc, Result));
		TestTrue(TEXT("the error offers this project's id"),
			Result.Errors.Num() > 0 && Result.Errors[0].Contains(MyId));
	}

	// The same export accepted when it does target this project — the positive
	// case, so a passing suite cannot be explained by everything being refused.
	{
		FDesignTokenDocument Doc;
		FDesignTokenImportResult Result;
		TestTrue(TEXT("accepts an export aimed at this project"),
			FDesignTokenImporter::ParseDocument(MakeDoc(MyId, GoalsChain), Doc, Result));
		TestEqual(TEXT("and records which project it was for"),
			UDesignTokenSettings::NormaliseProjectId(Doc.TargetProjectId),
			UDesignTokenSettings::NormaliseProjectId(MyId));
	}

	return true;
}


// ---------------------------------------------------------------------------
// Writing the asset.
//
// This is the half that had never run. CreatePackage, NewObject,
// FAssetRegistryModule::AssetCreated and UPackage::SavePackage are the most
// version-fragile calls in the plugin, and the diff that decides added vs
// updated vs renamed vs deprecated is what every re-sync depends on.
//
// The asset name carries a GUID and everything the test touches is restored on
// the way out, so the test cannot collide with a previous run and cannot leave
// the project pointing at a scratch asset.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignTokenWriteAssetTest,
	"FigmaTokenBridge.Import.WriteAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesignTokenWriteAssetTest::RunTest(const FString& Parameters)
{
	UDesignTokenSettings* Settings = GetMutableDefault<UDesignTokenSettings>();
	if (!Settings)
	{
		AddError(TEXT("Settings CDO unavailable."));
		return false;
	}

	const FString OldPath = Settings->GeneratedPackagePath;
	const FString OldName = Settings->TokensAssetName;
	const FSoftObjectPath OldActive = Settings->ActiveTokens;

	Settings->GeneratedPackagePath = TEXT("/Game/DesignSystem/Generated/AutomationTest");
	Settings->TokensAssetName = FString::Printf(TEXT("DA_TokensTest_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));

	const FString PackagePath = Settings->GetTokensAssetPackagePath();

	ON_SCOPE_EXIT
	{
		Settings->GeneratedPackagePath = OldPath;
		Settings->TokensAssetName = OldName;
		Settings->ActiveTokens = OldActive;
		UDesignTokenLibrary::InvalidateDesignTokenCache();

		// Restoring the CDO is not enough. WriteAsset persists settings via
		// TryUpdateDefaultConfigFile, which serialises the WHOLE section — so the
		// scratch package path and asset name have already been written into
		// DefaultGame.ini by the time we get here. Without this second save the
		// test permanently repoints the project at an asset it then deletes.
		Settings->TryUpdateDefaultConfigFile();

		const FString File = FPackageName::LongPackageNameToFilename(
			PackagePath, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().Delete(*File, false, true, true);
	};

	const FString MyId = UDesignTokenSettings::GetOrCreateProjectId();

	FDesignTokenDocument Doc;
	FDesignTokenImportResult Parse;
	if (!FDesignTokenImporter::ParseDocument(MakeDoc(MyId, GoalsChain), Doc, Parse))
	{
		AddError(TEXT("Setup failed: the fixture document did not parse."));
		return false;
	}

	// --- first write: everything is new ------------------------------------
	{
		const FDesignTokenImportResult R = FDesignTokenImporter::WriteAsset(Doc);

		TestTrue(TEXT("first write succeeds"), R.bSuccess);
		if (!R.bSuccess)
		{
			AddError(R.Errors.Num() ? R.Errors[0] : TEXT("no error reported"));
			return false;
		}

		TestEqual(TEXT("all four tokens added"), R.Added, 4);
		TestEqual(TEXT("nothing updated on a first write"), R.Updated, 0);
		TestEqual(TEXT("nothing deprecated on a first write"), R.Deprecated, 0);

		if (!R.Asset)
		{
			AddError(TEXT("write reported success but produced no asset."));
			return false;
		}

		TestEqual(TEXT("asset holds every token"), R.Asset->Colours.Num(), 4);
		TestEqual(TEXT("provenance carried onto the asset"), R.Asset->FigmaFileKey, TestFileKey());
		TestEqual(TEXT("the link name is recorded"), R.Asset->LinkName,
			FString(TEXT("Automated test link")));

		// The headline colour reaches the asset, not merely the parser.
		FLinearColor Colour;
		if (R.Asset->FindColour(TEXT("component.blades.accent_goals"), Colour))
		{
			TestNearlyEqual(TEXT("green channel on the asset"), Colour.G, 0.686685f, Tol);
			TestNearlyEqual(TEXT("blue channel on the asset"), Colour.B, 0.149960f, Tol);
		}
		else
		{
			AddError(TEXT("accent_goals is missing from the written asset."));
		}

		// Publishing gates the picker: the two Component tokens are published,
		// their Semantic and Global parents are imported but not offered.
		TestEqual(TEXT("picker offers published tokens only"),
			R.Asset->GetPublishedTokenNames().Num(), 2);

		// The package really reached disk, rather than only existing in memory.
		const FString File = FPackageName::LongPackageNameToFilename(
			PackagePath, FPackageName::GetAssetPackageExtension());
		TestTrue(TEXT("package written to disk"), IFileManager::Get().FileExists(*File));

		// A first import points the runtime lookup at what it just generated,
		// otherwise every Blueprint call returns the fallback and looks broken.
		TestFalse(TEXT("active tokens set on first import"), Settings->ActiveTokens.IsNull());
	}

	// --- second write, identical content: a no-op --------------------------
	{
		const FDesignTokenImportResult R = FDesignTokenImporter::WriteAsset(Doc);

		TestTrue(TEXT("re-write succeeds"), R.bSuccess);
		TestEqual(TEXT("re-writing identical content adds nothing"), R.Added, 0);
		TestEqual(TEXT("re-writing identical content updates nothing"), R.Updated, 0);
		TestEqual(TEXT("everything is unchanged"), R.Unchanged, 4);
	}

	// --- a token disappears from Figma: kept, flagged, never dropped -------
	{
		FDesignTokenDocument Shrunk = Doc;
		Shrunk.Colours.Remove(TEXT("component.blades.bg_blade"));

		const FDesignTokenImportResult R = FDesignTokenImporter::WriteAsset(Shrunk);

		TestTrue(TEXT("write with a missing token still succeeds"), R.bSuccess);
		TestEqual(TEXT("the absent token is deprecated, not deleted"), R.Deprecated, 1);

		if (R.Asset)
		{
			TestEqual(TEXT("token count unchanged — nothing was dropped"),
				R.Asset->Colours.Num(), 4);

			FDesignColourToken Kept;
			if (R.Asset->FindToken(TEXT("component.blades.bg_blade"), Kept))
			{
				TestTrue(TEXT("it is marked deprecated"), Kept.bDeprecated);
				TestNearlyEqual(TEXT("it still resolves to its last known alpha"),
					Kept.Colour.A, 0.8f, Tol);
			}
			else
			{
				AddError(TEXT("a token vanished instead of being deprecated."));
			}

			TestFalse(TEXT("deprecated tokens leave the picker"),
				R.Asset->GetPublishedTokenNames().Contains(TEXT("component.blades.bg_blade")));
		}
	}

	// --- a token is renamed in Figma: matched by key, not by name ----------
	{
		FDesignTokenDocument Renamed = Doc;
		FDesignColourToken Moved = Renamed.Colours.FindChecked(TEXT("component.blades.accent_goals"));
		Renamed.Colours.Remove(TEXT("component.blades.accent_goals"));
		Moved.FigmaName = TEXT("Component/Blades/accent-objectives");
		Renamed.Colours.Add(TEXT("component.blades.accent_objectives"), Moved);

		const FDesignTokenImportResult R = FDesignTokenImporter::WriteAsset(Renamed);

		TestTrue(TEXT("write after a rename succeeds"), R.bSuccess);
		// Identity is the Figma key, so this is one rename — not an add plus a
		// deprecation, which is what a name-keyed importer would report.
		TestEqual(TEXT("reported as a rename"), R.Renamed, 1);
		TestEqual(TEXT("not reported as an addition"), R.Added, 0);
	}

	// --- the struct UMG widgets actually store ---------------------------
	//
	// FDesignTokenColourRef is what a TokenBorder/TokenImage/TokenTextBlock saves
	// instead of an RGBA value, so if it resolves wrongly every token-aware widget
	// in the project is wrong at once. Pointed at the asset written above rather
	// than whatever the project happens to have active.
	{
		const FDesignTokenImportResult R = FDesignTokenImporter::WriteAsset(Doc);
		if (R.bSuccess && R.Asset)
		{
			Settings->ActiveTokens = FSoftObjectPath(R.Asset);
			UDesignTokenLibrary::InvalidateDesignTokenCache();

			const FLinearColor Sentinel(0.f, 0.f, 0.f, 0.f);

			// Unset is not an error: the widget keeps whatever colour was set by
			// hand, which is what makes adding the property non-destructive.
			FDesignTokenColourRef Unset;
			TestFalse(TEXT("an empty ref is not set"), Unset.IsSet());
			TestEqual(TEXT("an empty ref returns the fallback untouched"),
				Unset.Resolve(Sentinel), Sentinel);

			FDesignTokenColourRef Good;
			Good.Token = TEXT("component.blades.accent_goals");
			TestTrue(TEXT("a named ref is set"), Good.IsSet());

			const FLinearColor Resolved = Good.Resolve();
			TestNearlyEqual(TEXT("ref resolves the green channel"), Resolved.G, 0.686685f, Tol);
			TestNearlyEqual(TEXT("ref resolves the blue channel"), Resolved.B, 0.149960f, Tol);

			// The Slate-typed overload is what UTokenTextBlock uses, and it must not
			// quietly differ from the linear one.
			TestNearlyEqual(TEXT("the Slate overload agrees"),
				Good.ResolveSlate().GetSpecifiedColor().G, Resolved.G, Tol);

			FDesignColourToken Info;
			if (Good.ResolveToken(Info))
			{
				TestEqual(TEXT("ref exposes the Figma name for the details panel"),
					Info.FigmaName, FString(TEXT("Component/Blades/accent-goals")));
				TestEqual(TEXT("ref exposes the alias parent"),
					Info.AliasOf, FName(TEXT("semantic.accent_green")));
			}
			else
			{
				AddError(TEXT("ResolveToken failed for a token that is present."));
			}

			// A typo must be loud. Magenta is the deliberate choice here.
			FDesignTokenColourRef Bogus;
			Bogus.Token = TEXT("component.blades.no_such_token");
			TestEqual(TEXT("an unknown token falls back to magenta"),
				Bogus.Resolve(), FLinearColor(1.f, 0.f, 1.f, 1.f));
			TestFalse(TEXT("an unknown token has no record"), Bogus.ResolveToken(Info));
		}
		else
		{
			AddError(TEXT("Could not write an asset to resolve refs against."));
		}
	}

	// The test has been mutating settings that live in DefaultGame.ini. Prove
	// the file on disk ends up back where it started, because a test that
	// silently rewrites project config is worse than no test.
	{
		Settings->GeneratedPackagePath = OldPath;
		Settings->TokensAssetName = OldName;
		Settings->ActiveTokens = OldActive;
		Settings->TryUpdateDefaultConfigFile();
		UDesignTokenLibrary::InvalidateDesignTokenCache();

		const UDesignTokenSettings* Reloaded = GetDefault<UDesignTokenSettings>();
		TestEqual(TEXT("generated package path restored"), Reloaded->GeneratedPackagePath, OldPath);
		TestEqual(TEXT("asset name restored"), Reloaded->TokensAssetName, OldName);
		TestEqual(TEXT("active tokens restored"), Reloaded->ActiveTokens.ToString(), OldActive.ToString());
	}

	return true;
}

// ---------------------------------------------------------------------------
// Pulling a publish out of the Figma file.
//
// The plugin splits the export into chunks and checksums it (planPublication in
// figma-plugin/code.js); this reverses it. The two are written in different
// languages, so the checksum vectors below are the same ones tools/
// build-tokens.cjs asserts. If either side drifts, both suites fail.
//
// The REST response is built here rather than captured, so the test needs no
// network and no Figma account.
// ---------------------------------------------------------------------------

namespace
{
	FString Condensed(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	/** A manifest as the plugin writes it, for a document split into ChunkKeys. */
	FString MakeManifest(const FString& LinkId, const FString& ProjectId, const FString& PublishedAt,
		const TArray<FString>& ChunkKeys, const FString& FullText)
	{
		const TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("schema"), FFigmaPublicationReader::Schema);
		M->SetStringField(TEXT("linkId"), LinkId);
		M->SetStringField(TEXT("linkName"), LinkId + TEXT(" name"));
		M->SetStringField(TEXT("projectId"), ProjectId);
		M->SetStringField(TEXT("projectName"), TEXT("TestProject"));
		M->SetStringField(TEXT("publishId"), TEXT("1000"));
		M->SetStringField(TEXT("publishedAt"), PublishedAt);
		M->SetStringField(TEXT("publishedBy"), TEXT("automation"));
		M->SetStringField(TEXT("exportedAt"), TEXT("2026-08-19T00:00:00.000Z"));

		TArray<TSharedPtr<FJsonValue>> Keys;
		for (const FString& K : ChunkKeys)
		{
			Keys.Add(MakeShared<FJsonValueString>(K));
		}
		M->SetArrayField(TEXT("chunks"), Keys);
		M->SetNumberField(TEXT("length"), FullText.Len());
		M->SetStringField(TEXT("checksum"), FFigmaPublicationReader::Fnv1a32(FullText));
		return Condensed(M);
	}

	/** GET /v1/files/:key?plugin_data=shared, reduced to the part we read. */
	FString MakeFileResponse(const TMap<FString, FString>& Entries)
	{
		const TSharedRef<FJsonObject> Ns = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& E : Entries)
		{
			Ns->SetStringField(E.Key, E.Value);
		}
		const TSharedRef<FJsonObject> Shared = MakeShared<FJsonObject>();
		Shared->SetObjectField(FFigmaPublicationReader::Namespace, Ns);

		const TSharedRef<FJsonObject> Document = MakeShared<FJsonObject>();
		Document->SetStringField(TEXT("type"), TEXT("DOCUMENT"));
		Document->SetObjectField(TEXT("sharedPluginData"), Shared);

		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("name"), TEXT("Test file"));
		Root->SetObjectField(TEXT("document"), Document);
		return Condensed(Root);
	}

	/** Split Text into three chunks for LinkId and add them, plus a manifest. */
	void AddPublication(TMap<FString, FString>& Entries, const FString& LinkId, const FString& ProjectId,
		const FString& PublishedAt, const FString& Text)
	{
		const int32 Third = Text.Len() / 3;
		const TArray<FString> Parts = { Text.Left(Third), Text.Mid(Third, Third), Text.Mid(2 * Third) };
		TArray<FString> Keys;
		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			const FString Key = FString::Printf(TEXT("chunk/%s/1000/%d"), *LinkId, i);
			Entries.Add(Key, Parts[i]);
			Keys.Add(Key);
		}
		Entries.Add(TEXT("manifest/") + LinkId, MakeManifest(LinkId, ProjectId, PublishedAt, Keys, Text));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignTokenPublicationTest,
	"FigmaTokenBridge.Publication.PullFromFigma",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesignTokenPublicationTest::RunTest(const FString& Parameters)
{
	// Same vectors as tools/build-tokens.cjs.
	TestEqual(TEXT("fnv1a32 of the empty string"), FFigmaPublicationReader::Fnv1a32(TEXT("")), TEXT("811c9dc5"));
	TestEqual(TEXT("fnv1a32 of \"a\""), FFigmaPublicationReader::Fnv1a32(TEXT("a")), TEXT("e40c292c"));
	TestEqual(TEXT("fnv1a32 of \"foobar\""), FFigmaPublicationReader::Fnv1a32(TEXT("foobar")), TEXT("bf9cf968"));

	const FString MyId = UDesignTokenSettings::GetOrCreateProjectId();
	const FString OtherId = TEXT("FFFFFFFF-FFFF-4FFF-8FFF-FFFFFFFFFFFF");
	const FString DocText = MakeDoc(MyId, GoalsChain);

	// The round trip, ending in the importer so the pulled text is proven usable.
	{
		TMap<FString, FString> Entries;
		AddPublication(Entries, TEXT("link-1"), MyId, TEXT("2026-09-23T10:00:00.000Z"), DocText);
		Entries.Add(TEXT("links"), TEXT("{\"version\":1,\"links\":[]}"));  // the plugin's own config, ignored

		FFigmaPublication P;
		TArray<FString> Errors, Warnings;
		TestTrue(TEXT("reads this project's publish"),
			FFigmaPublicationReader::ReadFromFileResponse(MakeFileResponse(Entries), MyId, P, Errors, Warnings));
		TestEqual(TEXT("reassembles the export exactly"), P.DocumentJson, DocText);
		TestEqual(TEXT("carries who published"), P.PublishedBy, FString(TEXT("automation")));
		TestEqual(TEXT("carries exportedAt for change detection"), P.ExportedAt, FString(TEXT("2026-08-19T00:00:00.000Z")));

		FDesignTokenDocument Doc;
		FDesignTokenImportResult Result;
		TestTrue(TEXT("the pulled export passes the importer"), FDesignTokenImporter::ParseDocument(P.DocumentJson, Doc, Result));
	}

	// A missing chunk is refused, never imported short.
	{
		TMap<FString, FString> Entries;
		AddPublication(Entries, TEXT("link-1"), MyId, TEXT("2026-09-23T10:00:00.000Z"), DocText);
		Entries.Remove(TEXT("chunk/link-1/1000/1"));

		FFigmaPublication P;
		TArray<FString> Errors, Warnings;
		TestFalse(TEXT("refuses a publish with a missing chunk"),
			FFigmaPublicationReader::ReadFromFileResponse(MakeFileResponse(Entries), MyId, P, Errors, Warnings));
		TestTrue(TEXT("and says it is incomplete"), Errors.Num() > 0 && Errors[0].Contains(TEXT("incomplete")));
	}

	// A changed chunk fails the checksum.
	{
		TMap<FString, FString> Entries;
		AddPublication(Entries, TEXT("link-1"), MyId, TEXT("2026-09-23T10:00:00.000Z"), DocText);
		FString& First = Entries.FindChecked(TEXT("chunk/link-1/1000/0"));
		First[First.Len() - 1] = First[First.Len() - 1] == TEXT('x') ? TEXT('y') : TEXT('x');

		FFigmaPublication P;
		TArray<FString> Errors, Warnings;
		TestFalse(TEXT("refuses a corrupted publish"),
			FFigmaPublicationReader::ReadFromFileResponse(MakeFileResponse(Entries), MyId, P, Errors, Warnings));
		TestTrue(TEXT("and says why"), Errors.Num() > 0 && Errors[0].Contains(TEXT("checksum")));
	}

	// Published for someone else only: the error names what IS there.
	{
		TMap<FString, FString> Entries;
		AddPublication(Entries, TEXT("link-other"), OtherId, TEXT("2026-09-23T10:00:00.000Z"), DocText);

		FFigmaPublication P;
		TArray<FString> Errors, Warnings;
		TestFalse(TEXT("finds nothing for this project"),
			FFigmaPublicationReader::ReadFromFileResponse(MakeFileResponse(Entries), MyId, P, Errors, Warnings));
		TestTrue(TEXT("and names both project ids"),
			Errors.Num() > 0 && Errors[0].Contains(OtherId) && Errors[0].Contains(MyId));
	}

	// Nothing published at all.
	{
		FFigmaPublication P;
		TArray<FString> Errors, Warnings;
		TestFalse(TEXT("reports an unpublished file"),
			FFigmaPublicationReader::ReadFromFileResponse(TEXT("{\"name\":\"Test file\",\"document\":{\"type\":\"DOCUMENT\"}}"),
				MyId, P, Errors, Warnings));
		TestTrue(TEXT("and tells the reader to publish"),
			Errors.Num() > 0 && Errors[0].Contains(TEXT("Publish to Unreal")));
	}

	// Two links aimed here: the newest wins, and the ambiguity is reported.
	{
		TMap<FString, FString> Entries;
		AddPublication(Entries, TEXT("link-old"), MyId, TEXT("2026-09-20T10:00:00.000Z"), DocText);
		AddPublication(Entries, TEXT("link-new"), MyId, TEXT("2026-09-23T10:00:00.000Z"), DocText);

		FFigmaPublication P;
		TArray<FString> Errors, Warnings;
		TestTrue(TEXT("reads one of two publishes"),
			FFigmaPublicationReader::ReadFromFileResponse(MakeFileResponse(Entries), MyId, P, Errors, Warnings));
		TestEqual(TEXT("picks the most recent"), P.LinkId, FString(TEXT("link-new")));
		TestTrue(TEXT("warns about the duplicate link"), Warnings.Num() > 0);
	}

	// The pulled file must diff cleanly against a downloaded one, which is
	// JSON.stringify(doc, null, 2). Expected output generated by Node.
	TestEqual(TEXT("pretty-prints like JSON.stringify(..., null, 2)"),
		FFigmaPublicationReader::PrettyPrint(TEXT("{\"a\":1,\"b\":[0.8,\"x,y:{\"],\"c\":{},\"d\":[],\"e\":{\"f\":\"q\\\"}\"}}")),
		FString(TEXT("{\n  \"a\": 1,\n  \"b\": [\n    0.8,\n    \"x,y:{\"\n  ],\n  \"c\": {},\n  \"d\": [],\n  \"e\": {\n    \"f\": \"q\\\"}\"\n  }\n}\n")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
