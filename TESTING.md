# Testing the bridge end to end

A runbook for taking the Figma Token Bridge from source to a colour resolved in
a Blueprint, using a Figma file and an Unreal project that have never seen it
before. Follow it in order the first time; after that, the loop in
[§4](#4-the-loop-youll-actually-live-in) is the one you repeat.

Two things are already built for you:

| | |
|---|---|
| **Test Figma file** | [Figma Token Bridge — Test Design System](https://www.figma.com/design/FruupulmvbUZbkUlTQ0kIm) · key `FruupulmvbUZbkUlTQ0kIm` |
| **Test Unreal project** | `unreal/TestProject/FigmaBridgeTest.uproject` |

The Figma file is a deliberately small three-tier system — 25 variables, 8
Global / 7 Semantic / 10 Component — built to contain every awkward case the
bridge has to survive rather than to look like a product:

- a group named `Icons - Secondary`, so name normalisation has a space and a dash to deal with
- one upper-case leaf, `icon-secondary-Inactive`, so the naming warning fires
- alpha baked into primitives (`gray-900-80`, `red-500-40`), so alpha has to survive an alias chain
- `accent-goals` and `vital-heart-rate` aliasing the *same* Semantic parent, so the "duplicate values" check has to *not* fire
- `Global/green-400` = `#00d86c`, the headline colour-space test vector

---

## 1. What you need

| | Why | Status on this machine |
|---|---|---|
| Unreal Engine **5.8** | the plugin targets it | installed at `C:\Program Files\Epic Games\UE_5.8` |
| Visual Studio with **Desktop development with C++** | UBT needs MSVC | installed — VS 2026 Community, MSVC 14.51 |
| **Node 18+** | the exporter's test harness | installed |
| **Figma desktop** | development plugins only load in the desktop app | needed for §3 |

For Visual Studio, the components that matter are *MSVC v14.3x C++ x64/x86 build
tools*, *Windows 11 SDK*, and *.NET desktop development*.
`tools/setup-test-project.ps1` checks for MSVC and says so plainly if it is
missing, rather than letting UBT fail obscurely later.

---

## 2. The Unreal half

### 2.1 Link the plugin into the test project

```powershell
powershell -File tools\setup-test-project.ps1
```

The plugin lives at `unreal/Plugins/FigmaTokenBridge` — that is the folder you
copy into a real project. Unreal only looks under `<Project>/Plugins`, so this
creates a directory junction rather than a second copy that would immediately
drift. Re-running it is safe.

### 2.2 Build

```powershell
powershell -File tools\build-unreal.ps1
```

This builds `FigmaBridgeTestEditor Win64 Development` and then runs the
automation tests headless, failing the whole script if any test fails. Build
only, or test only:

```powershell
powershell -File tools\build-unreal.ps1 -NoTests
powershell -File tools\build-unreal.ps1 -TestsOnly
```

**This now builds clean and all five tests pass**, under
`BuildSettingsVersion.V7` — 5.8's strictest setting, which promotes
return-type, dangling-pointer and unreachable-code warnings to errors. The
whole plugin needed exactly one fix to compile: `SNotificationItem` is declared
in `SNotificationList.h`, and there is no `SNotificationItem.h` despite the
class name.

Five automation tests should run:

```
FigmaTokenBridge.Colour.SrgbToLinear     the transfer function, both ends and the knee
FigmaTokenBridge.Import.ParseDocument    JSON to tokens, alias keys to names
FigmaTokenBridge.Import.RejectsBadInput  bad schema, bad colour, missing source
FigmaTokenBridge.Import.ProjectHandshake the interlock, positive and negative
FigmaTokenBridge.Import.WriteAsset       package creation, the re-sync diff
```

`WriteAsset` is the one worth knowing about. It drives the real asset path —
`CreatePackage`, `NewObject`, `FAssetRegistryModule::AssetCreated`,
`UPackage::SavePackage` — and then checks the four re-sync outcomes that
everything else depends on: a first import adds, an identical re-import is a
no-op, a token absent from Figma is deprecated rather than dropped, and a
renamed token is matched by its Figma key rather than counted as a delete plus
an add. It writes to a GUID-named scratch asset and restores every setting it
touches, then asserts the restore happened.

### 2.3 Get the project id

The project id is generated on first editor run and written to
`unreal/TestProject/Config/DefaultGame.ini`. The automation run in 2.2 is an
editor run, so it will already be there — on this machine it is
`6F30C3F5-4C69-CDC4-3E57-28BCFECECADF`:

```powershell
Select-String -Path unreal\TestProject\Config\DefaultGame.ini -Pattern ProjectId
```

In the editor it is one click: **Tools → Copy Project Id for Figma**.

> It has to land in `DefaultGame.ini` specifically, not the `Saved` config —
> otherwise it is not committed, a teammate cloning the project generates a
> different id, and every Figma link aimed at this project starts being refused
> for no visible reason.

### 2.4 Prove the importer works, without Figma

You can close the loop on the Unreal half before touching Figma at all. Build a
`tokens.json` aimed at this project from the captured fixture — on one line, or
with PowerShell backtick continuations:

```powershell
node tools/build-tokens.cjs --fixture tools/fixtures/tokenbridgetest.raw.json --project-id <the id from 2.3> --project-name FigmaBridgeTest --out tokens/FigmaBridgeTest.tokens.json
```

`tokens/FigmaBridgeTest.tokens.json` is already checked in, built against the
id above, so if you are on this machine you can skip straight to the sync.

> Running the harness **without** `--project-id` would otherwise rewrite that
> file with the placeholder target, and the only symptom is Unreal refusing the
> import over an id mismatch that looks like a handshake bug. The script now
> refuses to retarget an existing export and prints the command to regenerate
> it unchanged. Declining to write is not a failure, so the assertions still
> exit 0 and CI stays green.

Then in the editor: **Project Settings → Plugins → Figma Token Bridge**, set
*Figma File Key* to `FruupulmvbUZbkUlTQ0kIm`, and run **Tools → Sync Design
Tokens**.

You should get `25 tokens (25 added, 0 updated, …)` and a new
`/Game/DesignSystem/Generated/DA_DesignTokens`. Open it: `Colours` has 25
entries, and `component.blades.accent_goals` reads
`(0.000000, 0.686685, 0.149960)` with `SrgbHex` `#00d86c`.

Then drop a `Get Design Colour` node into any Blueprint. The **Token** pin is a
dropdown, and it lists exactly ten names — the Component tier, which is what the
link published. Semantic and Global were imported, because the alias graph needs
them, but they are not offered.

### 2.5 Use the palette from the UMG editor

The Blueprint node works anywhere, but inside a widget there is a better way.
Open any Widget Blueprint and look in the palette under **Design System**:

| Widget | Token properties | Drives |
|---|---|---|
| **Token Border** | Brush Colour Token, Content Colour Token | `BrushColor`, `ContentColorAndOpacity` |
| **Token Image** | Colour Token | `ColorAndOpacity` |
| **Token Text Block** | Colour Token | `ColorAndOpacity` |

Drop a **Token Border** on the canvas and set *Brush Colour Token* to
`component.chat.bg_bubble`. Three things should happen:

1. A **swatch** appears next to the dropdown showing the resolved colour, with
   alpha drawn as a split block — several tokens differ only in opacity, and a
   combined block makes `bg_blade` (80%) and `bg_abnormal` (40%) look like
   different colours rather than the same one at different strengths.
2. Expanding the row shows **Resolves to** — the hex, the alpha, the tier, and
   the alias chain in Figma names:
   `Component/Chat/bg-bubble  →  Semantic/bg-primary  →  Global/gray-900`.
   That row is what answers "why is this colour this colour" without opening Figma.
3. The border in the Designer turns that colour immediately — no compile, no
   PreConstruct node, no property binding.

Then prove the point: change that colour in Figma, re-export, re-sync, and the
widget follows without being touched. That is the difference between a token
reference and a copied RGBA value.

**What the widget stores is the token name, not the colour.** Once a token is
set, the underlying colour property is machine-owned: editing *Brush Color* by
hand will appear to work and then be overwritten on the next synchronise. Clear
the token if you want manual control back. Leaving a token as `None` makes the
widget behave exactly like the stock one it derives from, so reparenting an
existing `Border` to a `TokenBorder` is safe and reversible.

The details panel also warns, in amber, when a token is **deprecated** (gone
from Figma, still resolving to its last value), **unknown** (resolving to
magenta), or **imported but not published** to this project.

---

## 3. The Figma half

### 3.1 Load the plugin

Figma **desktop** app → **Plugins → Development → Import plugin from manifest…**
→ pick `figma-plugin/manifest.json`.

There is no build step. `manifest.json` points straight at `code.js`, so editing
the source and re-running the plugin is the whole edit loop — **except for
`manifest.json` itself**, which Figma reads once at import. After a manifest
change, remove the plugin from *Plugins → Development* and import it again, or
the old permissions stay in force.

### 3.2 Scan

Open the [test design system file](https://www.figma.com/design/FruupulmvbUZbkUlTQ0kIm)
→ **Plugins → Development → Figma Token Bridge** → **Scan this file**.

Expected report:

```
Read mode          library file
Variables found    25
```

`library file` is the important word. It means this file *owns* its variables, so
the scan is authoritative — every variable is returned whether or not anything on
the canvas uses it. The other mode, `consuming file`, appears when you run the
plugin in a file that merely subscribes to a published library; there the scan
can only find variables bound to something on the current page, and both the
plugin and the Unreal importer say so rather than letting you mistake a partial
export for a complete one.

The group list should show `Blades (4)`, `Chat (3)` and `Icons - Secondary (3)`
under Component, plus the Global and Semantic tiers.

### 3.3 Create the link

**New link…** →

| Field | Value |
|---|---|
| Link name | `FigmaBridgeTest — smoke test` |
| Unreal project id | the id from [§2.3](#23-get-the-project-id) |
| Unreal project name | `FigmaBridgeTest` |
| Figma file key | `FruupulmvbUZbkUlTQ0kIm` |
| Publish to Unreal | tick the three Component groups |

The file key field exists because `figma.fileKey` is readable only by **private
organisation plugins** that also set `enablePrivatePluginApi`. A plugin loaded
through *Import plugin from manifest* — which is how this one is developed and
tested — cannot read it, so the export would carry `fileKey: null` and Unreal
would refuse it on the file-key interlock. Having the designer confirm it from
their own address bar is symmetric with pasting the Unreal project id, and it
means the interlock works without publishing the plugin first.

Both manifest flags are set, so once the plugin *is* published privately to the
org the field prefills itself from the API and the designer just confirms it.

**Save link.** It is stored on the Figma document itself, not in your local
plugin storage, so everyone in the file sees the same set of links.

### 3.4 Export

**Export tokens.json** on the link card. The report should say 10 published, 25
exported in total, with one warning about `icon-secondary-Inactive`. That gap
between 10 and 25 is the design, not a bug: publishing controls what Unreal
*offers* in the Blueprint dropdown, while the whole alias graph still crosses the
boundary, because a Component token cannot resolve to a literal without its
Semantic and Global parents.

**Download tokens.json**, save it as `tokens/FigmaBridgeTest.tokens.json`, and
diff it against the file §2.4 generated. They should agree on every token — same
keys, same names, same resolved values. If they do, the plugin running inside
Figma and the offline harness are computing the same thing, which is the entire
reason PART 1 of `code.js` is kept free of the `figma` global.

Then re-run **Tools → Sync Design Tokens** in Unreal. A second import of
identical content should report `0 added, 0 updated, 25 unchanged`.

### 3.5 Publish, then pull (no file sent)

This is the everyday path; §3.4's download stays as a fallback. The designer
publishes into the Figma file itself, and Unreal fetches it.

**Re-import the plugin first** (§3.1): the manifest gained a menu and a
properties-panel button.

**In Figma:** **Publish to Unreal** on the link card. The card should then read
`published just now by <you> · 10 tokens`. After the first link exists, the same
thing is one click with the plugin closed:

- **Plugins → Development → Figma Token Bridge → Publish all links to Unreal**, or
- the **Publish tokens to Unreal** button in the right-hand panel when nothing is
  selected.

Either way, a toast confirms it and no window opens.

**In Unreal, once per developer:** create a Figma personal access token (Figma →
Settings → Security → *Personal access tokens*, scope *File content: read*) and
paste it into **Editor Preferences → Plugins → Figma Token Bridge → Figma Access
Token**. It is saved under `Saved/`, never in `DefaultGame.ini`, so it is never
committed. `FIGMA_ACCESS_TOKEN` in the environment works too.

**Tools → Sync Design Tokens.** Expected: a *Fetching the latest publish from
Figma…* toast, then the usual summary with *From Figma, published <time> by
<you>.* underneath. `tokens/FigmaBridgeTest.tokens.json` is rewritten with what
was fetched, in the same 2-space layout the download uses, so it diffs cleanly and
can still be reviewed and committed.

**First live run — check this specifically.** The automation test builds Figma's
response by hand. What it cannot prove is that Figma really returns shared plugin
data stored on the *document* node from
`GET /v1/files/:key?depth=1&plugin_data=shared`. If the first sync says *Nothing
has been published* straight after a publish, that assumption is the cause; tell
whoever maintains the plugin.

Without a token, Sync still works: it imports the local `tokens.json` and warns
that it did not fetch from Figma. Turn off **Pull from Figma** in project settings
to always use the local file.

**Seat type matters.** Figma allows a Dev or Full seat about 20 file reads a
minute, but a **View or Collab seat only 20 a month**. Sync costs one read. The
opt-in *Check For Published Tokens On Startup* costs one read per editor launch,
so leave it off on a View seat. If you hit the limit, the error says so and names
the seat type.

---

## 4. The loop you'll actually live in

1. Designer changes a colour in Figma
2. Designer clicks **Publish tokens to Unreal**, with nothing to download or send
3. One developer runs **Tools → Sync Design Tokens** and commits the rewritten
   `tokens.json` and the generated asset. **The `tokens.json` diff is the review
   gate.**
4. Everyone else gets it from source control; no Figma account needed
5. Every widget referencing the token follows; nothing is regenerated

---

## 5. The tests worth running deliberately

Each of these protects against a failure that would otherwise be silent.

### The colour space

```powershell
node tools/build-tokens.cjs --fixture tools/fixtures/tokenbridgetest.raw.json
```

60 assertions, well under a second, no Figma access. The last 16 cover publishing:
chunking, the round trip, and refusing a missing or corrupted chunk. The headline one: `#00d86c`
must arrive as `(0.000000, 0.686685, 0.149960)`. Copying the bytes across instead
gives `(0, 0.847059, 0.423529)` — 19% out in green and 2.8× in blue. That is
wrong in a way that looks like a design decision, which is why it survives review
and why both test suites pin it.

Run it with no arguments to check the other fixture, the 171-variable capture
from the real Rocket Science system.

### The two interlocks

Neither side can be silently wrong about where tokens are going. Try to break
each on purpose:

| Test | How | Expected |
|---|---|---|
| **Figma → Unreal** | Build a `tokens.json` with a different `--project-id`, then sync | Refused; the error names *both* project ids |
| **Unreal → Figma** | Set *Figma File Key* to something else, then sync | Refused; the error names both file keys |
| **No link at all** | Omit `--project-id` — the default is a deliberately impossible one | Refused; the error hands you this project's id |

Each has an off switch, `bRequireProjectIdMatch` and `bRequireFileKeyMatch`, for
a deliberate re-link. Both default on.

### Deletion is a two-step

Delete a Component token in Figma, re-export, re-sync. The token should **not**
vanish from the asset. It is marked deprecated, keeps resolving to its last known
value, and warns. Removing it is a separate deliberate act — otherwise a tidy-up
in Figma turns shipped UI magenta in a build nobody looked at.

### Rename tracking

Rename a Component token in Figma, re-export, re-sync. The summary should say
`1 renamed`, not `1 added, 1 deprecated`. Identity is Figma's stable variable
`key`, not the name, which is what lets the importer tell the difference.

---

## 6. When it doesn't work

| Symptom | Cause |
|---|---|
| `Could not find Unreal Engine 5.8` | pass `-EnginePath "C:\Path\To\UE_5.8"` |
| UBT cannot find a compiler | the Visual Studio C++ workload from §1 |
| `the plugin is not linked into the test project` | run `tools/setup-test-project.ps1` |
| Plugin missing from Figma's menu | development plugins only load in the **desktop** app, not the browser |
| `"currentuser" permission not specified` | the manifest changed since you imported it — re-import the plugin (§3.1) |
| Import refused over the file key | the export carried no key. Fill in *Figma file key* on the link, or clear *Figma File Key* in project settings to downgrade the check to a warning |
| Sync warns *Imported the local tokens.json, not Figma's latest publish* | no access token for this user, no file key in project settings, or *Pull from Figma* is off. The warning says which |
| *Nothing has been published to Unreal from Figma file…* | nobody has clicked Publish in this file yet. If they have, see the first-live-run note in §3.5 |
| *…has publishes for …, but none for this project* | a link exists, but its project id is not this project's. Fix the id on the link and publish again |
| *The publish … is incomplete* or *failed its checksum* | two designers probably published at the same moment. Publish once more |
| Figma 403 on sync | the token is wrong, expired, or lacks *File content: read* |
| Figma 429 on sync | rate limit. On a View or Collab seat that is 20 reads a month, so use a Dev/Full seat's token or the committed file |
| Scan finds 0 variables | you are in a file that neither owns nor binds any — open the library file |
| `No tests ran` | the `FigmaTokenBridgeEditor` module did not load; check the Output Log for `LogFigmaTokens` |
| `Incompatible or missing module` | a previous build failed partway. Rebuild; `build-unreal.ps1` now prints the compiler errors |
| Editor exits 255 with no test output | transient startup failure, seen once. Re-run before investigating |
| Blueprint dropdown is empty | no import has run yet, or *Active Tokens* in project settings is unset |
| Every colour comes out magenta | that is the fallback. Either there is no token asset, or the token name is unknown — check `LogFigmaTokens` |
