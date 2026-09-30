// Check shader-program bindings against the independently baked geometry scene.
import { BAKE, SHADERS } from './paths.mjs';
import fs from 'node:fs';
import path from 'node:path';

const shaders = JSON.parse(fs.readFileSync(path.join(SHADERS, 'manifest.json')));
const geometry = JSON.parse(fs.readFileSync(path.join(BAKE, 'geometry.json')));
if (shaders.source !== geometry.source || shaders.threeRevision !== geometry.threeRevision)
  throw new Error('Shader and geometry bakes came from different JS sources');
const programs = new Set(shaders.entries.map(e => e.id));
const bindings = new Set();
for (const use of shaders.usages) {
  const mesh = geometry.meshes[use.objectOrdinal];
  const material = geometry.materials[use.materialId];
  if (!programs.has(use.id) || !mesh || !material || mesh.name !== use.object ||
      mesh.material[use.materialSlot] !== use.materialId || mesh.instanced !== use.instanced ||
      (mesh.instanceColor !== null) !== use.instanceColor ||
      material.type !== use.type || Object.keys(mesh.attributes).sort().join(',') !== use.attributes.sort().join(','))
    throw new Error(`Shader/geometry mismatch at object ${use.objectOrdinal}, material ${use.materialId}`);
  const source = fs.readFileSync(path.join(SHADERS,use.id+'.vert.glsl'),'utf8');
  if (use.instanceColor && !source.includes('#define USE_INSTANCING_COLOR'))
    throw new Error(`Missing instance color shader variant for ${use.object}`);
  if (!use.uniformValues || typeof use.uniformValues !== 'object')
    throw new Error(`Missing numeric uniforms for program ${use.id}`);
  if (!use.renderState || !Number.isInteger(use.renderState.blending) || !Number.isInteger(use.renderState.depthFunc))
    throw new Error(`Missing render state for program ${use.id}`);
}
for (const link of shaders.objectPrograms ?? []) {
  const mesh = geometry.meshes[link.objectOrdinal], use = shaders.usages[link.usageIndex];
  if (!mesh || !use || link.id !== use.id || link.pass !== use.pass ||
      mesh.material[link.materialSlot] !== use.materialId || mesh.instanced !== use.instanced ||
      Object.keys(mesh.attributes).sort().join(',') !== [...use.attributes].sort().join(','))
    throw new Error(`Invalid program binding at mesh ${link.objectOrdinal}`);
  const key = `${link.objectOrdinal}:${link.materialSlot}:${link.pass}`;
  if (bindings.has(key)) throw new Error(`Duplicate program binding ${key}`);
  bindings.add(key);
}
for (const mesh of geometry.meshes) for (let slot = 0; slot < mesh.material.length; slot++)
  for (const pass of ['main', 'mirror', 'depth'])
    if (!bindings.has(`${mesh.objectOrdinal}:${slot}:${pass}`)) throw new Error(`Missing ${pass} binding at mesh ${mesh.objectOrdinal}`);
if (shaders.shadowObjects?.length !== geometry.meshes.length || shaders.shadowConfig?.splits?.length !== shaders.cascadeCount + 1)
  throw new Error('Missing cascade/mesh metadata');
for (const mesh of geometry.meshes) {
  const meta = shaders.shadowObjects[mesh.objectOrdinal];
  if (meta.objectOrdinal !== mesh.objectOrdinal || Object.keys(meta.attributeDivisors ?? {}).sort().join(',') !== Object.keys(mesh.attributes).sort().join(','))
    throw new Error(`Missing attribute divisors at mesh ${mesh.objectOrdinal}`);
}
console.log(`linked ${shaders.usages.length} shader usages to ${geometry.meshes.length} meshes and ${geometry.materials.length} materials`);
console.log(`verified ${bindings.size} per-object program bindings and exported uniform values`);
