import { renderer, SRC_ROOT } from './stubs.mjs';
import path from 'node:path'; import { pathToFileURL } from 'node:url';
const imp = (p) => import(pathToFileURL(path.join(SRC_ROOT, p)).href);
const THREE = await imp('node_modules/three/build/three.module.js');
const { buildCity } = await imp('src/world/city.js');
const scene = new THREE.Scene();
const world = await buildCity({ scene, renderer });
const by = new Map(); let bytes = 0;
scene.traverse(o => {
  if (!o.isMesh || !o.geometry.attributes.position) return;
  const g = o.geometry, t = (g.index ? g.index.count : g.attributes.position.count) / 3 * (o.isInstancedMesh ? o.count : 1);
  let b = 0; for (const a of Object.values(g.attributes)) b += a.array.byteLength; if (g.index) b += g.index.array.byteLength;
  if (o.isInstancedMesh) b += o.instanceMatrix.array.byteLength;
  const k = (o.name || '?').replace(/[-_]?\d+$/, '') + (o.isInstancedMesh ? ' [inst]' : '');
  const e = by.get(k) || { n: 0, t: 0, b: 0, vis: 0 }; e.n++; e.t += t; e.b += b; e.vis += o.visible ? 1 : 0; by.set(k, e); bytes += b;
});
console.log('total MB', (bytes / 1048576).toFixed(0));
for (const [k, e] of [...by].sort((a, b) => b[1].b - a[1].b)) console.log(k.padEnd(34), String(e.n).padStart(5), 'vis', String(e.vis).padStart(4), (e.t / 1e3).toFixed(0).padStart(7) + 'k tris', (e.b / 1048576).toFixed(1).padStart(8) + ' MB');
