// Capture distance-culled Pool item lists before their InstancedMesh buffers are
// repacked for one camera position. These lists are the source for native LODs.
import { BAKE, SHADERS } from './paths.mjs';
import { renderer, SRC_ROOT } from './stubs.mjs';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import zlib from 'node:zlib';
import crypto from 'node:crypto';

const imp = p => import(pathToFileURL(path.join(SRC_ROOT, p)).href);
const THREE = await imp('node_modules/three/build/three.module.js');
const { Pool } = await imp('src/world/pool.js');
const pools = new Set();
// Every constructor assigns `items`; several LOD pools later share another
// pool's array and never call add() themselves. Capture at construction time.
Object.defineProperty(Pool.prototype, 'items', { configurable: true, set(value) {
  pools.add(this);
  Object.defineProperty(this, 'items', { value, writable: true, enumerable: true, configurable: true });
} });
const { buildCity } = await imp('src/world/city.js');
const scene = new THREE.Scene();
await buildCity({ scene, renderer });
delete Pool.prototype.items;

const ordinals = new WeakMap();
let ordinal = 0;
scene.traverse(o => { if (o.isMesh && o.geometry?.attributes.position) ordinals.set(o, ordinal++); });
const items = [...pools].map((p, id) => {
  const objectOrdinal = ordinals.get(p.mesh);
  if (objectOrdinal == null) throw new Error(`Pool mesh missing from scene: ${p.mesh?.name}`);
  return { id, objectOrdinal, name: p.mesh.name, max: p.max, near: p.near, far: p.far,
    fadeIn: p.fadeIn, fadeOut: p.fadeOut, shadowFar: p.shadowFar,
    isStatic: p.isStatic, castShadow: p.mesh.castShadow, receiveShadow: p.mesh.receiveShadow,
    extraDefs: p.extraDefs, hasColor: !!p.mesh.instanceColor,
    items: p.items.map(it => ({ x: it.x, y: it.y, z: it.z, ry: it.ry, s: it.s,
      rx: it.rx ?? 0, rz: it.rz ?? 0, color: it.color, extra: it.extra,
      scale3: it.scale3, hidden: !!it.hidden })) };
});
const raw = Buffer.from(JSON.stringify({ format: 'SBPOOL1', version: 1, source: SRC_ROOT,
  threeRevision: THREE.REVISION, pools: items }));
const compressed = zlib.gzipSync(raw, { level: 6 });
const dir = BAKE;
fs.mkdirSync(dir, { recursive: true });
fs.writeFileSync(path.join(dir, 'pools.json.gz'), compressed);
const summary = { format: 'SBPOOL1', version: 1, source: SRC_ROOT,
  threeRevision: THREE.REVISION, pools: items.length,
  instances: items.reduce((n, p) => n + p.items.length, 0),
  rawBytes: raw.length, compressedBytes: compressed.length,
  sha256: crypto.createHash('sha256').update(raw).digest('hex') };
fs.writeFileSync(path.join(dir, 'pools.json'), JSON.stringify(summary, null, 2));
console.log(JSON.stringify(summary));
