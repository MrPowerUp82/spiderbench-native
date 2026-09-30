import { SRC_ROOT, renderer } from './stubs.mjs';
import './scene_instrumentation.mjs';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
const imp = p => import(pathToFileURL(path.join(SRC_ROOT,p)).href);
const THREE = await imp('node_modules/three/build/three.module.js');
const { buildCity } = await imp('src/world/city.js');
const scene = new THREE.Scene();
const world = await buildCity({ scene, renderer });
const objects = [];
scene.traverse(o => { if (o.isMesh && o.geometry?.attributes.position) objects.push(o); });
const manifest = JSON.parse(fs.readFileSync('build/refshaders/manifest.json'));
if (objects.length !== manifest.shadowObjects.length) throw new Error('Object inventory mismatch');
for (let i=0;i<objects.length;i++) {
  const o=objects[i];
  manifest.shadowObjects[i].frustumCulled=o.frustumCulled;
  manifest.shadowObjects[i].geometryInstances=o.geometry.isInstancedBufferGeometry ? o.geometry.instanceCount : 0;
}
fs.writeFileSync('build/refshaders/manifest.json',JSON.stringify(manifest,null,2));
const managed = objects.map((o,objectOrdinal) => ({o,objectOrdinal})).filter(({o}) => o.userData.sbTileCenter && !o.name.endsWith(' super'));
const groups = new Map();
for (const o of objects) if (o.name.endsWith(' super')) groups.set(o.geometry.index, o);
const camera = new THREE.PerspectiveCamera(55,16/9,0.1,20000);
const frames = [];
// Deliberately cross both sides of the 650/690 m hysteresis bands.
const center = objects.find(o => o.name === 'facadeLod 0').userData.sbTileCenter;
const positions = [[250,32,175],[0,70,-600],[120,100,2760],
  ...[710,680,670,640,680,700].map(d => [center[0] + 128 + d,32,center[1]])];
for (const position of positions) {
  camera.position.fromArray(position); camera.lookAt(camera.position.clone().add(new THREE.Vector3(0,-24,-425))); camera.updateMatrixWorld();
  world.update(0,camera);
  const states = managed.map(({o,objectOrdinal}) => {
    const superMesh = groups.get(o.geometry.index);
    return {objectOrdinal,visible:o.visible || !!superMesh?.visible,shadow:o.castShadow};
  });
  frames.push({position,states});
}
fs.writeFileSync('build/city-bake/tile-queries.json',JSON.stringify({format:'SBTILECHECK1',frames}));
console.log(`exported ${frames.length * managed.length} original visibility/shadow states (${managed.length} meshes, ${frames.length} camera frames)`);
