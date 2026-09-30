// Validate the compact pool archive, then use the original Pool methods as the
// oracle for native selection, repack thresholds, rotation and shadow prefixes.
import { SRC_ROOT } from './stubs.mjs';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';
const imp = p => import(pathToFileURL(path.join(SRC_ROOT, p)).href);
const THREE = await imp('node_modules/three/build/three.module.js');
const { Pool } = await imp('src/world/pool.js');
const { CSM } = await imp('src/render/csm.js');
const dir = path.resolve('build/city-bake');
const source = JSON.parse(zlib.gunzipSync(fs.readFileSync(path.join(dir, 'pools.json.gz'))));
const manifest = JSON.parse(fs.readFileSync(path.join(dir, 'pool-data.json')));
const geometry = JSON.parse(fs.readFileSync(path.join(dir, 'geometry.json')));
if (manifest.kind !== 'SBPOOL1' || manifest.source !== source.source || manifest.source !== geometry.source ||
    manifest.threeRevision !== source.threeRevision || manifest.meshes.length !== source.pools.length)
  throw new Error('Pool archive/source mismatch');
const archive = fs.readFileSync(path.join(dir, manifest.archive));
if (!archive.subarray(0, 8).equals(Buffer.from('SBGEO01\0'))) throw new Error('Bad pool magic');
const buffers = manifest.blobs.map(b => {
  const header = archive.subarray(b.offset, b.offset + 8);
  if (header.readUInt32LE(0) !== b.bytes || header.readUInt32LE(4) !== b.compressedBytes) throw new Error('Pool blob header mismatch');
  const bytes = zlib.inflateSync(archive.subarray(b.offset + 8, b.offset + 8 + b.compressedBytes));
  if (bytes.length !== b.bytes || crypto.createHash('sha256').update(bytes).digest('hex') !== b.sha256)
    throw new Error('Pool checksum mismatch');
  return bytes;
});
let samples = 0;
for (const [i, original] of source.pools.entries()) {
  const meta = manifest.meshes[i];
  if (meta.objectOrdinal !== original.objectOrdinal || meta.name !== original.name || meta.count !== original.items.length ||
      geometry.meshes[meta.objectOrdinal].name !== meta.name || !geometry.meshes[meta.objectOrdinal].instanced)
    throw new Error(`Pool metadata mismatch: ${meta.name}`);
  const positionBytes = buffers[meta.positions];
  for (const [j, item] of original.items.entries()) for (const [k, v] of [item.x, item.y, item.z].entries())
    if (positionBytes.readDoubleLE((j * 3 + k) * 8) !== v) throw new Error('Position precision lost');
  const pool = Object.create(Pool.prototype);
  Object.assign(pool, original, { extra: {}, mesh: { instanceColor: original.hasColor ? new THREE.InstancedBufferAttribute(new Float32Array(3), 3) : null } });
  const matrix = new Float32Array(16);
  pool.mesh.setMatrixAt = (_k, m) => m.toArray(matrix);
  for (const [name, size] of Object.entries(original.extraDefs)) pool.extra[name] = new THREE.InstancedBufferAttribute(new Float32Array(size), size);
  for (const j of new Set([0, Math.floor(meta.count / 2), meta.count - 1].filter(j => j >= 0 && j < meta.count))) {
    Pool.prototype.write.call(pool, 0, original.items[j]);
    for (const [name, attr] of Object.entries(meta.attributes)) {
      const expected = name === 'instanceMatrix' ? matrix : name === 'instanceColor' ? pool.mesh.instanceColor.array : pool.extra[name].array;
      const packed = buffers[attr.blob].subarray(j * attr.itemSize * 4, (j + 1) * attr.itemSize * 4);
      if (!packed.equals(Buffer.from(expected.buffer, expected.byteOffset, expected.byteLength)))
        throw new Error(`Original Pool.write mismatch at ${meta.name}/${j}/${name}`);
    }
    samples++;
  }
}
const instances = source.pools.map(meta => {
  const pool = Object.create(Pool.prototype), ids = new WeakMap();
  meta.items.forEach((item, i) => ids.set(item, i));
  Object.assign(pool, meta, { last: new THREE.Vector3(1e9, 0, 0), mesh: {}, picked: [] });
  pool.write = (_k, item) => pool.picked.push(ids.get(item));
  pool.upload = () => {};
  return pool;
});
const cameras = [
  { position: [250, 32, 175], quaternion: [0, 0, 0, 1] },
  { position: [253, 33, 178], quaternion: [0, 0, 0, 1] }, // some pools remain inside repack hysteresis
  { position: [253, 33, 178], quaternion: [0, 1, 0, 0] }, // rotation forces a view-wedge repack
  { position: [0, 70, -600], quaternion: [0, 0, 0, 1] },
  { position: [120, 100, 2760], quaternion: [0, 1, 0, 0] },
  { position: [-650, 12, 600], quaternion: [0, 0, 0, 1] },
];
const frames = cameras.map(config => {
  const camera = new THREE.PerspectiveCamera(55, 16 / 9, .1, 20000);
  camera.position.fromArray(config.position); camera.quaternion.fromArray(config.quaternion); camera.updateMatrixWorld();
  CSM.prototype._viewWedge.call({ _fwd: camera.getWorldDirection(new THREE.Vector3()), _tmp: new THREE.Vector3() }, camera);
  const pools = instances.map(pool => {
    const pickedBefore = pool.picked; pool.picked = [];
    Pool.prototype.update.call(pool, camera.position);
    if (!pool.picked.length && pool.mesh.count > 0) pool.picked = pickedBefore;
    return { objectOrdinal: pool.objectOrdinal, selection: pool.picked, shadowCount: pool.nShadow };
  });
  return { camera: config, pools };
});
fs.writeFileSync(path.join(dir, 'pool-queries.json'), JSON.stringify({ format: 'SBPOOLCHECK1', version: 1, frames }));
console.log(`verified ${manifest.meshes.length} pools, ${manifest.blobs.length} checksums, ${samples} original Pool.write samples; exported ${frames.length} original Pool.update frames`);
