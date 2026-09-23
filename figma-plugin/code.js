/*
 * Figma Token Bridge — plugin main thread.
 *
 * Plain JavaScript on purpose. Phase 0 keeps the moving parts to a minimum:
 * no TypeScript, no bundler, no build step. `manifest.json` points straight at
 * this file. TS + esbuild is a phase-1 upgrade once the contract has settled.
 *
 * The pure functions in PART 1 never touch the `figma` global, so tools/ can
 * require() this file in Node and exercise them against a captured fixture.
 * PART 2 is the only part that talks to Figma.
 *
 * Why this reads variables two different ways
 * -------------------------------------------
 * A design system usually lives in a published library file and is *subscribed
 * to* by the files that use it. In a subscribing file `getLocalVariablesAsync()`
 * returns nothing at all — the variables are remote. So:
 *
 *   library mode   run inside the file that owns the variables. Authoritative:
 *                  every variable is returned whether or not it is used.
 *   consumer mode  run inside a file that subscribes. We discover variables by
 *                  walking what is actually bound on the page, then follow the
 *                  alias chain. Only finds what the canvas references.
 *
 * Identity is `variable.key`, not `variable.id`. Ids are file-scoped, so they
 * all change when a design system moves to a different file. Keys survive.
 */

/* ============================================================
   PART 1 — pure logic (no `figma` global, safe to unit test)
   ============================================================ */

var SCHEMA = 'figma-token-bridge/1';
var MAX_ALIAS_DEPTH = 12;

/**
 * Figma name -> stable, Unreal-friendly token name.
 *   "Component/Icons - Secondary/vital-heart-rate"
 *     -> "component.icons_secondary.vital_heart_rate"
 * Lowercasing also quietly absorbs casing slips like "icon-secondary-Inactive".
 */
function normaliseTokenName(figmaName) {
  return String(figmaName)
    .split('/')
    .map(function (seg) {
      return seg
        .trim()
        .toLowerCase()
        .replace(/[^a-z0-9]+/g, '_')
        .replace(/^_+|_+$/g, '');
    })
    .filter(function (seg) { return seg.length > 0; })
    .join('.');
}

/** First path segment is the tier: Global / Semantic / Component. */
function tierOf(figmaName) {
  var head = String(figmaName).split('/')[0].trim();
  if (head === 'Global' || head === 'Semantic' || head === 'Component') return head;
  return 'Other';
}

/** Group is everything between tier and leaf: "Component/Blades/x" -> "Blades". */
function groupOf(figmaName) {
  var parts = String(figmaName).split('/');
  return parts.length > 2 ? parts.slice(1, -1).join('/') : '';
}

/**
 * Is this token part of the surface the designer published to Unreal?
 *
 * `selection` is a list of "Tier" or "Tier/Group" strings. An empty or absent
 * selection defaults to the Component tier, which matches the source board's
 * instruction to choose from there.
 *
 * Note what this does NOT do: unpublished tokens are still exported. The
 * importer needs the whole alias graph to resolve a Component token down to a
 * literal — drop the Global tier and nothing resolves. Publishing controls what
 * Unreal *offers* in its token picker, not what crosses the boundary.
 */
function isPublished(tier, group, selection) {
  if (!selection || !selection.length) return tier === 'Component';
  for (var i = 0; i < selection.length; i++) {
    var entry = selection[i];
    if (entry === tier) return true;
    if (group && entry === tier + '/' + group) return true;
  }
  return false;
}

/**
 * Follow aliasKey until a record carries a literal value.
 * Returns { srgbHex, alpha, chain } or { error, chain }.
 */
