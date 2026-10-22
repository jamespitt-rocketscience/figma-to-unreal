# Plan A: Trustworthy Imports — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An import that changes tokens shows the change straight away: the open widget editor refreshes, and both the Figma and Unreal reports list each change with its old and new colour. An import that changes nothing writes nothing.

**Architecture:**
- **Change detection.** A content fingerprint, the export JSON minus its volatile provenance fields, decides whether anything changed. It is stored on the asset and checked before any file is written.
- **Change list.** `WriteAsset` records each change in the import result. The Message Log and the live link response both render from that list.
- **Widget refresh.** The runtime module broadcasts a "tokens changed" event. An editor-side refresher listens and calls `RefreshPreview()` on every open widget editor.
- **Plugin reports.** File-tidiness notes move out of push and publish reports, so only warnings that affect Unreal appear there.

**Tech Stack:** UE 5.8 C++ (FigmaTokenBridge runtime and editor modules, UMGEditor), Figma plugin plain JS, Node harness.

**Spec:** [2026-10-22-token-bridge-roadmap.md](2026-10-22-token-bridge-roadmap.md), Plan A row; findings in the 2026-10-22 walkthrough.

## Global Constraints

- Engine **UE 5.8**; plugin JS stays **plain JavaScript, no build step**.
- Never write `tokens.json` or the `.uasset` when the fingerprint is unchanged. Plan B builds Perforce support on this.
- The fingerprint ignores exactly `source.exportedAt` and `source.exportedBy`. Every other field counts, including `target.*`.
- Every existing test keeps passing: `node tools/build-tokens.cjs` (both fixtures) and `powershell -File tools\build-unreal.ps1`.
- No secret (pairing code) in any screenshot or log line.

## Review Focus

1. **The first import after this ships.** An asset written before this change has an empty `ContentFingerprint`. It must be treated as changed and written once, not skipped forever. (Task 1 test: `upgrade write`.)
2. **The same tokens with a renamed link.** This is a real provenance change: write the asset, report 0 token changes. (Task 1 test: `link rename writes`.)
3. **The startup check with a republished but identical export.** It must not offer "Sync now" on every launch. (Task 1 Step 6.)
4. **A large change set.** For example, the first import of 171 tokens. The Message Log is capped at 50 change lines plus "…and N more", and the plugin list at 20. (Task 2 test: `change lines capped`.)
5. **A widget editor open on a different widget, or none open.** The refresher must do nothing and not crash. (Task 3 test: `refresh with no editors`.)

---

### Task 1: Content fingerprint; an import with nothing changed writes nothing

**Files:**
- Modify: `unreal/Plugins/FigmaTokenBridge/Source/FigmaTokenBridge/Public/DesignTokens.h` (add property)
- Modify: `unreal/Plugins/FigmaTokenBridge/Source/FigmaTokenBridgeEditor/Public/DesignTokenImporter.h`
- Modify: `unreal/Plugins/FigmaTokenBridge/Source/FigmaTokenBridgeEditor/Private/DesignTokenImporter.cpp` (`ParseDocument`, `WriteAsset`, `ImportFromText`, `Summary`)
- Modify: `unreal/Plugins/FigmaTokenBridge/Source/FigmaTokenBridgeEditor/Private/FigmaTokenBridgeEditorModule.cpp` (`CheckForPublishedTokens`)
- Test: `unreal/Plugins/FigmaTokenBridge/Source/FigmaTokenBridgeEditor/Private/Tests/DesignTokenTests.cpp`

**Interfaces:**
- Produces: `static FString FDesignTokenImporter::ContentFingerprint(const FString& JsonText)`. Returns the lower-case hex SHA-1 of the condensed re-serialisation with `source.exportedAt` and `source.exportedBy` removed. Returns an empty string for invalid JSON.
- Produces: `FString FDesignTokenDocument::Fingerprint`, set by `ParseDocument`.
- Produces: `UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source") FString UDesignTokens::ContentFingerprint`.
- Produces: `bool FDesignTokenImportResult::bWroteAsset = false` and `bool FDesignTokenImportResult::bWroteTokensFile = false`.

