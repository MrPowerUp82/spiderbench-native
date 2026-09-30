// Preserve the Three.js city scene's vertex/index buffers without re-tessellating.
// `--limit 12` makes a small archive for format verification; omit for the city.
import { renderer, SRC_ROOT } from './stubs.mjs';
import './scene_instrumentation.mjs';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import zlib from 'node:zlib';
import crypto from 'node:crypto';

const imp = p => import(pathToFileURL(path.join(SRC_ROOT, p)).href);
const THREE = await imp('node_modules/three/build/three.module.js');
const { buildCity } = await imp('src/world/city.js');
const scene = new THREE.Scene();
await buildCity({ scene, renderer });
scene.updateMatrixWorld(true);
const argv = process.argv.slice(2);
const li = argv.indexOf('--limit');
const limit = li < 0 ? Infinity : Number(argv[li + 1]);
if (!(limit > 0)) throw new Error('--limit requires a positive mesh count');
const dir = path.resolve('build/city-bake');
fs.mkdirSync(dir, { recursive: true });
const stem = Number.isFinite(limit) ? 'geometry-sample' : 'geometry';
const packPath = path.join(dir, stem + '.sbgeo');
const tempPath = packPath + '.tmp';
const fd = fs.openSync(tempPath, 'w');
const blobs = [], meshes = [], materials = [];
const blobId = new WeakMap(), matId = new WeakMap();
let offset = 0, rawBytes = 0;
const write = data => { let p = 0; while (p < data.length) p += fs.writeSync(fd, data, p, data.length - p); offset += data.length; };
write(Buffer.from('SBGEO01\0', 'ascii'));

function blob(a, usedLength = a.length) {
  if (!ArrayBuffer.isView(a)) throw new Error('Expected typed geometry array');
  if (usedLength === a.length && blobId.has(a)) return blobId.get(a);
  const raw = Buffer.from(a.buffer, a.byteOffset, usedLength * a.BYTES_PER_ELEMENT);
  const packed = zlib.deflateSync(raw, { level: 1 });
  const id = blobs.length;
  const header = Buffer.allocUnsafe(8);
  header.writeUInt32LE(raw.length, 0); header.writeUInt32LE(packed.length, 4);
  const at = offset;
  write(header); write(packed);
  blobs.push({ offset: at, bytes: raw.length, compressedBytes: packed.length,
    arrayType: a.constructor.name, elements: usedLength,
    sha256: crypto.createHash('sha256').update(raw).digest('hex') });
  rawBytes += raw.length;
  if (usedLength === a.length) blobId.set(a, id);
  return id;
}
function attr(a) {
  if (a.isInterleavedBufferAttribute) {
    const dense = new Float32Array(a.count * a.itemSize);
    for (let i = 0; i < a.count; i++) for (let k = 0; k < a.itemSize; k++)
      dense[i * a.itemSize + k] = a.getComponent(i, k);
    return { blob: blob(dense), itemSize: a.itemSize, count: a.count, normalized: !!a.normalized };
  }
  return { blob: blob(a.array), itemSize: a.itemSize, count: a.count, normalized: !!a.normalized, divisor: a.isInstancedBufferAttribute ? a.meshPerAttribute : 0 };
}
function color(c) { return c?.isColor ? [c.r, c.g, c.b] : null; }
function texture(t) {
  if (!t) return null;
  const im = t.image;
  return { name: t.name, source: im?.src ?? im?._src ?? null, wrapS: t.wrapS, wrapT: t.wrapT,
    repeat: t.repeat?.toArray(), offset: t.offset?.toArray(), rotation: t.rotation,
    colorSpace: t.colorSpace, flipY: t.flipY };
}
function material(m) {
  if (matId.has(m)) return matId.get(m);
  const id = materials.length;
  matId.set(m, id);
  const maps = {};
  for (const key of ['map', 'normalMap', 'roughnessMap', 'metalnessMap', 'emissiveMap', 'alphaMap', 'aoMap', 'bumpMap'])
    if (m[key]) maps[key] = texture(m[key]);
  let customKey = '';
  try { customKey = m.customProgramCacheKey?.() ?? ''; } catch { /* unsupported key */ }
  materials.push({ name: m.name, type: m.type, color: color(m.color), emissive: color(m.emissive),
    roughness: m.roughness, metalness: m.metalness, opacity: m.opacity, alphaTest: m.alphaTest,
    side: m.side, transparent: m.transparent, depthWrite: m.depthWrite, vertexColors: m.vertexColors,
    envMapIntensity: m.envMapIntensity, emissiveIntensity: m.emissiveIntensity,
    defines: m.defines ?? {}, customKey, maps });
  return id;
}

try {
  scene.traverse(o => {
    if (meshes.length >= limit || !o.isMesh || !o.geometry?.attributes.position) return;
    const g = o.geometry, attrs = {};
    for (const [name, a] of Object.entries(g.attributes)) attrs[name] = attr(a);
    const indices = g.index ? attr(g.index) : null;
    const count = g.index?.count ?? g.attributes.position.count;
    const start = Math.min(g.drawRange.start, count);
    const drawCount = Number.isFinite(g.drawRange.count) ? Math.min(g.drawRange.count, count - start) : count - start;
    if (!g.boundingBox) g.computeBoundingBox();
    const instanceCount = o.isInstancedMesh ? o.count : 0;
    meshes.push({ objectOrdinal: meshes.length, name: o.name, type: o.type, visible: o.visible, castShadow: o.castShadow,
      receiveShadow: o.receiveShadow, renderOrder: o.renderOrder, layers: o.layers.mask,
      dynamic: !!o.userData?.dynamic, material: (Array.isArray(o.material) ? o.material : [o.material]).map(material),
      attributes: attrs, index: indices, drawRange: [start, drawCount], groups: g.groups,
      matrixWorld: o.matrixWorld.toArray(), bounds: [g.boundingBox.min.toArray(), g.boundingBox.max.toArray()],
      instanced: !!o.isInstancedMesh, instanceCount,
      geometryInstances: g.isInstancedBufferGeometry ? g.instanceCount : 0, frustumCulled: o.frustumCulled,
      instanceMatrix: o.isInstancedMesh ? blob(o.instanceMatrix.array, instanceCount * 16) : null,
      instanceColor: o.isInstancedMesh && o.instanceColor ? blob(o.instanceColor.array, instanceCount * 3) : null });
  });
  fs.closeSync(fd);
  fs.renameSync(tempPath, packPath);
} catch (error) {
  fs.closeSync(fd);
  fs.rmSync(tempPath, { force: true });
  throw error;
}

const manifest = { format: 'SBGEO01', version: 1, source: SRC_ROOT, threeRevision: THREE.REVISION,
  archive: path.basename(packPath), rawBytes, compressedBytes: offset, blobs, materials, meshes };
fs.writeFileSync(path.join(dir, stem + '.json'), JSON.stringify(manifest));
console.log(JSON.stringify({ meshes: meshes.length, materials: materials.length, blobs: blobs.length,
  rawMB: +(rawBytes / 1048576).toFixed(1), packedMB: +(offset / 1048576).toFixed(1), archive: packPath }));