function resolveTerminal(byKey, key, chain) {
  chain = chain || [];
  if (chain.length > MAX_ALIAS_DEPTH) return { error: 'alias chain deeper than ' + MAX_ALIAS_DEPTH, chain: chain };
  if (chain.indexOf(key) !== -1) return { error: 'alias cycle', chain: chain.concat([key]) };

  var rec = byKey[key];
  if (!rec) return { error: 'alias target not found: ' + key, chain: chain };

  var next = chain.concat([key]);
  if (rec.srgbHex) return { srgbHex: rec.srgbHex, alpha: rec.alpha === undefined ? 1 : rec.alpha, chain: next };
  if (rec.aliasKey) return resolveTerminal(byKey, rec.aliasKey, next);
  return { error: 'no value and no alias', chain: next };
}

/** sRGB 0-1 channel -> linear. Reference implementation; Unreal does its own. */
function srgbChannelToLinear(c) {
  return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
}

/** "#rrggbb" -> { r, g, b } linear floats. Alpha is never transfer-encoded. */
function srgbHexToLinear(hex) {
  var m = /^#?([0-9a-fA-F]{6})$/.exec(String(hex).trim());
  if (!m) return null;
  var n = parseInt(m[1], 16);
  return {
    r: srgbChannelToLinear(((n >> 16) & 255) / 255),
    g: srgbChannelToLinear(((n >> 8) & 255) / 255),
    b: srgbChannelToLinear((n & 255) / 255)
  };
}

/**
 * records: [{ key, name, aliasKey|null, srgbHex|null, alpha|null, scopes? }]
 * meta:    { fileKey, fileName, readMode, collectionId, modeId, exportedAt, exportedBy }
 *
 * Returns { doc, counts, warnings, errors }. Errors are blocking; the UI will
 * not offer a download while any are present.
 */
