// Bake what the gameplay queries need besides the collision grid: the analytic terrain (ground.js terrainHeight) as a
// hierarchical palette raster, the tree swing anchors (anchors.js treePoints over the 'trees-*-near' pools) and
// reference answers of the original world.raycast / groundHeight / getZipPoints for the native validator.
// Run: node --max-old-space-size=8192 tools/ref/bake_traversal.mjs
import { BAKE, SHADERS } from './paths.mjs';
import { renderer, SRC_ROOT } from './stubs.mjs';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { pathToFileURL } from 'node:url';

const imp = p => import(pathToFileURL(path.join(SRC_ROOT, p)).href);
const THREE = await imp('node_modules/three/build/three.module.js');
const { Pool } = await imp('src/world/pool.js');
const pools = new Set();
Object.defineProperty(Pool.prototype, 'items', { configurable: true, set(value) {
  pools.add(this);
  Object.defineProperty(this, 'items', { value, writable: true, enumerable: true, configurable: true });
} });
const { buildCity } = await imp('src/world/city.js');
const { terrainHeight } = await imp('src/world/ground.js');
const city = await buildCity({ scene: new THREE.Scene(), renderer });
delete Pool.prototype.items;
const grid = city.collision;

// ---------------------------------------------------------------- terrain raster
// Fine level: 0.25 m samples (value at the sample centre), grouped in 2 m sub-tiles (8x8) and 16 m top tiles (8x8 subs).
// A tile is stored as one palette index when uniform. Top tiles are tested on their full fine boundary plus a 1 m
// interior lattice; sub-tiles of mixed top tiles are sampled completely.
const FINE = 0.25, SUB = 8, TOP = 8, TOPM = FINE * SUB * TOP;
const fx0 = grid.ox, fz0 = grid.oz;
const tnx = Math.ceil(grid.nx * grid.cell / TOPM), tnz = Math.ceil(grid.nz * grid.cell / TOPM);
const palette = [], pal = new Map();
const idxOf = v => { let i = pal.get(v); if (i === undefined) { i = palette.length; if (i > 255) throw new Error('terrain palette overflow'); pal.set(v, i); palette.push(v); } return i; };
const fineAt = (ix, iz) => idxOf(terrainHeight(fx0 + (ix + 0.5) * FINE, fz0 + (iz + 0.5) * FINE));
const UNI = 0x80000000;
const top = new Uint32Array(tnx * tnz), sub = [], blocks = [];
const N = SUB * TOP; // fine samples per top-tile side
const t0 = performance.now();
for (let tz = 0; tz < tnz; tz++) for (let tx = 0; tx < tnx; tx++) {
  const bx = tx * N, bz = tz * N, first = fineAt(bx, bz);
  let uniform = true;
  for (let k = 0; k < N && uniform; k++)
    if (fineAt(bx + k, bz) !== first || fineAt(bx + k, bz + N - 1) !== first || fineAt(bx, bz + k) !== first || fineAt(bx + N - 1, bz + k) !== first) uniform = false;
  for (let i = 2; i < N && uniform; i += 4) for (let j = 2; j < N && uniform; j += 4) if (fineAt(bx + i, bz + j) !== first) uniform = false;
  if (uniform) { top[tz * tnx + tx] = UNI | first; continue; }
  top[tz * tnx + tx] = sub.length;
  for (let sz = 0; sz < TOP; sz++) for (let sx = 0; sx < TOP; sx++) {
    const block = new Uint8Array(SUB * SUB);
    for (let j = 0; j < SUB; j++) for (let i = 0; i < SUB; i++) block[j * SUB + i] = fineAt(bx + sx * SUB + i, bz + sz * SUB + j);
    if (block.every(v => v === block[0])) sub.push(UNI | block[0]);
    else { sub.push(blocks.length); blocks.push(block); }
  }
}
// Coarse level for the far shores / open water outside the collision grid (16 m, sample centres).
const CO = 16, cx0 = -9000, cz0 = -13000, cnx = 1125, cnz = 1625;
const coarse = new Uint8Array(cnx * cnz);
for (let z = 0; z < cnz; z++) for (let x = 0; x < cnx; x++) coarse[z * cnx + x] = idxOf(terrainHeight(cx0 + (x + 0.5) * CO, cz0 + (z + 0.5) * CO));
const tTerrain = performance.now() - t0;

const lookup = (x, z) => {
  const ix = Math.floor((x - fx0) / FINE), iz = Math.floor((z - fz0) / FINE);
  if (ix >= 0 && iz >= 0 && ix < tnx * N && iz < tnz * N) {
    const t = top[Math.floor(iz / N) * tnx + Math.floor(ix / N)];
    if (t & UNI) return palette[t & 0xff];
    const s = sub[t + (Math.floor(iz / SUB) % TOP) * TOP + (Math.floor(ix / SUB) % TOP)];
    if (s & UNI) return palette[s & 0xff];
    return palette[blocks[s][(iz % SUB) * SUB + (ix % SUB)]];
  }
  const cx = Math.floor((x - cx0) / CO), cz = Math.floor((z - cz0) / CO);
  if (cx >= 0 && cz >= 0 && cx < cnx && cz < cnz) return palette[coarse[cz * cnx + cx]];
  return terrainHeight(x, z); // not reached by the native lookup: it answers the open-water value there
};
let rng = 0x7e22a1n;
const rand = () => { rng = (rng * 6364136223846793005n + 1442695040888963407n) & 0xffffffffffffffffn; return Number(rng >> 11n) / 2 ** 53; };
let mismatch = 0; const SAMPLES = 400000;
for (let i = 0; i < SAMPLES; i++) {
  const x = fx0 + rand() * tnx * TOPM, z = fz0 + rand() * tnz * TOPM;
  if (lookup(x, z) !== terrainHeight(x, z)) mismatch++;
}
if (mismatch / SAMPLES > 0.01) throw new Error(`terrain raster mismatch ${(mismatch / SAMPLES * 100).toFixed(3)}%`);

