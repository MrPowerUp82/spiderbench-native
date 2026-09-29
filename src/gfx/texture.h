#pragma once
#include "gfx/gl.h"
#include <string>
#include <vector>
#include <cstdint>

struct Image {
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;
};

// .tex = "SBTX" | u32 w | u32 h | u32 zsize | zlib(RGBA8) (tools/convert_assets.py)
bool loadTexFile(const std::string& path, Image& out);
// uploads with mipmaps + anisotropy; srgb = colour data (GL_SRGB8_ALPHA8)
GLuint uploadTexture(const Image& img, bool srgb, bool repeat = true, bool mips = true);
GLuint loadTexture(const std::string& path, bool srgb, bool repeat = true);
GLuint solidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

std::string assetPath(const std::string& name);
