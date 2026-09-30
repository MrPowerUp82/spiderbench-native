// Validate all distance-culled LOD lists and their scene mesh bindings.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import crypto from 'node:crypto';

const dir = path.resolve('build/city-bake');
const packed = fs.readFileSync(path.join(dir, 'pools.json.gz'));
const raw = zlib.gunzipSync(packed);
const pools = JSON.parse(raw);
const summary = JSON.parse(fs.readFileSync(path.join(dir, 'pools.json')));
const geometry = JSON.parse(fs.readFileSync(path.join(dir, 'geometry.json')));
if (pools.format !== 'SBPOOL1' || pools.version !== 1 ||
    pools.source !== geometry.source || pools.threeRevision !== geometry.threeRevision ||
    pools.source !== summary.source || pools.threeRevision !== summary.threeRevision ||
    packed.length !== summary.compressedBytes || raw.length !== summary.rawBytes ||
    crypto.createHash('sha256').update(raw).digest('hex') !== summary.sha256)
  throw new Error('Pool archive header, source, or checksum mismatch');

const unique = new Set();
let n = 0;
for (const p of pools.pools) {
  const mesh = geometry.meshes[p.objectOrdinal];
  if (p.id !== unique.size || unique.has(p.objectOrdinal) || !mesh?.instanced ||
      mesh.name !== p.name || !p.items || p.max < 0 ||
      mesh.attributes.aLod?.itemSize !== 4)
    throw new Error(`Pool/geometry mismatch: ${p.name} (${p.objectOrdinal})`);
  unique.add(p.objectOrdinal);
  for (const [name, size] of Object.entries(p.extraDefs))
    if (mesh.attributes[name]?.itemSize !== size)
      throw new Error(`Missing instance attribute ${name} on ${p.name}`);
  for (const [i, it] of p.items.entries()) {
    if (![it.x, it.y, it.z, it.ry, it.s, it.rx, it.rz].every(Number.isFinite) ||
        it.scale3 && (!Array.isArray(it.scale3) || it.scale3.length !== 3 || !it.scale3.every(Number.isFinite)) ||
        it.color && (!Array.isArray(it.color) || it.color.length !== 3 || !it.color.every(Number.isFinite)))
      throw new Error(`Invalid instance ${p.name}[${i}]`);
    if (it.extra) for (const [name, value] of Object.entries(it.extra)) {
      const size = p.extraDefs[name];
      if (!size) continue;
      if (size === 1 ? !Number.isFinite(value) : !Array.isArray(value) || value.length !== size || !value.every(Number.isFinite))
        throw new Error(`Invalid ${name} on ${p.name}[${i}]`);
    }
  }
  n += p.items.length;
}
if (pools.pools.length !== summary.pools || n !== summary.instances)
  throw new Error('Pool count mismatch');
for (const mesh of geometry.meshes)
  if (mesh.instanced && mesh.attributes.aLod && !unique.has(mesh.objectOrdinal))
    throw new Error(`Uncaptured LOD pool: ${mesh.name} (${mesh.objectOrdinal})`);
console.log(`linked ${pools.pools.length} pools and ${n} instances to baked geometry`);
