// Headless browser stubs so the original city generator (src/world/city.js) runs under Node.
// Real 2D canvases preserve the procedural and image-derived texture pixels.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createCanvas, Image as CanvasImage } from '@napi-rs/canvas';
import { resolveObjectURL } from 'node:buffer';

const siblingRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const candidates = [process.env.SB_JS_ROOT, path.join(siblingRoot, 'spiderbench-remake'), path.join(siblingRoot, 'spiderbench')].filter(Boolean);
export const SRC_ROOT = candidates.find(p => fs.existsSync(path.join(p, 'src/world/city.js')) && fs.existsSync(path.join(p, 'node_modules/three/package.json')));
if (!SRC_ROOT) throw new Error(`Original JS with installed dependencies not found. Set SB_JS_ROOT or run npm ci in ${candidates[1]}.`);
const PUB = path.join(SRC_ROOT, 'public');

function canvas(w = 300, h = 150) {
  const c = createCanvas(w, h);
  c.style = {};
  c.addEventListener = () => {};
  c.removeEventListener = () => {};
  return c;
}

class ImageElement extends CanvasImage {
  constructor() { super(); this._listeners = {}; this.style = {}; }
  // @napi-rs/canvas decodes asynchronously after `src` is assigned: 'load' fires only once the pixels exist
  // (reading them earlier through drawImage gives an empty image while width / height are already known)
  _loaded(bytes) {
    super.src = bytes;
    CanvasImage.prototype.decode.call(this).then(() => {
      this.onload?.({ target: this }); this._listeners.load?.forEach(f => f.call(this, { target: this }));
    }, error => this._failed(error));
  }
  _failed(error) { this.onerror?.(error); this._listeners.error?.forEach(f => f.call(this, error)); }
  set src(value) {
    this._src = value;
    if (String(value).startsWith('blob:')) { // GLTFLoader: images embedded in a GLB bufferView (URL.createObjectURL)
      const blob = resolveObjectURL(String(value));
      if (!blob) { setTimeout(() => this._failed(new Error(`unknown object URL ${value}`)), 0); return; }
      blob.arrayBuffer().then(bytes => this._loaded(Buffer.from(bytes)), error => this._failed(error));
      return;
    }
    try {
      const pathname = String(value).startsWith('http') ? new URL(value).pathname : String(value);
      this._loaded(String(value).startsWith('data:') ? value : fs.readFileSync(path.join(PUB, pathname.replace(/^\/+/, ''))));
    } catch (error) {
      setTimeout(() => this._failed(error), 0);
    }
  }
  get src() { return this._src; }
  get naturalWidth() { return this.width; }
  get naturalHeight() { return this.height; }
  get complete() { return this.width > 0 && this.height > 0; }
  decode() { return CanvasImage.prototype.decode.call(this); }
  addEventListener(type, fn) { (this._listeners[type] ||= []).push(fn); }
  removeEventListener(type, fn) { this._listeners[type] = (this._listeners[type] || []).filter(f => f !== fn); }
}

globalThis.window = globalThis;
globalThis.self = globalThis;
// SB_QUALITY=low|med|high selects the remake's quality preset (render/quality.js reads ?q=)
globalThis.location = { search: process.env.SB_QUALITY ? '?q=' + process.env.SB_QUALITY : '', href: 'http://localhost/' };
globalThis.document = { createElement: (t) => (t === 'canvas' ? canvas() : { style: {}, appendChild() {}, addEventListener() {} }), body: { appendChild() {} }, addEventListener() {} };
globalThis.OffscreenCanvas = class { constructor(w, h) { return canvas(w, h); } };
globalThis.addEventListener = () => {};
globalThis.requestAnimationFrame = () => 0;
globalThis.Image = ImageElement;
// <img> for three's ImageLoader (createElementNS + addEventListener('load'))
globalThis.document.createElementNS = (ns, t) => (t === 'img' ? new ImageElement() : t === 'canvas' ? canvas() : { style: {}, addEventListener() {} });
globalThis.Request = class { constructor(url, o) { this.url = url; Object.assign(this, o); } };
globalThis.Headers = class { constructor() {} };
globalThis.fetch = async (url) => {
  url = url && url.url ? url.url : url;
  const f = path.join(PUB, String(url).replace(/^\//, '').replace(/\?.*$/, ''));
  const buf = fs.readFileSync(f);
  return { ok: true, status: 200, headers: { get: () => null }, blob: async () => ({}), json: async () => JSON.parse(buf.toString('utf8')), arrayBuffer: async () => buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength), text: async () => buf.toString('utf8') };
};
globalThis.__boot = { stage: async () => {}, sub() {}, done() {} };

// minimal WebGLRenderer stand-in (the city only asks for capabilities / compiles nothing at build time)
const rendererBase = {
  capabilities: { getMaxAnisotropy: () => 16, isWebGL2: true, maxTextures: 16, reversedDepthBuffer: true },
  getPixelRatio: () => 1, getSize: (v) => v.set(1600, 900), getContext: () => ({}), initTexture() {}, compile() {}, compileAsync: async () => {},
  setRenderTarget() {}, render() {}, getRenderTarget: () => null, outputColorSpace: 'srgb', shadowMap: {},
  domElement: canvas(),
};

const noop = () => undefined;
export const renderer = new Proxy(rendererBase, { get(t, k) { if (k in t) return t[k]; if (k === 'getDrawingBufferSize') return (v) => v.set(1600, 900); return noop; } });
