// Add export-only tile/cell metadata while loading the original modules.
// No source files or generated vertex arrays in the remake are changed.
import { registerHooks } from 'node:module';
registerHooks({ load(url, context, nextLoad) {
  const loaded = nextLoad(url, context);
  if (!url.endsWith('/src/world/tilebatch.js') && !url.endsWith('/src/world/signage.js')) return loaded;
  let source = String(loaded.source);
  const replace = (before, after) => {
    if (!source.includes(before)) throw new Error(`Export instrumentation no longer matches ${url}`);
    source = source.replace(before, after);
  };
  if (url.endsWith('/tilebatch.js')) {
    replace('const n = geoms.length;', 'const n = geoms.length; for (let i=0;i<n;i++) if (geoms[i]) geoms[i].userData.sbTileCenter = centers[i];');
    replace('const m = new THREE.Mesh(g, material); m.name = name;', 'const m = new THREE.Mesh(g, material); m.name = name; m.userData.sbTileCenter = g.userData.sbTileCenter;');
    replace('g.userData.sharedRange = true;', 'g.userData.sharedRange = true; g.userData.sbTileCenter = centers[i];');
  } else {
    replace("m.name = 'signage';", "m.name = 'signage'; m.userData.sbTileCenter = [c.x0 + 256, c.z0 + 256];");
    replace("m.name = 'signageGhost';", "m.name = 'signageGhost'; m.userData.sbTileCenter = [c.x0 + 256, c.z0 + 256];");
  }
  return { ...loaded, source };
} });
