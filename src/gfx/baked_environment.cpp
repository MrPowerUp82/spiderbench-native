#include "gfx/baked_environment.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
GLenum arrayType(const std::string& type) {
  if (type == "Uint8Array") return GL_UNSIGNED_BYTE;
  if (type == "Uint16Array") return GL_UNSIGNED_SHORT;
  if (type == "Uint32Array") return GL_UNSIGNED_INT;
  return GL_FLOAT;
}
int typeSize(GLenum type) { return type == GL_UNSIGNED_BYTE ? 1 : type == GL_UNSIGNED_SHORT || type == GL_HALF_FLOAT ? 2 : 4; }
GLint wrapping(int value) { return value == 1000 ? GL_REPEAT : value == 1002 ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE; }
}

bool BakedEnvironment::allocateTextures() {
  for (const auto& meta : programs_.manifest["resources"]) {
    Texture t; t.width = meta["width"].asInt(); t.height = meta["height"].asInt();
    const int depth = meta["depth"].asInt();
    if (t.width <= 0 || t.height <= 0 || t.width > 4096 || t.height > 4096 || depth <= 0 || depth > 256) return false;
    const std::string target = meta["target"].asString();
    t.target = target == "cube" ? GL_TEXTURE_CUBE_MAP : target == "3d" ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    GLenum type = meta["type"].asInt() == 1016 ? GL_HALF_FLOAT : meta["type"].asInt() == 1015 ? GL_FLOAT : GL_UNSIGNED_BYTE;
    GLenum format = meta["format"].asInt() == 1028 ? GL_RED : GL_RGBA;
    GLenum internal = type == GL_HALF_FLOAT ? GL_RGBA16F : type == GL_FLOAT ? (format == GL_RED ? GL_R32F : GL_RGBA32F) : GL_RGBA8;
    const int channels = format == GL_RED ? 1 : 4;
    std::vector<uint8_t> pixels(size_t(t.width) * t.height * depth * channels * typeSize(type), 0);
    if (!meta["blob"].isNull()) {
      std::vector<uint8_t> data; if (!archive_.readBlob(meta["blob"].asUInt(), data) || data.size() != pixels.size()) return false;
      pixels = std::move(data);
    }
    glGenTextures(1, &t.id); glBindTexture(t.target, t.id); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (t.target == GL_TEXTURE_3D) glTexImage3D(t.target, 0, internal, t.width, t.height, depth, 0, format, type, pixels.data());
    else if (t.target == GL_TEXTURE_CUBE_MAP) for (int face = 0; face < 6; face++) glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, internal, t.width, t.height, 0, format, type, pixels.data());
    else glTexImage2D(t.target, 0, internal, t.width, t.height, 0, format, type, pixels.data());
    glTexParameteri(t.target, GL_TEXTURE_WRAP_S, wrapping(meta["wrapS"].asInt()));
    glTexParameteri(t.target, GL_TEXTURE_WRAP_T, wrapping(meta["wrapT"].asInt()));
    if (t.target == GL_TEXTURE_3D) glTexParameteri(t.target, GL_TEXTURE_WRAP_R, wrapping(meta["wrapR"].asInt()));
    glTexParameteri(t.target, GL_TEXTURE_MIN_FILTER, meta["minFilter"].asInt() == 1003 ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(t.target, GL_TEXTURE_MAG_FILTER, meta["magFilter"].asInt() == 1003 ? GL_NEAREST : GL_LINEAR);
    textures_.push_back(t);
  }
  return glGetError() == GL_NO_ERROR;
}

bool BakedEnvironment::attach(const Json::Value& target) {
  const uint32_t index = target["texture"].asUInt(); if (index >= textures_.size()) return false;
  const auto& t = textures_[index]; glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  if (t.target == GL_TEXTURE_3D) glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, t.id, 0, target["face"].asInt());
  else glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, t.target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + target["face"].asInt() : GL_TEXTURE_2D, t.id, 0);
  GLenum color = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &color); glReadBuffer(color);
  const auto& v = target["viewport"]; glViewport(v[0].asInt(), v[1].asInt(), v[2].asInt(), v[3].asInt());
  if (target["scissorTest"].asBool()) {
    const auto& s = target["scissor"]; glEnable(GL_SCISSOR_TEST); glScissor(s[0].asInt(), s[1].asInt(), s[2].asInt(), s[3].asInt());
  } else glDisable(GL_SCISSOR_TEST);
  return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

