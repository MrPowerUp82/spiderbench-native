#include "gfx/renderer.h"
#include <SDL.h>
#include "gfx/baked_city.h"
#include "gfx/shaders.h"
#include "gfx/texture.h"
#include "world/world.h"
#include "anim/rig.h"
#include <cstdio>
#include <cstdlib>
#include <string>

#ifndef GL_LOWER_LEFT
#define GL_LOWER_LEFT 0x8CA1
#endif
#ifndef GL_ZERO_TO_ONE
#define GL_ZERO_TO_ONE 0x935F
#define GL_NEGATIVE_ONE_TO_ONE 0x935E
#endif

// ------------------------------------------------------------------------------------------------ character upload
void CharGpu::build(const Rig& rig) {
  const GltfModel& m = rig.model;
  std::vector<GLuint> imgs(m.imageCount, 0);
  for (int i = 0; i < m.imageCount; i++) {
    bool srgb = false;
    for (const auto& mt : m.materials) if (mt.baseTex == i) srgb = true;
    imgs[i] = loadTexture(assetPath("spiderman_img" + std::to_string(i) + ".tex"), srgb);
  }
  for (const auto& mt : m.materials) {
    Mat M; for (int k = 0; k < 4; k++) M.color[k] = mt.baseColor[k];
    M.rough = mt.roughness; M.metal = mt.metallic;
    M.tex = mt.baseTex >= 0;
    M.base = mt.baseTex >= 0 ? imgs[mt.baseTex] : 0; M.orm = mt.mrTex >= 0 ? imgs[mt.mrTex] : 0; M.normal = mt.normalTex >= 0 ? imgs[mt.normalTex] : 0;
    mats.push_back(M);
  }
  // only meshes attached to skinned nodes
  for (const auto& n : m.nodes) {
    if (n.mesh < 0) continue;
    for (const auto& p : m.meshes[n.mesh].prims) {
      Part P; P.material = p.material;
      size_t nv = p.vertexCount();
      struct V { float p[3], n[3], uv[2]; uint8_t j[4]; float w[4]; };
      std::vector<V> vs(nv);
      for (size_t i = 0; i < nv; i++) {
        V& v = vs[i];
        for (int k = 0; k < 3; k++) { v.p[k] = p.pos[i * 3 + k]; v.n[k] = p.nrm[i * 3 + k]; }
        v.uv[0] = p.uv[i * 2]; v.uv[1] = p.uv[i * 2 + 1];
        float ws = 0; for (int k = 0; k < 4; k++) { v.j[k] = p.joints[i * 4 + k]; v.w[k] = p.weights[i * 4 + k]; ws += v.w[k]; }
        if (ws > 0) for (float& w : v.w) w /= ws;
      }
      glGenVertexArrays(1, &P.vao); glGenBuffers(1, &P.vbo); glGenBuffers(1, &P.ibo);
      glBindVertexArray(P.vao);
      glBindBuffer(GL_ARRAY_BUFFER, P.vbo); glBufferData(GL_ARRAY_BUFFER, vs.size() * sizeof(V), vs.data(), GL_STATIC_DRAW);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, P.ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, p.idx.size() * 4, p.idx.data(), GL_STATIC_DRAW);
      glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, p));
      glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, n));
      glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, uv));
      glEnableVertexAttribArray(3); glVertexAttribIPointer(3, 4, GL_UNSIGNED_BYTE, sizeof(V), (void*)offsetof(V, j));
      glEnableVertexAttribArray(4); glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, w));
      glBindVertexArray(0);
      P.count = (GLsizei)p.idx.size();
      parts.push_back(P);
    }
  }
}

// ------------------------------------------------------------------------------------------------ init
static GLuint makeTex2D(int w, int h, GLenum internal, GLenum fmt, GLenum type, GLenum filter) {
  GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, fmt, type, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}

