# Figma Token Bridge

A designer changes a colour in Figma. Every widget in Unreal that uses it follows, without anyone regenerating or re-importing a single widget.

That is the whole product. This page explains how it works on both sides, then walks through installing and using it.

**Status:** Phase 0 — colour only. Typography, icons and layout are deliberately out of scope for now; see **Not in scope yet**, below.

**Engine:** Unreal Engine 5.8.

---

## The problem it solves

Before this, getting a colour from Figma into Unreal meant reading a hex value and typing it into a widget. That copy has three problems:

- **It forgets where it came from.** Nothing in the project records that `#00d86c` was the Goals accent rather than an arbitrary green.
- **It does not update.** Rebrand a colour and someone has to find every widget that used it. Anything missed is wrong, quietly.
- **It is easy to get subtly wrong.** Figma gives you sRGB; Unreal's `FLinearColor` is linear. Copying the bytes across turns `#00d86c` into a colour 19% out in green and nearly 3× out in blue — wrong in a way that looks like a design decision, so it survives review.

The two existing open-source Figma-to-Unreal tools both generate widgets with colours baked in, so a recolour means regenerating widgets. This bridge does the opposite: it generates **one referenced asset**, and widgets point at it.

---

## How it works

| Step | Where | What happens | Result |
|---|---|---|---|
| 1 | Figma | Designer scans the design system file | Every colour variable, listed by tier and group |
| 2 | Figma | Designer picks a link and exports | `tokens.json` downloaded |
| 3 | Git | The export is committed | A reviewable diff — **this is the review gate** |
| 4 | Unreal | **Tools → Sync Design Tokens** | `DA_DesignTokens` regenerated in place |
| 5 | Unreal | Nothing — widgets already reference the asset | Every widget using those tokens follows |

Data flows one way only: Figma to Git to Unreal. Nothing is ever written back.

Three deliberate choices sit behind that flow.

**The exchange format is a committed JSON file, not a live API call.** Figma's Variables REST API is Enterprise-only and we are on the Organization tier, so a direct read is not available to us. More importantly, a committed `tokens.json` means every change to the design system arrives as a reviewable diff rather than appearing in someone's editor unannounced. **That diff is the review gate.**

**Generated assets are machine-owned.** Everything lands under `/Game/DesignSystem/Generated/`, which nobody edits by hand. Hand-authored widgets live elsewhere and only *reference* what is generated. This is what makes re-syncing non-destructive by construction, rather than by a merge algorithm that is only ever mostly right.

**Identity is Figma's variable key, not the name.** Keys survive renames and survive the design system moving between files. That is how the importer can tell a rename from a delete-plus-add.

---

## Concepts you need

### The three tiers

Our design system is an alias graph in three tiers. Only the first holds real values.

| Tier | Holds | Example |
|---|---|---|
| `Global/` | The only literal values | `Global/green-400` = `#00d86c` |
| `Semantic/` | Aliases naming an *intent* | `Semantic/accent-green` → `Global/green-400` |
| `Component/` | Aliases naming a *usage* | `Component/Blades/accent-goals` → `Semantic/accent-green` |

**UI work should only ever reference `Component/` tokens.** That is why the source board in Figma is titled *"Only choose color variables from here"*, and the Unreal side enforces it: the token dropdown offers Component tokens and nothing else.

A useful consequence: two Component tokens can share a colour without being duplicates. `accent-goals` and `vital-heart-rate` are both green because both alias `Semantic/accent-green`. Change that one Semantic alias and both follow — which is correct, because they mean related things.

### Token names

Figma names are normalised into something Unreal can use as an `FName`:

```text
Component/Icons - Secondary/vital-heart-rate
        ↓
component.icons_secondary.vital_heart_rate
```

Lower-cased, non-alphanumerics collapsed to underscores, path separators become dots. The conversion is automatic and there are no per-token overrides.

### Links

A **link** says: *this part of the design system feeds that Unreal project*.

The designer authors links in the Figma plugin. Each one carries:

- a name, for humans
- the target **Unreal project id**
- the **Figma file key**
- which tiers and groups are **published** to that project

Links are stored on the Figma document itself, so the whole design team sees the same set rather than one person's local config. One Figma file can hold as many links as there are Unreal projects consuming it.