GLuint BakedEnvironment::buffer(uint32_t index) {
  auto found = buffers_.find(index); if (found != buffers_.end()) return found->second;
  std::vector<uint8_t> data; if (!archive_.readBlob(index, data)) return 0;
  GLuint id; glGenBuffers(1, &id); glBindBuffer(GL_ARRAY_BUFFER, id); glBufferData(GL_ARRAY_BUFFER, data.size(), data.data(), GL_STATIC_DRAW);
  buffers_[index] = id; return id;
}

bool BakedEnvironment::draw(const Json::Value& job, const Camera* camera, int width, int height, uint64_t frame,
                            const std::unordered_map<uint32_t, GLuint>* overrides) {
  Shader* program = programs_.get(job["program"].asString()); if (!program) return false;
  program->use(); uploadCapturedUniforms(*program, job["uniforms"]);
  if (camera) {
    program->set("uCamPos", camera->position); program->set("uCamWorld", camera->world());
    program->set("uProjInv", camera->proj(true).inverse()); program->set("uReversed", 1.f);
    program->set("uFullRes", Vec2{float(width), float(height)}); program->set("uFrame", float(frame % 64));
  }
  int unit = 0;
  for (const auto& name : job["textures"].getMemberNames()) {
    const uint32_t index = job["textures"][name].asUInt(); if (index >= textures_.size()) return false;
    const auto& t = textures_[index]; GLuint id = t.id;
    if (overrides) { auto override = overrides->find(index); if (override != overrides->end()) id = override->second; }
    glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(t.target, id); program->set(name.c_str(), unit++);
  }
  const uint32_t geometry = job["geometry"].asUInt(); const auto& g = programs_.manifest["geometries"][geometry];
  const uint64_t key = uint64_t(program->id) << 32 | geometry;
  auto found = vaos_.find(key);
  if (found == vaos_.end()) {
    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao); vaos_[key] = vao;
    for (const auto& name : g["attributes"].getMemberNames()) {
      GLint location = glGetAttribLocation(program->id, name.c_str()); if (location < 0) continue;
      const auto& a = g["attributes"][name]; uint32_t blob = a["blob"].asUInt(); GLuint id = buffer(blob); if (!id) return false;
      GLenum type = arrayType(archive_.blob(blob)["arrayType"].asString()); glBindBuffer(GL_ARRAY_BUFFER, id); glEnableVertexAttribArray(location);
      glVertexAttribPointer(location, a["itemSize"].asInt(), type, a["normalized"].asBool() ? GL_TRUE : GL_FALSE, 0, nullptr);
    }
    if (!g["index"].isNull()) { GLuint id = buffer(g["index"]["blob"].asUInt()); if (!id) return false; glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, id); }
  } else glBindVertexArray(found->second);
  glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND); glDisable(GL_POLYGON_OFFSET_FILL); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  const int side = job["side"].asInt();
  if (side == 2) glDisable(GL_CULL_FACE); else { glEnable(GL_CULL_FACE); glCullFace(side == 1 ? GL_FRONT : GL_BACK); }
  uint32_t first = g["drawRange"][0].asUInt(), end = g["count"].asUInt();
  if (!g["drawRange"][1].isNull()) end = std::min(end, first + g["drawRange"][1].asUInt());
  if (!job["range"].isNull()) { first = std::max(first, job["range"][0].asUInt()); end = std::min(end, job["range"][0].asUInt() + job["range"][1].asUInt()); }
  if (end <= first) return true;
  if (!g["index"].isNull()) {
    GLenum type = arrayType(archive_.blob(g["index"]["blob"].asUInt())["arrayType"].asString());
    glDrawElements(GL_TRIANGLES, end - first, type, (void*)(uintptr_t(first) * typeSize(type)));
  } else glDrawArrays(GL_TRIANGLES, first, end - first);
  GLenum error = glGetError();
  if (error) { std::fprintf(stderr, "[baked-env] GL 0x%x at %s program %s\n", error, job["name"].asCString(), job["program"].asCString()); return false; }
  return true;
}