- [ ] **Step 1: Write the failing tests.** Add `FigmaTokenBridge.Import.NoOpWhenUnchanged`. Reuse the scratch-settings setup and `ON_SCOPE_EXIT` clean-up from `FDesignTokenWriteAssetTest`, and make the four document variants by string replacement on `MakeDoc(MyId, GoalsChain)`.

```cpp
const FString A = MakeDoc(MyId, GoalsChain);
const FString B = A.Replace(TEXT("2026-08-19T00:00:00.000Z"), TEXT("2026-10-22T14:05:48.655Z"));      // re-exported, same tokens
const FString C = A.Replace(TEXT("#00d86c"), TEXT("#7b61ff"));                                         // recoloured
const FString D = A.Replace(TEXT("Automated test link"), TEXT("Renamed link"));                        // provenance only

TestEqual(TEXT("fingerprint ignores exportedAt"), FDesignTokenImporter::ContentFingerprint(A), FDesignTokenImporter::ContentFingerprint(B));
TestNotEqual(TEXT("fingerprint sees a colour"),   FDesignTokenImporter::ContentFingerprint(A), FDesignTokenImporter::ContentFingerprint(C));
TestEqual(TEXT("fingerprint ignores whitespace"), FDesignTokenImporter::ContentFingerprint(A), FDesignTokenImporter::ContentFingerprint(FFigmaPublicationReader::PrettyPrint(A)));
TestEqual(TEXT("invalid JSON has no fingerprint"), FDesignTokenImporter::ContentFingerprint(TEXT("not json")), FString());
// WriteAsset(A) -> bWroteAsset true; record the package file timestamp and Asset->ImportedAt
// WriteAsset(B) -> bWroteAsset false, Unchanged == 4, file timestamp unchanged, ImportedAt unchanged, Summary() == "Already up to date (4 tokens unchanged)"
// "upgrade write": clear Asset->ContentFingerprint, WriteAsset(B) -> bWroteAsset true
// "link rename writes": WriteAsset(D) -> bWroteAsset true, Added+Updated+Renamed+Deprecated == 0
// ImportFromText(A, TempPath) then ImportFromText(B, TempPath) -> second bWroteTokensFile false, file still contains "2026-08-19T00:00:00.000Z"
```

- [ ] **Step 2: Run the tests and check they fail.** Run: `powershell -File tools\build-unreal.ps1`. Expected: the build fails because `ContentFingerprint` is not defined.

- [ ] **Step 3: Implement `ContentFingerprint`.**
  - Parse with `FJsonSerializer`. Remove the two fields from the `source` object, then re-serialise with `TCondensedJsonPrintPolicy`.
  - Hash with `FSHA1::HashBuffer` over the UTF-8 bytes and return `FSHAHash::ToString().ToLower()`.
  - Set `OutDoc.Fingerprint` at the end of a successful `ParseDocument`.

- [ ] **Step 4: Make `WriteAsset` skip the write.** Compute the counts as now. If the asset already existed, nothing was added, updated, renamed or deprecated, and `Asset->ContentFingerprint == Doc.Fingerprint`:
  - return `bSuccess = true` and `bWroteAsset = false`, with `Result.Asset` set;
  - do not assign any field, call `MarkPackageDirty`, save, or call `InvalidateDesignTokenCache`.

  Otherwise assign `Asset->ContentFingerprint = Doc.Fingerprint` alongside the other provenance fields, and set `bWroteAsset = true` after a successful save.

- [ ] **Step 5: Make `ImportFromText` and `Summary` follow.**
  - `ImportFromText`: write `SaveToPath` only if it is missing, or if `ContentFingerprint(existing file text) != Doc.Fingerprint`. Set `bWroteTokensFile` to match.
  - `Summary()`: when `bSuccess && !bWroteAsset` and the four change counts are zero, return `FString::Printf(TEXT("Already up to date (%d tokens unchanged)"), Unchanged)`.