function buildDocument(records, meta) {
  var errors = [];
  var warnings = [];
  var byKey = {};
  var i;

  var defaultModeId = meta.defaultModeId || meta.modeId || 'default';
  var modes = (meta.modes && meta.modes.length)
    ? meta.modes
    : [{ id: defaultModeId, name: 'Value' }];

  for (i = 0; i < records.length; i++) {
    var r = records[i];
    if (byKey[r.key]) {
      errors.push('duplicate variable key ' + r.key + ' (' + byKey[r.key].name + ' vs ' + r.name + ')');
      continue;
    }
    byKey[r.key] = r;
  }

  // Normalised names must stay unique — two Figma names can collapse onto one.
  var seenNames = {};
  for (i = 0; i < records.length; i++) {
    var norm = normaliseTokenName(records[i].name);
    if (seenNames[norm]) {
      errors.push('name collision: "' + seenNames[norm] + '" and "' + records[i].name + '" both normalise to "' + norm + '"');
    } else {
      seenNames[norm] = records[i].name;
    }
  }

  var tokens = [];
  var counts = {};

  for (i = 0; i < records.length; i++) {
    var rec = records[i];
    var tier = tierOf(rec.name);
    counts[tier] = (counts[tier] || 0) + 1;

    if (tier === 'Other') {
      warnings.push('"' + rec.name + '" is not under Global/, Semantic/ or Component/ — it will import but sits outside the tier convention');
    }

    var resolved = resolveTerminal(byKey, rec.key, []);
    if (resolved.error) {
      errors.push('"' + rec.name + '": ' + resolved.error);
      continue;
    }
    if (resolved.chain.length > 4) {
      warnings.push('"' + rec.name + '" resolves through ' + (resolved.chain.length - 1) + ' aliases — deep chains are hard to reason about');
    }

    // The leaf of the Figma name should be lower-case by convention.
    var leaf = String(rec.name).split('/').pop();
    if (leaf !== leaf.toLowerCase()) {
      warnings.push('"' + rec.name + '" has upper-case characters in its leaf name — inconsistent with the other tokens');
    }

    if (rec.scopes && rec.scopes.length === 1 && rec.scopes[0] === 'ALL_SCOPES') {
      // Collected once below rather than per token; see the summary push after the loop.
      counts.__allScopes = (counts.__allScopes || 0) + 1;
    }

    // Values are keyed by mode even though the system currently has exactly one.
    // A single-entry map costs nothing now and means adding light/dark later is
    // additive rather than a schema break.
    var values = {};
    values[defaultModeId] = { srgbHex: resolved.srgbHex, alpha: resolved.alpha };

    var own = null;
    if (rec.srgbHex) {
      own = {};
      own[defaultModeId] = { srgbHex: rec.srgbHex, alpha: rec.alpha === undefined ? 1 : rec.alpha };
    }

    var group = groupOf(rec.name);

    tokens.push({
      key: rec.key,
      name: normaliseTokenName(rec.name),
      figmaName: rec.name,
      tier: tier,
      group: group,
      published: isPublished(tier, group, meta.published),
      aliasOf: rec.aliasKey || null,
      // Terminal primitive the chain lands on. Same as `key` for primitives.
      resolvesTo: resolved.chain[resolved.chain.length - 1],
      values: values,
      own: own
    });
  }

  if (counts.__allScopes) {
    warnings.push(counts.__allScopes + ' variables use ALL_SCOPES — narrowing scopes in Figma keeps property pickers clean (does not affect this export)');
    delete counts.__allScopes;
  }

  // Genuine drift is two tokens that LOOK identical but are driven by different
  // primitives — change one primitive and they silently diverge.
  //
  // Sharing an immediate parent is not required: `accent-my-class` pointing at
  // Semantic/accent-blue while `icon-new` points at Semantic/alert-new is correct
  // design. They mean different things and are free to diverge later. What
  // matters is whether they bottom out on the same primitive.
  var byValue = {};
  for (i = 0; i < tokens.length; i++) {
    var t = tokens[i];
    var v = t.values[defaultModeId];
    var sig = v.srgbHex + '@' + v.alpha;
    (byValue[sig] = byValue[sig] || []).push(t);
  }
  Object.keys(byValue).forEach(function (sig) {
    var group = byValue[sig];
    if (group.length < 2) return;
    var roots = {};
    group.forEach(function (t) { roots[t.resolvesTo] = true; });
    if (Object.keys(roots).length > 1) {
      warnings.push(Object.keys(roots).length + ' different primitives all equal ' + sig +
        ' — duplicated values in the primitive tier will drift apart: ' +
        group.map(function (t) { return t.figmaName; }).join(', '));
    }
  });

  tokens.sort(function (a, b) { return a.name < b.name ? -1 : a.name > b.name ? 1 : 0; });

  var publishedCount = 0;
  for (i = 0; i < tokens.length; i++) {
    if (tokens[i].published) publishedCount++;
  }
  if (publishedCount === 0) {
    errors.push('Nothing is published to Unreal. Select at least one tier or group in the link.');
  }

  // The designer half of the handshake. The Unreal importer refuses an export
  // whose target does not match the project it is being imported into, so a
  // link authored here cannot deliver tokens to the wrong project.
  var target = meta.target
    ? {
        projectId: meta.target.projectId || null,
        projectName: meta.target.projectName || null,
        linkName: meta.target.linkName || null,
        published: (meta.published && meta.published.length) ? meta.published : ['Component'],
        linkedBy: meta.target.linkedBy || null,
        linkedAt: meta.target.linkedAt || null
      }
    : null;

  if (!target || !target.projectId) {
    errors.push('This export has no Unreal project link. Create one before exporting.');
  }

  return {
    doc: {
      schema: SCHEMA,
      source: {
        fileKey: meta.fileKey || null,
        fileName: meta.fileName || null,
        readMode: meta.readMode,
        collectionId: meta.collectionId || null,
        defaultModeId: defaultModeId,
        modes: modes,
        exportedAt: meta.exportedAt,
        exportedBy: meta.exportedBy || null
      },
      target: target,
      colours: tokens
    },
    counts: counts,
    publishedCount: publishedCount,
    warnings: warnings,
    errors: errors
  };
}