bool BakedEnvironment::open(const std::string& directory) {
  clear();
  if (!programs_.open(directory) || programs_.manifest["format"].asString() != "SBENV1" || !glClipControl_ ||
      !archive_.open((std::filesystem::path(directory) / "geometry.json").string())) return false;
  GLint savedFbo, savedViewport[4]; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &savedFbo); glGetIntegerv(GL_VIEWPORT, savedViewport);
  glClipControl_(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
  if (!allocateTextures()) return false;
  glGenFramebuffers(1, &fbo_);
  size_t count = 0;
  for (const auto& job : programs_.manifest["jobs"]) {
    if (!attach(job["target"])) return false;
    if (job["op"].asString() == "clear") {
      const auto& c = job["color"]; glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glClearColor(c[0].asFloat(), c[1].asFloat(), c[2].asFloat(), c[3].asFloat()); glClear(GL_COLOR_BUFFER_BIT);
    } else { if (!draw(job)) return false; count++; }
  }
  glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE); glActiveTexture(GL_TEXTURE0);
  glBindFramebuffer(GL_FRAMEBUFFER, savedFbo); glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
  std::printf("[baked-env] replayed %zu original draws: cloud noise, sky LUT, cubemap and GGX PMREM\n", count);
  return validate();
}

bool BakedEnvironment::drawSky(const Camera& camera, int width, int height, uint64_t frame) {
  GLint destination; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &destination);
  const auto& job = programs_.manifest["skyJob"]; Json::Value target = job["target"];
  auto& texture = textures_[target["texture"].asUInt()]; const int w = std::max(1, width / 2), h = std::max(1, height / 2);
  if (texture.width != w || texture.height != h) {
    texture.width = w; texture.height = h; glBindTexture(GL_TEXTURE_2D, texture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
  }
  target["viewport"][2] = w; target["viewport"][3] = h;
  if (!attach(target) || !draw(job, &camera, width, height, frame)) return false;
  glDisable(GL_SCISSOR_TEST); glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
  glBlitFramebuffer(0, 0, w, h, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
  glBindFramebuffer(GL_FRAMEBUFFER, destination); glViewport(0, 0, width, height); glActiveTexture(GL_TEXTURE0); glDepthMask(GL_TRUE);
  return glGetError() == GL_NO_ERROR;
}

GLuint BakedEnvironment::environment() const {
  uint32_t index = programs_.manifest["environment"].asUInt(); return index < textures_.size() ? textures_[index].id : 0;
}

Vec2 BakedEnvironment::jitter(int width, int height) const {
  auto halton = [](uint32_t index, uint32_t base) {
    float result = 0, fraction = 1;
    while (index) { fraction /= base; result += fraction * (index % base); index /= base; }
    return result;
  };
  uint32_t index = uint32_t((postFrame_ + 1) % 16) + 1;
  return {2 * (halton(index, 2) - .5f) / width, 2 * (halton(index, 3) - .5f) / height};
}

bool BakedEnvironment::drawPost(const Camera& camera, int width, int height, GLuint color, GLuint depth, float dt) {
  const auto& manifest = programs_.manifest;
  if (manifest["postJobs"].empty()) return false;
  const auto& inputs = manifest["postInputs"];
  std::unordered_map<uint32_t, GLuint> overrides = {
    {inputs["color"].asUInt(), color}, {inputs["depth"].asUInt(), depth},
    {inputs["sky"].asUInt(), textures_[manifest["skyJob"]["target"]["texture"].asUInt()].id}
  };
  // Match the original integer halving at each bloom level, including odd sizes.
  auto resizeTarget = [&](uint32_t index, int w, int h) {
    auto& t = textures_[index];
    if (t.width != w || t.height != h) {
      t.width = w; t.height = h; glBindTexture(GL_TEXTURE_2D, t.id);
      GLenum type = manifest["resources"][index]["type"].asInt() == 1015 ? GL_FLOAT : GL_HALF_FLOAT;
      glTexImage2D(GL_TEXTURE_2D, 0, type == GL_FLOAT ? GL_RGBA32F : GL_RGBA16F, w, h, 0, GL_RGBA, type, nullptr);
    }
  };
  const bool resized = previousWidth_ != width || previousHeight_ != height;
  const bool cut = postFrame_ == 0 || resized || (camera.position - previousPosition_).length() > 60 ||
                   std::abs(camera.quaternion.dot(previousQuaternion_)) < std::cos(0.3f);
  const bool swap = (postFrame_ & 1) != 0;
  uint32_t adapted = 0;
  uint32_t resolvedOriginal = UINT32_MAX, resolvedCurrent = UINT32_MAX;
  size_t invalid = 0; float peak = 0, adaptedEV = 0;
  const bool trace = std::getenv("SB_BAKE_TRACE") != nullptr;
  Camera unjittered = camera; unjittered.projectionJitter = {};
  auto matrix = [](Json::Value& value, const Mat4& m) {
    value = Json::Value(Json::arrayValue); for (float v : m.m) value.append(v);
  };
  for (const auto& original : manifest["postJobs"]) {
    Json::Value job = original; const std::string name = job["name"].asString();
    if (name != "taa" && resolvedOriginal != UINT32_MAX) {
      for (const auto& sampler : job["textures"].getMemberNames())
        if (job["textures"][sampler].asUInt() == resolvedOriginal) job["textures"][sampler] = resolvedCurrent;
    }
    auto& u = job["uniforms"];
    if (name == "taa") {
      const uint32_t out = original["target"]["texture"].asUInt(), prev = original["textures"]["uHistory"].asUInt();
      const uint32_t current = swap ? prev : out;
      resizeTarget(out, width, height); resizeTarget(prev, width, height);
      job["target"]["texture"] = current; job["textures"]["uHistory"] = swap ? out : prev;
      resolvedOriginal = out; resolvedCurrent = current;
      matrix(u["uCamWorld"], camera.world()); matrix(u["uProjInv"], camera.proj(true).inverse());
      matrix(u["uProjInvU"], unjittered.proj(true).inverse()); matrix(u["uPrevViewProj"], previousViewProjection_);
      u["uReset"] = cut ? 1.f : 0.f; u["uRes"][0] = float(width); u["uRes"][1] = float(height);
      u["uJitter"][0] = -.5f * camera.projectionJitter.x; u["uJitter"][1] = -.5f * camera.projectionJitter.y;
    }
    if (name == "autoExposure") {
      const uint32_t out = original["target"]["texture"].asUInt(), prev = original["textures"]["uPrev"].asUInt();
      adapted = swap ? prev : out; job["target"]["texture"] = adapted;
      job["textures"]["uPrev"] = swap ? out : prev;
      u["uReset"] = postFrame_ == 0 || resized ? 1.f : 0.f;
      const auto& grade = manifest["postGrade"];
      u["uRate"] = 1.f - std::exp(-std::max(dt, 1.f / 240) * .5f * (grade["aeSpeedUp"].asFloat() + grade["aeSpeedDown"].asFloat()));
    }
    if (name == "final") {
      job["textures"]["uAE"] = adapted;
      u["uFrame"] = float((postFrame_ + 1) % 36000); u["uAspect"] = float(width) / height;
      u["uPx"][0] = 1.f / width; u["uPx"][1] = 1.f / height;
      // Sun visibility is supplied when the lens/shaft stage is ported.
      u["uFlare"] = 0.f;
      matrix(u["uProjInvS"], unjittered.proj(true).inverse());
      glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, width, height); glDisable(GL_SCISSOR_TEST);
    } else {
      const uint32_t targetIndex = job["target"]["texture"].asUInt(); int w = width, h = height;
      if (name == "bloomDown" || name == "bloomUp") {
        const uint32_t inputIndex = job["textures"][name == "bloomDown" ? "uSrc" : "uCur"].asUInt();
        const auto& input = textures_[inputIndex];
        w = name == "bloomDown" ? std::max(1, input.width >> 1) : input.width;
        h = name == "bloomDown" ? std::max(1, input.height >> 1) : input.height;
        const auto& source = textures_[job["textures"]["uSrc"].asUInt()];
        u["uPx"][0] = 1.f / source.width; u["uPx"][1] = 1.f / source.height;
      } else if (name == "autoExposure") w = h = 1;
      resizeTarget(targetIndex, w, h);
      job["target"]["viewport"][2] = w; job["target"]["viewport"][3] = h;
      if (!attach(job["target"])) return false;
    }
    if (!draw(job, name == "composite" ? &camera : nullptr, width, height, postFrame_ + 1, &overrides)) return false;
    if (trace) {
      const int w = name == "final" ? width : textures_[job["target"]["texture"].asUInt()].width;
      const int h = name == "final" ? height : textures_[job["target"]["texture"].asUInt()].height;
      std::vector<float> pixels(size_t(w) * h * 4); glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, pixels.data());
      for (float v : pixels) { if (!std::isfinite(v) || std::abs(v) > 10000) invalid++; peak = std::max(peak, v); }
      if (name == "autoExposure") adaptedEV = pixels[0];
    }
  }
  previousPosition_ = camera.position; previousQuaternion_ = camera.quaternion; postFrame_++;
  previousViewProjection_ = unjittered.proj(true) * camera.view(); previousWidth_ = width; previousHeight_ = height;
  glDepthMask(GL_TRUE); glActiveTexture(GL_TEXTURE0);
  if (trace) std::printf("[baked-post] %zu invalid channels, peak %g, exposure EV %g, %u passes\n", invalid, peak, adaptedEV, manifest["postJobs"].size());
  if (invalid) return false;
  return true;
}