### Publishing gates the picker, not the import

This is the one thing most likely to be misread, so to be precise:

**An unpublished token is still exported and still imported.** It has to be — the alias graph needs `Semantic` and `Global` present to resolve a `Component` token down to a literal value. Drop them and nothing resolves.

What publishing changes is what Unreal **offers**. The Blueprint and UMG token dropdowns list published tokens only. So a developer sees exactly the surface the designer intended, and cannot reach for something out of scope by accident.

### The two interlocks

Neither side can be silently wrong about where tokens are going.

| Direction | Mechanism | Failure |
|---|---|---|
| **Figma → Unreal** | The export names the Unreal project id it was authored for | The importer refuses an export aimed elsewhere, and names both ids |
| **Unreal → Figma** | Project settings name the Figma file this project is paired with | The importer refuses a `tokens.json` from any other file |

Both are on by default. Both have an off switch — `bRequireProjectIdMatch` and `bRequireFileKeyMatch` — for a deliberate re-link.

The project id is a **GUID**, not the project name. Names get renamed and reused, and a link silently pointing at the wrong project is worse than one that visibly breaks.

### Read modes

When the plugin scans a file it reports which of two modes applied:

- **Library file** — this file *owns* the variables. Authoritative: every variable is returned whether anything uses it or not.
- **Consuming file** — this file *subscribes* to a published library. The scan can only find variables bound to something on the current page.

Consuming mode is fine for a complete token board, but both the plugin and the Unreal importer flag it, so nobody mistakes a partial export for a full one. **Run the plugin in the library file whenever you can.**

---

## Part 1 — For designers (Figma)

### Install the plugin

Development plugins only load in the **Figma desktop app**, not the browser.

1. **Plugins → Development → Import plugin from manifest…**
2. Choose `figma-plugin/manifest.json` from the repository.

There is no build step. If `manifest.json` itself changes, remove the plugin and re-import it — Figma reads the manifest once, at import.

### Scan the file

Open the design system file, then **Plugins → Development → Figma Token Bridge → Scan this file**.

The report tells you the read mode, how many variables were found, and lists the tiers and groups. Check the read mode says **library file** before going further.

If it reports **0 variables**, you are in a file that neither owns nor binds any — open the library file instead.

### Create a link

Click **New link…** and fill in:

| Field | Where it comes from |
|---|---|
| Link name | Whatever helps humans, e.g. *Clinical Sim — main UI* |
| Unreal project id | The developer sends it: **Tools → Copy Project Id for Figma** in Unreal |
| Unreal project name | Optional, for readability |
| Figma file key | The part of this file's URL after `/design/` |
| Publish to Unreal | Tick the tiers and groups this project may use |

A new link defaults to the `Component` tier, matching the board's own instruction.

> **Why you have to type the file key.** `figma.fileKey` is readable only by plugins published privately to the organisation. While the plugin is loaded from a manifest for development, it cannot read it, so you confirm it from your address bar. Once the plugin is published to the org this field fills itself in.

### Export

Click **Export tokens.json** on the link card. The report shows how many tokens are published, how many are exported in total, and any warnings — naming inconsistencies, unusually deep alias chains, genuinely duplicated primitives.

The published count being lower than the exported count is expected, not a bug. See **Publishing gates the picker, not the import**, above.

Then **Download tokens.json**, put it in the repository, and commit it. **That commit is how the change reaches developers, and the diff is what gets reviewed.**

---

## Part 2 — For developers (Unreal)

### Install the plugin into a project

1. Copy `unreal/Plugins/FigmaTokenBridge` into your project's `Plugins/` folder.
2. Regenerate project files and build.
3. Enable the plugin if it is not already.

The plugin has two modules: a runtime module carrying the asset, settings and Blueprint API, and an editor module carrying the importer, menus and tests.

### Pair the project with a Figma file

**Project Settings → Plugins → Figma Token Bridge**

| Setting | What to set |
|---|---|
| **Figma File Key** | The key of the design system file. Leave empty to accept any file — not recommended |
| **Tokens File** | Path to `tokens.json`, relative to the project directory |
| **Generated Package Path** | Defaults to `/Game/DesignSystem/Generated`. Machine-owned |
| **Tokens Asset Name** | Defaults to `DA_DesignTokens` |
| **Active Tokens** | Set automatically on first import |

