// Validate every compressed geometry buffer and its references without rebuilding the city.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import crypto from 'node:crypto';

const stem = process.argv.includes('--sample') ? 'geometry-sample' : 'geometry';
const dir = path.resolve('build/city-bake');
const m = JSON.parse(fs.readFileSync(path.join(dir, stem + '.json')));
if (m.format !== 'SBGEO01' || m.version !== 1) throw new Error('Unsupported archive');
const fd = fs.openSync(path.join(dir, m.archive), 'r');
const head = Buffer.alloc(8);
fs.readSync(fd, head, 0, 8, 0);
if (!head.equals(Buffer.from('SBGEO01\0', 'ascii'))) throw new Error('Bad magic');
const bytesPer = { Float32Array: 4, Float64Array: 8, Uint32Array: 4, Int32Array: 4,
  Uint16Array: 2, Int16Array: 2, Uint8Array: 1, Int8Array: 1, Uint8ClampedArray: 1 };
for (const b of m.blobs) {
  const h = Buffer.alloc(8);
  if (fs.readSync(fd, h, 0, 8, b.offset) !== 8) throw new Error('Truncated blob header');
  if (h.readUInt32LE(0) !== b.bytes || h.readUInt32LE(4) !== b.compressedBytes) throw new Error('Blob length mismatch');
  const compressed = Buffer.allocUnsafe(b.compressedBytes);
  if (fs.readSync(fd, compressed, 0, compressed.length, b.offset + 8) !== compressed.length) throw new Error('Truncated blob');
  const raw = zlib.inflateSync(compressed);
  if (raw.length !== b.bytes || b.elements * bytesPer[b.arrayType] !== b.bytes ||
      crypto.createHash('sha256').update(raw).digest('hex') !== b.sha256) throw new Error('Blob checksum mismatch');
}
for (const mesh of m.meshes) {
  if (mesh.matrixWorld.length !== 16 || mesh.bounds.length !== 2 || !mesh.attributes.position) throw new Error('Invalid mesh');
  for (const a of Object.values(mesh.attributes)) {
    if (!m.blobs[a.blob] || a.itemSize < 1 || a.count < 0 ||
        a.count * a.itemSize > m.blobs[a.blob].elements) throw new Error('Invalid attribute');
  }
  if (mesh.index && !m.blobs[mesh.index.blob]) throw new Error('Invalid index');
  if (mesh.material.some(id => !m.materials[id])) throw new Error('Invalid material');
}
fs.closeSync(fd);
console.log(`verified ${m.meshes.length} meshes, ${m.blobs.length} buffers, ${(m.rawBytes / 1048576).toFixed(1)} MiB raw`);
