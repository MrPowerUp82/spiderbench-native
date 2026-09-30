// Capture the actual Three.js programs after lighting.js has installed its global
// surface and CSM patches. Run with `node tools/ref/capture_shaders.mjs [--city]`.
import { SRC_ROOT, renderer as buildRenderer } from './stubs.mjs';
import './scene_instrumentation.mjs';
import { createMockGL } from './mockgl.mjs';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import zlib from 'node:zlib';
import crypto from 'node:crypto';
import { createCanvas } from '@napi-rs/canvas';

const imp = p => import(pathToFileURL(path.join(SRC_ROOT, p)).href);
const THREE = await imp('node_modules/three/build/three.module.js');
const { getDFGLUT } = await imp('node_modules/three/src/renderers/shaders/DFGLUTData.js');
const { WebGLMaterials } = await imp('node_modules/three/src/renderers/webgl/WebGLMaterials.js');
const { createLighting } = await imp('src/render/lighting.js');
const args = new Set(process.argv.slice(2));
const out = path.resolve(args.has('--city') ? 'build/refshaders' : 'build/refshaders-smoke');
fs.mkdirSync(out, { recursive: true });
for (const name of fs.readdirSync(out)) {
  if (/^\d{4}\.(vert|frag)\.glsl$/.test(name) || name === 'manifest.json')
    fs.rmSync(path.join(out, name));
}

const canvas = { width: 1600, height: 900, style: {}, addEventListener() {}, removeEventListener() {}, getContext() {} };
const gl = createMockGL(canvas);
const renderer = new THREE.WebGLRenderer({ canvas, context: gl, antialias: false, reversedDepthBuffer: true });
const materialUniforms = WebGLMaterials(renderer, renderer.properties);
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(55, 16 / 9, 0.1, 150000);
camera.position.set(0, 30, 80);
camera.lookAt(0, 0, 0);
camera.updateMatrixWorld();
const mirrorCamera = camera.clone();
mirrorCamera.layers.disableAll();
mirrorCamera.layers.enable(27); // REFL_LAYER in world/water.js
mirrorCamera.layers.enable(28); // BIG_CASTER_LAYER in render/csm.js
const sub = object => ({ traverse: fn => fn(object), traverseVisible() {} });
const lighting = createLighting({ renderer, scene });
// PMREM is a CubeUV texture at program-key level. The headless bake does not
// render pixels, but this enables the same USE_ENVMAP shader branch as the game.
// PMREM from the game's 256 px cubemap packs into 3*256 by 4*256.
// A non-power-of-two height makes Three emit an invalid CUBEUV_MAX_MIP.
scene.environment = new THREE.Texture({ width: 768, height: 1024 });
scene.environment.mapping = THREE.CubeUVReflectionMapping;
scene.environmentIntensity = lighting.envIntensity;
lighting.csm.update(camera, scene);
// The game's warm-up compiles with a half-float render target bound, which
// selects linear output rather than the default framebuffer's sRGB variant.
const captureTarget = new THREE.WebGLRenderTarget(1, 1, { type: THREE.HalfFloatType });
renderer.setRenderTarget(captureTarget);

const targets = [];
if (args.has('--city')) {
  const { buildCity } = await imp('src/world/city.js');
  const cityScene = new THREE.Scene();
  await buildCity({ scene: cityScene, renderer: buildRenderer });
  lighting.csm.tagCasters(cityScene);
  cityScene.traverse(o => { if (o.isMesh && o.geometry?.attributes.position && o.material) targets.push(o); });
} else {
  const material = new THREE.MeshStandardMaterial({ color: 0x888888, roughness: 0.6 });
  material.name = 'standard-smoke';
  const mesh = new THREE.Mesh(new THREE.BoxGeometry(), material);
  mesh.name = 'standard-smoke';
  mesh.castShadow = mesh.receiveShadow = true;
  targets.push(mesh);
}