/* ============================================================
   PART 2 — Figma runtime
   ============================================================ */

var h2 = function (v) {
  var s = Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16);
  return s.length === 1 ? '0' + s : s;
};
var toHex = function (c) { return '#' + h2(c.r) + h2(c.g) + h2(c.b); };
var r3 = function (n) { return Math.round(n * 1000) / 1000; };

/**
 * Turn a Figma Variable into a flat record. Colour variables only.
 *
 * preferredModeId should be the variable's OWN collection default. Reading
 * Object.keys(valuesByMode)[0] instead is only safe while a collection has one
 * mode: Figma makes no promise about key order, so the moment someone adds a
 * light/dark pair the export would start carrying whichever mode happened to
 * come first — a wrong colour that looks entirely plausible.
 */
function recordFromVariable(v, preferredModeId) {
  var modeIds = Object.keys(v.valuesByMode);
  var modeId = (preferredModeId && v.valuesByMode[preferredModeId] !== undefined)
    ? preferredModeId
    : modeIds[0];
  var val = v.valuesByMode[modeId];
  var rec = { key: v.key, name: v.name, aliasKey: null, srgbHex: null, alpha: null, scopes: v.scopes, modeId: modeId, _aliasId: null };

  if (val && typeof val === 'object' && val.type === 'VARIABLE_ALIAS') {
    rec._aliasId = val.id;
  } else if (val && typeof val === 'object' && 'r' in val) {
    rec.srgbHex = toHex(val);
    rec.alpha = val.a === undefined ? 1 : r3(val.a);
  }
  return rec;
}

/**
 * Every variable id bound to a colour-ish property anywhere on the page.
 *
 * Deliberately one page, not the whole document: under dynamic-page access other
 * pages need an explicit load, and a design system's token board is a page, not
 * a document. The cost is that consumer mode only ever sees what the CURRENT
 * page binds — which is why the report names the read mode so prominently.
 */
function collectBoundVariableIds(page) {
  var ids = {};
  var nodes = page.findAll(function () { return true; });
  nodes.push(page);

  for (var i = 0; i < nodes.length; i++) {
    var n = nodes[i];
    var bv = n.boundVariables;
    if (bv) {
      ['fills', 'strokes', 'effects'].forEach(function (prop) {
        var arr = bv[prop];
        if (arr && arr.length) {
          for (var j = 0; j < arr.length; j++) if (arr[j] && arr[j].id) ids[arr[j].id] = true;
        }
      });
    }
    // Paints carry their own binding when only the colour is bound.
    ['fills', 'strokes'].forEach(function (prop) {
      var paints = n[prop];
      if (paints && paints.length && typeof paints !== 'symbol') {
        for (var k = 0; k < paints.length; k++) {
          var p = paints[k];
          if (p && p.boundVariables && p.boundVariables.color && p.boundVariables.color.id) {
            ids[p.boundVariables.color.id] = true;
          }
        }
      }
    });
  }
  return Object.keys(ids);
}

