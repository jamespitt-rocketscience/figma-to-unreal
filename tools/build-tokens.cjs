#!/usr/bin/env node
/*
 * Runs the Figma plugin's pure export logic over a captured fixture, writes
 * tokens.json, and asserts the results. No Figma access needed.
 *
 *   node tools/build-tokens.cjs
 *
 * This is the regression harness for PART 1 of figma-plugin/code.js. If the
 * plugin's naming, alias resolution or colour handling ever changes behaviour,
 * this fails before anything reaches Unreal.
 */

const fs = require('fs');
const path = require('path');
const bridge = require('../figma-plugin/code.js');

const ROOT = path.resolve(__dirname, '..');
/*
 * Flags exist so the Unreal importer can be exercised with no Figma access at
 * all. The handshake means an export is only accepted by the project it names,
 * so testing the importer against a real project needs a tokens.json carrying
 * that project's id — which is otherwise only obtainable by round-tripping
 * through Figma. --project-id short-circuits that.
 *
 *   node tools/build-tokens.cjs
 *   node tools/build-tokens.cjs --fixture tools/fixtures/tokenbridgetest.raw.json
 *
 * Declining to write is not a failure: the assertions are the harness's job, and
 * a refusal only means an existing export was left alone.
 *
 *   node tools/build-tokens.cjs --project-id <guid> \
 *                                --project-name FigmaBridgeTest \
 *                                --out tokens/FigmaBridgeTest.tokens.json
 */
function arg(name, fallback) {
  const i = process.argv.indexOf('--' + name);
  if (i === -1) return fallback;
  const v = process.argv[i + 1];
  if (v === undefined || v.startsWith('--')) {
    console.error('--' + name + ' needs a value');
    process.exit(2);
  }
  return v;
}

const RAW = path.resolve(ROOT, arg('fixture', path.join('tools', 'fixtures', 'figmaunrealtest.raw.json')));
const raw = JSON.parse(fs.readFileSync(RAW, 'utf8'));
const OUT = path.resolve(ROOT, arg('out', raw.out || path.join('tokens', 'FigmaUnrealTest.tokens.json')));

/*
 * Counts differ per fixture; everything else — alias resolution, naming, colour
 * maths, the publishing rules — is meant to hold for any design system that
 * follows the three-tier convention. So the counts live in the fixture and the
 * assertions below stay shared. A fixture that omits them keeps the original
 * FigmaUnrealTest numbers, so the committed regression case is unchanged.
 */
const EXPECT = raw.expect || {
  total: 171,
  tiers: { Component: 103, Semantic: 37, Global: 31 },
  publishedComponentTier: 103,
  publishedBladesAndChat: 35
};

let failures = 0;
function check(label, actual, expected) {
  const a = JSON.stringify(actual);
  const e = JSON.stringify(expected);
  if (a === e) {
    console.log(`  ok    ${label}`);
  } else {
    console.log(`  FAIL  ${label}\n          expected ${e}\n          actual   ${a}`);
    failures++;
  }
}

function f4(n) { return Number(n.toFixed(4)); }
function f6(n) { return Number(n.toFixed(6)); }

// ---------------------------------------------------------------- build

const records = raw.colours.map(([name, key, aliasKey, srgbHex, alpha, scopes]) => ({
  key,
  name,
  aliasKey,
  srgbHex,
  alpha,
  scopes: scopes || ['ALL_SCOPES']
}));

// Stands in for the link a designer authors in the plugin.
// Deliberately impossible: RFC 4122 variant/version bits are valid but the value
// is all zeros, so no real project will ever generate it. An export carrying it
// is refused by every project, which is what makes it a safe default.
const PLACEHOLDER_PROJECT_ID = '00000000-0000-4000-8000-000000000001';