- [ ] **Step 6: Make the startup check compare fingerprints.**
  - In `CheckForPublishedTokens`, replace `Active->ExportedAt == Fetched.Publication.ExportedAt` with `Active->ContentFingerprint == FDesignTokenImporter::ContentFingerprint(Fetched.Publication.DocumentJson)`.
  - Log text: `Design tokens are up to date with Figma.`

- [ ] **Step 7: Run all tests.** Run `powershell -File tools\build-unreal.ps1` and `node tools/build-tokens.cjs`. Expected: all automation tests pass, including `Import.NoOpWhenUnchanged`, and the existing `Import.WriteAsset` "everything is unchanged" case still reports `Unchanged == 4`.

- [ ] **Step 8: Commit.**

```bash
git add unreal/Plugins/FigmaTokenBridge
git commit -m "Skip writing tokens.json and the token asset when the content is unchanged"
```

---

### Task 2: The change list, in the result, the Message Log and the live link response

**Files:**
- Modify: `.../FigmaTokenBridgeEditor/Public/DesignTokenImporter.h`
- Modify: `.../FigmaTokenBridgeEditor/Private/DesignTokenImporter.cpp` (`WriteAsset`)
- Create: `.../FigmaTokenBridgeEditor/Private/FigmaTokenBridgeReport.h/.cpp` (formats change lines)
- Modify: `.../FigmaTokenBridgeEditor/Private/FigmaTokenBridgeEditorModule.cpp` (`WriteReport`)
- Modify: `.../FigmaTokenBridgeEditor/Private/FigmaLiveLink.cpp` (`MakePushResultJson`)
- Test: `.../Private/Tests/DesignTokenTests.cpp`

**Interfaces:**
- Consumes: `bWroteAsset` from Task 1.
- Produces, in `DesignTokenImporter.h`:

```cpp
enum class EDesignTokenChangeKind : uint8 { Added, Updated, Renamed, Deprecated };

struct FIGMATOKENBRIDGEEDITOR_API FDesignTokenChange
{
	FName Name;
	EDesignTokenChangeKind Kind = EDesignTokenChangeKind::Updated;
	FString Before;   // "" when Added; e.g. "#00D86C" or "#1C1B1A 80%"
	FString After;    // "" when Deprecated
	FName OldName;    // set when Renamed
};
// FDesignTokenImportResult gains: TArray<FDesignTokenChange> Changes;
// FDesignTokenImporter gains: static FString FormatSwatch(const FDesignColourToken& Token);
//   upper-case "#RRGGBB", plus " NN%" (rounded alpha * 100) when alpha < 0.995
// FDesignTokenImporter gains: static const TCHAR* ChangeKindName(EDesignTokenChangeKind Kind);
//   "added" | "updated" | "renamed" | "deprecated"
```

- Produces: push response JSON gains `"changes": [{ "name", "kind", "before", "after", "oldName" }]`. Task 4 renders it.

- [ ] **Step 1: Write the failing tests.**
  - In `Import.NoOpWhenUnchanged`, after `WriteAsset(C)`:
    - `Changes.Num() == 3`;
    - every entry has `Kind == Updated`;
    - the `component.blades.accent_goals` entry has `Before == "#00D86C"` and `After == "#7B61FF"`.
  - **change lines capped:** for 60 synthetic `Added` changes, `FFigmaTokenBridgeReport::ChangeLines(Changes, 50)` returns 51 lines, the last being `…and 10 more`.
  - In `LiveLink.Protocol`: `MakePushResultJson` for a result holding one `Updated` change contains `"kind":"updated"`, `"before":"#00D86C"` and `"after":"#7B61FF"`.
  - `FormatSwatch` of a token with `SrgbHex "#1c1b1a"` and alpha `0.8` returns `"#1C1B1A 80%"`.

- [ ] **Step 2: Run the tests and check they fail.** Run `powershell -File tools\build-unreal.ps1`. Expected: compile failure because `Changes` is undefined.