async function scan() {
  var readMode = 'local';
  var records = [];
  var collectionId = null;
  var modeId = null;
  var modes = null;
  var warnings = [];

  var localVars = await figma.variables.getLocalVariablesAsync('COLOR');

  if (localVars.length > 0) {
    // Library mode — this file owns the variables.
    var colls = await figma.variables.getLocalVariableCollectionsAsync();
    var defaultModeOf = {};
    for (var c = 0; c < colls.length; c++) defaultModeOf[colls[c].id] = colls[c].defaultModeId;

    if (colls.length) {
      collectionId = colls[0].id;
      modeId = colls[0].defaultModeId;
      modes = colls[0].modes.map(function (m) { return { id: m.modeId, name: m.name }; });
      if (colls.length > 1) {
        // Splitting primitives and semantics across collections is common, and
        // the export still carries every variable with the right value, because
        // each one is read from its own collection's default mode. What the
        // designer needs to know is that only the first collection's modes are
        // declared, so multi-mode theming later will need revisiting.
        warnings.push(colls.length + ' variable collections found. All of them are exported, but only "' +
          colls[0].name + '" supplies the declared mode list.');
      }
      if (colls[0].modes.length > 1) {
        warnings.push('"' + colls[0].name + '" has ' + colls[0].modes.length +
          ' modes. Only the default mode ("' + colls[0].modes[0].name + '") is exported — Unreal has no theme concept yet.');
      }
    }
    for (var i = 0; i < localVars.length; i++) {
      records.push(recordFromVariable(localVars[i], defaultModeOf[localVars[i].variableCollectionId]));
    }

    // Aliases are local ids here; map id -> key so the pure layer only sees keys.
    var idToKey = {};
    for (i = 0; i < localVars.length; i++) idToKey[localVars[i].id] = localVars[i].key;
    for (i = 0; i < records.length; i++) {
      if (records[i]._aliasId) records[i].aliasKey = idToKey[records[i]._aliasId] || null;
    }
  } else {
    // Consumer mode — walk what the canvas binds, then follow aliases.
    readMode = 'consumer';
    var seen = {};
    var queue = collectBoundVariableIds(figma.currentPage);
    var guard = 0;

    while (queue.length && guard++ < 5000) {
      var id = queue.shift();
      if (seen[id]) continue;
      seen[id] = true;

      var v = await figma.variables.getVariableByIdAsync(id);
      if (!v || v.resolvedType !== 'COLOR') continue;

      // Remote variables still expose their collection, so the default mode is
      // knowable here too rather than guessed from key order.
      var coll = await figma.variables.getVariableCollectionByIdAsync(v.variableCollectionId);
      if (!collectionId) collectionId = v.variableCollectionId;
      if (!modeId) modeId = coll ? coll.defaultModeId : Object.keys(v.valuesByMode)[0];
      if (!modes && coll) modes = coll.modes.map(function (m) { return { id: m.modeId, name: m.name }; });

      var rec = recordFromVariable(v, coll ? coll.defaultModeId : null);
      rec._id = v.id;
      records.push(rec);
      if (rec._aliasId) queue.push(rec._aliasId);
    }

    var idToKey2 = {};
    for (i = 0; i < records.length; i++) idToKey2[records[i]._id] = records[i].key;
    for (i = 0; i < records.length; i++) {
      if (records[i]._aliasId) records[i].aliasKey = idToKey2[records[i]._aliasId] || null;
    }
  }

  var clean = records.map(function (r) {
    return { key: r.key, name: r.name, aliasKey: r.aliasKey, srgbHex: r.srgbHex, alpha: r.alpha, scopes: r.scopes };
  });

  return {
    records: clean,
    meta: {
      fileKey: safeFileKey(),
      fileName: figma.root.name,
      readMode: readMode,
      collectionId: collectionId,
      defaultModeId: modeId,
      modes: modes
    },
    warnings: warnings
  };
}

/**
 * The tier/group inventory the designer chooses from when authoring a link.
 * Derived from a scan so it always reflects what is actually in the file.
 */
function inventory(records) {
  var seen = {};
  var order = [];

  for (var i = 0; i < records.length; i++) {
    var tier = tierOf(records[i].name);
    var group = groupOf(records[i].name);
    var path = group ? tier + '/' + group : tier;

    if (!seen[path]) {
      seen[path] = { path: path, tier: tier, group: group, count: 0 };
      order.push(path);
    }
    seen[path].count++;
  }

  order.sort();
  return order.map(function (p) { return seen[p]; });
}

/* ---------------- link storage (shared, lives in the Figma file) ---------- */

var LINK_NAMESPACE = 'figmaTokenBridge';
var LINK_KEY = 'links';

