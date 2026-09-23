# Figma Token Bridge

Carries colour, typography and icons out of Figma and into Unreal Engine as a
single referenced asset, so a designer's change reaches every widget without
regenerating any of them.

**Status: Phase 0 — colour only.** Full architecture: see the design doc
(`Figma Token Bridge`, published as an artifact).

**To run it end to end, start at [TESTING.md](TESTING.md).** A test Figma file
and a test Unreal project are already built.

Target engine: **UE 5.8**. Dev Figma file: **FigmaUnrealTest**
(`f7tYGbrdTli91GaFdgioLi`), which is a sandbox — the bridge is built to link any
Unreal project to any Figma file.

---

## What Phase 0 establishes

1. A Figma plugin that reads colour variables and writes `tokens.json`.
2. A committed fixture captured from the real file, so the Unreal side can be
   built and tested with no Figma access at all.
3. An Unreal plugin that imports that file into a `UDesignTokens` DataAsset and
   exposes it to Blueprint through a searchable token dropdown.
4. The colour-space conversion, pinned by tests on both sides.

The one number that matters: `#00d86c` (the Goals accent) must arrive as
`FLinearColor(0.000000, 0.686685, 0.149960)`. Copying the bytes across instead
gives `(0, 0.847059, 0.423529)` — 19% out in green, 2.8x in blue — which looks
like a design decision rather than a bug, and so survives review. Both test
suites assert the correct value.

## What the design system actually turned out to be

Worth knowing before reading any of the code, because it shaped all of it.

The 103 colour variables visible on the token board are not the design system —
they are its public surface. Reading the file through the Plugin API showed
**171 variables in three tiers**, none of them local to `FigmaUnrealTest`:

| Tier | Count | Holds | Example |
|---|---|---|---|
| `Global/*` | 31 | the only literal values | `Global/green-400` = `#00d86c` |
| `Semantic/*` | 37 | aliases naming an intent | `Semantic/accent-green` |
| `Component/*` | 103 | aliases naming a usage | `Component/Blades/accent-goals` |

Every variable is `remote: true` — they are owned by a published library and
subscribed to by this file. `getLocalVariablesAsync()` returns **zero** here.
That is why the exporter has two read modes, and why identity is Figma's stable
`key` rather than its file-scoped `id`.

It also means the apparent duplicate hex values are not duplication.
`vital-heart-rate` and `accent-goals` are both `#00d86c` because both alias
`Semantic/accent-green`. The alias graph is already correct.

## Layout

```
figma-plugin/           the exporter — plain JS, no build step
  code.js               PART 1 pure logic (testable in Node), PART 2 Figma runtime
  ui.html               one button, a validation report, a download
  manifest.json
tokens/
  FigmaUnrealTest.tokens.json   from the real Rocket Science system
  FigmaBridgeTest.tokens.json   from the small purpose-built test system
tools/
  build-tokens.cjs        replays PART 1 over a fixture; 44 assertions
  setup-test-project.ps1  junctions the plugin into the test project
  build-unreal.ps1        builds the plugin, then runs the automation tests
  md-to-confluence.cjs    regenerates the Confluence version of docs/
  fixtures/
    figmaunrealtest.raw.json   the real system, 171 vars, captured 2026-08-19
    tokenbridgetest.raw.json   the test system,  25 vars, captured 2026-09-22
unreal/Plugins/FigmaTokenBridge/
  Source/FigmaTokenBridge/          runtime: DataAsset, settings, Blueprint API
  Source/FigmaTokenBridgeEditor/    editor: JSON importer, menu, automation tests
unreal/TestProject/     throwaway host project; the plugin is junctioned in,
                        so there is only ever one copy of the source
docs/
  figma-token-bridge-guide.md          the team guide; source of truth
  figma-token-bridge-guide.confluence.xhtml  generated, paste into Confluence
```

## Running it

**Full walkthrough: [TESTING.md](TESTING.md)** — a test Figma file and a test
Unreal project are already set up. What follows is the shape of it; that doc has
the steps.

### The exporter, without Figma

```
node tools/build-tokens.cjs
node tools/build-tokens.cjs --fixture tools/fixtures/tokenbridgetest.raw.json
```

Replays the plugin's pure logic over a captured fixture and runs every assertion.
No Figma account needed, well under a second. Two fixtures: the real 171-variable
Rocket Science system, and a 25-variable one built to contain every awkward case
on purpose. The same 44 assertions run against both, which is the point — the
counts live in the fixture, the rules are shared.

A run defaults to a deliberately impossible target project id, so an export it
produces is refused by every real project. `--project-id` aims one at a specific
project; the script then refuses to retarget that file back to the placeholder,
because the only symptom of doing so is an import failure that reads like a bug
in the handshake.

### The Unreal plugin

```
powershell -File tools\setup-test-project.ps1   # one-time: link the plugin in
powershell -File tools\build-unreal.ps1         # build, then run the tests
```

Needs Visual Studio with the C++ workload. The setup script checks and says so
if it is missing.

### Links: the designer decides what goes where

A **link** says *this part of the design system feeds that Unreal project*. The
designer authors it in the plugin, and links are stored in the Figma file with
`setSharedPluginData`, so the whole design team sees the same set rather than one
person's local config. A file can hold as many links as there are projects.

Each link carries three things: the target Unreal project, which tiers and groups
are published to it, and a name for humans.

