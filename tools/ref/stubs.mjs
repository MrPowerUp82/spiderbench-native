// Headless browser stubs so the original city generator (src/world/city.js) runs under Node.
// Images report their real pixel size (read from the file header where possible) and carry no pixels; canvases accept
// every 2D call and read back blank. Enough for geometry, collision and zip-point generation.
import fs from 'node:fs';
import path from 'node:path';

export const SRC_ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')), '../../../spiderbench');
const PUB = path.join(SRC_ROOT, 'public');

function imageSize(file) {
  try {
    const b = fs.readFileSync(file);
    if (b[0] === 0x89 && b[1] === 0x50) return [b.readUInt32BE(16), b.readUInt32BE(20)]; // PNG
    if (b.toString('ascii', 0, 4) === 'RIFF' && b.toString('ascii', 8, 12) === 'WEBP') {
      const t = b.toString('ascii', 12, 16);
      if (t === 'VP8X') return [1 + b.readUIntLE(24, 3), 1 + b.readUIntLE(27, 3)];
      if (t === 'VP8L') { const v = b.readUInt32LE(21); return [1 + (v & 0x3fff), 1 + ((v >> 14) & 0x3fff)]; }
      if (t === 'VP8 ') return [b.readUInt16LE(26) & 0x3fff, b.readUInt16LE(28) & 0x3fff];
    }
    if (b[0] === 0xff && b[1] === 0xd8) { // JPEG: scan for SOFn
      let i = 2;
      while (i < b.length) { const m = b[i + 1], L = b.readUInt16BE(i + 2); if (m >= 0xc0 && m <= 0xc3) return [b.readUInt16BE(i + 7), b.readUInt16BE(i + 5)]; i += 2 + L; }
    }
  } catch (e) { /* missing */ }
  return [64, 64];
}

class Ctx2D {
  constructor(cv) { this.canvas = cv; this.fillStyle = '#000'; this.strokeStyle = '#000'; this.font = '10px sans'; this.globalAlpha = 1; this.lineWidth = 1; this.textAlign = 'left'; this.textBaseline = 'alphabetic'; this.globalCompositeOperation = 'source-over'; this.filter = 'none'; this.imageSmoothingEnabled = true; }
  getImageData(x, y, w, h) { return { data: new Uint8ClampedArray(w * h * 4), width: w, height: h }; }
  createImageData(w, h) { return { data: new Uint8ClampedArray(w * h * 4), width: w, height: h }; }
  measureText(t) { return { width: String(t).length * 6, actualBoundingBoxAscent: 8, actualBoundingBoxDescent: 2 }; }
  createLinearGradient() { return { addColorStop() {} }; }
  createRadialGradient() { return { addColorStop() {} }; }
  createPattern() { return {}; }
}
for (const m of ['putImageData', 'drawImage', 'clearRect', 'fillRect', 'strokeRect', 'fillText', 'strokeText', 'beginPath', 'closePath', 'moveTo', 'lineTo', 'arc', 'arcTo', 'rect', 'ellipse',
  'quadraticCurveTo', 'bezierCurveTo', 'fill', 'stroke', 'save', 'restore', 'translate', 'rotate', 'scale', 'setTransform', 'resetTransform', 'transform', 'clip', 'setLineDash', 'roundRect'])
  Ctx2D.prototype[m] = function () {};

class Canvas { constructor() { this.width = 300; this.height = 150; this.style = {}; } getContext() { return (this._c ||= new Ctx2D(this)); } toDataURL() { return ''; } addEventListener() {} }

globalThis.window = globalThis;
globalThis.location = { search: '', href: 'http://localhost/' };
globalThis.document = { createElement: (t) => (t === 'canvas' ? new Canvas() : { style: {}, appendChild() {}, addEventListener() {} }), body: { appendChild() {} }, addEventListener() {} };
globalThis.OffscreenCanvas = class extends Canvas { constructor(w, h) { super(); this.width = w; this.height = h; } };
globalThis.addEventListener = () => {};
globalThis.requestAnimationFrame = () => 0;
globalThis.Image = class {
  constructor() { this.width = 0; this.height = 0; this.onload = null; this.onerror = null; }
  set src(v) { this._src = v; const f = path.join(PUB, v.replace(/^\//, '')); [this.width, this.height] = imageSize(f); this.naturalWidth = this.width; this.naturalHeight = this.height; this.complete = true; setTimeout(() => this.onload && this.onload(), 0); }
  get src() { return this._src; }
  decode() { return Promise.resolve(); }
};
// <img> for three's ImageLoader (createElementNS + addEventListener('load'))
class ImgEl extends globalThis.Image {
  constructor() { super(); this._l = {}; this.style = {}; }
  addEventListener(t, f) { (this._l[t] ||= []).push(f); }
  removeEventListener(t, f) { this._l[t] = (this._l[t] || []).filter(g => g !== f); }
  set src(v) { super.src = v; setTimeout(() => (this._l.load || []).forEach(f => f.call(this, { target: this })), 0); }
  get src() { return super.src; }
}
globalThis.document.createElementNS = (ns, t) => (t === 'img' ? new ImgEl() : t === 'canvas' ? new Canvas() : { style: {}, addEventListener() {} });
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
  domElement: new Canvas(),
};

const noop = () => undefined;
export const renderer = new Proxy(rendererBase, { get(t, k) { if (k in t) return t[k]; if (k === 'getDrawingBufferSize') return (v) => v.set(1600, 900); return noop; } });
