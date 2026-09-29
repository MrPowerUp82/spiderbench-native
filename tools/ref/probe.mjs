// probe: can the original city modules load headless in Node?
globalThis.window = globalThis; globalThis.location = { search: '' }; globalThis.document = { createElement: () => ({ getContext: () => null, style: {} }) };
globalThis.performance ??= { now: () => Date.now() };
const SRC = '../../../spiderbench/src/world/';
for (const m of ['layout.js', 'geom.js', 'facade.js', 'collision.js', 'zippoints.js', 'residential.js', 'buildings.js', 'ground.js', 'rooftops.js', 'timessq.js', 'grandcentral.js', 'landmarks.js', 'bridges.js', 'farshore.js', 'waterfront.js', 'park.js', 'trees.js', 'props.js', 'signage.js', 'highway.js', 'hero.js']) {
  try { await import(SRC + m); console.log('ok  ', m); } catch (e) { console.log('FAIL', m, String(e).slice(0, 160)); }
}