### Send your project id to the designer

**Tools → Copy Project Id for Figma** puts it on the clipboard.

The id is generated on first editor run and written to `Config/DefaultGame.ini`, so it is committed with the project and is the same for everyone. Do not change it — every existing Figma link points at it.

### Sync

**Tools → Sync Design Tokens.**

You get a summary like `171 tokens (4 added, 2 updated, 165 unchanged, 1 renamed, 0 deprecated), 2 warning(s)`. Detail goes to the Output Log under `LogFigmaTokens`.

If the import is refused, the error says exactly which interlock failed and names both values. That is working as designed — read the message before changing settings.

---

## Using tokens

### In UMG — the token-aware widgets

This is the preferred way inside widgets. In the UMG palette, under **Design System**:

| Widget | Token properties | What they drive |
|---|---|---|
| **Token Border** | Brush Colour Token, Content Colour Token | `BrushColor`, `ContentColorAndOpacity` |
| **Token Image** | Colour Token | `ColorAndOpacity` |
| **Token Text Block** | Colour Token | `ColorAndOpacity` |

Set a token and the Details panel shows a swatch, the resolved value, and the alias chain in Figma names:

```text
Brush Colour Token  [swatch] [component.chat.bg_bubble ▾]
  Resolves to       #1c1b1a · alpha 1.00 · Component tier
                    Component/Chat/bg-bubble → Semantic/bg-primary → Global/gray-900
```

That last row is what answers *"why is this colour this colour"* without opening Figma.

The widget stores the token **name**, never the colour, and resolves it whenever the widget synchronises — which happens both in the Designer and at runtime. So the Designer shows the real colour while you author, and a re-sync reaches every placed instance with nothing regenerated.

Two things to know:

- **Leaving a token as `None` makes the widget behave exactly like the stock one it derives from.** Reparenting an existing `Border` to a `Token Border` is therefore safe and reversible.
- **Once a token is set, that colour property is machine-owned.** Editing *Brush Color* by hand will appear to work and then be overwritten on the next synchronise. Clear the token if you want manual control back.

### In Blueprint

Works anywhere, not just in widgets.

| Node | Returns | Notes |
|---|---|---|
| **Get Design Colour** | `FLinearColor` | Token pin is a dropdown of published tokens |
| **Get Design Slate Colour** | `FSlateColor` | For Slate-typed properties such as `UTextBlock::ColorAndOpacity` |
| **Get Design Colour Any Tier** | `FLinearColor` | Dropdown offers every tier. For the rare genuine exception |
| **Has Design Token** | `bool` | |
| **Get Design Token Info** | `FDesignColourToken` | Full record: hex, tier, group, Figma name and key, alias chain, flags |
| **Get Design Tokens** | `UDesignTokens` | The asset itself, for building palette or debug screens |
| **Invalidate Design Token Cache** | — | Rarely needed; the importer calls it for you |

If you are setting a colour on a stock widget from a graph, do it in **Event PreConstruct**. It runs in the Designer preview as well as at runtime, so you see the real colour while authoring. Avoid a property **Binding** — bindings re-evaluate every frame, and a token only ever changes at editor time.

### From C++

Add `"FigmaTokenBridge"` to your module's `Build.cs`, then:

```cpp
#include "DesignTokenLibrary.h"

const FLinearColor Accent =
    UDesignTokenLibrary::GetDesignColour(TEXT("component.blades.accent_goals"));

MyTextBlock->SetColorAndOpacity(
    UDesignTokenLibrary::GetDesignSlateColour(TEXT("component.chat.fg_message")));
```

### What a mistake looks like

A typo or a removed token returns **magenta**, and logs a warning once per token per session under `LogFigmaTokens`. Magenta is deliberate: a silent black or white reads as a design choice and ships, whereas magenta gets noticed and fixed.

---

## The everyday loop

1. Designer changes a colour in Figma.
2. Designer exports `tokens.json` and commits it. **The diff is the review.**
3. Developer pulls and runs **Tools → Sync Design Tokens**.
4. Every widget referencing those tokens follows. Nothing is regenerated.

---

## What happens when the design system changes

