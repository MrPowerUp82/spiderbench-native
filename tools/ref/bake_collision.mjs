// Bake the *final* JS collision grid, height fields, building boxes and validated
// zip points without rounding or reimplementing world generation.
// Run: node --max-old-space-size=8192 tools/ref/bake_collision.mjs
import { BAKE, SHADERS } from './paths.mjs';
import { renderer, SRC_ROOT } from './stubs.mjs';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const imp = p => import(pathToFileURL(path.join(SRC_ROOT, p)).href);
const THREE = await imp('node_modules/three/build/three.module.js');
const { buildCity } = await imp('src/world/city.js');
const { ZKIND } = await imp('src/world/zippoints.js');
const city = await buildCity({ scene: new THREE.Scene(), renderer });
const grid = city.collision;
const zip = city.getZipPoints(new THREE.Vector3(), 30000);
const boxes = city.buildings;
const outDir = BAKE;
fs.mkdirSync(outDir, { recursive: true });
const finalPath = path.join(outDir, 'collision.sbcol');
const tmpPath = finalPath + '.tmp';
const fd = fs.openSync(tmpPath, 'w');
let bytes = 0;
function write(buffer) {
  let off = 0;
  while (off < buffer.byteLength) off += fs.writeSync(fd, buffer, off, buffer.byteLength - off);
  bytes += buffer.byteLength;
}
function raw(a) { write(Buffer.from(a.buffer, a.byteOffset, a.byteLength)); }
function u32(...v) { const b = Buffer.allocUnsafe(v.length * 4); v.forEach((x, i) => b.writeUInt32LE(x, i * 4)); write(b); }
function f32(...v) { const b = Buffer.allocUnsafe(v.length * 4); v.forEach((x, i) => b.writeFloatLE(x, i * 4)); write(b); }

try {
  write(Buffer.from('SBCOLL1\0', 'ascii'));
  u32(1, grid.n, grid.fields.length, zip.length, boxes.length,
    grid.nx, grid.nz, grid.items.length);
  f32(grid.cell, grid.ox, grid.oz, city.spawn.x, city.spawn.y, city.spawn.z);
  raw(grid.type); raw(grid.flags); raw(grid.kind); raw(grid.bb); raw(grid.par);
  raw(grid.start); raw(grid.items);
  for (const field of grid.fields) {
    if (field.h.length !== field.nx * field.nz || field.lo.length !== field.h.length)
      throw new Error('Invalid collision height field');
    u32(field.nx, field.nz);
    f32(field.cell, field.hMax, field.loMin);
    raw(field.h); raw(field.lo);
  }
  const zipP = new Float32Array(zip.length * 6), zipK = new Uint8Array(zip.length);
  zip.forEach((p, i) => {
    zipP.set([p.pos.x, p.pos.y, p.pos.z, p.normal.x, p.normal.y, p.normal.z], i * 6);
    const k = ZKIND.indexOf(p.kind);
    if (k < 0) throw new Error(`Unknown zip kind ${p.kind}`);
    zipK[i] = k;
  });
  raw(zipP); raw(zipK);
  const boxP = new Float32Array(boxes.length * 6);
  boxes.forEach((b, i) => {
    // gen.boxes stores {min: [x, y, z], max: [x, y, z]} (player/traversal/collide.js BoxIndex)
    const v = [...b.min, ...b.max];
    if (v.length !== 6 || !v.every(Number.isFinite)) throw new Error(`Invalid building box ${i}`);
    boxP.set(v, i * 6);
  });
  raw(boxP);
  fs.closeSync(fd);
  fs.renameSync(tmpPath, finalPath);
} catch (error) {
  fs.closeSync(fd);
  fs.rmSync(tmpPath, { force: true });
  throw error;
}

const manifest = {
  format: 'SBCOLL1', version: 1, source: SRC_ROOT, threeRevision: THREE.REVISION,
  file: path.basename(finalPath), bytes, solids: grid.n, fields: grid.fields.length,
  fieldCells: grid.fields.reduce((n, f) => n + f.h.length, 0),
  zipPoints: zip.length, buildingBoxes: boxes.length,
  grid: { nx: grid.nx, nz: grid.nz, cell: grid.cell, items: grid.items.length },
  spawn: city.spawn.toArray(), zipKinds: ZKIND,
};
fs.writeFileSync(path.join(outDir, 'collision.json'), JSON.stringify(manifest, null, 2));
// Deterministic reference queries for the C++ port. Include open ground,
// validated perch locations and primitive interiors across the whole map.
let rng = 0x53b0c011;
const rand = () => ((rng = (Math.imul(rng, 1664525) + 1013904223) >>> 0) / 0x100000000);
const queries = [];
for (let i = 0; i < 512; i++) {
  let x, y, z;
  if (i < 128) { x = -800 + rand() * 1800; z = -3500 + rand() * 7000; y = rand() * 250; }
  else if (i < 256) { const p = zip[(i * 997) % zip.length].pos; x = p.x; z = p.z; y = p.y + (rand() - 0.5) * 2; }
  else { const j = ((i * 7919) % grid.n) * 6, b = grid.bb; x = (b[j] + b[j + 3]) * 0.5; z = (b[j + 2] + b[j + 5]) * 0.5; y = (b[j + 1] + b[j + 4]) * 0.5; }
  const all = grid.topAt(x, z, y + 0.5), grounded = grid.topAt(x, z, Infinity, true);
  queries.push({ x, y, z, all: { y: all.y, id: all.id }, grounded: { y: grounded.y, id: grounded.id }, inside: grid.inside(x, y, z) });
}
fs.writeFileSync(path.join(outDir, 'queries.json'), JSON.stringify({ format: 'SBCOLL1', queries }, null, 2));
console.log(JSON.stringify(manifest));