bool Renderer::init(int w, int h) {
  revZ = glHasClipControl() && !std::getenv("SB_NOREVZ");
  std::string C = glsl::COMMON;
  bool ok = true;
  ok &= world_.build("world", glsl::WORLD_VS, C + glsl::WORLD_FS);
  ok &= worldDepth_.build("worldDepth", glsl::DEPTH_VS, glsl::DEPTH_FS);
  ok &= skin_.build("skin", glsl::SKIN_VS, C + glsl::SKIN_FS);
  ok &= skinDepth_.build("skinDepth", glsl::DEPTH_SKIN_VS, glsl::DEPTH_FS);
  ok &= sky_.build("sky", glsl::FS_TRI_VS, C + glsl::SKY_FS);
  ok &= water_.build("water", glsl::WATER_VS, C + glsl::WATER_FS);
  ok &= web_.build("web", glsl::WEB_VS, C + glsl::WEB_FS);
  ok &= bright_.build("bright", glsl::FS_TRI_VS, glsl::BRIGHT_FS);
  ok &= up_.build("upsample", glsl::FS_TRI_VS, glsl::UPSAMPLE_FS);
  ok &= composite_.build("composite", glsl::FS_TRI_VS, glsl::COMPOSITE_FS);
  if (!ok) return false;
  // shadow cascades: depth texture array with hardware compare
  glGenTextures(1, &shadowTex_);
  glBindTexture(GL_TEXTURE_2D_ARRAY, shadowTex_);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, SHADOW, SHADOW, 3, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glGenFramebuffers(1, &shadowFbo_);
  glGenVertexArrays(1, &emptyVao_);
  // water: a big quad following the camera (positions rewritten per frame)
  glGenVertexArrays(1, &waterVao_); glGenBuffers(1, &waterVbo_);
  glBindVertexArray(waterVao_); glBindBuffer(GL_ARRAY_BUFFER, waterVbo_); glBufferData(GL_ARRAY_BUFFER, 6 * 12, nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
  glGenVertexArrays(1, &webVao_); glGenBuffers(1, &webVbo_);
  glBindVertexArray(webVao_); glBindBuffer(GL_ARRAY_BUFFER, webVbo_);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 28, nullptr);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 28, (void*)12);
  glBindVertexArray(0);
  tAsphalt_ = loadTexture(assetPath("asphalt_col.tex"), true);
  tSidewalk_ = loadTexture(assetPath("sidewalk_col.tex"), true);
  tGrass_ = loadTexture(assetPath("grass_col.tex"), true);
  tNoise_ = loadTexture(assetPath("noise.tex"), false);
  tBark_ = loadTexture(assetPath("bark_col.tex"), true);
  tWaterN_ = loadTexture(assetPath("water_nrm.tex"), false);
  resize(w, h);
  std::printf("[renderer] %s, reversed-Z %s\n", (const char*)glGetString(GL_RENDERER), revZ ? "on" : "off");
  return true;
}

void Renderer::resize(int w, int h) {
  if (w <= 0 || h <= 0) return;
  w_ = w; h_ = h;
  if (hdrFbo_) { glDeleteFramebuffers(1, &hdrFbo_); glDeleteTextures(1, &hdrColor_); glDeleteTextures(1, &hdrDepth_); }
  hdrColor_ = makeTex2D(w, h, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR);
  hdrDepth_ = makeTex2D(w, h, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST);
  glGenFramebuffers(1, &hdrFbo_); glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdrColor_, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, hdrDepth_, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "[renderer] HDR framebuffer incomplete\n");
  for (auto& L : bloomL_) { glDeleteFramebuffers(1, &L.fbo); glDeleteTextures(1, &L.tex); }
  bloomL_.clear();
  int bw = w / 2, bh = h / 2;
  for (int i = 0; i < 5 && bw > 8 && bh > 8; i++, bw /= 2, bh /= 2) {
    Level L; L.w = bw; L.h = bh; L.tex = makeTex2D(bw, bh, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GL_LINEAR);
    glGenFramebuffers(1, &L.fbo); glBindFramebuffer(GL_FRAMEBUFFER, L.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, L.tex, 0);
    bloomL_.push_back(L);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::clipDepth01(bool on) {
  if (!revZ) return;
  glClipControl_(GL_LOWER_LEFT, on ? GL_ZERO_TO_ONE : GL_NEGATIVE_ONE_TO_ONE);
}

// ------------------------------------------------------------------------------------------------ shadows
void Renderer::computeCascades(const Camera& cam) {
  float splits[4] = {cam.zNear, cascadeEnd_[0], cascadeEnd_[1], cascadeEnd_[2]};
  Mat4 camW = cam.world();
  float th = std::tan(cam.fov * PI / 360), tw = th * cam.aspect;
  Vec3 L = sunDir;
  for (int c = 0; c < 3; c++) {
    // bounding sphere of the frustum slice (stable under rotation -> no shimmering)
    float n = splits[c], f = splits[c + 1];
    Vec3 corners[8]; int k = 0;
    for (float d : {n, f}) for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) corners[k++] = camW.transformPoint({sx * tw * d, sy * th * d, -d});
    Vec3 ctr; for (auto& p : corners) ctr += p; ctr /= 8;
    float r = 0; for (auto& p : corners) r = std::max(r, p.distanceTo(ctr));
    r = std::ceil(r * 16) / 16;
    Vec3 up = std::fabs(L.y) > 0.99f ? Vec3{0, 0, 1} : UP;
    // rotation-only light view anchored at the world origin; the cascade centre is snapped to whole shadow texels
    Mat4 view = Mat4::lookAt(L, {0, 0, 0}, up);
    float texel = 2 * r / SHADOW;
    Vec3 cs = view.transformPoint(ctr);
    cs.x = std::floor(cs.x / texel) * texel; cs.y = std::floor(cs.y / texel) * texel;
    // depth: the slice +- r, extended 900 m toward the sun for tall casters (view -z is away from the light)
    Mat4 proj = Mat4::ortho(cs.x - r, cs.x + r, cs.y - r, cs.y + r, -(cs.z + r + 900), -(cs.z - r));
    shadowMat_[c] = proj * view;
  }
}