- [ ] **Step 3: Implement.**
  - **In `WriteAsset`:** append a `FDesignTokenChange` everywhere a counter is incremented today. Renamed entries carry `OldName`. Deprecated entries carry `Before` and an empty `After`.
  - **`FigmaTokenBridgeReport.h/.cpp`:** add `static TArray<FString> FFigmaTokenBridgeReport::ChangeLines(const TArray<FDesignTokenChange>& Changes, int32 Max)`. Each line has the form `Updated  component.blades.accent_goals  #00D86C → #7B61FF`, and lines are capped at `Max`.
  - **`WriteReport`:** writes one Info message per line.
  - **`MakePushResultJson`:** serialises `Changes`, capped at 200 entries.

- [ ] **Step 4: Run the tests and check they pass.** Run `powershell -File tools\build-unreal.ps1`. Expected: all pass.

- [ ] **Step 5: Commit.**

```bash
git commit -am "List each changed token with its old and new colour in the import report and live push response"
```

---

### Task 3: Refresh open widget editors after an import that changed tokens

**Files:**
- Modify: `.../FigmaTokenBridge/Public/DesignTokenLibrary.h`
- Modify: `.../FigmaTokenBridge/Private/DesignTokenLibrary.cpp`
- Create: `.../FigmaTokenBridgeEditor/Private/WidgetPreviewRefresher.h/.cpp`
- Modify: `.../FigmaTokenBridgeEditor/Private/FigmaTokenBridgeEditorModule.cpp` (register and unregister)
- Modify: `.../FigmaTokenBridgeEditor/FigmaTokenBridgeEditor.Build.cs` (add `"UMG"`, `"UMGEditor"`)
- Test: `.../Private/Tests/DesignTokenTests.cpp`

**Interfaces:**
- Consumes: Task 1 guarantees that `InvalidateDesignTokenCache` is reached only when the asset was written.
- Produces: `static FSimpleMulticastDelegate& UDesignTokenLibrary::OnDesignTokensChanged()`. `InvalidateDesignTokenCache()` broadcasts it after clearing the cache.
- Produces: `static int32 FWidgetPreviewRefresher::RefreshOpenWidgetEditors()`. It returns the number of widget editors refreshed. It also exposes `void Register()` and `void Unregister()`, which bind and unbind that function to `OnDesignTokensChanged`.

- [ ] **Step 1: Write the failing tests.** Add `FigmaTokenBridge.Import.BroadcastsChange`, using scratch settings as in Task 1.
  - Bind a lambda that counts calls to `UDesignTokenLibrary::OnDesignTokensChanged()`.
  - `WriteAsset(A)`: count is 1. `WriteAsset(B)` (unchanged): count is still 1. `WriteAsset(C)`: count is 2.
  - **refresh with no editors:** `FWidgetPreviewRefresher::RefreshOpenWidgetEditors() == 0` in the automation run, which has no editors open.

- [ ] **Step 2: Run the tests and check they fail.** Run `powershell -File tools\build-unreal.ps1`. Expected: compile failure because `OnDesignTokensChanged` is undefined.

- [ ] **Step 3: Implement.**
  - **`RefreshOpenWidgetEditors`:** for each `UWidgetBlueprint` in `GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->GetAllEditedAssets()`:
    - take `FindEditorForAsset(Asset, false)`;
    - when its `GetEditorName() == FName("WidgetBlueprintEditor")`, call `static_cast<FWidgetBlueprintEditor*>(Editor)->RefreshPreview()`.
  - **Logging:** log `Refreshed %d open widget editor(s).` when the count is greater than 0.
  - **Module:** owns a `FWidgetPreviewRefresher`; call `Register()` in `StartupModule` and `Unregister()` in `ShutdownModule`.

- [ ] **Step 4: Run the tests and check they pass.** Run `powershell -File tools\build-unreal.ps1`. Expected: all pass.

- [ ] **Step 5: Commit.**

```bash
git commit -am "Refresh open widget editors when an import changes design tokens"
```

---

### Task 4: Plugin reports — tidiness notes on Scan only, change list with swatches first

**Files:**
- Modify: `figma-plugin/code.js` (`buildDocument`, the `scan` handler, `showUI` size)
- Modify: `figma-plugin/ui.html` (scan report, `showPush`, published report)
- Test: `tools/build-tokens.cjs`