const FIXTURE_LINK = {
  id: 'link-fixture',
  linkName: arg('link-name', 'Fixture — dev sandbox'),
  // The default is a deliberately impossible project id: a tokens.json built
  // with it is refused by every real project, so it cannot be mistaken for a
  // genuine export. Pass --project-id to aim one at a project on purpose.
  projectId: arg('project-id', PLACEHOLDER_PROJECT_ID),
  projectName: arg('project-name', 'FigmaUnrealTestProject'),
  published: arg('published', 'Component').split(',').map(function (s) { return s.trim(); }).filter(Boolean),
  linkedBy: 'tools/build-tokens.cjs',
  linkedAt: '2026-08-19T00:00:00.000Z'
};

const FILE_KEY = arg('file-key', raw.source.fileKey);

const built = bridge.buildDocument(records, {
  fileKey: FILE_KEY,
  fileName: raw.source.fileName,
  readMode: raw.source.readMode,
  collectionId: raw.source.collectionId,
  defaultModeId: raw.source.modeId,
  modes: [{ id: raw.source.modeId, name: 'Value' }],
  published: FIXTURE_LINK.published,
  target: FIXTURE_LINK,
  // Fixed so the committed file is byte-stable and diffs stay readable.
  exportedAt: '2026-08-19T00:00:00.000Z',
  exportedBy: 'tools/build-tokens.cjs'
});

console.log(`\nFigma Token Bridge — fixture build`);
console.log(`  source   ${raw.source.fileName} (${FILE_KEY})`);
console.log(`  target   ${FIXTURE_LINK.projectName} (${FIXTURE_LINK.projectId})`);
console.log(`  readMode ${raw.source.readMode}`);
console.log(`  tokens   ${built.doc.colours.length}`);
console.log(`  tiers    ${JSON.stringify(built.counts)}`);
console.log(`  errors   ${built.errors.length}`);
console.log(`  warnings ${built.warnings.length}`);

if (built.errors.length) {
  console.log('\nErrors:');
  built.errors.forEach((e) => console.log(`  - ${e}`));
}
if (built.warnings.length) {
  console.log('\nWarnings:');
  built.warnings.forEach((w) => console.log(`  - ${w}`));
}

// ---------------------------------------------------------------- assertions

const byName = {};
built.doc.colours.forEach((t) => { byName[t.name] = t; });

const MODE = raw.source.modeId;
const val = (name) => byName[name].values[MODE];

console.log('\nAssertions:');

check('no blocking errors', built.errors.length, 0);
check('total tokens', built.doc.colours.length, EXPECT.total);
check('Component tier count', built.counts.Component, EXPECT.tiers.Component);
check('Semantic tier count', built.counts.Semantic, EXPECT.tiers.Semantic);
check('Global tier count', built.counts.Global, EXPECT.tiers.Global);

// Name normalisation, including the casing slip in the source data.
check('normalises groups with spaces and dashes',
  bridge.normaliseTokenName('Component/Icons - Secondary/vital-heart-rate'),
  'component.icons_secondary.vital_heart_rate');
check('lower-cases a stray capital',
  bridge.normaliseTokenName('Component/Icons - Secondary/icon-secondary-Inactive'),
  'component.icons_secondary.icon_secondary_inactive');

// Values are keyed by mode so Figma modes can be added without a schema break.
check('source declares its modes',
  built.doc.source.modes,
  [{ id: MODE, name: 'Value' }]);
check('source names the default mode', built.doc.source.defaultModeId, MODE);
check('exactly one mode today', Object.keys(val('global.green_400') ? byName['global.green_400'].values : {}).length, 1);

// Alias chains resolve Component -> Semantic -> Global.
check('accent-goals resolves through two aliases',
  val('component.blades.accent_goals'),
  { srgbHex: '#00d86c', alpha: 1 });
check('accent-goals records its Semantic parent',
  byName['component.blades.accent_goals'].aliasOf,
  byName['semantic.accent_green'].key);
check('vital-heart-rate shares the Goals green by aliasing the same parent',
  byName['component.icons_secondary.vital_heart_rate'].aliasOf,
  byName['component.blades.accent_goals'].aliasOf);

// Alpha survives the chain and is never transfer-encoded.
check('bg-blade carries 80% alpha',
  val('component.blades.bg_blade'),
  { srgbHex: '#1c1b1a', alpha: 0.8 });