const entries = [];
const usages = [];
const textureBindings = [];
const textureEntries = [];
const textureIds = new WeakMap();
const objectPrograms = [];
const variants = new Map();
function depthMaterial(object, source) {
  // Mirrors WebGLShadowMap.getDepthMaterial for the PCF directional cascades.
  const material = object.customDepthMaterial?.clone() ?? new THREE.MeshDepthMaterial();
  for (const name of ['visible', 'wireframe', 'alphaMap', 'map', 'clipShadows', 'clippingPlanes', 'clipIntersection',
    'displacementMap', 'displacementScale', 'displacementBias', 'wireframeLinewidth', 'linewidth']) material[name] = source[name];
  material.alphaTest = source.alphaToCoverage ? 0.5 : source.alphaTest;
  material.side = source.shadowSide ?? [THREE.BackSide, THREE.FrontSide, THREE.DoubleSide][source.side];
  return material;
}
function renderState(material) {
  const result = {};
  for (const key of ['side','transparent','depthWrite','depthTest','depthFunc','colorWrite','blending','premultipliedAlpha',
    'blendEquation','blendSrc','blendDst','blendEquationAlpha','blendSrcAlpha','blendDstAlpha','blendAlpha',
    'polygonOffset','polygonOffsetFactor','polygonOffsetUnits']) result[key] = material[key];
  result.blendColor = material.blendColor.toArray();
  return result;
}
function uniformValue(value) {
  if (value == null || value?.isTexture) return undefined;
  if (typeof value === 'number') return Number.isFinite(value) ? value : undefined;
  if (typeof value === 'boolean') return value;
  if (typeof value?.toArray === 'function') return uniformValue(value.toArray());
  if (Array.isArray(value) || ArrayBuffer.isView(value)) return Array.from(value, v => uniformValue(v) ?? null);
  if (typeof value === 'object') {
    const result = {};
    for (const [key, v] of Object.entries(value)) {
      const serialized = uniformValue(v);
      if (serialized !== undefined) result[key] = serialized;
    }
    return result;
  }
  return undefined;
}
const texDir = path.resolve('build/city-bake/texture-pixels');
if (args.has('--city')) {
  fs.mkdirSync(texDir, { recursive: true });
  for (const name of fs.readdirSync(texDir))
    if (/^\d{4}\.bin\.z$/.test(name)) fs.rmSync(path.join(texDir, name));
}
function textureId(texture) {
  if (!texture?.isTexture) return null;
  if (texture === scene.environment) return null; // program-key placeholder, no PMREM pixels
  if (textureIds.has(texture)) return textureIds.get(texture);
  const id = textureEntries.length;
  textureIds.set(texture, id);
  const image = texture.image;
  let bytes = null, arrayType = null, pixelError = null;
  let width = image?.width ?? 0, height = image?.height ?? 0, depth = image?.depth ?? 1;
  if (ArrayBuffer.isView(image?.data)) {
    const a = image.data;
    bytes = Buffer.from(a.buffer, a.byteOffset, a.byteLength);
    arrayType = a.constructor.name;
  } else if (width && height && image) {
    try {
      const cv = createCanvas(width, height);
      const cx = cv.getContext('2d');
      cx.drawImage(image, 0, 0);
      const a = cx.getImageData(0, 0, width, height).data;
      bytes = Buffer.from(a.buffer, a.byteOffset, a.byteLength);
      arrayType = 'Uint8ClampedArray';
    } catch (error) { pixelError = String(error); }
  }
  const name = String(id).padStart(4, '0') + '.bin.z';
  if (bytes) fs.writeFileSync(path.join(texDir, name), zlib.deflateSync(bytes, { level: 1 }));
  textureEntries.push({ id, name: texture.name, source: image?.src ?? image?._src ?? null,
    width, height, depth, format: texture.format, type: texture.type,
    colorSpace: texture.colorSpace, flipY: texture.flipY,
    wrapS: texture.wrapS, wrapT: texture.wrapT, magFilter: texture.magFilter,
    minFilter: texture.minFilter, generateMipmaps: texture.generateMipmaps,
    arrayType, bytes: bytes?.length ?? 0, file: bytes ? `texture-pixels/${name}` : null,
    pixelError,
    sha256: bytes ? crypto.createHash('sha256').update(bytes).digest('hex') : null });
  return id;
}
const programIds = new Map();
const seen = new Set();
const materialIds = new WeakMap();
let nextMaterialId = 0;
for (const [objectOrdinal, object] of targets.entries()) {
  const mats = Array.isArray(object.material) ? object.material : [object.material];
  for (const [materialSlot, material] of mats.entries()) {
    if (!materialIds.has(material)) materialIds.set(material, nextMaterialId++);
    const materialId = materialIds.get(material);
    // pipeline.prepareMaterials() performs this scan before the game's warm-up.
    let customKey = '';
    try { customKey = material.customProgramCacheKey?.() ?? ''; } catch { /* same fallback as pipeline.js */ }
    if (material.userData?.noSSR || customKey.includes('hero-glass')) {
      material.defines = { ...(material.defines || {}), NO_SSR: '' };
      material.needsUpdate = true;
    }
    const key = [material.uuid, object.isInstancedMesh, !!object.instanceColor, object.isSkinnedMesh,
      Object.keys(object.geometry.attributes).sort().join(','), object.receiveShadow].join('|');
    if (seen.has(key)) {
      for (const use of variants.get(key)) objectPrograms.push({ objectOrdinal, materialSlot, ...use });
      continue;
    }
    seen.add(key);
    variants.set(key, []);
    const probe = new THREE.Mesh(object.geometry, material);
    probe.name = object.name;
    probe.castShadow = object.castShadow;
    probe.receiveShadow = object.receiveShadow;
    let drawable = probe;
    if (object.isInstancedMesh) {
      // An InstancedMesh is required for the USE_INSTANCING program variant.
      const inst = new THREE.InstancedMesh(object.geometry, material, 1);
      // This controls USE_INSTANCING_COLOR in Three's program key. A newly
      // constructed probe has no color buffer, even when the source mesh does.
      inst.instanceColor = object.instanceColor;
      inst.name = probe.name; inst.castShadow = probe.castShadow; inst.receiveShadow = probe.receiveShadow;
      drawable = inst;
    }
    // Main scene and unshadowed river-mirror passes have different program keys.
    const depth = args.has('--city') ? depthMaterial(object, material) : null;
    for (const [pass, shadows] of args.has('--city') ? [['main', true], ['mirror', false], ['depth', false]] : [['main', true]]) {
      const activeMaterial = pass === 'depth' ? depth : material;
      drawable.material = activeMaterial;
      renderer.shadowMap.enabled = shadows;
      const before = gl._captured.length;
      renderer.compile(sub(drawable), pass === 'mirror' ? mirrorCamera : camera, scene);
      for (let i = before; i < gl._captured.length; i++) {
        const c = gl._captured[i], id = String(i).padStart(4, '0');
        programIds.set(c.prog.id, id);
        fs.writeFileSync(path.join(out, `${id}.vert.glsl`), c.vs ?? '');
        fs.writeFileSync(path.join(out, `${id}.frag.glsl`), c.fs ?? '');
        entries.push({ id, pass, objectOrdinal, materialSlot, materialId,
          object: object.name, material: material.name, type: material.type,
          programMaterialType: activeMaterial.type,
        instanced: !!object.isInstancedMesh, instanceColor: !!object.instanceColor, attributes: Object.keys(object.geometry.attributes),
        castShadow: !!object.castShadow, receiveShadow: !!object.receiveShadow,
        vertexBytes: c.vs?.length ?? 0, fragmentBytes: c.fs?.length ?? 0 });
      }
      const props = renderer.properties.get(activeMaterial);
      materialUniforms.refreshMaterialUniforms(props.uniforms, activeMaterial, 1, canvas.height, null);
      // The renderer assigns this built-in LUT during setProgram(), after compile().
      // Missing it makes the PBR multiscattering calculation produce 0/0.
      if (props.uniforms?.dfgLUT) props.uniforms.dfgLUT.value = getDFGLUT();
      const program = props.currentProgram?.program;
      const id = program && programIds.get(program.id);
      if (!id) throw new Error(`No captured ${pass} program for ${object.name || material.type}`);
      const usageIndex = usages.length;
      variants.get(key).push({ pass, id, usageIndex });
      objectPrograms.push({ objectOrdinal, materialSlot, pass, id, usageIndex });
      const values = {};
      for (const [name, u] of Object.entries(props.uniforms ?? {})) {
        const value = uniformValue(u?.value);
        if (value !== undefined) values[name] = value;
      }
      // compile() creates programs without refreshing the standard material
      // uniforms. Export the actual material values instead of ShaderLib defaults.
      for (const name of ['opacity', 'roughness', 'metalness', 'alphaTest', 'envMapIntensity', 'bumpScale', 'aoMapIntensity', 'reflectivity', 'ior', 'clearcoat', 'clearcoatRoughness'])
        if (activeMaterial[name] !== undefined) values[name] = activeMaterial[name];
      if (activeMaterial.color) values.diffuse = activeMaterial.color.toArray();
      if (activeMaterial.emissive) values.emissive = activeMaterial.emissive.clone().multiplyScalar(activeMaterial.emissiveIntensity).toArray();
      if (activeMaterial.normalScale) values.normalScale = activeMaterial.normalScale.toArray();
      if (props.uniforms.envMapIntensity && !activeMaterial.envMap) values.envMapIntensity = scene.environmentIntensity;
      for (const name of ['map', 'normalMap', 'roughnessMap', 'metalnessMap', 'emissiveMap', 'alphaMap', 'aoMap', 'bumpMap']) {
        if (activeMaterial[name]) {
          activeMaterial[name].updateMatrix();
          values[name + 'Transform'] = activeMaterial[name].matrix.toArray();
        }
      }
      usages.push({ id, pass, objectOrdinal, materialSlot, materialId,
        object: object.name, material: material.name, type: material.type,
        programMaterialType: activeMaterial.type, side: activeMaterial.side,
        renderState: renderState(activeMaterial),
        instanced: !!object.isInstancedMesh, instanceColor: !!object.instanceColor, attributes: Object.keys(object.geometry.attributes),
        uniformValues: values,
        customUniforms: Object.keys(props.uniforms ?? {}).filter(k => /^u[A-Z]|^(ambData|csmData)$/.test(k)) });
      if (args.has('--city')) {
        const uniforms = {};
        for (const [name, uniform] of Object.entries(props.uniforms ?? {})) {
          const tid = textureId(uniform?.value);
          if (tid !== null) uniforms[name] = tid;
        }
        // Include material maps even if an optimized shader variant omits them.
        for (const name of ['map', 'normalMap', 'roughnessMap', 'metalnessMap', 'emissiveMap', 'alphaMap', 'aoMap', 'bumpMap']) {
          const tid = textureId(activeMaterial[name]);
          if (tid !== null) uniforms[name] = tid;
        }
        textureBindings.push({ objectOrdinal, materialSlot, materialId, pass, uniforms });
      }
    }
    renderer.shadowMap.enabled = true;
    drawable.material = material;
  }
}
fs.writeFileSync(path.join(out, 'manifest.json'), JSON.stringify({
  source: SRC_ROOT, threeRevision: THREE.REVISION, timeOfDay: lighting.tod.name,
  reversedDepth: true, environmentMapping: 'CubeUVReflectionMapping',
  quality: lighting.quality.name, cascadeCount: lighting.csm.N,
  shadowConfig: { splits: lighting.csm.splits, mapSize: lighting.csm.size },
  sunDirection: lighting.sun.position.clone().sub(lighting.sun.target.position).normalize().toArray(),
  environmentIntensity: scene.environmentIntensity,
  shadowObjects: targets.map((o, objectOrdinal) => ({ objectOrdinal, layers: o.layers.mask,
    frustumCulled: o.frustumCulled,
    geometryInstances: o.geometry.isInstancedBufferGeometry ? o.geometry.instanceCount : 0,
    tileCenter: o.userData.sbTileCenter,
    attributeDivisors: Object.fromEntries(Object.entries(o.geometry.attributes).map(([k,a]) => [k, a.isInstancedBufferAttribute ? a.meshPerAttribute : 0])),
    minCascade: o.userData.minCascade ?? 0, maxCascade: o.userData.maxCascade ?? 100,
    smallCasters: !!o.userData.smallCasters })),
  shadowTaps: lighting.quality.shadowTaps, charCascade: !!lighting.csm.charLight,
  entries, usages, objectPrograms,
}, null, 2));
if (args.has('--city')) {
  fs.writeFileSync(path.resolve('build/city-bake/textures.json'), JSON.stringify({
    format: 'SBTEX1', version: 1, source: SRC_ROOT, threeRevision: THREE.REVISION,
    textures: textureEntries, bindings: textureBindings,
  }));
}
const lit = entries.filter(e => e.pass === 'main' && (e.type === 'MeshStandardMaterial' || e.type === 'MeshPhysicalMaterial'));
for (const e of lit) {
  const fragment = fs.readFileSync(path.join(out, `${e.id}.frag.glsl`), 'utf8');
  if (!fragment.includes('float csmShadow()') || !fragment.includes('uniform AmbData ambData') ||
      !fragment.includes('#define USE_ENVMAP')) {
    throw new Error(`Missing lighting/CSM/IBL patch in program ${e.id} (${e.object})`);
  }
}
console.log(`captured ${entries.length} programs, ${usages.length} material/geometry variants from ${targets.length} meshes in ${out}`);
if (args.has('--city')) console.log(`captured ${textureEntries.length} textures (${textureEntries.filter(t => t.file).length} with pixels)`);