**A token is renamed.** Matched by its Figma key, so the import reports `1 renamed` rather than an add plus a delete. The log names the old and new names so references can be updated.

**A token is deleted.** It is *not* removed from the asset. It is marked deprecated, keeps resolving to its last known value, and warns. Removing it is a separate, deliberate act. Otherwise a tidy-up in Figma turns shipped UI magenta in a build nobody looked at.

**A colour changes.** Nothing to do beyond re-syncing. This is the case the whole system exists for.

**Publishing changes.** A token moving in or out of the published set counts as a real change, because it moves in or out of the developer-facing dropdown.

---

## Version control

| What | Where | Why |
|---|---|---|
| `tokens.json` | **Git** | The diff is the review gate, and JSON review is what Git is good at |
| Figma plugin source | **Git** | Plain JS, no build step |
| Unreal plugin source | **Git** | |
| Generated `.uasset` | **Perforce** | Binary assets belong with the rest of the project's content |

---

## Troubleshooting

| Symptom | Cause |
|---|---|
| Plugin missing from Figma's menu | Development plugins only load in the Figma **desktop** app |
| `"currentuser" permission not specified` | The manifest changed since you imported it — remove and re-import the plugin |
| Scan finds 0 variables | You are in a file that neither owns nor binds any — open the library file |
| Scan says *consuming file* | You are in a subscriber. Only canvas-bound variables were found; run it in the library file |
| Export refuses: no project link | Create a link and paste the Unreal project id into it |
| Export refuses: nothing published | Tick at least one tier or group in the link |
| Import refused, names two project ids | The export was authored for a different Unreal project. Export from the right link |
| Import refused, names two file keys | The `tokens.json` came from a different Figma file than this project is paired with |
| Import refused: no target project | The export predates links, or has no project id. Re-export from a link |
| Blueprint dropdown is empty | No import has run yet, or *Active Tokens* is unset in project settings |
| Everything is magenta | The fallback. Either no token asset is set, or the token name is unknown — check `LogFigmaTokens` |
| Colours will not change after a re-sync | *Active Tokens* may point at an old asset. Re-check project settings |

---

## Not in scope yet

Phase 0 is colour only. In rough order:

- **Phase 1** — Perforce checkout and revert-if-unchanged, reimport factories, a commandlet so CI can sync headlessly.
- **Phase 2** — icons as multi-scale PNG to `Texture2D` and `FSlateBrush`; typography as a composite `UFont`; Common UI style Blueprints.
- **Phase 3** — gradient paint styles as material instances, Figma modes for theming, and `LIBRARY_PUBLISH` webhook automation so a publish in Figma can trigger a sync.

Widget and layout generation is **not** planned. Two open-source tools already do that, and neither solves the token problem this bridge exists for.

---

## Where things live

| Path | What |
|---|---|
| `figma-plugin/` | The Figma plugin — plain JS, no build step |
| `tokens/` | Committed `tokens.json` exports |
| `tools/build-tokens.cjs` | Replays the exporter's logic over a captured fixture; 44 assertions, no Figma access needed |
| `tools/setup-test-project.ps1` | Links the plugin into the test project |
| `tools/build-unreal.ps1` | Builds the plugin and runs the automation tests |
| `tools/md-to-confluence.cjs` | Regenerates the Confluence version of this page from its Markdown source |
| `unreal/Plugins/FigmaTokenBridge/` | The Unreal plugin — this is the folder you copy into a project |
| `unreal/TestProject/` | A throwaway host project for testing the plugin |
| `TESTING.md` | End-to-end test runbook |

### Running the tests

```bash
node tools/build-tokens.cjs                  # exporter logic, under a second, no Figma needed
powershell -File tools\build-unreal.ps1      # builds the plugin, runs UE automation tests
```

The Unreal tests also run from **Tools → Test Automation → `FigmaTokenBridge`** in the editor.

### Updating this page

The Markdown at `docs/figma-token-bridge-guide.md` is the source of truth. After editing it:

```bash
node tools/md-to-confluence.cjs docs/figma-token-bridge-guide.md
```

Then paste the regenerated `.confluence.xhtml` over the page body via **Insert → Markup → Confluence storage format**. Editing the Confluence page directly is fine for a typo, but the change will be lost the next time the page is regenerated.