// ---------------------------------------------------------------- tree anchors (anchors.js treePoints)
const trees = [];
for (const p of pools) {
  if (!/^trees-.*-near$/.test(p.mesh.name)) continue;
  const g = p.geo; if (!g.boundingBox) g.computeBoundingBox();
  const h = g.boundingBox.max.y;
  for (const it of p.items) {
    if (it.hidden) continue;
    const sy = it.scale3 ? it.scale3[1] * it.s : it.s;
    if (h * sy * 0.82 < 6) continue; // small ornamentals can't hold a swing
    trees.push([it.x, it.y + h * sy * 0.82, it.z, it.y + h * sy * 0.72, h * sy * 0.34]);
  }
}

// ---------------------------------------------------------------- SBTRV1
const parts = [];
const u32 = (...v) => { const b = Buffer.allocUnsafe(v.length * 4); v.forEach((x, i) => b.writeUInt32LE(x >>> 0, i * 4)); parts.push(b); };
const f32 = (...v) => { const b = Buffer.allocUnsafe(v.length * 4); v.forEach((x, i) => b.writeFloatLE(x, i * 4)); parts.push(b); };
const raw = a => parts.push(Buffer.from(a.buffer, a.byteOffset, a.byteLength));
f32(fx0, fz0, FINE); u32(SUB, TOP, tnx, tnz);
f32(cx0, cz0, CO); u32(cnx, cnz);
u32(palette.length); f32(...palette);
raw(top);
u32(sub.length); raw(Uint32Array.from(sub));
u32(blocks.length); for (const b of blocks) raw(b);
raw(coarse);
u32(trees.length); raw(Float32Array.from(trees.flat()));
const payload = Buffer.concat(parts);
const packed = zlib.deflateSync(payload, { level: 6 });
const head = Buffer.alloc(16); head.write('SBTRV1\0\0', 0, 'ascii'); head.writeUInt32LE(1, 8); head.writeUInt32LE(payload.length, 12);
const outDir = BAKE;
fs.mkdirSync(outDir, { recursive: true });
fs.writeFileSync(path.join(outDir, 'traversal.sbtrv'), Buffer.concat([head, packed]));

// ---------------------------------------------------------------- reference queries (makeQueries / zips.query)
const zipAll = city.getZipPoints(new THREE.Vector3(), 30000);
const pick = () => zipAll[Math.floor(rand() * zipAll.length)].pos;
const v3 = v => v && [v.x, v.y, v.z];
const ground = [], rays = [], zipq = [];
for (let i = 0; i < 1500; i++) {
  let x, y, z;
  if (i < 500) { x = fx0 + 300 + rand() * (grid.nx * grid.cell - 600); z = fz0 + 300 + rand() * (grid.nz * grid.cell - 600); y = rand() * 60; }
  else if (i < 1000) { const p = pick(); x = p.x + (rand() - 0.5) * 3; z = p.z + (rand() - 0.5) * 3; y = p.y + (rand() - 0.5) * 2; }
  else { const p = pick(); x = p.x + (rand() - 0.5) * 60; z = p.z + (rand() - 0.5) * 60; y = 0.3; }
  ground.push({ x, z, y, free: city.groundHeight(x, z), step: city.groundHeight(x, z, y), terrain: terrainHeight(x, z) });
}
for (let i = 0; i < 3000; i++) {
  const p = pick(), o = new THREE.Vector3(p.x + (rand() - 0.5) * 80, i < 1000 ? 1 + rand() * 2 : p.y + (rand() - 0.3) * 40, p.z + (rand() - 0.5) * 80);
  const u = rand() * 2 - 1, a = rand() * Math.PI * 2, s = Math.sqrt(1 - u * u);
  const d = new THREE.Vector3(s * Math.cos(a), i % 3 === 0 ? -Math.abs(u) : u, s * Math.sin(a));
  const max = 20 + rand() * 120, h = city.raycast(o, d, max);
  rays.push({ o: v3(o), d: v3(d), max, hit: h ? { p: v3(h.point), n: v3(h.normal), t: h.distance, kind: h.kind, ground: !!h.ground } : null });
}
for (let i = 0; i < 200; i++) {
  const p = pick(), c = new THREE.Vector3(p.x + (rand() - 0.5) * 100, p.y + (rand() - 0.5) * 30, p.z + (rand() - 0.5) * 100), r = 20 + rand() * 60;
  const list = city.getZipPoints(c, r);
  zipq.push({ c: v3(c), r, count: list.length, first: list.slice(0, 8).map(q => [...v3(q.pos), ...v3(q.normal), q.kind]) });
}
fs.writeFileSync(path.join(outDir, 'traversal_queries.json'), JSON.stringify({ format: 'SBTRV1', ground, rays, zips: zipq }));
const summary = { format: 'SBTRV1', bytes: head.length + packed.length, payload: payload.length, palette,
  topTiles: [tnx, tnz], mixedTop: sub.length / (TOP * TOP), mixedSub: blocks.length, coarse: [cnx, cnz],
  trees: trees.length, terrainMs: Math.round(tTerrain), rasterMismatch: mismatch / SAMPLES,
  queries: { ground: ground.length, rays: rays.length, rayHits: rays.filter(r => r.hit).length, zips: zipq.length } };
fs.writeFileSync(path.join(outDir, 'traversal.json'), JSON.stringify(summary, null, 2));
console.log(JSON.stringify(summary));
