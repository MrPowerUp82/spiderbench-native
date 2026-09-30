import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import zlib from 'node:zlib';
const dir=path.resolve('build/city-bake/environment');
const m=JSON.parse(fs.readFileSync(path.join(dir,'manifest.json')));
const g=JSON.parse(fs.readFileSync(path.join(dir,'geometry.json')));
const bytes=fs.readFileSync(path.join(dir,'geometry.sbgeo'));
if(m.format!=='SBENV1'||bytes.subarray(0,8).toString()!=='SBGEO01\0'||crypto.createHash('sha256').update(bytes).digest('hex')!==m.geometryDigest) throw new Error('Invalid environment archive');
for(const b of g.blobs) {
  const header=bytes.subarray(b.offset,b.offset+8);
  if(header.readUInt32LE()!==b.bytes||header.readUInt32LE(4)!==b.compressedBytes) throw new Error('Invalid environment buffer header');
  const raw=zlib.inflateSync(bytes.subarray(b.offset+8,b.offset+8+b.compressedBytes));
  if(raw.length!==b.bytes||crypto.createHash('sha256').update(raw).digest('hex')!==b.sha256) throw new Error('Environment buffer checksum mismatch');
}
const programIds=new Set(m.entries.map(e=>e.id));
for(const id of programIds) for(const stage of ['vert','frag']) if(!fs.readFileSync(path.join(dir,`${id}.${stage}.glsl`),'utf8').startsWith('#version 300 es')) throw new Error('Missing captured program');
const initialized=new Set(m.resources.filter(r=>r.blob!==null).map(r=>r.id)), noiseLayers=new Set(), cubeFaces=new Set();
let draws=0;
for(const j of [...m.jobs,m.skyJob]) {
  const r=m.resources[j.target.texture], v=j.target.viewport;
  if(!r||v[0]<0||v[1]<0||v[0]+v[2]>r.width||v[1]+v[3]>r.height) throw new Error('Viewport outside texture');
  if(j.op!=='draw') continue;
  if(!programIds.has(j.program)||!m.geometries[j.geometry]) throw new Error('Invalid environment draw');
  for(const input of Object.values(j.textures)) {
    if(input===j.target.texture||!initialized.has(input)) throw new Error('Read before production or texture feedback');
  }
  if(j.name==='cloudNoise') noiseLayers.add(j.target.face);
  if(j.name==='envSky') cubeFaces.add(j.target.face);
  initialized.add(r.id);draws++;
}
const output=m.resources[m.environment];
if(noiseLayers.size!==128||cubeFaces.size!==6||output.width!==768||output.height!==1024||!initialized.has(m.environment)) throw new Error('Incomplete environment graph');
for(const input of Object.values(m.postInputs)) initialized.add(input);
const down=m.postJobs.filter(j=>j.name==='bloomDown'), up=m.postJobs.filter(j=>j.name==='bloomUp');
if(down.length!==6||up.length!==5||m.postJobs.at(-1)?.name!=='final') throw new Error('Incomplete original post chain');
for(const j of m.postJobs) {
  if(!programIds.has(j.program)||!m.geometries[j.geometry]) throw new Error('Invalid post program/geometry');
  for(const [name,input] of Object.entries(j.textures)) {
    if((j.name==='composite'&&['uSSR','uShafts','uGI'].includes(name))||
       (j.name==='taa'&&((name==='uHistory'&&j.uniforms.uReset===1)||(name==='uMask'&&j.uniforms.uMaskOn===0)))||
       (j.name==='autoExposure'&&name==='uPrev'&&j.uniforms.uReset===1)||
       (j.name==='final'&&name==='uSunVis'&&j.uniforms.uFlare===0)) continue;
    if(input===j.target?.texture||!initialized.has(input)) throw new Error(`Invalid post dependency ${j.name}.${name}`);
  }
  if(j.target) initialized.add(j.target.texture);
}
console.log(`verified ${g.blobs.length} buffer checksums, ${programIds.size} programs, ${draws} draws, 128 noise layers, six cube faces and GGX PMREM dependencies`);
console.log(`verified ${m.postJobs.length} original post passes: atmosphere, TAA, six bloom levels, adaptation and grading`);
