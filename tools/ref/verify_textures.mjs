// Validate baked pixel data and every program/material texture binding.
import { BAKE, SHADERS } from './paths.mjs';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import crypto from 'node:crypto';

const dir = BAKE;
const textureBake = JSON.parse(fs.readFileSync(path.join(dir, 'textures.json')));
const geometry = JSON.parse(fs.readFileSync(path.join(dir, 'geometry.json')));
const shaders = JSON.parse(fs.readFileSync(path.join(SHADERS, 'manifest.json')));
if (textureBake.format !== 'SBTEX1' || textureBake.version !== 1 ||
    textureBake.source !== geometry.source || textureBake.source !== shaders.source ||
    textureBake.threeRevision !== geometry.threeRevision || textureBake.threeRevision !== shaders.threeRevision ||
    textureBake.bindings.length !== shaders.usages.length)
  throw new Error('Texture source or binding count mismatch');
let total = 0, baked = 0, dynamic = 0;
for (const [id, t] of textureBake.textures.entries()) {
  if (id !== t.id) throw new Error(`Texture ID mismatch at ${id}`);
  if (!t.file) {
    if (t.bytes !== 0 || t.sha256 || t.source || t.type !== 1016)
      throw new Error(`Unexpected missing texture pixels at ${id}`);
    dynamic++;
    continue;
  }
  const packed = fs.readFileSync(path.join(dir, t.file));
  const raw = zlib.inflateSync(packed);
  if (raw.length !== t.bytes || crypto.createHash('sha256').update(raw).digest('hex') !== t.sha256)
    throw new Error(`Texture pixel mismatch at ${id}`);
  if (t.arrayType === 'Uint8ClampedArray' && raw.length !== t.width * t.height * 4)
    throw new Error(`Canvas image size mismatch at ${id}`);
  total += raw.length;
  baked++;
}
for (const [i, binding] of textureBake.bindings.entries()) {
  const use = shaders.usages[i];
  const mesh = geometry.meshes[binding.objectOrdinal];
  if (!mesh || binding.objectOrdinal !== use.objectOrdinal ||
      binding.materialId !== use.materialId || binding.materialSlot !== use.materialSlot ||
      binding.pass !== use.pass || mesh.material[binding.materialSlot] !== binding.materialId)
    throw new Error(`Texture binding mismatch at usage ${i}`);
  for (const id of Object.values(binding.uniforms))
    if (!textureBake.textures[id]) throw new Error(`Missing bound texture ${id} at usage ${i}`);
}
for (const t of textureBake.textures.filter(t => !t.file)) {
  const names = textureBake.bindings.flatMap(b => Object.entries(b.uniforms)
    .filter(([, id]) => id === t.id).map(([name]) => name));
  if (names.length === 0 || names.some(name => name !== 'tRefl'))
    throw new Error(`Unexpected non-reflection texture without pixels: ${t.id}`);
}
console.log(`verified ${baked} pixel textures (${(total / 1048576).toFixed(1)} MiB raw), ${dynamic} dynamic render targets, ${textureBake.bindings.length} bindings`);
