// Reference screenshot of the web remake (running on the local vite dev server) for side-by-side comparison with
// the native renderer at the same camera.
//   node tools/ref/web_shot.mjs --cam 250,32,175,250,8,-250 [--shot street] [--q med] [--tod day] [--size 1280x720]
//        [--url http://127.0.0.1:5173/] [--out build/compare/web.png]
// Uses the installed Chrome/Edge (puppeteer-core) with the GPU enabled; waits for shots.js to report __shotReady.
import puppeteer from 'puppeteer-core';
import fs from 'node:fs';
import path from 'node:path';

const args = Object.fromEntries(process.argv.slice(2).reduce((a, v, i, all) => (v.startsWith('--') ? [...a, [v.slice(2), all[i + 1]]] : a), []));
const [width, height] = (args.size || '1280x720').split('x').map(Number);
const base = args.url || 'http://127.0.0.1:5173/';
const query = new URLSearchParams({ shot: args.shot || 'street', q: args.q || 'med', tod: args.tod || 'day' });
if (args.cam) query.set('cam', args.cam);
const url = `${base}?${query.toString().replace(/%2C/g, ',')}`;
const out = path.resolve(args.out || 'build/compare/web.png');
fs.mkdirSync(path.dirname(out), { recursive: true });

const executablePath = [process.env.SB_CHROME, 'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(p => p && fs.existsSync(p));
const browser = await puppeteer.launch({ executablePath, headless: 'new', protocolTimeout: 600000,
  args: ['--use-angle=d3d11', '--enable-gpu', '--ignore-gpu-blocklist', '--enable-unsafe-swiftshader=false',
    `--window-size=${width},${height}`, '--disable-background-timer-throttling', '--disable-renderer-backgrounding'] });
try {
  const page = await browser.newPage();
  await page.setViewport({ width, height, deviceScaleFactor: 1 });
  page.on('console', m => { if (m.type() === 'error') console.error('[web]', m.text().slice(0, 300)); });
  page.on('pageerror', e => console.error('[web] page error', String(e).slice(0, 300)));
  const t0 = Date.now();
  await page.goto(url, { waitUntil: 'load', timeout: 120000 });
  await page.waitForFunction('window.__shotReady === true', { timeout: 600000, polling: 1000 });
  const info = await page.evaluate(() => {
    const gl = document.createElement('canvas').getContext('webgl2');
    const ext = gl && gl.getExtension('WEBGL_debug_renderer_info');
    const c = window.__ctx?.camera;
    const camera = c && { fov: c.fov, near: c.near, far: c.far, aspect: c.aspect, position: c.position.toArray(), quaternion: c.quaternion.toArray() };
    return { shot: window.__shotInfo, gpu: ext ? gl.getParameter(ext.UNMASKED_RENDERER_WEBGL) : null, camera };
  });
  await page.screenshot({ path: out });
  const result = { url, out, seconds: Math.round((Date.now() - t0) / 1000), ...info };
  fs.writeFileSync(out.replace(/\.png$/, '.json'), JSON.stringify(result, null, 2));
  console.log(JSON.stringify(result));
} finally {
  await browser.close();
}
