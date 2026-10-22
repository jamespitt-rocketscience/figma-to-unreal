# Plan C: Setup in Two Clicks — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A designer who has paired with an editor creates that project's link in one click, with nothing to copy or type. Teammates install the plugin from the organisation instead of importing a manifest.

**Architecture:** The live link's `/hello` already returns the project ID, the project name and the project's Figma file key. A pairing will keep that data, and a "Link this project" button will open the link form pre-filled from it. Publishing the plugin privately to the org is mostly an admin task. Afterwards Figma supplies `figma.fileKey`, which the code already prefers, and teammates get updates automatically.

**Tech Stack:** Figma plugin plain JS, Node harness.

**Spec:** [2026-10-22-token-bridge-roadmap.md](2026-10-22-token-bridge-roadmap.md), Plan C row.

## Global Constraints

- Plain JavaScript, no build step. Keep pure logic in PART 1 of `code.js`, so `tools/build-tokens.cjs` can test it.
- The pairing secret never appears in UI text, links or logs.
- The project-ID route (**Tools → Copy Project Id for Figma**) stays. Designers who don't run Unreal still need it.

## Review Focus

1. **A paired project whose Figma File Key points at a different file.** Expect a warning in the form, not a silent link that Unreal will refuse. (Task 1 test.)
2. **A paired project that already has a link.** Expect no second "Link this project" button. (Task 1 test.)
3. **A pairing saved before this change, with no stored file key.** The form still opens, using the file's own key or an empty field. (Task 1 test.)
4. **The org-published plugin opened in a file where it has never run.** It must load existing links. The plugin ID changes on publish, but shared plugin data is keyed by namespace, so links must survive. (Task 2 check.)
5. **Teammates who still have the development plugin imported.** Two copies would show up in the Plugins menu, so the onboarding page must say to remove the development copy. (Task 2 docs.)

---

### Task 1: "Link this project" from a pairing

**Files:**
- Modify: `figma-plugin/code.js` (PART 1 helper; `savePairing` stores `hello.fileKey`)
- Modify: `figma-plugin/ui.html` (`renderPairings` button; `openEditor` accepts a draft)
- Test: `tools/build-tokens.cjs`

**Interfaces:**
- Produces, in PART 1 (exported): `function linkDefaultsFromPairing(pairing, fileKeyFromApi)`. It returns `{ linkName, projectId, projectName, fileKey, warning }`.
  - `fileKey` is `fileKeyFromApi` when set, otherwise `pairing.fileKey`, otherwise `''`.
  - `warning` is null unless both keys are set and differ. Then it reads: `This Unreal project is paired with a different Figma file (<pairing.fileKey>). Unreal will refuse tokens from this file until its Figma File Key matches.`
  - `linkName` is `pairing.projectName || 'Unreal project'`.
- Produces: pairings store `fileKey` (from `hello.fileKey`). The pairing summary sent to the UI includes it.
- Produces: `openEditor(link, draft)` in `ui.html`. When `draft` is given, the title is "New link" and the fields are pre-filled from the draft.

- [ ] **Step 1: Write the failing tests** in `build-tokens.cjs`.

```js
const p = { projectId: 'B608334A-43F8-7269-83DF-96892C5638C5', projectName: 'FigmaBridgeTest', fileKey: 'FruupulmvbUZbkUlTQ0kIm' };
check('link defaults come from the pairing', bridge.linkDefaultsFromPairing(p, null),
  { linkName: 'FigmaBridgeTest', projectId: p.projectId, projectName: 'FigmaBridgeTest', fileKey: 'FruupulmvbUZbkUlTQ0kIm', warning: null });
check('a different file is flagged',
  /different Figma file \(FruupulmvbUZbkUlTQ0kIm\)/.test(bridge.linkDefaultsFromPairing(p, 'OtherKey1234567890ab').warning), true);
check('an old pairing without a file key still works',
  bridge.linkDefaultsFromPairing({ projectId: p.projectId, projectName: '' }, null).fileKey, '');
```

- [ ] **Step 2: Run the tests and check they fail.** Run `node tools/build-tokens.cjs`. Expected: FAIL because `linkDefaultsFromPairing` is not a function.

- [ ] **Step 3: Implement.**
  - **`code.js`:** implement and export the helper. In `savePairing`, store `fileKey: hello.fileKey || ''`.
  - **`renderPairings`:** for each pairing with no link where `normId(link.projectId) === key`, add a **Link this project** button. It asks the main thread for `linkDefaultsFromPairing(pairing, state.fileKey)`, either through a new `linkDefaults` message or by duplicating the pure helper inline.
  - **The form:** opens with those defaults. The warning shows as a `msg('warn', …)` above it, and Component is ticked.

- [ ] **Step 4: Run the tests and check they pass.** Run `node tools/build-tokens.cjs` on both fixtures. Expected: `All assertions passed.`

- [ ] **Step 5: Verify in Figma desktop with the desk-automation helper.**
  1. Delete the "FigmaBridgeTest - walkthrough" link.
  2. A **Link this project** button appears on the pairing row.
  3. Clicking it pre-fills the project ID, project name and file key.
  4. Save. **Push live** works.
  5. Screenshot steps 2–4 for the walkthrough page.

- [ ] **Step 6: Commit.**

```bash
git commit -am "Plugin: create a link straight from a live link pairing"
```

---

### Task 2: Publish the plugin privately to the organisation

This task needs **James and a Figma org admin**. Code changes are limited to the manifest ID and the docs.

- [ ] **Step 1: Publish.** In Figma desktop, **Plugins → Development → Sygma → Publish**.
  - Audience: **Only people in Rocket Science**.
  - Fill in the description and cover art. The `networkAccess.reasoning` text already explains the localhost access.
  - Expected: Figma assigns a plugin ID and the plugin appears under **Plugins → From your organisation**.
- [ ] **Step 2: Commit the assigned ID.** Copy it into `figma-plugin/manifest.json` `"id"`, then commit:

```bash
git commit -am "Plugin: use the org-published plugin id"
```

- [ ] **Step 3: Check it in the published version.**
  - The file line shows the file key automatically, with no "file key unavailable" message.
  - Existing links and pairings still load. Links live in shared plugin data under the `figmaTokenBridge` namespace, which doesn't change with the plugin ID. Pairings are in `clientStorage`, which **is** per plugin, so re-pair once. Write that down.
  - **Push live** and **Publish** both work.
- [ ] **Step 4: Update the docs.**
  - Onboarding guide and team guide: replace "Import plugin from manifest" with "Plugins → From your organisation → Sygma". Keep the manifest route for plugin developers only. Tell teammates to remove the development copy, and to re-pair once.
  - Regenerate the Confluence guide.
  - Add an Updates row on the landing page.