/**
 * Links are stored on the document with setSharedPluginData rather than in
 * clientStorage, so every designer in the file sees the same set. Shared data is
 * readable by other plugins — fine, this is project configuration and not a
 * secret. Nothing here is sent anywhere.
 */
function loadLinkStore() {
  try {
    var raw = figma.root.getSharedPluginData(LINK_NAMESPACE, LINK_KEY);
    if (!raw) return { version: 1, links: [], activeLinkId: null };
    var parsed = JSON.parse(raw);
    if (!parsed || !parsed.links) return { version: 1, links: [], activeLinkId: null };
    return parsed;
  } catch (e) {
    // Corrupt data should not brick the plugin; report empty and let the
    // designer re-create the link rather than failing to open.
    return { version: 1, links: [], activeLinkId: null, recoveredFrom: String(e) };
  }
}

function saveLinkStore(store) {
  figma.root.setSharedPluginData(LINK_NAMESPACE, LINK_KEY, JSON.stringify(store));
}

/**
 * Who is authoring this, for the linkedBy/exportedBy provenance fields.
 *
 * Reading figma.currentUser THROWS when the manifest does not declare the
 * "currentuser" permission — the check is on the property getter, so the
 * obvious `figma.currentUser ? figma.currentUser.name : null` guard does not
 * help: it throws while evaluating its own condition. That took down the whole
 * save-link flow over a field that is a nice-to-have.
 *
 * The manifest now declares the permission, so this should always succeed. The
 * try/catch stays because provenance is not worth failing an export for.
 */
/**
 * The Figma file key, when the API will give it to us.
 *
 * figma.fileKey is readable only by PRIVATE ORGANISATION plugins that also set
 * enablePrivatePluginApi. A plugin run from "Import plugin from manifest" — which
 * is how this one is developed, and how it is tested — gets nothing. Both
 * manifest flags are set, so this starts working the moment the plugin is
 * published privately to the org, and returns null until then.
 *
 * Wrapped because gated Figma properties throw from the getter rather than
 * returning a falsy value; see currentUserName below for the same trap.
 */
function safeFileKey() {
  try {
    return figma.fileKey === undefined ? null : figma.fileKey;
  } catch (e) {
    return null;
  }
}

function currentUserName() {
  try {
    return figma.currentUser ? figma.currentUser.name : null;
  } catch (e) {
    return null;
  }
}

/** File key as it appears in the file URL, after /design/. Format-checked only. */
function looksLikeFileKey(value) {
  return /^[0-9A-Za-z]{18,128}$/.test(String(value || '').trim());
}

/** GUID as shown in Unreal's Project Settings. Format-checked, not validated. */
function looksLikeProjectId(value) {
  return /^[0-9A-Fa-f]{8}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{12}$/.test(
    String(value || '').trim()
  ) || /^[0-9A-Fa-f]{32}$/.test(String(value || '').trim());
}

/* ------------------------------- Figma runtime ---------------------------- */