bool BakedEnvironment::validate(const std::string& reportPath) {
  GLint savedFbo, savedViewport[4]; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &savedFbo); glGetIntegerv(GL_VIEWPORT, savedViewport);
  Json::Value report; report["format"] = "SBENVGPUCHECK1"; report["driver"] = (const char*)glGetString(GL_RENDERER);
  for (uint32_t index : {1u, programs_.manifest["environment"].asUInt()}) {
    const auto& t = textures_[index]; Json::Value target; target["texture"] = index; target["face"] = 0;
    target["viewport"] = Json::Value(Json::arrayValue); for (int v : {0, 0, t.width, t.height}) target["viewport"].append(v);
    if (!attach(target)) return false;
    std::vector<float> pixels(size_t(t.width) * t.height * 4); glReadPixels(0, 0, t.width, t.height, GL_RGBA, GL_FLOAT, pixels.data());
    double energy = 0; float peak = 0; size_t valid = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
      for (int c = 0; c < 4; c++) if (!std::isfinite(pixels[i+c]) || pixels[i+c] < 0) { std::fprintf(stderr, "[baked-env] invalid radiance at resource %u pixel %zu\n", index, i/4); return false; }
      if (pixels[i+3] > 0) { energy += pixels[i] + pixels[i+1] + pixels[i+2]; peak = std::max({peak, pixels[i], pixels[i+1], pixels[i+2]}); valid++; }
    }
    if (!valid || energy <= 0 || glGetError() != GL_NO_ERROR) { std::fprintf(stderr, "[baked-env] empty resource %u\n", index); return false; }
    Json::Value result; result["resource"] = index; result["coveredPixels"] = Json::UInt64(valid); result["meanRadiance"] = energy / (valid * 3); result["peak"] = peak;
    report["textures"].append(result);
    std::printf("[baked-env] %s: %zu finite pixels, mean %.6f, peak %.6f\n", index == 1 ? "LUT" : "PMREM", valid, energy / (valid * 3), peak);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, savedFbo); glDisable(GL_SCISSOR_TEST);
  glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
  if (!reportPath.empty()) { std::ofstream file(reportPath); file << report; if (!file) return false; }
  return true;
}

void BakedEnvironment::clear() {
  for (auto& entry : vaos_) glDeleteVertexArrays(1, &entry.second);
  for (auto& entry : buffers_) glDeleteBuffers(1, &entry.second);
  for (auto& t : textures_) glDeleteTextures(1, &t.id);
  if (fbo_) glDeleteFramebuffers(1, &fbo_); fbo_ = 0;
  vaos_.clear(); buffers_.clear(); textures_.clear(); programs_.clear();
  postFrame_ = 0;
  previousWidth_ = previousHeight_ = 0;
}
