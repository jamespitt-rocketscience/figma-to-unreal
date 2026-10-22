# Sygma — improvement roadmap

**Date:** 2026-10-22
**Source:** the end-to-end live link run of 2026-10-22 and its review
([Confluence walkthrough](https://rocketsciencegg.atlassian.net/wiki/spaces/~712020d1c308abc7294af3a7e1f85779e2a45f/pages/335183955)).

This roadmap covers six improvements, split into six plans. Each plan ships working, tested software on its own. Plans A–C have implementation plans and can start now. Plans D–F each need a short design spec first, because they depend on decisions nobody has made yet.

## The plans

| Plan | Covers | Why | Ready? | Depends on |
|---|---|---|---|---|
| **A. Trustworthy imports** | Open widget editors refresh after an import. Each import lists what changed, old and new colour. A sync or push with nothing changed writes nothing. Figma reports cleaned up | In the walkthrough the preview stayed green after a push, and every push rewrote files even when nothing had changed | [Plan](2026-10-22-plan-a-trustworthy-imports.md) | — |
| **B. Perforce** | Check out before writing; a sync with nothing changed opens nothing | Studios keep generated `.uasset`s in Perforce. Today, writing to a read-only, checked-in asset fails | [Plan](2026-10-22-plan-b-revision-control.md) | A (relies on A's "nothing changed, write nothing") |
| **C. Setup in two clicks** | "Link this project" built from a pairing; the plugin published privately to the org | Setup currently means copying a GUID, typing a file key, and re-importing the manifest through a file dialog | [Plan](2026-10-22-plan-c-setup.md) | — (independent of B) |
| **D. Themes from Figma modes** | One token asset per Figma mode, plus a way to choose the active one | Colour-blind, high-contrast and team-colour palettes are where token syncing starts saving real time | Needs a spec | A |
| **E. Typography** | Figma text styles → fonts and text-style tokens | Text styles are where UI engineers spend hours | Needs a spec | A, D (text styles can vary by mode) |
| **F. Common UI styles** | Generated `UCommonTextStyle` / `UCommonButtonStyle` classes, and variants mapped to button states | The phase 2 goal agreed on 2026-08-18 | Needs a spec | E |

**Order:** A, then B and C (they can run in parallel), then a single design session covering D–F, then D, E and F in that order.

## Decisions needed before D–F

1. **Switching themes at runtime.** On 2026-08-18 we locked decision 4: tokens are baked in the editor, with no switching in packaged builds. But colour-blind and high-contrast palettes are a player setting, so they have to switch at runtime. Pick one:
   - (a) Reverse decision 4 for themes only. All mode assets ship, and a runtime subsystem picks the active one.
   - (b) Keep decision 4. Themes are an editor or build-time choice only, which rules out accessibility palettes.
   - (c) Ship every mode baked in, and switch at runtime only between already-baked assets.

   Option (c) keeps the "no live data in builds" intent and still allows a player setting. **(c) is the recommendation.**
2. **Which collection's modes.** The scan currently warns and exports only the first collection's default mode. Decide whether modes come from one designated collection, set per link, or from every collection.
3. **Seat cost.** Multiple modes per collection need Figma Professional or above. Confirm the studio's plan.
4. **Typography mapping.** Use the conversion rule verified on 2026-08-19: Unreal's line-height percentage = (Figma line-height % ÷ 100) × font size ÷ the font's natural line height. Import static font weights, not variable fonts. Plan E needs one decision: which fonts ship, and who owns the font files.
5. **How Common UI styles are generated.** Common UI uses its styles as Blueprint *classes* (it reads their default values), not as instances. Generating them means generating Blueprints. Decide whether those classes are machine-owned under `Generated/`, as the token asset is, or are a one-off scaffold.

## How each plan is verified

Every plan ends with:
- `node tools/build-tokens.cjs` passing;
- `powershell -File tools\build-unreal.ps1` passing;
- a rerun of the affected steps of the 2026-10-22 walkthrough, with screenshots.

After that, update the Confluence walkthrough and add an Updates row to the Sygma landing page.