if (typeof figma !== 'undefined') {
  figma.showUI(__html__, { width: 400, height: 640, themeColors: true });

  // Scans are not cheap on a large page, so the result is reused between
  // configuring a link and exporting it.
  var cached = null;

  var reply = function (payload) { figma.ui.postMessage(payload); };

  var fail = function (message, detail) {
    reply({ type: 'error', message: message, details: detail ? [String(detail)] : [] });
  };

  figma.ui.onmessage = async function (msg) {
    try {
      if (msg.type === 'init') {
        var store = loadLinkStore();
        reply({
          type: 'init',
          fileName: figma.root.name,
          fileKey: safeFileKey(),
          links: store.links,
          activeLinkId: store.activeLinkId
        });
        return;
      }

      if (msg.type === 'scan') {
        cached = await scan();
        reply({
          type: 'scan',
          readMode: cached.meta.readMode,
          total: cached.records.length,
          warnings: cached.warnings || [],
          inventory: inventory(cached.records)
        });
        return;
      }

      if (msg.type === 'saveLink') {
        if (!msg.link || !msg.link.linkName) {
          return fail('A link needs a name.');
        }
        if (!looksLikeProjectId(msg.link.projectId)) {
          return fail('That does not look like an Unreal project id.',
            'Copy it from Project Settings > Plugins > Figma Token Bridge > Project Id.');
        }
        if (!msg.link.published || !msg.link.published.length) {
          return fail('Select at least one tier or group to publish.');
        }
        // Unreal refuses an export whose file key does not match the one the
        // project is paired with, so an export without a key is useless the
        // moment that check is on. Better to refuse here, where the designer
        // can see the key in their own address bar.
        if (!looksLikeFileKey(msg.link.fileKey)) {
          return fail('That does not look like a Figma file key.',
            'It is the part of this file\u2019s URL after /design/ and before the file name.');
        }

        var s = loadLinkStore();
        var link = {
          id: msg.link.id || ('link-' + Date.now()),
          linkName: msg.link.linkName,
          fileKey: msg.link.fileKey.trim(),
          projectId: msg.link.projectId.trim(),
          projectName: msg.link.projectName || '',
          published: msg.link.published,
          linkedBy: currentUserName(),
          linkedAt: new Date().toISOString()
        };

        var idx = -1;
        for (var i = 0; i < s.links.length; i++) {
          if (s.links[i].id === link.id) { idx = i; break; }
        }
        if (idx >= 0) { s.links[idx] = link; } else { s.links.push(link); }
        s.activeLinkId = link.id;
        saveLinkStore(s);

        reply({ type: 'links', links: s.links, activeLinkId: s.activeLinkId, saved: link.id });
        return;
      }

      if (msg.type === 'deleteLink') {
        var st = loadLinkStore();
        st.links = st.links.filter(function (l) { return l.id !== msg.id; });
        if (st.activeLinkId === msg.id) st.activeLinkId = st.links.length ? st.links[0].id : null;
        saveLinkStore(st);
        reply({ type: 'links', links: st.links, activeLinkId: st.activeLinkId });
        return;
      }

      if (msg.type === 'export') {
        if (!cached) {
          cached = await scan();
        }

        var store2 = loadLinkStore();
        var chosen = null;
        for (var j = 0; j < store2.links.length; j++) {
          if (store2.links[j].id === msg.id) { chosen = store2.links[j]; break; }
        }
        if (!chosen) {
          return fail('That link no longer exists. Re-create it and try again.');
        }

        var built = buildDocument(cached.records, {
          // The API wins when it is available, because it cannot be mistyped.
          fileKey: cached.meta.fileKey || chosen.fileKey || null,
          fileName: cached.meta.fileName,
          readMode: cached.meta.readMode,
          collectionId: cached.meta.collectionId,
          defaultModeId: cached.meta.defaultModeId,
          modes: cached.meta.modes,
          published: chosen.published,
          target: chosen,
          exportedAt: new Date().toISOString(),
          exportedBy: currentUserName()
        });

        reply({
          type: 'export',
          link: chosen,
          readMode: cached.meta.readMode,
          total: built.doc.colours.length,
          publishedCount: built.publishedCount,
          counts: built.counts,
          warnings: built.warnings,
          errors: built.errors,
          doc: built.doc
        });
        return;
      }
    } catch (e) {
      fail('Something went wrong.', e && e.message ? e.message : e);
    }
  };
}

// Exported for tools/ and tests. `module` does not exist in the Figma sandbox.
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    SCHEMA: SCHEMA,
    normaliseTokenName: normaliseTokenName,
    tierOf: tierOf,
    groupOf: groupOf,
    isPublished: isPublished,
    inventory: inventory,
    resolveTerminal: resolveTerminal,
    srgbChannelToLinear: srgbChannelToLinear,
    srgbHexToLinear: srgbHexToLinear,
    buildDocument: buildDocument
  };
}
