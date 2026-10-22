// Copyright Rocket Science.

#include "Misc/AutomationTest.h"

#include "DesignTokenImporter.h"
#include "DesignTokenSettings.h"
#include "DesignTokenTypes.h"
#include "DesignTokenLibrary.h"
#include "DesignTokens.h"
#include "FigmaLiveLink.h"
#include "FigmaPublication.h"
#include "FigmaTokenBridgeReport.h"
#include "WidgetPreviewRefresher.h"

#include "Algo/AllOf.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
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

/**
 * An import whose content has not changed must write nothing: no asset save, no
 * tokens.json rewrite. Re-exports only change exportedAt, and under Perforce a
 * pointless write means a pointless checkout of a binary asset.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignTokenNoOpTest,
	"FigmaTokenBridge.Import.NoOpWhenUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesignTokenNoOpTest::RunTest(const FString& Parameters)
{
	const FString MyId = UDesignTokenSettings::GetOrCreateProjectId();
	const FString A = MakeDoc(MyId, GoalsChain);
	const FString B = A.Replace(TEXT("2026-08-19T00:00:00.000Z"), TEXT("2026-10-22T14:05:48.655Z"));
	const FString C = A.Replace(TEXT("#00d86c"), TEXT("#7b61ff"));
	const FString D = A.Replace(TEXT("Automated test link"), TEXT("Renamed link"));

	TestEqual(TEXT("fingerprint ignores exportedAt"),
		FDesignTokenImporter::ContentFingerprint(A), FDesignTokenImporter::ContentFingerprint(B));
	TestNotEqual(TEXT("fingerprint sees a colour"),
		FDesignTokenImporter::ContentFingerprint(A), FDesignTokenImporter::ContentFingerprint(C));
	TestEqual(TEXT("fingerprint ignores whitespace"),
		FDesignTokenImporter::ContentFingerprint(A), FDesignTokenImporter::ContentFingerprint(FFigmaPublicationReader::PrettyPrint(A)));
	TestEqual(TEXT("invalid JSON has no fingerprint"),
		FDesignTokenImporter::ContentFingerprint(TEXT("not json")), FString());
	TestFalse(TEXT("a fingerprint is not empty"), FDesignTokenImporter::ContentFingerprint(A).IsEmpty());

	UDesignTokenSettings* Settings = GetMutableDefault<UDesignTokenSettings>();
	const FString OldPath = Settings->GeneratedPackagePath;
	const FString OldName = Settings->TokensAssetName;
	const FSoftObjectPath OldActive = Settings->ActiveTokens;
	Settings->GeneratedPackagePath = TEXT("/Game/DesignSystem/Generated/AutomationTest");
	Settings->TokensAssetName = FString::Printf(TEXT("DA_TokensNoOp_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));

	const FString PackagePath = Settings->GetTokensAssetPackagePath();
	const FString AssetFile = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	const FString TempJson = FPaths::ProjectSavedDir() / TEXT("Automation") /
		FString::Printf(TEXT("tokens-%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));

	ON_SCOPE_EXIT
	{
		Settings->GeneratedPackagePath = OldPath;
		Settings->TokensAssetName = OldName;
		Settings->ActiveTokens = OldActive;
		UDesignTokenLibrary::InvalidateDesignTokenCache();
		// See FDesignTokenWriteAssetTest: WriteAsset saved the whole section to
		// DefaultGame.ini, so it has to be saved again with the real values.
		Settings->TryUpdateDefaultConfigFile();
		IFileManager::Get().Delete(*AssetFile, false, true, true);
		IFileManager::Get().Delete(*TempJson, false, true, true);
	};

	auto Parse = [this](const FString& Text)
	{
		FDesignTokenDocument Doc;
		FDesignTokenImportResult Unused;
		TestTrue(TEXT("fixture parses"), FDesignTokenImporter::ParseDocument(Text, Doc, Unused));
		return Doc;
	};

	// First write: everything is new.
	const FDesignTokenImportResult First = FDesignTokenImporter::WriteAsset(Parse(A));
	TestTrue(TEXT("first write saves the asset"), First.bWroteAsset);
	if (!First.Asset)
	{
		AddError(TEXT("first write produced no asset"));
		return false;
	}
	UDesignTokens* Asset = First.Asset;
	TestEqual(TEXT("the asset records the fingerprint"), Asset->ContentFingerprint, FDesignTokenImporter::ContentFingerprint(A));

	// Same tokens, re-exported later: nothing is written.
	const FDateTime Old(2000, 1, 1);
	IFileManager::Get().SetTimeStamp(*AssetFile, Old);
	const FString ImportedAtBefore = Asset->ImportedAt;
	{
		const FDesignTokenImportResult R = FDesignTokenImporter::WriteAsset(Parse(B));
		TestTrue(TEXT("unchanged import succeeds"), R.bSuccess);
		TestFalse(TEXT("unchanged import does not save"), R.bWroteAsset);
		TestEqual(TEXT("unchanged import counts every token"), R.Unchanged, 4);
		TestEqual(TEXT("the package file is untouched"), IFileManager::Get().GetTimeStamp(*AssetFile), Old);
		TestEqual(TEXT("ImportedAt is untouched"), Asset->ImportedAt, ImportedAtBefore);
		TestEqual(TEXT("summary says so"), R.Summary(), FString(TEXT("Already up to date (4 tokens unchanged)")));
	}

	// The package is still in memory but its file is gone (deleted, or a branch
	// switch with the editor open): a sync must put it back, not call it current.
	// A package loaded from disk keeps its file open on Windows until its loader
	// is reset, which is what the editor does before deleting a package's file.
	ResetLoaders(Asset->GetPackage());
	IFileManager::Get().Delete(*AssetFile, false, true, true);
	TestFalse(TEXT("the asset file is gone before the sync"), IFileManager::Get().FileExists(*AssetFile));
	{
		const FDesignTokenImportResult R = FDesignTokenImporter::WriteAsset(Parse(B));
		TestTrue(TEXT("a missing asset file is written again"), R.bWroteAsset);
		TestTrue(TEXT("and is back on disk"), IFileManager::Get().FileExists(*AssetFile));
	}

	// An asset written before fingerprints existed is written once.
	Asset->ContentFingerprint.Reset();
	TestTrue(TEXT("upgrade write"), FDesignTokenImporter::WriteAsset(Parse(B)).bWroteAsset);

	// Same tokens under a renamed link is a provenance change: written, no token changes.
	{
		const FDesignTokenImportResult R = FDesignTokenImporter::WriteAsset(Parse(D));
		TestTrue(TEXT("link rename writes"), R.bWroteAsset);
		TestEqual(TEXT("link rename changes no token"), R.Added + R.Updated + R.Renamed + R.Deprecated, 0);
	}

	// A token already deprecated is not a change the next time round.
	{
		const FString Chain(GoalsChain);
		const int32 KeyAt = Chain.Find(TEXT("\"key\": \"5dda8aa2"));
		const int32 BraceAt = Chain.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromEnd, KeyAt);
		const int32 CommaAt = Chain.Find(TEXT(","), ESearchCase::CaseSensitive, ESearchDir::FromEnd, BraceAt);
		const FString E = MakeDoc(MyId, Chain.Left(CommaAt));

		const FDesignTokenImportResult Dropped = FDesignTokenImporter::WriteAsset(Parse(E));
		TestTrue(TEXT("dropping a token writes"), Dropped.bWroteAsset);
		TestEqual(TEXT("and deprecates it"), Dropped.Deprecated, 1);

		const FDesignTokenImportResult Again = FDesignTokenImporter::WriteAsset(Parse(E));
		TestFalse(TEXT("an already-deprecated token does not force a write"), Again.bWroteAsset);
		TestEqual(TEXT("it is still reported as deprecated"), Again.Deprecated, 1);
	}

	// tokens.json is rewritten only when its content changes.
	{
		const FDesignTokenImportResult R1 = FDesignTokenImporter::ImportFromText(A, TempJson);
		TestTrue(TEXT("a missing tokens.json is written"), R1.bWroteTokensFile);

		const FDesignTokenImportResult R2 = FDesignTokenImporter::ImportFromText(B, TempJson);
		TestTrue(TEXT("re-export imports"), R2.bSuccess);
		TestFalse(TEXT("re-export does not rewrite tokens.json"), R2.bWroteTokensFile);

		FString OnDisk;
		FFileHelper::LoadFileToString(OnDisk, *TempJson);
		TestTrue(TEXT("tokens.json keeps its first exportedAt"), OnDisk.Contains(TEXT("2026-08-19T00:00:00.000Z")));

		const FDesignTokenImportResult R3 = FDesignTokenImporter::ImportFromText(C, TempJson);
		TestTrue(TEXT("a real change rewrites tokens.json"), R3.bWroteTokensFile);

		// The recolour reaches three tokens: the Global and both aliases of it.
		TestEqual(TEXT("the recolour lists three changes"), R3.Changes.Num(), 3);
		TestTrue(TEXT("each is an update"), R3.Changes.Num() > 0 && Algo::AllOf(R3.Changes,
			[](const FDesignTokenChange& Change) { return Change.Kind == EDesignTokenChangeKind::Updated; }));
		const FDesignTokenChange* Goals = R3.Changes.FindByPredicate(
			[](const FDesignTokenChange& Change) { return Change.Name == FName(TEXT("component.blades.accent_goals")); });
		TestTrue(TEXT("accent_goals is listed"), Goals != nullptr);
		if (Goals)
		{
			TestEqual(TEXT("with its old colour"), Goals->Before, FString(TEXT("#00D86C")));
			TestEqual(TEXT("and its new colour"), Goals->After, FString(TEXT("#7B61FF")));
		}
	}

	return true;
}

/**
 * Open widget editors are refreshed off this event, so it must fire for an
 * import that changed tokens and stay quiet for one that changed nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignTokenBroadcastTest,
	"FigmaTokenBridge.Import.BroadcastsChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesignTokenBroadcastTest::RunTest(const FString& Parameters)
{
	const FString MyId = UDesignTokenSettings::GetOrCreateProjectId();
	const FString A = MakeDoc(MyId, GoalsChain);
	const FString B = A.Replace(TEXT("2026-08-19T00:00:00.000Z"), TEXT("2026-10-22T14:05:48.655Z"));
	const FString C = A.Replace(TEXT("#00d86c"), TEXT("#7b61ff"));

	UDesignTokenSettings* Settings = GetMutableDefault<UDesignTokenSettings>();
	const FString OldPath = Settings->GeneratedPackagePath;
	const FString OldName = Settings->TokensAssetName;
	const FSoftObjectPath OldActive = Settings->ActiveTokens;
	Settings->GeneratedPackagePath = TEXT("/Game/DesignSystem/Generated/AutomationTest");
	Settings->TokensAssetName = FString::Printf(TEXT("DA_TokensBroadcast_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString AssetFile = FPackageName::LongPackageNameToFilename(
		Settings->GetTokensAssetPackagePath(), FPackageName::GetAssetPackageExtension());

	int32 Broadcasts = 0;
	const FDelegateHandle Handle = UDesignTokenLibrary::OnDesignTokensChanged().AddLambda([&Broadcasts]() { ++Broadcasts; });

	ON_SCOPE_EXIT
	{
		UDesignTokenLibrary::OnDesignTokensChanged().Remove(Handle);
		Settings->GeneratedPackagePath = OldPath;
		Settings->TokensAssetName = OldName;
		Settings->ActiveTokens = OldActive;
		UDesignTokenLibrary::InvalidateDesignTokenCache();
		Settings->TryUpdateDefaultConfigFile();
		IFileManager::Get().Delete(*AssetFile, false, true, true);
	};

	auto Write = [this](const FString& Text)
	{
		FDesignTokenDocument Doc;
		FDesignTokenImportResult Unused;
		TestTrue(TEXT("fixture parses"), FDesignTokenImporter::ParseDocument(Text, Doc, Unused));
		return FDesignTokenImporter::WriteAsset(Doc);
	};

	Write(A);
	TestEqual(TEXT("a first import announces the change"), Broadcasts, 1);
	Write(B);
	TestEqual(TEXT("an unchanged import stays quiet"), Broadcasts, 1);
	Write(C);
	TestEqual(TEXT("a recolour announces the change"), Broadcasts, 2);

	TestEqual(TEXT("refresh with no editors open does nothing"), FWidgetPreviewRefresher::RefreshOpenWidgetEditors(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignTokenChangeReportTest,
	"FigmaTokenBridge.Import.ChangeReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesignTokenChangeReportTest::RunTest(const FString& Parameters)
{
	FDesignColourToken Translucent;
	Translucent.SrgbHex = TEXT("#1c1b1a");
	Translucent.Colour = FLinearColor(0.01f, 0.01f, 0.01f, 0.8f);
	TestEqual(TEXT("a swatch is upper-case hex with its alpha"),
		FDesignTokenImporter::FormatSwatch(Translucent), FString(TEXT("#1C1B1A 80%")));

	FDesignColourToken Opaque;
	Opaque.SrgbHex = TEXT("#00d86c");
	Opaque.Colour = FLinearColor(0.f, 0.686685f, 0.149960f, 1.f);
	TestEqual(TEXT("an opaque swatch has no alpha"), FDesignTokenImporter::FormatSwatch(Opaque), FString(TEXT("#00D86C")));

	TArray<FDesignTokenChange> Many;
	for (int32 i = 0; i < 60; ++i)
	{
		FDesignTokenChange Change;
		Change.Name = FName(*FString::Printf(TEXT("component.test.token_%02d"), i));
		Change.Kind = EDesignTokenChangeKind::Added;
		Change.After = TEXT("#FFFFFF");
		Many.Add(Change);
	}
	const TArray<FString> Lines = FFigmaTokenBridgeReport::ChangeLines(Many, 50);
	TestEqual(TEXT("change lines are capped"), Lines.Num(), 51);
	TestEqual(TEXT("and say how many were left out, and where the rest are"), Lines.Last(),
		FString(TEXT("…and 10 more. The Output Log (LogFigmaTokens) has the full list.")));

	FDesignTokenChange Updated;
	Updated.Name = TEXT("component.blades.accent_goals");
	Updated.Kind = EDesignTokenChangeKind::Updated;
	Updated.Before = TEXT("#00D86C");
	Updated.After = TEXT("#7B61FF");
	TestEqual(TEXT("an update line names both colours"), FFigmaTokenBridgeReport::ChangeLines({ Updated }, 50)[0],
		FString(TEXT("Updated  component.blades.accent_goals  #00D86C → #7B61FF")));

	FDesignTokenImportResult Result;
	Result.bSuccess = true;
	Result.bWroteAsset = true;
	Result.Updated = 1;
	Result.Changes.Add(Updated);
	const FString Json = FFigmaLiveLink::MakePushResultJson(Result);
	TestTrue(TEXT("the push response lists the change"), Json.Contains(TEXT("\"kind\":\"updated\"")));
	TestTrue(TEXT("with its old colour"), Json.Contains(TEXT("\"before\":\"#00D86C\"")));
	TestTrue(TEXT("and its new colour"), Json.Contains(TEXT("\"after\":\"#7B61FF\"")));
	TestTrue(TEXT("and its name"), Json.Contains(TEXT("\"name\":\"component.blades.accent_goals\"")));

	// The plugin's headline goes by what Unreal wrote, not by the token counts:
	// a renamed link writes the asset with no token changed.
	TestTrue(TEXT("the push response says the asset was written"), Json.Contains(TEXT("\"wroteAsset\":true")));
	TestTrue(TEXT("and whether tokens.json was"), Json.Contains(TEXT("\"wroteTokensFile\":false")));

	// The change list is capped at 200; the counts are not.
	FDesignTokenImportResult Large;
	Large.bSuccess = true;
	Large.bWroteAsset = true;
	for (int32 i = 0; i < 250; ++i)
	{
		FDesignTokenChange Change;
		Change.Name = FName(*FString::Printf(TEXT("component.test.token_%03d"), i));
		Change.Kind = i < 240 ? EDesignTokenChangeKind::Added : EDesignTokenChangeKind::Deprecated;
		Large.Changes.Add(Change);
	}
	TSharedPtr<FJsonObject> LargeJson;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FFigmaLiveLink::MakePushResultJson(Large));
	if (!TestTrue(TEXT("the large push response parses"), FJsonSerializer::Deserialize(Reader, LargeJson) && LargeJson.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("the change list is capped"), LargeJson->GetArrayField(TEXT("changes")).Num(), 200);
	TestEqual(TEXT("the change count is not"), LargeJson->GetIntegerField(TEXT("changeCount")), 250);
	TestEqual(TEXT("nor is the count of tokens gone from Figma"), LargeJson->GetIntegerField(TEXT("newlyGone")), 10);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignTokenLiveLinkTest,
	"FigmaTokenBridge.LiveLink.Protocol",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesignTokenLiveLinkTest::RunTest(const FString& Parameters)
{
	const FString Secret = TEXT("0123456789abcdef0123456789ABCDEF");

	// Same code as tools/build-tokens.cjs parses.
	TestEqual(TEXT("pairing code format"), FFigmaLiveLink::MakePairingCode(30871, Secret),
		FString(TEXT("ftb1:30871:0123456789abcdef0123456789ABCDEF")));

	// Header keys arrive lower-cased from HTTPServer.
	auto Headers = [](const FString& Value)
	{
		TMap<FString, TArray<FString>> H;
		H.Add(TEXT("authorization"), { Value });
		return H;
	};

	TestTrue(TEXT("accepts the secret as a bearer token"), FFigmaLiveLink::IsAuthorised(Headers(TEXT("Bearer ") + Secret), Secret));
	TestTrue(TEXT("the scheme name is case-insensitive"), FFigmaLiveLink::IsAuthorised(Headers(TEXT("bearer ") + Secret), Secret));
	TestFalse(TEXT("the secret itself is case-sensitive"), FFigmaLiveLink::IsAuthorised(Headers(TEXT("Bearer ") + Secret.ToLower()), Secret));
	TestFalse(TEXT("refuses a wrong secret"), FFigmaLiveLink::IsAuthorised(Headers(TEXT("Bearer nope")), Secret));
	TestFalse(TEXT("refuses the bare secret without a scheme"), FFigmaLiveLink::IsAuthorised(Headers(Secret), Secret));
	TestFalse(TEXT("refuses a request with no header"), FFigmaLiveLink::IsAuthorised({}, Secret));
	TestFalse(TEXT("an empty secret never matches"), FFigmaLiveLink::IsAuthorised(Headers(TEXT("Bearer ")), FString()));
	{
		TMap<FString, TArray<FString>> Twice;
		Twice.Add(TEXT("authorization"), { TEXT("Bearer nope"), TEXT("Bearer ") + Secret });
		TestFalse(TEXT("refuses two authorization headers"), FFigmaLiveLink::IsAuthorised(Twice, Secret));
	}

	// /hello is what the plugin keys a pairing on.
	{
		TSharedPtr<FJsonObject> Hello;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(
			FFigmaLiveLink::MakeHelloJson(TEXT("A1B2C3D4-E5F6-7890-ABCD-EF1234567890"), TEXT("FigmaBridgeTest"), TEXT("abc123")));
		TestTrue(TEXT("hello is valid JSON"), FJsonSerializer::Deserialize(Reader, Hello) && Hello.IsValid());
		if (Hello.IsValid())
		{
			TestEqual(TEXT("hello names the project"), Hello->GetStringField(TEXT("projectId")),
				FString(TEXT("A1B2C3D4-E5F6-7890-ABCD-EF1234567890")));
			TestEqual(TEXT("hello names the paired file"), Hello->GetStringField(TEXT("fileKey")), FString(TEXT("abc123")));
		}
	}

	// A refused push reaches the designer with the importer's own words.
	{
		FDesignTokenImportResult Refused;
		Refused.Errors.Add(TEXT("This export targets another project."));

		TSharedPtr<FJsonObject> Body;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FFigmaLiveLink::MakePushResultJson(Refused));
		TestTrue(TEXT("push result is valid JSON"), FJsonSerializer::Deserialize(Reader, Body) && Body.IsValid());
		if (Body.IsValid())
		{
			TestFalse(TEXT("push result says it failed"), Body->GetBoolField(TEXT("ok")));
			const TArray<TSharedPtr<FJsonValue>>& Errors = Body->GetArrayField(TEXT("errors"));
			TestTrue(TEXT("push result carries the importer's error"),
				Errors.Num() == 1 && Errors[0]->AsString() == TEXT("This export targets another project."));
		}
	}

	TestEqual(TEXT("names who pushed"),
		FFigmaLiveLink::DescribePush(TEXT("{\"source\":{\"exportedBy\":\"Ada\"}}")), FString(TEXT("Pushed live from Figma by Ada.")));
	TestEqual(TEXT("copes with an anonymous push"),
		FFigmaLiveLink::DescribePush(TEXT("{\"source\":{\"exportedBy\":null}}")), FString(TEXT("Pushed live from Figma.")));
	TestEqual(TEXT("copes with a body that is not JSON"),
		FFigmaLiveLink::DescribePush(TEXT("not json")), FString(TEXT("Pushed live from Figma.")));

	return true;
}

namespace
{
	/** One real HTTP request to the listener, and what came back. */
	struct FLiveLinkProbe
	{
		bool bDone = false;
		int32 Code = 0;
		FString Body;
		FString AllowOrigin;
		FString AllowPrivateNetwork;
	};

	constexpr int32 LiveLinkTestPort = 30899;
	const TCHAR* LiveLinkTestSecret = TEXT("feedfacefeedfacefeedfacefeedface");

	TSharedRef<FLiveLinkProbe> SendProbe(const FString& Verb, const FString& Route,
		const TMap<FString, FString>& Headers, const FString& Body = FString())
	{
		TSharedRef<FLiveLinkProbe> Probe = MakeShared<FLiveLinkProbe>();

		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb(Verb);
		Request->SetURL(FString::Printf(TEXT("http://127.0.0.1:%d/figma-token-bridge/v1/%s"), LiveLinkTestPort, *Route));
		for (const TPair<FString, FString>& Header : Headers)
		{
			Request->SetHeader(Header.Key, Header.Value);
		}
		if (!Body.IsEmpty())
		{
			Request->SetContentAsString(Body);
		}
		Request->OnProcessRequestComplete().BindLambda(
			[Probe](FHttpRequestPtr, FHttpResponsePtr Response, bool)
			{
				Probe->bDone = true;
				if (Response.IsValid())
				{
					Probe->Code = Response->GetResponseCode();
					Probe->Body = Response->GetContentAsString();
					Probe->AllowOrigin = Response->GetHeader(TEXT("Access-Control-Allow-Origin"));
					Probe->AllowPrivateNetwork = Response->GetHeader(TEXT("Access-Control-Allow-Private-Network"));
				}
			});
		Request->ProcessRequest();
		return Probe;
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FWaitForLiveLinkProbes, TArray<TSharedRef<FLiveLinkProbe>>, Probes, double, Deadline);

