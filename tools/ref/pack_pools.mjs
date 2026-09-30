// Compact the preserved Pool items into the same independently compressed
// buffer archive used by geometry. Does not rebuild the city.
import { BAKE, SHADERS } from './paths.mjs';
import { SRC_ROOT } from './stubs.mjs';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';
const THREE = await import(pathToFileURL(path.join(SRC_ROOT, 'node_modules/three/build/three.module.js')).href);
const dir = BAKE;
const source = JSON.parse(zlib.gunzipSync(fs.readFileSync(path.join(dir, 'pools.json.gz'))));
if (source.format !== 'SBPOOL1' || source.version !== 1) throw new Error('Invalid pool source');
const blobs = [], meshes = [], hashes = new Map();
const chunks = [Buffer.from('SBGEO01\0')]; let offset = 8, rawBytes = 0;
function blob(array) {
  const raw = Buffer.from(array.buffer, array.byteOffset, array.byteLength);
  const hash = crypto.createHash('sha256').update(raw).digest('hex');
  const key = array.constructor.name + ':' + hash;
  if (hashes.has(key)) return hashes.get(key);
  const packed = zlib.deflateSync(raw, { level: 1 }), head = Buffer.alloc(8);
  head.writeUInt32LE(raw.length, 0); head.writeUInt32LE(packed.length, 4);
  const id = blobs.length;
  blobs.push({ offset, bytes: raw.length, compressedBytes: packed.length,
    arrayType: array.constructor.name, elements: array.length, sha256: hash });
  hashes.set(key, id); chunks.push(head, packed); offset += 8 + packed.length; rawBytes += raw.length;
  return id;
}
const matrix = new THREE.Matrix4(), position = new THREE.Vector3(), quaternion = new THREE.Quaternion(),
  scale = new THREE.Vector3(), euler = new THREE.Euler();
for (const pool of source.pools) {
  const n = pool.items.length, matrices = new Float32Array(n * 16), positions = new Float64Array(n * 3);
  const attrs = {};
  if (pool.hasColor) attrs.instanceColor = { itemSize: 3, data: new Float32Array(n * 3) };
  for (const [name, size] of Object.entries(pool.extraDefs))
    if (name !== 'aLod') attrs[name] = { itemSize: size, data: new Float32Array(n * size) };
  for (const [i, item] of pool.items.entries()) {
    position.set(item.x, item.y, item.z); position.toArray(positions, i * 3);
    quaternion.setFromAxisAngle(THREE.Object3D.DEFAULT_UP, item.ry);
    if (item.rx || item.rz) quaternion.setFromEuler(euler.set(item.rx || 0, item.ry, item.rz || 0, 'YXZ'));
    if (item.scale3) scale.set(...item.scale3).multiplyScalar(item.s); else scale.setScalar(item.s);
    if (item.hidden) scale.setScalar(0);
    matrix.compose(position, quaternion, scale).toArray(matrices, i * 16);
    if (item.color && attrs.instanceColor) attrs.instanceColor.data.set(item.color, i * 3);
    for (const [name, value] of Object.entries(item.extra ?? {})) {
      const attr = attrs[name]; if (!attr) continue;
      if (attr.itemSize === 1) attr.data[i] = value;
      else attr.data.set(value, i * attr.itemSize);
    }
  }
  const { items, ...meta } = pool;
  meta.count = n; meta.positions = blob(positions);
  meta.attributes = { instanceMatrix: { itemSize: 16, blob: blob(matrices) } };
  for (const [name, attr] of Object.entries(attrs)) meta.attributes[name] = { itemSize: attr.itemSize, blob: blob(attr.data) };
  meshes.push(meta);
}
fs.writeFileSync(path.join(dir, 'pool-data.sbgeo'), Buffer.concat(chunks));
fs.writeFileSync(path.join(dir, 'pool-data.json'), JSON.stringify({ format: 'SBGEO01', version: 1,
  kind: 'SBPOOL1', source: source.source, threeRevision: source.threeRevision,
  archive: 'pool-data.sbgeo', rawBytes, compressedBytes: offset, blobs, meshes, materials: [] }));
console.log(`packed ${meshes.length} pools, ${blobs.length} unique buffers: ${(rawBytes / 1048576).toFixed(1)} MiB raw, ${(offset / 1048576).toFixed(1)} MiB compressed`);
