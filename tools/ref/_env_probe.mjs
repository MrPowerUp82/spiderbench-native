// Record the original GPU work for noise, atmosphere, environment cube and PMREM.
// The fake GL captures GLSL only; the native executable executes and validates it.
import { BAKE, SHADERS } from './paths.mjs';
import { SRC_ROOT } from './stubs.mjs';
import { createMockGL } from './mockgl.mjs';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import zlib from 'node:zlib';
import crypto from 'node:crypto';
const imp = p => import(pathToFileURL(path.join(SRC_ROOT,p)).href);
const THREE = await imp('node_modules/three/build/three.module.js');
const { createLighting } = await imp('src/render/lighting.js');
const { createPipeline } = await imp('src/render/pipeline.js');
const dir = path.join(BAKE, 'environment');
fs.mkdirSync(dir,{recursive:true});
const canvas = {width:1600,height:900,style:{},addEventListener(){},removeEventListener(){},getContext(){}};
const gl = createMockGL(canvas);
const renderer = new THREE.WebGLRenderer({canvas,context:gl,antialias:false,reversedDepthBuffer:true});
const blobs=[], resources=[], geometries=[], jobs=[], entries=[], usages=[];
const blobIds=new WeakMap(), textureIds=new WeakMap(), geometryIds=new WeakMap();
const chunks=[Buffer.from('SBGEO01\0')]; let offset=8;
function blob(array) {
  if(blobIds.has(array)) return blobIds.get(array);
  const raw=Buffer.from(array.buffer,array.byteOffset,array.byteLength), packed=zlib.deflateSync(raw);
  const header=Buffer.alloc(8); header.writeUInt32LE(raw.length,0);header.writeUInt32LE(packed.length,4);
  const id=blobs.length;
  blobs.push({offset,bytes:raw.length,compressedBytes:packed.length,arrayType:array.constructor.name,elements:array.length,sha256:crypto.createHash('sha256').update(raw).digest('hex')});
  chunks.push(header,packed);offset+=8+packed.length;blobIds.set(array,id);return id;
}
function resource(texture, target=null) {
  if(textureIds.has(texture)) return textureIds.get(texture);
  const id=resources.length, im=Array.isArray(texture.image)?texture.image[0]:texture.image;
  const r={id,name:texture.name,width:target?.width??im?.width,height:target?.height??im?.height,depth:target?.depth??im?.depth??1,
    target:texture.isCubeTexture?'cube':texture.isData3DTexture?'3d':'2d',type:texture.type,format:texture.format,
    wrapS:texture.wrapS,wrapT:texture.wrapT,wrapR:texture.wrapR,minFilter:texture.minFilter,magFilter:texture.magFilter,
    generateMipmaps:texture.generateMipmaps,blob:im?.data?blob(im.data):null};
  if(!r.width||!r.height) throw new Error('Texture without dimensions');
  resources.push(r);textureIds.set(texture,id);return id;
}
function geometry(g) {
  if(geometryIds.has(g)) return geometryIds.get(g);
  const id=geometries.length, attributes={};
  for(const [name,a] of Object.entries(g.attributes)) {
    if(a.isInterleavedBufferAttribute) throw new Error('Unexpected interleaved environment geometry');
    attributes[name]={blob:blob(a.array),itemSize:a.itemSize,normalized:a.normalized};
  }
  geometries.push({attributes,index:g.index?{blob:blob(g.index.array),count:g.index.count}:null,
    count:g.index?.count??g.attributes.position.count,drawRange:[g.drawRange.start,Number.isFinite(g.drawRange.count)?g.drawRange.count:null]});
  geometryIds.set(g,id);return id;
}
function value(v) {
  if(v==null||v.isTexture) return undefined;
  if(typeof v==='number'||typeof v==='boolean') return v;
  if(v.toArray) return v.toArray();
  if(Array.isArray(v)||ArrayBuffer.isView(v)) return Array.from(v,value);
  return undefined;
}
function destination() {
  const rt=renderer.getRenderTarget();
  if(!rt) return null;
  return {texture:resource(rt.texture,rt),face:renderer.getActiveCubeFace(),viewport:renderer.getCurrentViewport(new THREE.Vector4()).toArray(),
    scissor:rt.scissor.toArray(),scissorTest:rt.scissorTest};
}
let stage='init';
const originalClear=renderer.clear.bind(renderer);
renderer.clear=(color=true,depth=true,stencil=true)=>{
  const target=destination();
  if(target&&color) jobs.push({op:'clear',stage,target,color:[...renderer.getClearColor(new THREE.Color()).toArray(),renderer.getClearAlpha()]});
  return originalClear(color,depth,stencil);
};
const originalDraw=renderer.renderBufferDirect.bind(renderer);
renderer.renderBufferDirect=(camera,scene,g,material,object,group)=>{
  originalDraw(camera,scene,g,material,object,group);
  const props=renderer.properties.get(material), program=props.currentProgram?.program;
  const captured=gl._captured.find(c=>c.prog===program);
  if(!captured) throw new Error('Environment program was not captured');
  const id=String(gl._captured.indexOf(captured)).padStart(4,'0');
  if(!entries.some(e=>e.id===id)) {
    entries.push({id,material:material.name});
    fs.writeFileSync(path.join(dir,id+'.vert.glsl'),captured.vs);fs.writeFileSync(path.join(dir,id+'.frag.glsl'),captured.fs);
  }
  const uniforms={},textures={};
  for(const [name,u] of Object.entries(material.uniforms)) {
    if(u.value?.isTexture) textures[name]=resource(u.value);
    else { const v=value(u.value); if(v!==undefined) uniforms[name]=v; }
  }
  uniforms.projectionMatrix=camera.projectionMatrix.toArray(); uniforms.viewMatrix=camera.matrixWorldInverse.toArray();
  uniforms.modelMatrix=object.matrixWorld.toArray(); uniforms.modelViewMatrix=object.modelViewMatrix.toArray();
  uniforms.normalMatrix=object.normalMatrix.toArray(); uniforms.cameraPosition=new THREE.Vector3().setFromMatrixPosition(camera.matrixWorld).toArray();
  jobs.push({op:'draw',stage,program:id,geometry:geometry(g),name:material.name,target:destination(),uniforms,textures,
    range:group?[group.start,group.count]:null,side:material.side});
};
const scene=new THREE.Scene(), camera=new THREE.PerspectiveCamera(55,16/9,0.1,20000);
camera._reversedDepth=true;camera.updateProjectionMatrix();
camera.position.set(250,32,175);camera.lookAt(250,8,-250);camera.updateMatrixWorld();
const lighting=createLighting({renderer,scene});
// The city's world.update disables the procedural skyline before the environment
// is baked. Keep its real geometric horizon and use the same setting here.
lighting.sky.params.skylineVisible=0;lighting.refresh();lighting.update(camera);
const environment=resource(scene.environment);
const depth=new THREE.DataTexture(new Float32Array([0]),1,1,THREE.RedFormat,THREE.FloatType);depth.needsUpdate=true;
const skyTarget=new THREE.WebGLRenderTarget(800,450,{type:THREE.HalfFloatType,depthBuffer:false});
stage='sky';
lighting.sky.renderSkyPass(skyTarget,camera,depth,1600,900,0,true);
const skyJob=jobs.find(j=>j.op==='draw'&&j.stage==='sky');
if(!skyJob||!jobs.some(j=>j.name==='cloudNoise')||!jobs.some(j=>j.name==='envSky')) throw new Error('Incomplete original environment graph');
// Capture the core post chain with the original quality's bloom and grading.
// Effects that need additional runtime state are brought over separately.
stage='post';
renderer.setSize(1600,900,false);
const postLighting=lighting;
const pipeline=createPipeline({renderer,scene,camera,lighting:postLighting});
pipeline.render(1/60);
const postNames=new Set(['composite','taa','bloomDown','bloomUp','autoExposure','final']);
const postJobs=jobs.filter(j=>j.op==='draw'&&j.stage==='post'&&postNames.has(j.name));
const composite=postJobs.find(j=>j.name==='composite'), final=postJobs.find(j=>j.name==='final');
if(!composite||!final) throw new Error('Incomplete post chain');
const postInputs={color:composite.textures.uColor,depth:composite.textures.uDepth,sky:composite.textures.uSky};
fs.writeFileSync(path.join(dir,'geometry.sbgeo'),Buffer.concat(chunks));
fs.writeFileSync(path.join(dir,'geometry.json'),JSON.stringify({format:'SBGEO01',version:1,archive:'geometry.sbgeo',blobs,meshes:[],materials:[]}));
const geometryDigest=crypto.createHash('sha256').update(Buffer.concat(chunks)).digest('hex');
fs.writeFileSync(path.join(dir,'manifest.json'),JSON.stringify({format:'SBENV1',version:1,source:SRC_ROOT,threeRevision:THREE.REVISION,
  quality:lighting.quality.name,timeOfDay:lighting.tod.name,environment,skyJob,resources,geometries,jobs:jobs.filter(j=>j.stage==='init'),
  postJobs,postInputs,postSize:[1600,900],postGrade:pipeline.grade,
  geometryDigest,skyParams:lighting.sky.params,fog:{...lighting.fog,tint:lighting.fog.tint.toArray()},exposure:lighting.tod.exposure,entries,usages},null,2));
console.log(`captured ${entries.length} programs, ${jobs.filter(j=>j.stage==='init').length} GPU operations, ${resources.length} textures, ${geometries.length} geometries`);
console.log(`PMREM resource ${environment}: ${resources[environment].width} x ${resources[environment].height}; sky program ${skyJob.program}`);
const post=jobs.filter(j=>j.stage==='post');
for (const j of post) console.log('JOB', j.op, j.name, j.program, 'target', j.target?.texture, j.target && resources[j.target.texture] ? `${resources[j.target.texture].width}x${resources[j.target.texture].height}` : '', 'tex', JSON.stringify(j.textures||{}));
for (const r of resources) if (r.format===1026 || /depth/i.test(r.name||'')) console.log('DEPTHRES', r.id, r.name, r.width, r.height, r.format, r.type);
