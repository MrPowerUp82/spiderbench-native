// The player model drawn with its captured Three.js programs (capture_shaders.mjs --character): the GLB suit material
// with the suitfabric.js patch, skinning through three's boneTexture, the city's IBL / CSM lighting, and the player's
// shadows in cascades 0-2 plus the CSM_char cascade of render/csm.js.
//
// Skinning: three computes  modelMatrix * bindMatrixInverse * sum(w * boneWorld * boneInverse) * bindMatrix * p.
// Rig::skinMatrices already returns world-space  object * boneWorld * inverseBind, so the native draw uses
// modelMatrix = bindMatrix = bindMatrixInverse = identity with those matrices in the bone texture.
#include "gfx/baked_city.h"
#include "anim/rig.h"
#include <filesystem>
#include <fstream>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
bool readJsonFile(const std::filesystem::path& path, Json::Value& root) {
  std::ifstream file(path); Json::CharReaderBuilder builder; std::string error;
  return file && Json::parseFromStream(builder, file, &root, &error);
}
const Mat4 IDENTITY;
}  // namespace

bool BakedCity::openCharacter(const Rig& rig) {
  clearCharacter();
  namespace fs = std::filesystem;
  const fs::path bakeDir = fs::path(city_.directory) / "character", shaderDir = fs::path(shaderDir_) / "character";
  if (!char_.programs.open(shaderDir.string()) || !readJsonFile(bakeDir / "textures.json", char_.textures) ||
      char_.textures["format"].asString() != "SBTEX1") {
    std::fprintf(stderr, "[baked-char] no character capture in %s (run capture_shaders.mjs --character)\n", shaderDir.string().c_str());
    return false;
  }
  const Json::Value& manifest = char_.programs.manifest;
  if (manifest["source"] != city_.programs.manifest["source"] || manifest["quality"] != city_.programs.manifest["quality"] ||
      manifest["cascadeCount"] != city_.programs.manifest["cascadeCount"]) {
    std::fprintf(stderr, "[baked-char] character capture does not match the city bake (source / quality preset)\n");
    return false;
  }
  char_.directory = bakeDir.string();
  // usages by (material name, pass): the GLB material names identify the primitives (SpiderSuit, LensFrame, Lens)
  auto usageFor = [&](const std::string& material, const char* pass) {
    const Json::Value& usages = manifest["usages"];
    for (Json::ArrayIndex i = 0; i < usages.size(); i++)
      if (usages[i]["material"].asString() == material && usages[i]["pass"].asString() == pass) return int(i);
    return -1;
  };
  const GltfModel& m = rig.model;
  for (const auto& node : m.nodes) {
    if (node.mesh < 0) continue;
    for (const auto& prim : m.meshes[node.mesh].prims) {
      if (prim.material < 0 || prim.joints.empty()) continue;
      CharPart part;
      const std::string material = m.materials[prim.material].name;
      part.main = usageFor(material, "main"); part.depth = usageFor(material, "depth");
      if (part.main < 0 || part.depth < 0) { std::fprintf(stderr, "[baked-char] material %s was not captured\n", material.c_str()); return false; }
      // three attribute layout: position, normal, uv, skinIndex (float), skinWeight (as stored in the GLB)
      struct V { float p[3], n[3], uv[2], j[4], w[4]; };
      std::vector<V> vs(prim.vertexCount());
      for (size_t i = 0; i < vs.size(); i++) {
        V& v = vs[i];
        for (int k = 0; k < 3; k++) { v.p[k] = prim.pos[i * 3 + k]; v.n[k] = prim.nrm[i * 3 + k]; }
        v.uv[0] = prim.uv[i * 2]; v.uv[1] = prim.uv[i * 2 + 1];
        for (int k = 0; k < 4; k++) { v.j[k] = float(prim.joints[i * 4 + k]); v.w[k] = prim.weights[i * 4 + k]; }
      }
      glGenBuffers(1, &part.vbo); glBindBuffer(GL_ARRAY_BUFFER, part.vbo);
      glBufferData(GL_ARRAY_BUFFER, vs.size() * sizeof(V), vs.data(), GL_STATIC_DRAW);
      glGenBuffers(1, &part.ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, part.ibo);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, prim.idx.size() * 4, prim.idx.data(), GL_STATIC_DRAW);
      part.count = GLsizei(prim.idx.size());
      // one VAO per program: attribute locations come from each linked program
      for (int pass = 0; pass < 2; pass++) {
        UsagePlan* p = plan(char_, pass == 0 ? part.main : part.depth); if (!p) return false;
        GLuint& vao = pass == 0 ? part.vao : part.vaoDepth;
        glGenVertexArrays(1, &vao); glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, part.vbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, part.ibo);
        const struct { const char* name; int size; size_t offset; } attributes[] = {
          {"position", 3, offsetof(V, p)}, {"normal", 3, offsetof(V, n)}, {"uv", 2, offsetof(V, uv)},
          {"skinIndex", 4, offsetof(V, j)}, {"skinWeight", 4, offsetof(V, w)}};
        for (const auto& a : attributes) {
          const GLint loc = glGetAttribLocation(p->shader->id, a.name); if (loc < 0) continue;
          glEnableVertexAttribArray(loc); glVertexAttribPointer(loc, a.size, GL_FLOAT, GL_FALSE, sizeof(V), (void*)a.offset);
        }
      }
      glBindVertexArray(0);
      charParts_.push_back(part);
    }
  }
  glGenTextures(1, &boneTex_); glBindTexture(GL_TEXTURE_2D, boneTex_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  // csm.js character cascade: a 3.2 m box around the player, only the player casts into it
  const int charSize = manifest["shadowConfig"]["charSize"].asInt();
  if (charCascade_ && charSize > 0) {
    charLight_.size = charSize; charLight_.radius = 2.0f;
    glGenTextures(1, &charMap_); glBindTexture(GL_TEXTURE_2D, charMap_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, charSize, charSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_GEQUAL);
  }
  std::printf("[baked-char] %zu parts, %u programs, character cascade %d px\n", charParts_.size(), manifest["entries"].size(), charMap_ ? charSize : 0);
  return true;
}

