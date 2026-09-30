// Reference the actual CSM.update and Three.js shadow-matrix implementation.
import { BAKE, SHADERS } from './paths.mjs';
import { SRC_ROOT } from './stubs.mjs';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
const imp = p => import(pathToFileURL(path.join(SRC_ROOT, p)).href);
const THREE = await imp('node_modules/three/build/three.module.js');
const { CSM } = await imp('src/render/csm.js');
const { getQuality } = await imp('src/render/quality.js');
const manifestPath = path.join(SHADERS, 'manifest.json');
const manifest = JSON.parse(fs.readFileSync(manifestPath));
const quality = getQuality();
if (manifest.quality !== quality.name || manifest.cascadeCount !== quality.cascades) throw new Error('CSM quality mismatch');
const csm = new CSM({ scene: new THREE.Scene(), quality, reversed: true });
csm.setSunDirection(new THREE.Vector3().fromArray(manifest.sunDirection));
const config = { splits: csm.splits, mapSize: csm.size };
// Upgrade an already-captured manifest without rebuilding the full city.
manifest.shadowConfig = config;
fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2));
const frames = [];
const cases = [
  [[250,32,175], [250,8,-250],55,16/9],
  [[250,32,175], [250,8,-250],55,16/9],
  [[252,32,175], [252,8,-250],55,16/9],
  [[0,70,-600], [0,30,-1400],55,16/9],
  [[120,100,2760], [80,50,2200],86,2.2],
  [[-800,12,-1800], [-780,200,-1800],55,0.8],
  [[-800,12,-1800], [-780,200,-1800],55,0.8],
  [[-800,12,-1800], [-780,200,-1800],55,0.8],
];
for (const [position, target, fov, aspect] of cases) {
  const camera = new THREE.PerspectiveCamera(fov, aspect,0.1,20000);
  camera.position.fromArray(position); camera.lookAt(new THREE.Vector3().fromArray(target)); camera.updateMatrixWorld();
  csm.update(camera);
  const cascades = csm.lights.map(l => {
    l.shadow.camera._reversedDepth = true; l.shadow.camera.updateProjectionMatrix();
    l.shadow.updateMatrices(l);
    return { due: l.shadow.needsUpdate, matrix: l.shadow.matrix.toArray(), size: l.shadow.mapSize.x,
      bias: l.shadow.bias, normalBias: l.shadow.normalBias, radius: l.shadow.radius };
  });
  frames.push({ camera: { position, quaternion: camera.quaternion.toArray(), fov, aspect, zNear:camera.near,zFar:camera.far }, cascades });
}
fs.writeFileSync(path.join(BAKE, 'csm-queries.json'), JSON.stringify({ format:'SBCSMCHECK1',config,sunDirection:manifest.sunDirection,frames }));
console.log(`exported ${frames.length * csm.N} original JS cascade fits, including staggered updates, FOV and aspect changes`);