**Interfaces:**
- Consumes: the push response `changes` array from Task 2.
- Produces: `buildDocument(...)` returns `hygiene: string[]` alongside `warnings`. The five notes it currently pushes go to `hygiene` instead: outside the tier convention, deep alias chain, upper-case leaf, ALL_SCOPES, and primitives that drift apart. The `scan` reply gains `hygiene`.

- [ ] **Step 1: Write the failing tests** in `tools/build-tokens.cjs`.
  - Point the existing assertions at about lines 209 and 230 at `built.hygiene` instead of `built.warnings`.
  - Add:

```js
check('tidiness notes are not export warnings',
  built.warnings.some((w) => /upper-case|ALL_SCOPES|drift apart/.test(w)), false);
check('tidiness notes are still reported',
  built.hygiene.some((w) => w.indexOf('icon-secondary-Inactive') !== -1), true);
```

- [ ] **Step 2: Run the tests and check they fail.** Run `node tools/build-tokens.cjs --fixture tools/fixtures/tokenbridgetest.raw.json`. Expected: FAIL on "tidiness notes are not export warnings".

- [ ] **Step 3: Implement.**
  - **`buildDocument`:** route the five pushes to `hygiene`.
  - **`scan` handler:** builds with `target: null` to collect `hygiene`, and adds it to the reply.
  - **`showUI`:** `{ width: 460, height: 720 }`.
  - **`ui.html` scan report:** show `hygiene` inside `<details><summary>N tidy-ups in this file (they don't affect Unreal)</summary>`.
  - **`ui.html` publish, export and push reports:** show only `warnings`.
  - **`showPush` order:**
    1. A headline. On success use `Imported into <project>: N updated, N added`, or `Already up to date` when nothing changed. On a refused push, keep the existing refusal.
    2. A change list, up to 20 rows. Each row shows a 14 px swatch `span` for `before`, an arrow, a swatch for `after`, then the name.
    3. The counts.

- [ ] **Step 4: Run the tests and check they pass.** Run `node tools/build-tokens.cjs` and `node tools/build-tokens.cjs --fixture tools/fixtures/tokenbridgetest.raw.json`. Expected: `All assertions passed.` for both.

- [ ] **Step 5: Commit.**

```bash
git commit -am "Plugin: keep tidiness notes on Scan, lead push reports with the change list"
```

---

### Task 5: Verify in the real apps and update the docs

**Files:**
- Modify: `TESTING.md` §3.6, `docs/figma-token-bridge-guide.md` (then regenerate `.confluence.xhtml`), `README.md` caveats
- Confluence: the walkthrough page (335183955), plus an Updates row on the landing page (302579726)

- [ ] **Step 1: Re-import the plugin.** The UI size changed. Then rerun walkthrough steps 5–6 with the desk-automation helper:
  - **Push with nothing changed:** Figma shows `Already up to date`. Unreal's toast reads `Already up to date (25 tokens unchanged)`. `git status` shows **no** change to `tokens/tokens.tokens.json` or `DA_DesignTokens.uasset`.
  - **Change `Global/green-400` to `7B61FF` and push:** the open `WBP_Test` preview turns purple **without Compile**. The Figma report lists 4 rows with green → purple swatches. The Message Log lists the same 4 lines.
  - **Revert to `00D86C` and push again.**
- [ ] **Step 2: Update the docs.**
  - Remove the "needs Compile" notes from `TESTING.md`, the guide and `README.md`.
  - Regenerate the Confluence guide: `node tools/md-to-confluence.cjs docs/figma-token-bridge-guide.md`.
- [ ] **Step 3: Update Confluence.**
  - Walkthrough: replace screenshots 12–14 with the new ones, and change the "Open widget editor repaints" result to Pass.
  - Landing page: add an Updates row.
  - Onboarding guide: remove the Compile troubleshooting row.
- [ ] **Step 4: Commit and push** (with James's go-ahead).

```bash
git commit -am "Docs: imports refresh widgets and skip unchanged writes"
```
