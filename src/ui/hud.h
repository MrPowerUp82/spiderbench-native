// 2D overlay: bitmap-font text (atlases baked from the game's TTFs by tools/convert_assets.py), shapes, zip reticle.
#pragma once
#include "gfx/shader.h"
#include <string>
#include <unordered_map>
#include <vector>

struct Font {
  struct Glyph { float x, y, w, h, ox, oy, adv; };
  std::unordered_map<int, Glyph> glyphs;
  float size = 0, ascent = 0, lineH = 0, atlasW = 1, atlasH = 1;
  GLuint tex = 0;
  bool load(const std::string& jsonPath, const std::string& texPath);
};

struct Rgba { float r, g, b, a; };

class Hud {
 public:
  bool init();
  void begin(int w, int h);
  // fontId 0 = UI (Manrope), 1 = title (condensed); scale relative to the atlas size; align 0 left, 1 centre, 2 right
  float text(float x, float y, const std::string& s, float px, Rgba c, int fontId = 0, int align = 0);
  float measure(const std::string& s, float px, int fontId = 0);
  void rect(float x, float y, float w, float h, Rgba c);
  void ring(float cx, float cy, float r, float thick, Rgba c, int seg = 32, float a0 = 0, float a1 = 6.2831853f);
  void end();
 private:
  Shader sh_;
  Font fonts_[2];
  GLuint vao_ = 0, vbo_ = 0;
  int w_ = 0, h_ = 0;
  struct Batch { GLuint tex; std::vector<float> v; };
  std::vector<Batch> batches_;
  std::vector<float>& batch(GLuint tex);
  void quad(std::vector<float>& v, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Rgba c);
};