void Renderer::drawCharacter(const FrameInput& f, Shader& s, bool depthOnly) {
  if (!f.character || !f.rig) return;
  f.rig->skinMatrices(bones_);
  s.setMats("uBones[0]", bones_.data(), (int)std::min<size_t>(bones_.size(), 64));
  for (const auto& P : f.character->parts) {
    if (!depthOnly && P.material >= 0) {
      const auto& M = f.character->mats[P.material];
      s.set("uBaseColor", M.color[0], M.color[1], M.color[2], M.color[3]);
      s.set("uRough", M.rough); s.set("uMetal", M.metal); s.set("uHasTex", M.tex ? 1 : 0);
      glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, M.base);
      glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, M.orm);
      glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, M.normal);
    }
    glBindVertexArray(P.vao);
    glDrawElements(GL_TRIANGLES, P.count, GL_UNSIGNED_INT, nullptr);
  }
}

void Renderer::setCommon(Shader& s, const Camera& cam, float time) {
  s.set("uSunDir", sunDir); s.set("uSunColor", sunColor);
  s.set("uSkyZenith", skyZenith); s.set("uSkyHorizon", skyHorizon); s.set("uGroundAmb", groundAmb);
  s.set("uCamPos", cam.position); s.set("uFogDensity", fogDensity); s.set("uTime", time);
  s.set("uShadow", 0);
  static int dbg = std::getenv("SB_DEBUGVIEW") ? std::atoi(std::getenv("SB_DEBUGVIEW")) : 0;
  s.set("uDebug", dbg);
  s.setMats("uShadowMat[0]", shadowMat_, 3);
  s.set("uCascadeEnd", Vec3{cascadeEnd_[0], cascadeEnd_[1], cascadeEnd_[2]});
}

// ribbons: camera-facing quads along each polyline
void Renderer::drawWebs(const FrameInput& f, const Mat4& vp) {
  if (!f.webs || f.webs->empty()) return;
  std::vector<float> buf;
  for (const auto& L : *f.webs) {
    if (L.pts.size() < 2) continue;
    for (size_t i = 0; i + 1 < L.pts.size(); i++) {
      Vec3 a = L.pts[i], b = L.pts[i + 1];
      Vec3 d = (b - a).normalized(), toCam = (f.cam->position - a).normalized();
      Vec3 side = d.cross(toCam).normalized() * L.width;
      Vec3 q[4] = {a - side, a + side, b + side, b - side};
      int ord[6] = {0, 1, 2, 0, 2, 3};
      for (int k : ord) { buf.insert(buf.end(), {q[k].x, q[k].y, q[k].z, 1, 1, 1, L.alpha}); }
    }
  }
  web_.use(); setCommon(web_, *f.cam, f.time); web_.set("uViewProj", vp);
  glBindVertexArray(webVao_); glBindBuffer(GL_ARRAY_BUFFER, webVbo_);
  glBufferData(GL_ARRAY_BUFFER, buf.size() * 4, buf.data(), GL_STREAM_DRAW);
  glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
  glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(buf.size() / 7));
  glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}

// ------------------------------------------------------------------------------------------------ frame
static void glCheck(const char* where) {
  static bool on = std::getenv("SB_GLDEBUG") != nullptr;
  if (!on) return;
  for (GLenum e; (e = glGetError()) != GL_NO_ERROR;) std::fprintf(stderr, "[gl] error 0x%04x after %s\n", e, where);
}