bool FWaitForLiveLinkProbes::Update()
{
	if (FPlatformTime::Seconds() > Deadline)
	{
		return true;  // the checks then report whichever probe never answered
	}
	for (const TSharedRef<FLiveLinkProbe>& Probe : Probes)
	{
		if (!Probe->bDone)
		{
			return false;
		}
	}
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRunLiveLinkChecks, TFunction<void()>, Checks);

bool FRunLiveLinkChecks::Update()
{
	Checks();
	return true;
}

/**
 * The listener end to end, over real HTTP on 127.0.0.1: the browser preflight
 * the Figma plugin triggers, the secret check, /hello, and a push. The push is
 * aimed at another project so the importer refuses it before writing anything,
 * which proves the body reached the importer without touching this project's
 * tokens.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignTokenLiveLinkServerTest,
	"FigmaTokenBridge.LiveLink.Server",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDesignTokenLiveLinkServerTest::RunTest(const FString& Parameters)
{
	if (FFigmaLiveLink::IsRunning())
	{
		AddInfo(TEXT("Skipped: this editor's live link is on, and the test would take over its listener."));
		return true;
	}

	const FString StartError = FFigmaLiveLink::Start(LiveLinkTestPort, LiveLinkTestSecret, nullptr);
	if (!TestTrue(TEXT("the listener starts"), StartError.IsEmpty()))
	{
		AddError(StartError);
		return true;
	}

	const FString Bearer = FString(TEXT("Bearer ")) + LiveLinkTestSecret;
	const FString OtherId = TEXT("FFFFFFFF-FFFF-4FFF-8FFF-FFFFFFFFFFFF");

	const TSharedRef<FLiveLinkProbe> Preflight = SendProbe(TEXT("OPTIONS"), TEXT("push"), {
		{ TEXT("Origin"), TEXT("null") },
		{ TEXT("Access-Control-Request-Method"), TEXT("POST") },
		{ TEXT("Access-Control-Request-Headers"), TEXT("authorization,content-type") },
		{ TEXT("Access-Control-Request-Private-Network"), TEXT("true") } });
	const TSharedRef<FLiveLinkProbe> NoSecret = SendProbe(TEXT("GET"), TEXT("hello"), {});
	const TSharedRef<FLiveLinkProbe> Hello = SendProbe(TEXT("GET"), TEXT("hello"), { { TEXT("Authorization"), Bearer } });
	const TSharedRef<FLiveLinkProbe> Push = SendProbe(TEXT("POST"), TEXT("push"),
		{ { TEXT("Authorization"), Bearer }, { TEXT("Content-Type"), TEXT("application/json") } },
		MakeDoc(OtherId, GoalsChain));

	ADD_LATENT_AUTOMATION_COMMAND(FWaitForLiveLinkProbes({ Preflight, NoSecret, Hello, Push }, FPlatformTime::Seconds() + 10.0));
	ADD_LATENT_AUTOMATION_COMMAND(FRunLiveLinkChecks([this, Preflight, NoSecret, Hello, Push, OtherId]()
	{
		FFigmaLiveLink::Stop();

		TestEqual(TEXT("preflight is answered with 204"), Preflight->Code, 204);
		TestEqual(TEXT("preflight allows the plugin's null origin"), Preflight->AllowOrigin, FString(TEXT("*")));
		TestEqual(TEXT("preflight allows a private-network request"), Preflight->AllowPrivateNetwork, FString(TEXT("true")));

		TestEqual(TEXT("refuses a request without the secret"), NoSecret->Code, 401);
		TestEqual(TEXT("and the plugin can read the refusal"), NoSecret->AllowOrigin, FString(TEXT("*")));

		TestEqual(TEXT("hello answers"), Hello->Code, 200);
		TestTrue(TEXT("hello names this project"), Hello->Body.Contains(UDesignTokenSettings::GetOrCreateProjectId()));

		TestEqual(TEXT("push answers"), Push->Code, 200);
		TestTrue(TEXT("a push aimed at another project reaches the importer and is refused"),
			Push->Body.Contains(TEXT("\"ok\":false")) && Push->Body.Contains(OtherId));
	}));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