check('bg-abnormal carries 40% alpha',
  val('component.blades.bg_abnormal'),
  { srgbHex: '#c7002e', alpha: 0.4 });

// Primitives keep their own literal value as well as the resolved one.
check('Global primitive has an own value',
  byName['global.green_400'].own[MODE],
  { srgbHex: '#00d86c', alpha: 1 });
check('Component token has no own value',
  byName['component.blades.accent_goals'].own,
  null);

// resolvesTo bottoms out on the Global primitive, two hops down.
check('accent-goals resolves to the Global green primitive',
  byName['component.blades.accent_goals'].resolvesTo,
  byName['global.green_400'].key);
check('a primitive resolves to itself',
  byName['global.gray_900'].resolvesTo,
  byName['global.gray_900'].key);
check('the alias graph has no duplicated primitives',
  built.warnings.filter((w) => w.indexOf('drift apart') !== -1).length,
  0);

// The colour-space conversion — the headline Phase 0 test vector.
// Asserted at 6dp so it is unambiguous: the blue channel is 0.149960, which
// rounds to 0.1500 at 4dp, not 0.1499.
const lin = bridge.srgbHexToLinear('#00d86c');
check('#00d86c -> linear', [f6(lin.r), f6(lin.g), f6(lin.b)], [0, 0.686685, 0.14996]);

const naive = [0, f4(216 / 255), f4(108 / 255)];
console.log(`  note  naive byte copy would give ${JSON.stringify(naive)} — visibly wrong`);

check('#ffffff -> linear white', [f4(bridge.srgbHexToLinear('#ffffff').r)], [1]);
check('#000000 -> linear black', [f4(bridge.srgbHexToLinear('#000000').r)], [0]);
// 0x0a/255 = 0.0392 sits below the 0.04045 knee, so it takes the linear segment.
check('below-knee value uses the linear segment',
  f4(bridge.srgbHexToLinear('#0a0a0a').r),
  f4((10 / 255) / 12.92));

// The warning we expect the designer to see and act on.
check('warns about the upper-case leaf name',
  built.warnings.some((w) => w.indexOf('icon-secondary-Inactive') !== -1 && w.indexOf('upper-case') !== -1),
  true);

// --- the designer-authored link ------------------------------------------

check('target carries the Unreal project id', built.doc.target.projectId, FIXTURE_LINK.projectId);
check('target carries the link name', built.doc.target.linkName, FIXTURE_LINK.linkName);
check('target records what was published', built.doc.target.published, ['Component']);

// Publishing gates what Unreal OFFERS, not what crosses the boundary. The whole
// graph still ships, or Component tokens could not resolve to a literal.
if (FIXTURE_LINK.published.length === 1 && FIXTURE_LINK.published[0] === 'Component') {
  check('published count is the Component tier', built.publishedCount, EXPECT.publishedComponentTier);
}
check('everything is still exported', built.doc.colours.length, EXPECT.total);
check('a Component token is published', byName['component.blades.accent_goals'].published, true);
check('a Semantic token is not', byName['semantic.accent_green'].published, false);
check('a Global token is not', byName['global.green_400'].published, false);

check('isPublished matches a whole tier', bridge.isPublished('Component', 'Blades', ['Component']), true);
check('isPublished matches a tier/group', bridge.isPublished('Component', 'Blades', ['Component/Blades']), true);
check('isPublished rejects a different group', bridge.isPublished('Component', 'Chat', ['Component/Blades']), false);
check('isPublished defaults to Component', bridge.isPublished('Component', 'Blades', []), true);
check('isPublished default excludes Global', bridge.isPublished('Global', '', []), false);