In outline: copy the plugin into a project and build it; **Tools → Copy Project
Id for Figma** puts this project's GUID on the clipboard; the designer pastes it
into a new link in the Figma plugin, ticks what that project may use, and exports
`tokens.json`; **Tools → Sync Design Tokens** brings it in. After that,
`Get Design Colour` in any Blueprint offers a searchable dropdown of exactly what
the designer published, so a typo is not possible and neither is reaching for
something out of scope.

[TESTING.md](TESTING.md) has this as numbered steps, with what each one should
print.

### Two interlocks, one in each direction

Neither side can be silently wrong about where tokens are going:

- **Figma → Unreal.** The export names the project it was authored for. The
  importer refuses one aimed elsewhere, and the error names both ids.
- **Unreal → Figma.** Project settings name the Figma file this project is paired
  with. The importer refuses a `tokens.json` from anywhere else.

Both can be turned off for a deliberate re-link; both are on by default.

### Publishing controls the picker, not the import

Worth being precise, because it is easy to assume the opposite. An unpublished
token is still imported — the alias graph needs `Semantic` and `Global` present
to resolve a `Component` token down to a literal, so dropping them would break
every value. What publishing changes is what Unreal *offers*: the Blueprint
dropdown lists published tokens only.

The scan report also distinguishes two read modes. **Library file** is
authoritative — every variable is returned whether used or not. **Consuming
file** only finds variables bound to something on the canvas; fine for a complete
token board, but both the plugin and the importer flag it so nobody mistakes it
for a full export.

Run the C++ tests from **Tools → Test Automation → `FigmaTokenBridge`**, or
headless via `tools/build-unreal.ps1`, which parses the automation report and
exits non-zero on failure so it works as a CI step unchanged.

## Using the palette in UMG

`Get Design Colour` works in any Blueprint, but widgets have a first-class
surface: **Token Border**, **Token Image** and **Token Text Block**, under
*Design System* in the palette.

They store an `FDesignTokenColourRef` — the token **name** — and resolve it in
`SynchronizeProperties`, which UMG calls both when the Designer rebuilds the
preview and when the widget is constructed at runtime. So the Designer shows the
real colour while you author, and a re-sync reaches every placed instance without
regenerating anything. A property binding would tick every frame for a value that
only changes at editor time; a colour typed into *Brush Color* forgets where it
came from entirely.

The details customization is registered against the struct rather than the widget
classes, so any token-aware property added later gets the swatch, the resolved
value and the alias chain for free. See [TESTING.md §2.5](TESTING.md).

## Decisions worth not re-litigating

**Generated assets are machine-owned.** Everything lands under
`/Game/DesignSystem/Generated/`, which no human edits. Hand-authored widgets live
elsewhere and only *reference* generated assets. That is what makes re-sync
non-destructive by construction, rather than by a merge algorithm that is only
ever mostly right.

**Identity is the Figma `key`.** Names change; keys do not, and keys survive the
design system moving files. The importer can therefore tell a rename from a
delete-plus-add, which a name-keyed importer cannot.

**Deletion is a two-step.** A token absent from a new export is marked
deprecated and keeps resolving to its last known value, with a warning. Removing
it is a separate deliberate act. Otherwise a tidy-up in Figma turns shipped UI
magenta in a build nobody looked at.

**The dropdown offers Component tokens only.** The source board is titled *"Only
choose color variables from here"*; `GetDesignColour` enforces it, and
`GetDesignColourAnyTier` exists for the rare genuine exception.

**Missing tokens return magenta.** A silent black or white reads as a design
choice. Magenta gets noticed and fixed.

## Not done yet

Phase 0 is deliberately colour-only. Deferred, in order:

- **Phase 1** — Perforce checkout and revert-if-unchanged, reimport factories,
  the committed key→name map, a commandlet for CI.
- **Phase 2** — icons (multi-scale PNG → `Texture2D` → `FSlateBrush`),
  typography (Outfit as a composite `UFont`), Common UI style Blueprints,
  token-aware `UTokenImage` / `UTokenTextBlock` widgets.
- **Phase 3** — the 13 gradient paint styles as material instances, Figma modes,
  `LIBRARY_PUBLISH` webhook automation.

## Caveats

- **The C++ now compiles and its tests pass** against UE 5.8 under
  `BuildSettingsVersion.V7`, the strictest warning settings 5.8 offers. It
  needed exactly one fix: `SNotificationItem` is declared in
  `SNotificationList.h`, and there is no `SNotificationItem.h` despite the
  class name. The two APIs the original notes flagged as risky,
  `EAutomationTestFlags` and `FSavePackageArgs`, were both already correct.
- **The exporter is validated against two independent design systems** — the
  real 171-variable one and a 25-variable one built from scratch in a fresh
  Figma file via the Plugin API. All 44 assertions pass against both. What
  that does *not* cover is PART 2 of `code.js`, the half that talks to Figma:
  the packaged plugin has still not been imported into Figma desktop, so
  `scan()` and the link UI are the only things left untested. See
  [TESTING.md §3](TESTING.md).
- Two hygiene items in the **Rocket Science** file, both reported by the exporter as
  warnings: `icon-secondary-Inactive` is the only capitalised token in 103, and
  all 171 variables use `ALL_SCOPES`, which clutters Figma's property pickers
  without affecting this export.
