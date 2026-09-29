#include "ui/hud.h"
#include "gfx/shaders.h"
#include "gfx/texture.h"
#include <json/json.h>
#include <fstream>
#include <cmath>

// UTF-8 -> code points (the atlases include the Portuguese accented letters)
static std::vector<int> decode(const std::string& s) {
  std::vector<int> out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = s[i];
    if (c < 0x80) { out.push_back(c); i++; }
    else if ((c >> 5) == 6 && i + 1 < s.size()) { out.push_back(((c & 31) << 6) | (s[i + 1] & 63)); i += 2; }
    else if ((c >> 4) == 14 && i + 2 < s.size()) { out.push_back(((c & 15) << 12) | ((s[i + 1] & 63) << 6) | (s[i + 2] & 63)); i += 3; }
    else i++;
  }
  return out;
}

bool Font::load(const std::string& jsonPath, const std::string& texPath) {
  std::ifstream f(jsonPath); if (!f) return false;
  Json::Value j; Json::CharReaderBuilder b; std::string err;
  if (!Json::parseFromStream(b, f, &j, &err)) return false;
  size = j["size"].asFloat(); ascent = j["ascent"].asFloat(); lineH = j["lineHeight"].asFloat();
  atlasW = j["atlasW"].asFloat(); atlasH = j["atlasH"].asFloat();
  for (const auto& k : j["glyphs"].getMemberNames()) {
    const auto& g = j["glyphs"][k];
    glyphs[std::stoi(k)] = {g["x"].asFloat(), g["y"].asFloat(), g["w"].asFloat(), g["h"].asFloat(), g["ox"].asFloat(), g["oy"].asFloat(), g["adv"].asFloat()};
  }
  Image img; if (!loadTexFile(texPath, img)) return false;
  tex = uploadTexture(img, false, false, true);
  return true;
}

bool Hud::init() {
  if (!sh_.build("ui", glsl::UI_VS, glsl::UI_FS)) return false;
  fonts_[0].load(assetPath("font_ui.json"), assetPath("font_ui.tex"));
  fonts_[1].load(assetPath("font_title.json"), assetPath("font_title.tex"));
  glGenVertexArrays(1, &vao_); glGenBuffers(1, &vbo_);
  glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 32, nullptr);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, (void*)8);
  glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 32, (void*)16);
  glBindVertexArray(0);
  return true;
}

void Hud::begin(int w, int h) { w_ = w; h_ = h; batches_.clear(); }

std::vector<float>& Hud::batch(GLuint tex) {
  if (batches_.empty() || batches_.back().tex != tex) batches_.push_back({tex, {}});
  return batches_.back().v;
}

void Hud::quad(std::vector<float>& v, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Rgba c) {
  float q[6][4] = {{x0, y0, u0, v0}, {x1, y0, u1, v0}, {x1, y1, u1, v1}, {x0, y0, u0, v0}, {x1, y1, u1, v1}, {x0, y1, u0, v1}};
  for (auto& p : q) v.insert(v.end(), {p[0], p[1], p[2], p[3], c.r, c.g, c.b, c.a});
}

float Hud::measure(const std::string& s, float px, int fontId) {
  const Font& F = fonts_[fontId]; float k = px / std::max(F.size, 1.f), w = 0;
  for (int cp : decode(s)) { auto it = F.glyphs.find(cp); w += (it != F.glyphs.end() ? it->second.adv : F.size * 0.3f) * k; }
  return w;
}

float Hud::text(float x, float y, const std::string& s, float px, Rgba c, int fontId, int align) {
  const Font& F = fonts_[fontId]; if (!F.tex) return 0;
  float k = px / F.size;
  if (align) x -= measure(s, px, fontId) * (align == 1 ? 0.5f : 1.f);
  auto& v = batch(F.tex);
  float pen = x;
  for (int cp : decode(s)) {
    auto it = F.glyphs.find(cp);
    if (it == F.glyphs.end()) { pen += F.size * 0.3f * k; continue; }
    const auto& g = it->second;
    if (cp != 32) {
      float x0 = pen + g.ox * k, y0 = y + g.oy * k;
      quad(v, x0, y0, x0 + g.w * k, y0 + g.h * k, g.x / F.atlasW, g.y / F.atlasH, (g.x + g.w) / F.atlasW, (g.y + g.h) / F.atlasH, c);
    }
    pen += g.adv * k;
  }
  return pen - x;
}

void Hud::rect(float x, float y, float w, float h, Rgba c) { quad(batch(0), x, y, x + w, y + h, -1, 0, -1, 0, c); }

void Hud::ring(float cx, float cy, float r, float thick, Rgba c, int seg, float a0, float a1) {
  auto& v = batch(0);
  for (int i = 0; i < seg; i++) {
    float t0 = a0 + (a1 - a0) * i / seg, t1 = a0 + (a1 - a0) * (i + 1) / seg;
    float ri = r - thick / 2, ro = r + thick / 2;
    float p[4][2] = {{cx + std::cos(t0) * ri, cy + std::sin(t0) * ri}, {cx + std::cos(t0) * ro, cy + std::sin(t0) * ro},
                     {cx + std::cos(t1) * ro, cy + std::sin(t1) * ro}, {cx + std::cos(t1) * ri, cy + std::sin(t1) * ri}};
    int ord[6] = {0, 1, 2, 0, 2, 3};
    for (int k : ord) v.insert(v.end(), {p[k][0], p[k][1], -1, 0, c.r, c.g, c.b, c.a});
  }
}

void Hud::end() {
  glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  sh_.use(); sh_.set("uScreen", Vec2{(float)w_, (float)h_}); sh_.set("tFont", 0);
  glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glActiveTexture(GL_TEXTURE0);
  for (auto& b : batches_) {
    if (b.v.empty()) continue;
    glBindTexture(GL_TEXTURE_2D, b.tex ? b.tex : fonts_[0].tex);
    glBufferData(GL_ARRAY_BUFFER, b.v.size() * 4, b.v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(b.v.size() / 8));
  }
  glDisable(GL_BLEND);
}
