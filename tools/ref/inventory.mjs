// Runs the original buildCity headless and prints an inventory of the generated scene (meshes, materials, attributes).
import { renderer, SRC_ROOT } from './stubs.mjs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
const imp = (p) => import(pathToFileURL(path.join(SRC_ROOT, p)).href);
const THREE = await imp('node_modules/three/build/three.module.js');
const { buildCity } = await imp('src/world/city.js');

const scene = new THREE.Scene();
const t0 = Date.now();
const world = await buildCity({ scene, renderer });
console.log('built in', Date.now() - t0, 'ms');
const mats = new Map(); let nMesh = 0, nInst = 0, tris = 0, instTris = 0;
scene.traverse(o => {
  if (!o.isMesh && !o.isLine && !o.isPoints) return;
  const g = o.geometry, idx = g.index, pos = g.attributes.position;
  const t = (idx ? idx.count : pos ? pos.count : 0) / 3;
  const m = Array.isArray(o.material) ? o.material[0] : o.material;
  const key = (m.name || m.type) + '|' + m.type + (m.onBeforeCompile && m.onBeforeCompile.toString() !== 'onBeforeCompile(){}' ? '|custom' : '') + (m.isShaderMaterial ? '|shader' : '');
  const e = mats.get(key) || { meshes: 0, inst: 0, tris: 0, attrs: new Set(), names: new Set(), maps: new Set() };
  e.meshes++; if (o.isInstancedMesh) { e.inst += o.count; instTris += t * o.count; nInst++; } else tris += t;
  e.tris += o.isInstancedMesh ? t * o.count : t;
  for (const a of Object.keys(g.attributes)) e.attrs.add(a);
  if (e.names.size < 6) e.names.add(o.name);
  for (const k of ['map', 'normalMap', 'roughnessMap', 'emissiveMap', 'alphaMap', 'aoMap']) if (m[k]) e.maps.add(k);
  mats.set(key, e); nMesh++;
});
console.log(`meshes ${nMesh} (instanced ${nInst}), static tris ${(tris / 1e6).toFixed(2)}M, instanced tris ${(instTris / 1e6).toFixed(2)}M`);
for (const [k, e] of [...mats].sort((a, b) => b[1].tris - a[1].tris))
  console.log(`${k.padEnd(44)} meshes ${String(e.meshes).padStart(4)} inst ${String(e.inst).padStart(6)} tris ${(e.tris / 1e3).toFixed(0).padStart(6)}k attrs[${[...e.attrs].join(',')}] maps[${[...e.maps].join(',')}] e.g. ${[...e.names].join(',')}`);
console.log('solids', world.collision.n, 'zips', world.getZipPoints ? 'yes' : 'no', 'boxes', world.buildings.length);