void BakedCity::clearCharacter() {
  for (auto& p : charParts_) {
    glDeleteVertexArrays(1, &p.vao); glDeleteVertexArrays(1, &p.vaoDepth);
    glDeleteBuffers(1, &p.vbo); glDeleteBuffers(1, &p.ibo);
  }
  charParts_.clear();
  if (boneTex_) glDeleteTextures(1, &boneTex_);
  if (charMap_) glDeleteTextures(1, &charMap_);
  boneTex_ = charMap_ = 0; charNear_ = false; charLight_ = {};
}

// render/csm.js update(): the character light follows the player (+0.9 m) while it is within 25 m of the camera,
// fitted by _fit (texel-snapped in the light plane, depth unsnapped) with the tighter character normal bias.
void BakedCity::updateCharacterCascade(const Camera& camera) {
  const Vec3 sun = sunDirection_.normalized();
  const Vec3 up = std::abs(sun.y) > 0.99f ? Vec3{0, 0, 1} : UP;
  const Vec3 right = up.cross(sun).normalized(), lightUp = sun.cross(right).normalized();
  charNear_ = rig_ && charFocus_.distanceTo(camera.position) < 25;
  const Vec3 c = charNear_ ? charFocus_ + Vec3{0, 0.9f, 0} : Vec3{0, -5000, 0};
  const double r = 1.6, texel = 2 * r / charLight_.size;
  const auto snap = [](double v, double step) { return float(std::floor(v / step + 0.5) * step); }; // JS Math.round
  const Vec3 center = right * snap(c.dot(right), texel) + lightUp * snap(c.dot(lightUp), texel) + sun * c.dot(sun);
  const float D = float(r + 900), depthFar = float(D + r + 50);
  BakedCascade& l = charLight_;
  l.position = center + sun * D;
  l.view = Mat4::lookAt(l.position, center, up);
  l.cullProjection = Mat4::ortho(float(-r), float(r), float(-r), float(r), 1, depthFar);
  l.projection = l.cullProjection;
  l.projection.m[10] = 1 / (depthFar - 1); l.projection.m[14] = depthFar / (depthFar - 1);
  Mat4 bias; bias.m[0] = bias.m[5] = 0.5f; bias.m[12] = bias.m[13] = 0.5f;
  l.matrix = bias * l.projection * l.view;
  l.bias = float(texel * 0.6 / (depthFar - 1));
  l.normalBias = float(2 * r / l.size * 2.0);
  l.due = true;
}

namespace {
// SB_BAKE_TRACE: how much of a depth map a pass covered (reversed depth: cleared to 0)
void traceDepth(const char* label, int size) {
  static const bool on = std::getenv("SB_BAKE_TRACE") != nullptr;
  static int frames = 0;
  if (!on || (frames++ % 60) != 0) return;
  std::vector<float> depth(size_t(size) * size);
  glReadPixels(0, 0, size, size, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
  size_t covered = 0; for (float d : depth) covered += d > 0;
  std::printf("[baked-char] %s: %zu / %zu texels covered\n", label, covered, depth.size());
}
}  // namespace

void BakedCity::traceCharacterMap() { traceDepth("character cascade", charLight_.size); }

bool BakedCity::drawCharacterPass(bool shadow) {
  if (charParts_.empty() || !rig_ || !charVisible_) return true;
  rig_->skinMatrices(bones_);
  if (bones_.empty()) return true;
  glBindTexture(GL_TEXTURE_2D, boneTex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, GLsizei(bones_.size() * 4), 1, 0, GL_RGBA, GL_FLOAT, bones_.data());
  for (const CharPart& part : charParts_) {
    UsagePlan* p = plan(char_, shadow ? part.depth : part.main); if (!p) return false;
    if (!setupDraw(char_, *p, IDENTITY, true)) return false;
    glUniformMatrix4fv(p->shader->loc("bindMatrix"), 1, GL_FALSE, IDENTITY.m);
    glUniformMatrix4fv(p->shader->loc("bindMatrixInverse"), 1, GL_FALSE, IDENTITY.m);
    glBindVertexArray(shadow ? part.vaoDepth : part.vao);
    glDrawElements(GL_TRIANGLES, part.count, GL_UNSIGNED_INT, nullptr);
  }
  glBindVertexArray(0);
  glDepthMask(GL_TRUE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glEnable(GL_DEPTH_TEST);
  glDisable(GL_POLYGON_OFFSET_FILL); glDisable(GL_BLEND); glActiveTexture(GL_TEXTURE0);
  return true;
}

bool BakedCity::drawCharacter(const Camera& camera, float time) {
  if (charParts_.empty()) return true;
  beginPass(camera, camera.view(), camera.proj(true), time, -1);
  return drawCharacterPass(false);
}
