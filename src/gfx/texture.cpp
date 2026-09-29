#include "gfx/texture.h"
#include <SDL.h>
#include <zlib.h>
#include <cstdio>
#include <cstring>
#include <fstream>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

std::string assetPath(const std::string& name) {
  static std::string base;
  if (base.empty()) {
    // next to the executable (packaged), else the source tree (dev builds)
    char* bp = SDL_GetBasePath();
    std::string exeDir = bp ? bp : "./";
    if (bp) SDL_free(bp);
    std::ifstream probe(exeDir + "assets/spiderman.glb", std::ios::binary);
    base = probe.good() ? exeDir + "assets/" : std::string(SB_SOURCE_ASSETS) + "/";
  }
  return base + name;
}

bool loadTexFile(const std::string& path, Image& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { std::fprintf(stderr, "[tex] cannot open %s\n", path.c_str()); return false; }
  char magic[4]; uint32_t hdr[3];
  f.read(magic, 4); f.read((char*)hdr, 12);
  if (std::memcmp(magic, "SBTX", 4) != 0) { std::fprintf(stderr, "[tex] bad magic %s\n", path.c_str()); return false; }
  std::vector<uint8_t> z(hdr[2]);
  f.read((char*)z.data(), z.size());
  out.w = (int)hdr[0]; out.h = (int)hdr[1];
  out.rgba.resize((size_t)out.w * out.h * 4);
  uLongf dl = (uLongf)out.rgba.size();
  if (uncompress(out.rgba.data(), &dl, z.data(), (uLong)z.size()) != Z_OK || dl != out.rgba.size()) {
    std::fprintf(stderr, "[tex] inflate failed %s\n", path.c_str()); return false;
  }
  return true;
}

GLuint uploadTexture(const Image& img, bool srgb, bool repeat, bool mips) {
  GLuint t; glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
  GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  if (mips) {
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.f);
  } else glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  return t;
}

GLuint loadTexture(const std::string& path, bool srgb, bool repeat) {
  Image img;
  if (!loadTexFile(path, img)) return solidTexture(255, 0, 255, 255);
  return uploadTexture(img, srgb, repeat);
}

GLuint solidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  Image img; img.w = img.h = 1; img.rgba = {r, g, b, a};
  return uploadTexture(img, false, true, false);
}