// A narrower selection publishes less but still exports everything.
{
  const narrow = bridge.buildDocument(records, {
    fileKey: raw.source.fileKey, fileName: raw.source.fileName, readMode: raw.source.readMode,
    defaultModeId: raw.source.modeId, modes: [{ id: raw.source.modeId, name: 'Value' }],
    published: ['Component/Blades', 'Component/Chat'],
    target: Object.assign({}, FIXTURE_LINK, { published: ['Component/Blades', 'Component/Chat'] }),
    exportedAt: '2026-08-19T00:00:00.000Z'
  });
  check('a narrow selection publishes fewer', narrow.publishedCount, EXPECT.publishedBladesAndChat);
  check('a narrow selection still exports all', narrow.doc.colours.length, EXPECT.total);
  check('narrow selection has no errors', narrow.errors.length, 0);
}

// An export with no link is refused — the handshake is not optional.
{
  const noLink = bridge.buildDocument(records, {
    fileKey: raw.source.fileKey, readMode: 'local',
    defaultModeId: raw.source.modeId, exportedAt: '2026-08-19T00:00:00.000Z'
  });
  check('refuses to export without a link',
    noLink.errors.some((e) => e.indexOf('no Unreal project link') !== -1), true);
}

// Publishing nothing is a mistake worth blocking rather than shipping.
{
  const nothing = bridge.buildDocument(records, {
    fileKey: raw.source.fileKey, readMode: 'local',
    defaultModeId: raw.source.modeId, exportedAt: '2026-08-19T00:00:00.000Z',
    published: ['Nonexistent/Group'],
    target: Object.assign({}, FIXTURE_LINK, { published: ['Nonexistent/Group'] })
  });
  check('refuses to publish nothing',
    nothing.errors.some((e) => e.indexOf('Nothing is published') !== -1), true);
}

// ---------------------------------------------------------------- write

/*
 * Refuse to retarget an existing export.
 *
 * A plain run defaults to an impossible project id, and a fixture can name its
 * own output path — so running the harness to check the assertions will happily
 * overwrite a tokens.json that was aimed at a real project. The only symptom is
 * Unreal later refusing the import over an id mismatch, which reads like a bug
 * in the handshake rather than a clobbered file. That is exactly how this was
 * found, so it is worth a guard rather than a note in the README.
 */
let refusedToWrite = false;

if (fs.existsSync(OUT) && !process.argv.includes('--force')) {
  let existing = null;
  try {
    existing = JSON.parse(fs.readFileSync(OUT, 'utf8')).target;
  } catch (e) {
    // Unreadable or not ours: nothing worth protecting, fall through and write.
  }

  const was = existing && existing.projectId;
  const now = built.doc.target && built.doc.target.projectId;

  // Only one direction is ever a mistake. Aiming a placeholder export at a real
  // project is the whole point of --project-id, so that must go through. Aiming a
  // real export back at the placeholder is what a bare assertions run does, and it
  // silently breaks the import. Real-to-a-different-real is ambiguous, so refuse
  // that too and make the caller say which they meant.
  const downgradeToPlaceholder = now === PLACEHOLDER_PROJECT_ID && was !== PLACEHOLDER_PROJECT_ID;
  const retargetBetweenProjects = was !== PLACEHOLDER_PROJECT_ID && now !== PLACEHOLDER_PROJECT_ID;

  if (was && now && was !== now && (downgradeToPlaceholder || retargetBetweenProjects)) {
    refusedToWrite = true;
    console.log(`\nDid NOT write ${path.relative(ROOT, OUT)} — it targets a different project.`);
    console.log(`  on disk:   ${was} (${existing.projectName || 'unnamed'})`);
    console.log(`  this run:  ${now} (${built.doc.target.projectName || 'unnamed'})`);
    console.log(`\n  To regenerate it unchanged:  --project-id ${was}`);
    console.log(`  To replace it anyway:        --force`);
    console.log(`  To write elsewhere:          --out <path>`);
  }
}

if (!refusedToWrite) {
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, JSON.stringify(built.doc, null, 2) + '\n', 'utf8');
  console.log(`\nWrote ${path.relative(ROOT, OUT)} (${(fs.statSync(OUT).size / 1024).toFixed(1)} KB)`);
}

if (failures) {
  console.log(`\n${failures} assertion(s) failed.\n`);
  process.exit(1);
}
console.log('\nAll assertions passed.\n');
