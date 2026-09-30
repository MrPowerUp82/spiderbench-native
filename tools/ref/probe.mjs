// probe: can the original city modules load headless in Node?
import { SRC_ROOT } from './stubs.mjs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
for (const m of ['layout.js', 'geom.js', 'facade.js', 'collision.js', 'zippoints.js', 'residential.js', 'buildings.js', 'ground.js', 'rooftops.js', 'timessq.js', 'grandcentral.js', 'landmarks.js', 'bridges.js', 'farshore.js', 'waterfront.js', 'park.js', 'trees.js', 'props.js', 'signage.js', 'highway.js', 'hero.js']) {
  try { await import(pathToFileURL(path.join(SRC_ROOT, 'src/world', m)).href); console.log('ok  ', m); } catch (e) { console.log('FAIL', m, String(e).slice(0, 160)); }
}
