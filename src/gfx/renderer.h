// Frame pipeline (replaces render/pipeline.js + csm.js + lighting.js + sky.js with a compact forward renderer):
//   3-cascade sun shadows -> HDR forward pass (sky, city, far shores, water, character, webs; reversed-Z when
//   glClipControl is available) -> bloom (dual-filter mip chain) -> composite (speed blur, FXAA-lite, ACES, vignette).
#pragma once
#include "gfx/shader.h"
#include "gfx/camera.h"
#include "player/web.h"
#include <vector>

class World; class Rig; class BakedCity;

struct CharGpu {
  struct Part { GLuint vao = 0, vbo = 0, ibo = 0; GLsizei count = 0; int material = -1; };
  std::vector<Part> parts;
  struct Mat { GLuint base = 0, orm = 0, normal = 0; float color[4] = {1, 1, 1, 1}; float rough = 1, metal = 0; bool tex = false; };
  std::vector<Mat> mats;
  void build(const Rig& rig);
};

struct FrameInput {
  const Camera* cam = nullptr;
  const World* world = nullptr;
  BakedCity* bakedCity = nullptr;
  const Rig* rig = nullptr;
  const CharGpu* character = nullptr;
  bool characterVisible = true;
  Vec3 focus; // player object position (character shadow cascade)
  const std::vector<WebLine>* webs = nullptr;
  float time = 0, motionBlur = 0, dt = 1.f / 60;
};

class Renderer {
 public:
  bool revZ = false;
  Vec3 sunDir = Vec3{-0.36f, 0.8f, 0.48f}.normalized();   // early afternoon, ~53 deg up, from the south-west
  Vec3 sunColor{3.1f, 2.85f, 2.5f};
  Vec3 skyZenith{0.26f, 0.44f, 0.72f}, skyHorizon{0.7f, 0.77f, 0.84f}, groundAmb{0.16f, 0.15f, 0.14f};
  float fogDensity = 0.00022f, exposure = 0.9f, bloom = 0.3f;

  bool init(int w, int h);
  void resize(int w, int h);
  void render(const FrameInput& f);
  int width() const { return w_; }
  int height() const { return h_; }

 private:
  int w_ = 0, h_ = 0;
  Shader world_, worldDepth_, skin_, skinDepth_, sky_, water_, web_, bright_, up_, composite_;
  GLuint shadowTex_ = 0, shadowFbo_ = 0;
  static constexpr int SHADOW = 2048;
  Mat4 shadowMat_[3]; float cascadeEnd_[3] = {28, 110, 480};
  GLuint hdrFbo_ = 0, hdrColor_ = 0, hdrDepth_ = 0;
  struct Level { GLuint fbo = 0, tex = 0; int w = 0, h = 0; };
  std::vector<Level> bloomL_;
  GLuint emptyVao_ = 0, waterVao_ = 0, waterVbo_ = 0, webVao_ = 0, webVbo_ = 0;
  GLuint tAsphalt_ = 0, tSidewalk_ = 0, tGrass_ = 0, tNoise_ = 0, tBark_ = 0, tWaterN_ = 0;
  std::vector<Mat4> bones_;
  void setCommon(Shader& s, const Camera& cam, float time);
  void computeCascades(const Camera& cam);
  void drawCharacter(const FrameInput& f, Shader& s, bool depthOnly);
  void drawWebs(const FrameInput& f, const Mat4& vp);
  void clipDepth01(bool on);
};
