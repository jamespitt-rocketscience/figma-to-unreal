# Plan B: Revision Control (Perforce) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A sync or push checks out `DA_DesignTokens.uasset` and `tokens.json` through the editor's revision control before writing them. A sync with nothing changed opens nothing.

**Architecture:** One helper, `FTokenFileCheckout`, wraps `USourceControlHelpers`. It works with whatever provider the editor has connected (Perforce, Git, or none), so the bridge never talks to Perforce directly. Plan A already stops unchanged imports from writing. That covers "revert if unchanged" for free: a file that isn't going to be written is never checked out.

**Tech Stack:** UE 5.8 C++, `SourceControl` module (`USourceControlHelpers`, `ISourceControlModule`).

**Spec:** [2026-10-22-token-bridge-roadmap.md](2026-10-22-token-bridge-roadmap.md), Plan B row. Locked decision 6: `tokens.json` lives in Git and generated `.uasset`s live in Perforce.

## Global Constraints

- Requires Plan A Task 1 (`bWroteAsset` and `bWroteTokensFile`; no write when unchanged).
- Never call a Perforce API directly. Go only through the editor's revision control (`USourceControlHelpers`).
- A file that can't be written is an **error** for the asset (the import fails and the old asset stays), and a **warning** for `tokens.json` (the import succeeds, as it does today).
- New project setting `bUseRevisionControl`, default `true`. It is committed in `DefaultGame.ini`.

## Review Focus

1. **A read-only asset with revision control disconnected.** Expect a clear error saying "check it out or connect Revision Control"; the asset on disk is unchanged. (Task 1 test.)
2. **A brand-new asset on its first import under Perforce.** It must be marked for add **after** it is saved, not checked out before it exists. (Task 1, Step 3 ordering, plus the manual check.)
3. **`tokens.json` under Git while the asset is in Perforce.** Expect no error from the Git provider: a check-out under Git is a no-op. (Manual check.)
4. **A sync with nothing changed.** Expect nothing opened in any changelist. (Manual check; relies on Plan A.)
5. **A check-out refused because someone else has it exclusively locked.** Expect an error naming the file and quoting the provider's message. (Manual check.)

---

### Task 1: Check out before writing, mark new files for add

**Files:**
- Create: `unreal/Plugins/FigmaTokenBridge/Source/FigmaTokenBridgeEditor/Private/TokenFileCheckout.h/.cpp`
- Modify: `.../FigmaTokenBridge/Public/DesignTokenSettings.h` (add `bUseRevisionControl`)
- Modify: `.../FigmaTokenBridgeEditor/Private/DesignTokenImporter.cpp` (`WriteAsset` save path, `ImportFromText` file write)
- Modify: `.../FigmaTokenBridgeEditor/FigmaTokenBridgeEditor.Build.cs` (add `"SourceControl"`)
- Test: `.../Private/Tests/DesignTokenTests.cpp`

**Interfaces:**
- Produces:

```cpp
class FTokenFileCheckout
{
public:
	/** Before writing an existing file: check it out when revision control is on and connected.
	 *  False with OutError when the file stays read-only. True for a file that does not exist yet. */
	static bool PrepareForWrite(const FString& AbsoluteFile, FString& OutError);

	/** After writing a file that did not exist before: mark it for add. Failures become a warning in OutWarning. */
	static void AfterCreate(const FString& AbsoluteFile, FString& OutWarning);
};
```

- [ ] **Step 1: Write the failing test.** Add `FigmaTokenBridge.Import.ReadOnlyTargets`. Use the scratch settings from Plan A's tests; the automation run has no revision control provider.

```cpp
// asset: WriteAsset(A) succeeds; SetReadOnly(asset file, true); WriteAsset(C)
TestFalse(TEXT("a read-only asset fails the import"), R.bSuccess);
TestTrue(TEXT("and says how to fix it"), R.Errors.Num() > 0 && R.Errors[0].Contains(TEXT("read-only")) && R.Errors[0].Contains(TEXT("check it out")));
// asset file timestamp unchanged; restore SetReadOnly(false) in ON_SCOPE_EXIT

// tokens.json: ImportFromText(A, TempPath); SetReadOnly(TempPath, true); ImportFromText(C, TempPath)
TestTrue(TEXT("a read-only tokens.json is a warning, not a failure"), R2.bSuccess);
TestTrue(TEXT("naming the file"), R2.Warnings.ContainsByPredicate([&](const FString& W){ return W.Contains(TempPath) && W.Contains(TEXT("read-only")); }));
TestTrue(TEXT("a file that does not exist yet is fine"), FTokenFileCheckout::PrepareForWrite(TempDir / TEXT("new.json"), Err));
```

- [ ] **Step 2: Run the tests and check they fail.** Run `powershell -File tools\build-unreal.ps1`. Expected: compile failure because `FTokenFileCheckout` is undefined.

- [ ] **Step 3: Implement.**
  - **`PrepareForWrite`:**
    - If the file doesn't exist, return true.
    - If `bUseRevisionControl` is set, `ISourceControlModule::Get().IsEnabled()` and the provider is available, call `USourceControlHelpers::CheckOutOrAddFile(File, /*bSilent*/ true)`. On failure, set `OutError = FString::Printf(TEXT("Could not check out '%s': %s"), *File, *USourceControlHelpers::LastErrorMsg().ToString())`.
    - Finally, if the file is still read-only, set `OutError` to `'<file>' is read-only. Check it out in Perforce, or connect Revision Control in the editor, and sync again.` and return false.
  - **`AfterCreate`:** call `USourceControlHelpers::MarkFileForAdd` under the same enabled check.
  - **`WriteAsset`:**
    - Before `SavePackage`, if the package file exists, call `PrepareForWrite`. On false, add the error and return without saving.
    - After saving a package file that didn't exist before, call `AfterCreate`.
  - **`ImportFromText`:** call `PrepareForWrite` before saving `tokens.json`. Report a false result as a warning containing the path and "read-only" (replacing today's generic warning), and call `AfterCreate` for a new file.

- [ ] **Step 4: Run the tests and check they pass.** Run `powershell -File tools\build-unreal.ps1`. Expected: all pass.

- [ ] **Step 5: Commit.**

```bash
git commit -am "Check out the token asset and tokens.json through revision control before writing"
```

---

### Task 2: Verify in a Perforce workspace, and update the docs

This task needs James, or a project that is already in Perforce.

- [ ] **Step 1: Check the Perforce flows.** In a Perforce-connected project with the plugin installed:
  - **Changed sync:** `DA_DesignTokens.uasset` is opened for edit in the default changelist.
  - **Unchanged sync:** nothing is opened.
  - **First import:** a newly created asset is opened for add.
  - **Exclusive lock:** if someone else holds the asset exclusively, the error names the file and quotes the Perforce message.
- [ ] **Step 2: Update the docs.**
  - Guide: add a "Version control" note covering `bUseRevisionControl` and the check-out behaviour. Regenerate the Confluence version.
  - `TESTING.md`: add a §5 case for a read-only asset.
  - Confluence: add an Updates row on the landing page.
- [ ] **Step 3: Commit.**

```bash
git commit -am "Docs: revision control behaviour"
```