void Renderer::render(const FrameInput& f) {
  Camera cam = *f.cam;
  // SB_PROFILE=2: GPU time per phase (glFinish between phases; slows the frame down, for measurement only)
  static const bool gpuProfile = std::getenv("SB_PROFILE") && std::atoi(std::getenv("SB_PROFILE")) == 2;
  static double phaseMs[4] = {0, 0, 0, 0}; static int phaseFrames = 0;
  uint64_t mark = 0;
  auto phase = [&](int k) {
    if (!gpuProfile) return;
    glFinish(); const uint64_t now = SDL_GetPerformanceCounter();
    if (k >= 0) phaseMs[k] += (now - mark) * 1000.0 / SDL_GetPerformanceFrequency();
    mark = now;
  };
  phase(-1);
  glCheck("frame start");
  computeCascades(cam);
  // ---- shadow pass
  if (f.bakedCity) {
    f.bakedCity->setCharacter(f.rig, f.characterVisible && f.rig, f.focus);
    f.bakedCity->healthy = f.bakedCity->drawShadows(cam, f.time);
    if (!f.bakedCity->healthy) return;
  } else {
    clipDepth01(false);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, SHADOW, SHADOW);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.5f, 2.0f);
    for (int c = 0; c < 3; c++) {
      glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowTex_, 0, c);
      { GLenum none = GL_NONE; glDrawBuffers(1, &none); glReadBuffer(GL_NONE); }
      if (std::getenv("SB_GLDEBUG") && glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "[gl] shadow fbo incomplete 0x%x\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));
      glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
      worldDepth_.use(); worldDepth_.set("uViewProj", shadowMat_[c]);
      f.world->cityGpu.drawCulled(Frustum(shadowMat_[c]));
      glDisable(GL_CULL_FACE); skinDepth_.use(); skinDepth_.set("uViewProj", shadowMat_[c]); drawCharacter(f, skinDepth_, true); glEnable(GL_CULL_FACE);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
  }
  glCheck("shadows");
  phase(0);

  // ---- HDR forward pass
  if (f.bakedCity) cam.projectionJitter = f.bakedCity->jitter(w_, h_);
  clipDepth01(true);
  glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo_);
  glViewport(0, 0, w_, h_);
  Mat4 view = cam.view(), proj = cam.proj(revZ), vp = proj * view;
  glClearDepth(revZ ? 0.0 : 1.0); glDepthFunc(revZ ? GL_GREATER : GL_LESS);
  glClear(GL_DEPTH_BUFFER_BIT);
  // sky (fills the background, no depth)
  glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  if (f.bakedCity) {
    f.bakedCity->healthy = f.bakedCity->drawSky(cam, w_, h_);
    if (!f.bakedCity->healthy) return;
  } else {
    sky_.use(); setCommon(sky_, cam, f.time);
    sky_.set("uInvViewProj", (Mat4::perspective(cam.fov, cam.aspect, cam.zNear, cam.zFar) * view).inverse());
    glBindVertexArray(emptyVao_); glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glCheck("sky");
  phase(1);
  glEnable(GL_DEPTH_TEST);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, shadowTex_);
  // city + far shores
  glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
  world_.use(); setCommon(world_, cam, f.time);
  world_.set("uViewProj", vp); world_.set("uView", view);
  const GLuint texs[] = {tAsphalt_, tSidewalk_, tGrass_, tNoise_, tBark_};
  const char* names[] = {"tAsphalt", "tSidewalk", "tGrass", "tNoise", "tBark"};
  for (int i = 0; i < 5; i++) { glActiveTexture(GL_TEXTURE1 + i); glBindTexture(GL_TEXTURE_2D, texs[i]); world_.set(names[i], 1 + i); }
  { Frustum fr(Mat4::perspective(cam.fov, cam.aspect, cam.zNear, cam.zFar) * view);
    if (f.bakedCity) f.bakedCity->healthy = f.bakedCity->healthy && f.bakedCity->draw(cam, revZ, f.time);
    else { f.world->cityGpu.drawCulled(fr); f.world->farGpu.drawCulled(fr); } }
  // water
  if (!f.bakedCity) {
    float cx = cam.position.x, cz = cam.position.z, R = 20000, y = -1.6f;
    float v[18] = {cx - R, y, cz - R, cx + R, y, cz + R, cx + R, y, cz - R, cx - R, y, cz - R, cx - R, y, cz + R, cx + R, y, cz + R};
    glBindBuffer(GL_ARRAY_BUFFER, waterVbo_); glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glDisable(GL_CULL_FACE);
    water_.use(); setCommon(water_, cam, f.time); water_.set("uViewProj", vp); water_.set("uView", view);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, tWaterN_); water_.set("tWaterN", 1);
    glBindVertexArray(waterVao_); glDrawArrays(GL_TRIANGLES, 0, 6);
  }
  // character: the original programs with the original city, else the native skin shader
  if (f.bakedCity && f.bakedCity->hasCharacter()) {
    f.bakedCity->healthy = f.bakedCity->healthy && f.bakedCity->drawCharacter(cam, f.time);
  } else if (f.characterVisible) {
    glDisable(GL_CULL_FACE);
    skin_.use(); setCommon(skin_, cam, f.time); skin_.set("uViewProj", vp); skin_.set("uView", view);
    skin_.set("tBase", 1); skin_.set("tOrm", 2); skin_.set("tNormal", 3);
    drawCharacter(f, skin_, false);
  }
  drawWebs(f, vp);
  glCheck("forward");
  phase(2);
  if (f.bakedCity && std::getenv("SB_BAKE_TRACE")) {
    std::vector<float> pixels(size_t(w_) * h_ * 4);
    glReadPixels(0, 0, w_, h_, GL_RGBA, GL_FLOAT, pixels.data());
    size_t bad = 0; float high = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
      for (int c = 0; c < 3; c++) {
        high = std::max(high, pixels[i + c]);
        if (!std::isfinite(pixels[i + c]) || pixels[i + c] > 10000) {
          if (bad++ == 0) std::printf("[baked-hdr] first invalid pixel %zu %zu: %g %g %g\n", (i / 4) % w_, (i / 4) / w_, pixels[i], pixels[i + 1], pixels[i + 2]);
        }
      }
    }
    std::printf("[baked-hdr] %zu invalid channels, peak %g\n", bad, high);
  }
  if (std::getenv("SB_GLDEBUG")) {
    float px[4] = {0}; glReadPixels(w_ / 2, h_ / 2, 1, 1, GL_RGBA, GL_FLOAT, px);
    float top[4] = {0}; glReadPixels(w_ / 2, h_ - 5, 1, 1, GL_RGBA, GL_FLOAT, top);
    Vec3 d = cam.direction();
    std::fprintf(stderr, "[dbg] cam (%.1f %.1f %.1f) dir (%.2f %.2f %.2f) fov %.1f | hdr centre %.3f %.3f %.3f top %.3f %.3f %.3f\n",
      cam.position.x, cam.position.y, cam.position.z, d.x, d.y, d.z, cam.fov, px[0], px[1], px[2], top[0], top[1], top[2]);
  }

  // ---- post: bloom chain
  clipDepth01(false);
  if (f.bakedCity) {
    f.bakedCity->healthy = f.bakedCity->healthy && f.bakedCity->drawPost(cam, w_, h_, hdrColor_, hdrDepth_, f.dt);
    glCheck("original post");
    phase(3);
    if (gpuProfile && ++phaseFrames == 30) {
      std::printf("[gpu] shadows %.1f ms, sky %.1f ms, forward %.1f ms, post %.1f ms\n", phaseMs[0] / 30, phaseMs[1] / 30, phaseMs[2] / 30, phaseMs[3] / 30);
      for (double& v : phaseMs) v = 0;
      phaseFrames = 0;
    }
    return;
  }
  glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  glBindVertexArray(emptyVao_);
  bright_.use(); bright_.set("tSrc", 0);
  for (size_t i = 0; i < bloomL_.size(); i++) {
    const Level& L = bloomL_[i];
    glBindFramebuffer(GL_FRAMEBUFFER, L.fbo); glViewport(0, 0, L.w, L.h);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, i == 0 ? hdrColor_ : bloomL_[i - 1].tex);
    int sw = i == 0 ? w_ : bloomL_[i - 1].w, sh = i == 0 ? h_ : bloomL_[i - 1].h;
    bright_.set("uTexel", Vec2{1.f / sw, 1.f / sh}); bright_.set("uThreshold", i == 0 ? 1.0f : 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  up_.use(); up_.set("tSrc", 0);
  glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
  for (int i = (int)bloomL_.size() - 1; i > 0; i--) {
    const Level& dst = bloomL_[i - 1];
    glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo); glViewport(0, 0, dst.w, dst.h);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, bloomL_[i].tex);
    up_.set("uTexel", Vec2{1.f / bloomL_[i].w, 1.f / bloomL_[i].h});
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glDisable(GL_BLEND);
  // ---- composite to the window
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, w_, h_);
  composite_.use();
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, hdrColor_);
  glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, bloomL_.empty() ? 0 : bloomL_[0].tex);
  composite_.set("tScene", 0); composite_.set("tBloom", 1);
  composite_.set("uExposure", exposure); composite_.set("uBloom", bloom); composite_.set("uBlur", f.motionBlur);
  composite_.set("uTexel", Vec2{1.f / w_, 1.f / h_});
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
  glCheck("post");
}
