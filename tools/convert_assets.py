#!/usr/bin/env python3
"""Asset conversion for the native build (spiderbench -> spiderbench_native).

The browser build ships WebP / PNG / JPG textures, GLB models with embedded WebP images, TTF fonts and OGG audio.
The native engine only links SDL2 + zlib, so this step bakes everything into formats it can read directly:

  *.tex   "SBTX" | u32 width | u32 height | u32 zlib size | zlib(RGBA8, rows top -> bottom)
  *.glb   copied as-is (the engine ignores the image buffer views); embedded images -> <name>_img<N>.tex
  font_*.tex + font_*.json   glyph atlas (white RGBA, alpha = coverage) + per-glyph metrics
  *.wav   16-bit PCM (SDL_LoadWAV)

Usage:  python tools/convert_assets.py [--src ../spiderbench] [--out assets]
Requires Pillow (with WebP) and ffmpeg on PATH for audio.
"""
import argparse, io, json, os, shutil, struct, subprocess, sys, zlib
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def write_tex(img, path, max_size=2048):
    img = img.convert('RGBA')
    if max(img.size) > max_size:
        k = max_size / max(img.size)
        img = img.resize((max(1, round(img.width * k)), max(1, round(img.height * k))), Image.LANCZOS)
    raw = img.tobytes()
    z = zlib.compress(raw, 6)
    with open(path, 'wb') as f:
        f.write(b'SBTX' + struct.pack('<III', img.width, img.height, len(z)) + z)
    return img.size


def convert_glb(src, dst_dir, name):
    data = open(src, 'rb').read()
    magic, ver, total = struct.unpack('<III', data[:12])
    assert magic == 0x46546C67, 'not a GLB: ' + src
    jlen, jtype = struct.unpack('<II', data[12:20])
    j = json.loads(data[20:20 + jlen])
    off = 20 + jlen
    blen, btype = struct.unpack('<II', data[off:off + 8])
    bin_ = data[off + 8:off + 8 + blen]
    shutil.copyfile(src, os.path.join(dst_dir, name + '.glb'))
    out = []
    for i, im in enumerate(j.get('images', [])):
        bv = j['bufferViews'][im['bufferView']]
        blob = bin_[bv.get('byteOffset', 0):bv.get('byteOffset', 0) + bv['byteLength']]
        size = write_tex(Image.open(io.BytesIO(blob)), os.path.join(dst_dir, f'{name}_img{i}.tex'))
        out.append((im.get('name'), size))
    print(f'  {name}.glb: {len(j.get("meshes", []))} meshes, {len(j.get("animations", []))} clips, images {out}')


def font_atlas(ttf, px, dst_dir, name):
    font = ImageFont.truetype(ttf, px)
    chars = [chr(c) for c in range(32, 127)] + list('ÁÂÃÀÇÉÊÍÓÔÕÚáâãàçéêíóôõú·°')
    pad, W = 2, 1024
    asc, desc = font.getmetrics()
    lh = asc + desc
    x = y = 0
    glyphs = {}
    boxes = []
    for ch in chars:
        l, t, r, b = font.getbbox(ch)
        w, h = max(1, r - l), max(1, b - t)
        if x + w + pad > W: x = 0; y += lh + pad
        boxes.append((ch, x, y, l, t, w, h))
        x += w + pad
    H = 1
    while H < y + lh + pad: H *= 2
    img = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(img)
    for ch, gx, gy, l, t, w, h in boxes:
        d.text((gx - l, gy - t), ch, font=font, fill=255)
        glyphs[str(ord(ch))] = {'x': gx, 'y': gy, 'w': w, 'h': h, 'ox': l, 'oy': t, 'adv': font.getlength(ch)}
    rgba = Image.merge('RGBA', (Image.new('L', img.size, 255),) * 3 + (img,))
    write_tex(rgba, os.path.join(dst_dir, name + '.tex'), max_size=4096)
    json.dump({'size': px, 'ascent': asc, 'descent': desc, 'lineHeight': lh, 'atlasW': W, 'atlasH': H, 'glyphs': glyphs},
              open(os.path.join(dst_dir, name + '.json'), 'w'))
    print(f'  font {name}: {len(glyphs)} glyphs, atlas {W}x{H}')


def ogg_to_wav(src, dst, rate=44100, channels=None):
    cmd = ['ffmpeg', '-y', '-loglevel', 'error', '-i', src, '-ar', str(rate), '-acodec', 'pcm_s16le']
    if channels: cmd += ['-ac', str(channels)]
    subprocess.run(cmd + [dst], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', default=os.path.join(os.path.dirname(ROOT), 'spiderbench'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'assets'))
    ap.add_argument('--no-audio', action='store_true')
    a = ap.parse_args()
    pub = os.path.join(a.src, 'public', 'assets')
    if not os.path.isdir(pub): sys.exit('source assets not found: ' + pub)
    os.makedirs(a.out, exist_ok=True)

    print('[models]')
    convert_glb(os.path.join(pub, 'spiderman.glb'), a.out, 'spiderman')

    print('[textures]')
    city = os.path.join(pub, 'city', 'tex')
    for f in ['asphalt_col.png', 'asphalt_nrm.png', 'sidewalk_col.png', 'sidewalk_nrm.png', 'roof_col.png', 'grass_col.png',
              'water_nrm.png', 'noise.png', 'bark_col.webp', 'leaves.png']:
        p = os.path.join(city, f)
        if not os.path.exists(p): print('  (missing)', f); continue
        size = write_tex(Image.open(p), os.path.join(a.out, os.path.splitext(f)[0] + '.tex'), max_size=1024)
        print(f'  {f} -> {size}')

    print('[fonts]')
    fonts = os.path.join(pub, 'ui', 'fonts')
    font_atlas(os.path.join(fonts, 'spiderbench-condensed-800.ttf'), 56, a.out, 'font_title')
    font_atlas(os.path.join(fonts, 'manrope-variable.ttf'), 28, a.out, 'font_ui')

    if not a.no_audio:
        print('[audio]')
        au = os.path.join(pub, 'audio')
        shutil.copyfile(os.path.join(au, 'manifest.json'), os.path.join(a.out, 'audio_manifest.json'))
        for f, ch in [('sfx_trav.ogg', 2), ('sfx_world.ogg', 1), ('music_day.ogg', 2)]:
            ogg_to_wav(os.path.join(au, f), os.path.join(a.out, os.path.splitext(f)[0] + '.wav'), channels=ch)
            print('  ' + f)
    print('done ->', a.out)


if __name__ == '__main__':
    main()
