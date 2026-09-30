#include "gfx/baked_city.h"
#include "gfx/mesh.h"
#include <zlib.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <algorithm>
#include <cstdlib>

namespace {
bool readJson(const std::filesystem::path& path, Json::Value& root) {
  std::ifstream file(path); Json::CharReaderBuilder builder; std::string error;
  return file && Json::parseFromStream(builder, file, &root, &error);
}
GLenum arrayType(const std::string& name) {
  if (name == "Uint8Array" || name == "Uint8ClampedArray") return GL_UNSIGNED_BYTE;
  if (name == "Uint16Array") return GL_UNSIGNED_SHORT;
  if (name == "Uint32Array") return GL_UNSIGNED_INT;
  if (name == "Int8Array") return GL_BYTE;
  if (name == "Int16Array") return GL_SHORT;
  if (name == "Int32Array") return GL_INT;
  return GL_FLOAT;
}
int typeBytes(GLenum type) { return type == GL_UNSIGNED_BYTE || type == GL_BYTE ? 1 : type == GL_UNSIGNED_SHORT || type == GL_SHORT ? 2 : 4; }
Vec3 vec(const Json::Value& v) { return {v[0].asFloat(), v[1].asFloat(), v[2].asFloat()}; }
GLenum blendFactor(int v) {
  const GLenum factors[] = {GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
    GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA_SATURATE,
    GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR, GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA};
  return v >= 200 && v <= 214 ? factors[v - 200] : GL_ONE;
}
GLenum blendEquation(int v) {
  const GLenum equations[] = {GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX};
  return v >= 100 && v <= 104 ? equations[v - 100] : GL_FUNC_ADD;
}
void applyState(const Json::Value& m) {
  const int side = m["side"].asInt();
  if (side == 2) glDisable(GL_CULL_FACE); else { glEnable(GL_CULL_FACE); glCullFace(side == 1 ? GL_FRONT : GL_BACK); }
  glDepthMask(m["depthWrite"].asBool() ? GL_TRUE : GL_FALSE);
  if (m["depthTest"].asBool()) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
  const GLenum depth[] = {GL_NEVER, GL_ALWAYS, GL_GREATER, GL_GEQUAL, GL_EQUAL, GL_LEQUAL, GL_LESS, GL_NOTEQUAL};
  glDepthFunc(depth[std::clamp(m["depthFunc"].asInt(), 0, 7)]);
  GLboolean color = m["colorWrite"].asBool() ? GL_TRUE : GL_FALSE; glColorMask(color, color, color, color);
  if (m["polygonOffset"].asBool()) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-m["polygonOffsetFactor"].asFloat(), m["polygonOffsetUnits"].asFloat());
  } else glDisable(GL_POLYGON_OFFSET_FILL);
  const int blending = m["blending"].asInt(); const bool premultiplied = m["premultipliedAlpha"].asBool();
  if (blending == 0 || (blending == 1 && !m["transparent"].asBool())) { glDisable(GL_BLEND); return; }
  glEnable(GL_BLEND); glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
  switch (blending) {
    case 1: glBlendFuncSeparate(premultiplied ? GL_ONE : GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
    case 2: glBlendFuncSeparate(premultiplied ? GL_ONE : GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE); break;
    case 3: glBlendFuncSeparate(GL_ZERO, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE); break;
    case 4: glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE); break;
    case 5: {
      const auto fallback = [&m](const char* key, const char* other) { return m[key].isNull() ? m[other].asInt() : m[key].asInt(); };
      glBlendEquationSeparate(blendEquation(m["blendEquation"].asInt()), blendEquation(fallback("blendEquationAlpha", "blendEquation")));
      glBlendFuncSeparate(blendFactor(m["blendSrc"].asInt()), blendFactor(m["blendDst"].asInt()), blendFactor(fallback("blendSrcAlpha", "blendSrc")), blendFactor(fallback("blendDstAlpha", "blendDst")));
      const auto& c = m["blendColor"]; glBlendColor(c[0].asFloat(), c[1].asFloat(), c[2].asFloat(), m["blendAlpha"].asFloat()); break;
    }
  }
}
}

bool BakedCity::open(const std::string& directory, const std::string& shaderDirectory) {
  clear();
  if (!geometry_.open((std::filesystem::path(directory) / "geometry.json").string()) ||
      !city_.programs.open(shaderDirectory) || !readJson(std::filesystem::path(directory) / "textures.json", city_.textures) ||
      city_.textures["format"].asString() != "SBTEX1" || !city_.programs.manifest["objectPrograms"].isArray()) return false;
  city_.directory = directory; shaderDir_ = shaderDirectory;
  sunDirection_ = vec(city_.programs.manifest["sunDirection"]);
  charCascade_ = city_.programs.manifest["charCascade"].asBool();
  if (!pools_.open((std::filesystem::path(directory) / "pool-data.json").string(), geometry_.meshCount())) return false;
  for (const auto& pool : pools_.entries()) {
    const auto& mesh = geometry_.mesh(pool.meta["objectOrdinal"].asUInt());
    if (mesh["name"] != pool.meta["name"] || !mesh["instanced"].asBool() || !mesh["attributes"].isMember("aLod")) return false;
  }
  bindings_.resize(geometry_.meshCount());
  depthBindings_.resize(geometry_.meshCount());
  for (const auto& bind : city_.programs.manifest["objectPrograms"]) {
    std::string pass = bind["pass"].asString();
    if (pass != "main" && pass != "depth") continue;
    size_t mesh = bind["objectOrdinal"].asUInt();
    if (mesh >= bindings_.size()) return false;
    auto& list = pass == "main" ? bindings_[mesh] : depthBindings_[mesh]; size_t slot = bind["materialSlot"].asUInt();
    if (list.size() <= slot) list.resize(slot + 1, -1);
    list[slot] = bind["usageIndex"].asInt();
    if (!city_.programs.manifest["usages"][list[slot]]["renderState"].isObject()) return false;
  }
  if (!csm_.configure(city_.programs.manifest["shadowConfig"], vec(city_.programs.manifest["sunDirection"]))) return false;
  if (!visibility_.open(geometry_.meshes(), city_.programs.manifest["shadowObjects"], city_.programs.manifest.get("lodScale", 1.0).asDouble())) return false;
  if (int(csm_.cascades.size()) > UsagePlan::MAX_CASCADES || city_.programs.manifest["cascadeCount"].asUInt() != csm_.cascades.size()) return false;
  if (!environment_.open((std::filesystem::path(directory) / "environment").string()) ||
      environment_.manifest()["source"] != city_.programs.manifest["source"] ||
      environment_.manifest()["threeRevision"] != city_.programs.manifest["threeRevision"] ||
      environment_.manifest()["timeOfDay"] != city_.programs.manifest["timeOfDay"]) return false;
  std::printf("[baked-city] %zu meshes, %zu materials\n", geometry_.meshCount(), geometry_.materialCount());
  return true;
}

GLuint BakedCity::buffer(uint32_t id) {
  auto found = buffers_.find(id);
  if (found != buffers_.end()) { found->second.frame = frame_; return found->second.id; }
  std::vector<uint8_t> data;
  if (!geometry_.readBlob(id, data)) return 0;
  Buffer b; b.bytes = data.size(); b.frame = frame_;
  glGenBuffers(1, &b.id); glBindBuffer(GL_ARRAY_BUFFER, b.id);
  glBufferData(GL_ARRAY_BUFFER, data.size(), data.data(), GL_STATIC_DRAW);
  if (glGetError() != GL_NO_ERROR) { glDeleteBuffers(1, &b.id); return 0; }
  residentBytes += data.size(); buffers_[id] = b;
  return b.id;
}

BakedCity::Texture* BakedCity::texture(Library& lib, int id) {
  auto found = lib.gpu.find(id);
  if (found != lib.gpu.end()) return &found->second;
  if (id < 0 || id >= int(lib.textures["textures"].size())) return nullptr;
  const auto& meta = lib.textures["textures"][id];
  if (meta["file"].isNull()) return nullptr; // dynamic reflection targets are not baked
  std::ifstream file(std::filesystem::path(lib.directory) / meta["file"].asString(), std::ios::binary);
  std::vector<uint8_t> packed((std::istreambuf_iterator<char>(file)), {});
  uLongf size = (uLongf)meta["bytes"].asUInt64();
  if (size > 512ull * 1024 * 1024 || packed.empty()) return nullptr;
  std::vector<uint8_t> pixels(size);
  if (uncompress(pixels.data(), &size, packed.data(), (uLong)packed.size()) != Z_OK || size != pixels.size()) return nullptr;
  int w = meta["width"].asInt(), h = meta["height"].asInt(), depth = meta["depth"].asInt();
  GLenum format = GL_RGBA, internal = meta["colorSpace"].asString() == "srgb" ? GL_SRGB8_ALPHA8 : GL_RGBA8;
  GLenum type = arrayType(meta["arrayType"].asString());
  if (meta["format"].asInt() == 1028) { format = GL_RED; internal = GL_R8; }
  if (meta["format"].asInt() == 1030) { format = GL_RG; internal = GL_RG8; }
  if (type == GL_FLOAT) internal = format == GL_RED ? GL_R32F : GL_RGBA32F;
  if (meta["type"].asInt() == 1016) { type = GL_HALF_FLOAT; internal = format == GL_RG ? GL_RG16F : GL_RGBA16F; }
  const int channels = format == GL_RED ? 1 : format == GL_RG ? 2 : 4;
  const size_t row = size_t(w) * channels * (type == GL_HALF_FLOAT ? 2 : typeBytes(type));
  if (size != row * h * depth) return nullptr;
  if (meta["flipY"].asBool()) {
    for (int layer = 0; layer < depth; layer++) for (int y = 0; y < h / 2; y++)
      for (size_t x = 0; x < row; x++) std::swap(pixels[(size_t(layer) * h + y) * row + x], pixels[(size_t(layer) * h + h - 1 - y) * row + x]);
  }
  Texture t; t.target = depth > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
  glGenTextures(1, &t.id); glBindTexture(t.target, t.id); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (depth > 1) glTexImage3D(t.target, 0, internal, w, h, depth, 0, format, type, pixels.data());
  else glTexImage2D(t.target, 0, internal, w, h, 0, format, type, pixels.data());
  const auto wrap = [](int v) { return v == 1000 ? GL_REPEAT : v == 1002 ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE; };
  const auto filter = [](int v) -> GLint {
    switch (v) { case 1003: return GL_NEAREST; case 1004: return GL_NEAREST_MIPMAP_NEAREST;
      case 1005: return GL_NEAREST_MIPMAP_LINEAR; case 1007: return GL_LINEAR_MIPMAP_NEAREST;
      case 1008: return GL_LINEAR_MIPMAP_LINEAR; default: return GL_LINEAR; }
  };
  glTexParameteri(t.target, GL_TEXTURE_WRAP_S, wrap(meta["wrapS"].asInt()));
  glTexParameteri(t.target, GL_TEXTURE_WRAP_T, wrap(meta["wrapT"].asInt()));
  glTexParameteri(t.target, GL_TEXTURE_MAG_FILTER, filter(meta["magFilter"].asInt()));
  glTexParameteri(t.target, GL_TEXTURE_MIN_FILTER, filter(meta["minFilter"].asInt()));
  if (meta["generateMipmaps"].asBool()) glGenerateMipmap(t.target);
  if (glGetError() != GL_NO_ERROR) { glDeleteTextures(1, &t.id); return nullptr; }
  return &lib.gpu.emplace(id, t).first->second;
}

BakedCity::Texture& BakedCity::placeholder(GLenum target, bool shadow) {
  GLenum key = shadow ? GL_SAMPLER_2D_SHADOW : target;
  auto found = placeholders_.find(key); if (found != placeholders_.end()) return found->second;
  Texture t; t.target = target; const uint8_t black[4] = {0, 0, 0, 255};
  glGenTextures(1, &t.id); glBindTexture(target, t.id);
  if (target == GL_TEXTURE_2D_ARRAY || target == GL_TEXTURE_3D) glTexImage3D(target, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
  else if (target == GL_TEXTURE_CUBE_MAP) for (int i = 0; i < 6; i++) glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
  else if (shadow) {
    float depth = 0; glTexImage2D(target, 0, GL_DEPTH_COMPONENT32F, 1, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTexParameteri(target, GL_TEXTURE_COMPARE_FUNC, GL_GEQUAL);
  } else glTexImage2D(target, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return placeholders_.emplace(key, t).first->second;
}

bool BakedCity::bindMesh(size_t index, Shader& shader) {
  uint64_t key = uint64_t(shader.id) << 32 | index;
  auto found = meshes_.find(key);
  const auto& meta = geometry_.mesh(index);
  if (found != meshes_.end()) {
    glBindVertexArray(found->second.vao);
    for (uint32_t id : found->second.blobs) buffers_.at(id).frame = frame_;
    return true;
  }
  Mesh mesh; glGenVertexArrays(1, &mesh.vao); glBindVertexArray(mesh.vao);
  BakedPool* pool = pools_.forMesh(index);
  auto attribute = [&](const std::string& name, uint32_t id, int items, bool normalized, bool instanced) {
    GLint loc = glGetAttribLocation(shader.id, name.c_str());
    if (loc < 0) return true;
    auto dynamic = pool ? pool->attributes.find(name) : std::unordered_map<std::string, BakedPool::Attribute>::iterator{};
    bool pooled = pool && dynamic != pool->attributes.end();
    GLuint b = pooled ? dynamic->second.gpu : buffer(id); if (!b) return false;
    if (pooled) { items = dynamic->second.size; instanced = true; normalized = false; }
    else mesh.blobs.push_back(id);
    glBindBuffer(GL_ARRAY_BUFFER, b);
    GLenum type = pooled ? GL_FLOAT : arrayType(geometry_.blob(id)["arrayType"].asString());
    int columns = items == 16 ? 4 : 1, width = items / columns;
    for (int c = 0; c < columns; c++) {
      glEnableVertexAttribArray(loc + c);
      glVertexAttribPointer(loc + c, width, type, normalized ? GL_TRUE : GL_FALSE, columns == 1 ? 0 : items * typeBytes(type), (void*)(uintptr_t(c * width * typeBytes(type))));
      glVertexAttribDivisor(loc + c, instanced ? 1 : 0);
    }
    return true;
  };
  bool ok = true;
  for (const auto& name : meta["attributes"].getMemberNames()) {
    const auto& a = meta["attributes"][name];
    bool instanced = city_.programs.manifest["shadowObjects"][Json::ArrayIndex(index)]["attributeDivisors"][name].asInt() != 0;
    ok = ok && attribute(name, a["blob"].asUInt(), a["itemSize"].asInt(), a["normalized"].asBool(), instanced);
  }
  if (meta["instanced"].asBool()) {
    ok = ok && attribute("instanceMatrix", meta["instanceMatrix"].asUInt(), 16, false, true);
    if (!meta["instanceColor"].isNull() || (pool && pool->attributes.contains("instanceColor")))
      ok = ok && attribute("instanceColor", meta["instanceColor"].asUInt(), 3, false, true);
  }
  if (!meta["index"].isNull()) {
    uint32_t id = meta["index"]["blob"].asUInt(); GLuint b = buffer(id);
    if (!b) ok = false;
    else { mesh.blobs.push_back(id); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b); }
  }
  if (!ok) { glDeleteVertexArrays(1, &mesh.vao); return false; }
  meshes_.emplace(key, std::move(mesh)); return true;
}

void BakedCity::setUniforms(Shader& shader, const Json::Value& values) { uploadCapturedUniforms(shader, values); }

bool BakedCity::draw(const Camera& camera, bool reversedDepth, float time) {
  if (!reversedDepth) { std::fprintf(stderr, "[baked-city] captured shaders require reversed depth\n"); return false; }
  drawn = 0; triangles = 0;
  Mat4 view = camera.view();
  Frustum frustum(Mat4::perspective(camera.fov, camera.aspect, camera.zNear, camera.zFar) * view);
  bool ok = renderPass(camera, view, camera.proj(true), frustum, time, -1);
  evict(); return ok;
}

bool BakedCity::drawShadows(const Camera& camera, float time) {
  if (!glClipControl_) { std::fprintf(stderr, "[baked-city] reversed depth requires GL_ARB_clip_control\n"); return false; }
  ++frame_; shadowDrawn = 0;
  if (!pools_.update(camera)) return false;
  visibility_.update(camera.position);
  csm_.update(camera);
  if (!shadowFbo_) {
    glGenFramebuffers(1, &shadowFbo_);
    for (const auto& cascade : csm_.cascades) {
      Texture texture; glGenTextures(1, &texture.id); glBindTexture(GL_TEXTURE_2D, texture.id);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, cascade.size, cascade.size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_GEQUAL);
      shadowMaps_.push_back(texture);
    }
  }
  glClipControl_(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
  glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
  GLenum none = GL_NONE; glDrawBuffers(1, &none); glReadBuffer(GL_NONE);
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_GREATER); glClearDepth(0);
  glDisable(GL_POLYGON_OFFSET_FILL);
  for (int i = 0; i < int(csm_.cascades.size()); i++) {
    const auto& c = csm_.cascades[i]; if (!c.due) continue;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMaps_[i].id, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    glViewport(0, 0, c.size, c.size); glDepthMask(GL_TRUE); glClear(GL_DEPTH_BUFFER_BIT);
    if (!renderPass(camera, c.view, c.projection, Frustum(c.cullProjection * c.view), time, i)) return false;
    // the player casts in the cascades whose camera layers include layer 0 (csm.js: cascades >= 3 see big casters only)
    if (i <= 2 && !drawCharacterPass(true)) return false;
  }
  if (charMap_) { // CSM_char: only the player (CHAR_LAYER), refreshed every frame
    updateCharacterCascade(camera);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, charMap_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    glViewport(0, 0, charLight_.size, charLight_.size); glDepthMask(GL_TRUE); glClear(GL_DEPTH_BUFFER_BIT);
    if (charNear_) {
      beginPass(camera, charLight_.view, charLight_.projection, time, int(csm_.cascades.size()));
      if (!drawCharacterPass(true)) return false;
    }
    traceCharacterMap();
  }
  return true;
}

void BakedCity::prepare() {
  info_.assign(geometry_.meshCount(), {});
  for (size_t i = 0; i < info_.size(); i++) {
    const auto& m = geometry_.mesh(i);
    const auto& so = city_.programs.manifest["shadowObjects"][Json::ArrayIndex(i)];
    MeshInfo& info = info_[i];
    info.name = m["name"].asString();
    info.skip = m["dynamic"].asBool() || info.name.ends_with(" super");
    info.instanced = m["instanced"].asBool();
    info.instanceCount = m["instanceCount"].asInt();
    info.layers = m["layers"].asUInt(); info.shadowLayers = so["layers"].asUInt();
    info.minCascade = so["minCascade"].asInt(); info.maxCascade = so["maxCascade"].asInt();
    info.frustumCulled = so["frustumCulled"].asBool(); info.geometryInstances = so["geometryInstances"].asInt();
    info.receiveShadow = m["receiveShadow"].asBool();
    info.transparent = geometry_.material(m["material"][0].asUInt())["transparent"].asBool();
    info.renderOrder = m["renderOrder"].asInt();
    info.first = m["drawRange"][0].asUInt(); info.count = m["drawRange"][1].asUInt();
    info.hasIndex = !m["index"].isNull();
    if (info.hasIndex) info.indexType = arrayType(geometry_.blob(m["index"]["blob"].asUInt())["arrayType"].asString());
    for (const auto& g : m["groups"]) info.groups.push_back({g["start"].asUInt(), g["count"].asUInt(), g["materialIndex"].asUInt()});
    info.pool = pools_.forMesh(i);
    for (int j = 0; j < 16; j++) info.model.m[j] = m["matrixWorld"][j].asFloat();
    const Vec3 mn = vec(m["bounds"][0]), mx = vec(m["bounds"][1]);
    info.lo = {INF, INF, INF}; info.hi = {-INF, -INF, -INF};
    for (int j = 0; j < 8; j++) {
      const Vec3 q = info.model.transformPoint({j & 1 ? mx.x : mn.x, j & 2 ? mx.y : mn.y, j & 4 ? mx.z : mn.z});
      info.lo = {std::min(info.lo.x, q.x), std::min(info.lo.y, q.y), std::min(info.lo.z, q.z)};
      info.hi = {std::max(info.hi.x, q.x), std::max(info.hi.y, q.y), std::max(info.hi.z, q.z)};
    }
  }
}

BakedCity::UsagePlan* BakedCity::plan(Library& lib, int usageIndex) {
  if (lib.plans.empty()) lib.plans.assign(lib.programs.manifest["usages"].size(), {});
  if (usageIndex < 0 || usageIndex >= int(lib.plans.size())) return nullptr;
  UsagePlan& p = lib.plans[usageIndex];
  if (p.ready) return &p;
  const auto& usage = lib.programs.manifest["usages"][usageIndex];
  p.shader = lib.programs.get(usage["id"].asString()); if (!p.shader) return nullptr;
  Shader& sh = *p.shader; sh.use();
  p.state = &usage["renderState"];
  p.uniforms.build(sh, usage["uniformValues"]);
  p.modelMatrix = sh.loc("modelMatrix"); p.modelViewMatrix = sh.loc("modelViewMatrix");
  p.projectionMatrix = sh.loc("projectionMatrix"); p.viewMatrix = sh.loc("viewMatrix"); p.normalMatrix = sh.loc("normalMatrix");
  p.cameraPosition = sh.loc("cameraPosition"); p.isOrthographic = sh.loc("isOrthographic"); p.receiveShadow = sh.loc("receiveShadow");
  p.shadowMatrix = sh.loc("directionalShadowMatrix[0]"); p.csmParams = sh.loc("csmData.params"); p.lodFrame = sh.loc("uLodFrame");
  p.lightDirection = sh.loc("directionalLights[0].direction"); p.uTime = sh.loc("uTime"); p.time = sh.loc("time"); p.uSun = sh.loc("uSun");
  for (int j = 0; j < UsagePlan::MAX_CASCADES; j++) {
    const std::string prefix = "directionalLightShadows[" + std::to_string(j) + "].";
    p.shadowBias[j] = sh.loc((prefix + "shadowBias").c_str()); p.shadowNormalBias[j] = sh.loc((prefix + "shadowNormalBias").c_str());
    p.shadowRadius[j] = sh.loc((prefix + "shadowRadius").c_str()); p.shadowIntensity[j] = sh.loc((prefix + "shadowIntensity").c_str());
    p.shadowMapSize[j] = sh.loc((prefix + "shadowMapSize").c_str());
  }
  // samplers take consecutive units in active-uniform order; the unit assignment is fixed per program
  const auto& tex = lib.textures["bindings"][usageIndex]["uniforms"];
  GLint uniforms = 0; glGetProgramiv(sh.id, GL_ACTIVE_UNIFORMS, &uniforms);
  int unit = 0;
  for (GLint u = 0; u < uniforms; u++) {
    char name[256]; GLsizei len; GLint size; GLenum type;
    glGetActiveUniform(sh.id, u, sizeof(name), &len, &size, &type, name);
    GLenum target = 0;
    if (type == GL_SAMPLER_2D || type == GL_SAMPLER_2D_SHADOW) target = GL_TEXTURE_2D;
    else if (type == GL_SAMPLER_2D_ARRAY) target = GL_TEXTURE_2D_ARRAY;
    else if (type == GL_SAMPLER_3D) target = GL_TEXTURE_3D;
    else if (type == GL_SAMPLER_CUBE) target = GL_TEXTURE_CUBE_MAP;
    if (!target) continue;
    TextureSlot t; t.name = name; t.target = target; t.size = size; t.shadowSampler = type == GL_SAMPLER_2D_SHADOW;
    t.env = t.name == "envMap" && target == GL_TEXTURE_2D;
    t.shadowMaps = t.shadowSampler && t.name == "directionalShadowMap[0]";
    if (tex.isMember(name)) t.textureId = tex[name].asInt();
    std::vector<GLint> units(size); for (auto& v : units) v = unit++;
    glUniform1iv(sh.loc(name), size, units.data());
    p.textures.push_back(std::move(t));
  }
  p.ready = true;
  return &p;
}

void BakedCity::beginPass(const Camera& camera, const Mat4& view, const Mat4& proj, float time, int cascade) {
  PassState& ps = pass_;
  ps.shadow = cascade >= 0; ps.cascade = cascade; ps.view = view; ps.proj = proj; ps.time = time;
  ps.cascades = int(csm_.cascades.size()); ps.lights = ps.cascades + (charCascade_ ? 1 : 0);
  ps.lodFrame = float(std::fmod((csm_.frame % 64) * 0.618034, 1.0));
  ps.lightDirection = view.transformDir(sunDirection_);
  ps.eye = cascade >= 0 ? (cascade < ps.cascades ? csm_.cascades[cascade].position : charLight_.position) : camera.position;
  if (!ps.shadow) {
    for (int j = 0; j < ps.cascades; j++) ps.shadowMatrices[j] = csm_.cascades[j].matrix;
    if (charCascade_) {
      if (charMap_) ps.shadowMatrices[ps.cascades] = charLight_.matrix;
      else { // no player model: move the character cascade outside coverage
        Mat4 outside; outside.m[12] = outside.m[13] = outside.m[14] = 2; ps.shadowMatrices[ps.cascades] = outside;
      }
    }
  }
}

// Every uniform and sampler of one captured program for one object (three.js WebGLRenderer.setProgram equivalent).
bool BakedCity::setupDraw(Library& lib, UsagePlan& p, const Mat4& model, bool receiveShadow) {
  const PassState& ps = pass_;
  Shader& sh = *p.shader;
  sh.use(); p.uniforms.upload();
  const Mat4 mv = ps.view * model, inv = mv.inverse(); float normal[9];
  for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++) normal[c * 3 + r] = inv.at(c, r);
  glUniformMatrix4fv(p.modelMatrix, 1, GL_FALSE, model.m); glUniformMatrix4fv(p.modelViewMatrix, 1, GL_FALSE, mv.m);
  glUniformMatrix4fv(p.projectionMatrix, 1, GL_FALSE, ps.proj.m); glUniformMatrix4fv(p.viewMatrix, 1, GL_FALSE, ps.view.m);
  glUniformMatrix3fv(p.normalMatrix, 1, GL_FALSE, normal);
  glUniform3f(p.cameraPosition, ps.eye.x, ps.eye.y, ps.eye.z); glUniform1i(p.isOrthographic, ps.shadow ? 1 : 0);
  glUniform1i(p.receiveShadow, receiveShadow ? 1 : 0);
  if (!ps.shadow) {
    for (int j = 0; j < ps.cascades + (charMap_ ? 1 : 0); j++) {
      const BakedCascade& c = j < ps.cascades ? csm_.cascades[j] : charLight_;
      glUniform1f(p.shadowBias[j], c.bias); glUniform1f(p.shadowNormalBias[j], c.normalBias);
      glUniform1f(p.shadowRadius[j], c.radius); glUniform1f(p.shadowIntensity[j], 1.f);
      glUniform2f(p.shadowMapSize[j], float(c.size), float(c.size));
    }
    glUniformMatrix4fv(p.shadowMatrix, ps.lights, GL_FALSE, ps.shadowMatrices[0].m);
    glUniform4f(p.csmParams, ps.lodFrame, 0.06f, 1.f, 0.f);
    glUniform4f(p.lodFrame, ps.lodFrame, 0.06f, 1.f, 0.f);
  }
  glUniform3f(p.lightDirection, ps.lightDirection.x, ps.lightDirection.y, ps.lightDirection.z);
  glUniform1f(p.uTime, ps.time); glUniform1f(p.time, ps.time);
  glUniform3f(p.uSun, sunDirection_.x, sunDirection_.y, sunDirection_.z);
  applyState(*p.state);
  int unit = 0;
  for (const TextureSlot& ts : p.textures) {
    for (int element = 0; element < ts.size; element++) {
      glActiveTexture(GL_TEXTURE0 + unit++);
      if (ts.env) { glBindTexture(ts.target, environment_.environment()); continue; }
      if (ts.name == "boneTexture") { glBindTexture(ts.target, boneTex_); continue; }
      Texture* t = ts.textureId >= 0 ? texture(lib, ts.textureId) : nullptr;
      if (!t && ts.textureId >= 0 && !lib.textures["textures"][ts.textureId]["file"].isNull()) {
        std::fprintf(stderr, "[baked-city] cannot upload texture %d (%s)\n", ts.textureId, ts.name.c_str()); return false;
      }
      if (ts.shadowMaps) {
        if (element < int(shadowMaps_.size())) t = &shadowMaps_[element];
        else if (element == int(shadowMaps_.size()) && charMap_) { glBindTexture(ts.target, charMap_); continue; }
      }
      if (!t) t = &placeholder(ts.target, ts.shadowSampler);
      if (t->target != ts.target) return false;
      glBindTexture(ts.target, t->id);
    }
  }
  return true;
}

bool BakedCity::renderPass(const Camera& camera, const Mat4& view, const Mat4& proj, const Frustum& frustum, float time, int cascade) {
  if (info_.empty()) prepare();
  const bool shadow = cascade >= 0;
  const auto& allBindings = shadow ? depthBindings_ : bindings_;
  static std::vector<size_t> visible; visible.clear();
  for (size_t i = 0; i < info_.size(); i++) {
    const MeshInfo& m = info_[i];
    if (m.skip) continue;
    const uint32_t layers = shadow ? m.shadowLayers : m.layers;
    // render/csm.js camera layers: cascade 2 adds the shadow proxies (29), cascades >= 3 see big casters only (28)
    const uint32_t mask = !shadow || cascade < 2 ? 1u : cascade == 2 ? 1u | (1u << 29) : 1u << 28;
    if (!(layers & mask) || (m.instanced && (m.pool ? (shadow ? m.pool->shadowCount : m.pool->count) == 0 : m.instanceCount == 0))) continue;
    if (shadow && (!visibility_.at(i).shadow || cascade < m.minCascade || cascade > m.maxCascade)) continue;
    if (!m.pool && !visibility_.at(i).visible) continue;
    if (!m.instanced && m.frustumCulled && !frustum.visible(m.lo, m.hi)) continue;
    visible.push_back(i);
  }
  std::stable_sort(visible.begin(), visible.end(), [&](size_t a, size_t b) {
    const MeshInfo& x = info_[a]; const MeshInfo& y = info_[b];
    if (x.transparent != y.transparent) return !x.transparent;
    return x.renderOrder < y.renderOrder;
  });
  static const bool checkErrors = std::getenv("SB_GLDEBUG") != nullptr;
  beginPass(camera, view, proj, time, cascade);
  for (size_t i : visible) {
    const MeshInfo& m = info_[i];
    for (size_t slot = 0; slot < allBindings[i].size(); slot++) {
      UsagePlan* p = plan(city_, allBindings[i][slot]); if (!p) return false;
      Shader& sh = *p->shader;
      if (!setupDraw(city_, *p, m.model, m.receiveShadow)) return false;
      if (!bindMesh(i, sh)) return false;
      auto draw = [&](uint32_t start, uint32_t count) {
        const uint32_t last = m.first + m.count;
        uint32_t end = std::min(start + count, last); start = std::max(start, m.first); if (end <= start) return;
        GLsizei n = end - start, instances = m.pool ? (shadow ? m.pool->shadowCount : m.pool->count) : m.instanceCount;
        const bool instanced = m.instanced || m.geometryInstances > 0;
        if (!m.instanced) instances = m.geometryInstances;
        if (m.hasIndex) {
          void* offset = (void*)(uintptr_t(start) * typeBytes(m.indexType));
          if (instanced) glDrawElementsInstanced(GL_TRIANGLES, n, m.indexType, offset, instances);
          else glDrawElements(GL_TRIANGLES, n, m.indexType, offset);
        } else if (instanced) glDrawArraysInstanced(GL_TRIANGLES, start, n, instances);
        else glDrawArrays(GL_TRIANGLES, start, n);
        if (shadow) shadowDrawn++;
        else { drawn++; triangles += uint64_t(n / 3) * (instanced ? instances : 1); }
      };
      if (m.groups.empty()) draw(m.first, m.count);
      else for (const auto& g : m.groups) if (g.slot == slot) draw(g.start, g.count);
      if (checkErrors) {
        GLenum error = glGetError();
        if (error) { std::fprintf(stderr, "[baked-city] GL 0x%x at mesh %zu (%s)\n", error, i, m.name.c_str()); return false; }
      }
    }
  }
  glDepthMask(GL_TRUE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glEnable(GL_DEPTH_TEST);
  glDisable(GL_POLYGON_OFFSET_FILL); glDisable(GL_BLEND); glActiveTexture(GL_TEXTURE0);
  return true;
}

void BakedCity::evict() {
  // Removing a buffer requires invalidating every VAO that references it.
  for (auto it = buffers_.begin(); it != buffers_.end();) {
    if (it->second.frame + 120 >= frame_ && residentBytes < 512ull * 1024 * 1024) { ++it; continue; }
    if (it->second.frame == frame_) { ++it; continue; }
    for (auto mesh = meshes_.begin(); mesh != meshes_.end();) {
      if (std::find(mesh->second.blobs.begin(), mesh->second.blobs.end(), it->first) == mesh->second.blobs.end()) { ++mesh; continue; }
      glDeleteVertexArrays(1, &mesh->second.vao); mesh = meshes_.erase(mesh);
    }
    residentBytes -= it->second.bytes; glDeleteBuffers(1, &it->second.id); it = buffers_.erase(it);
  }
}

void BakedCity::clear() {
  for (auto& m : meshes_) glDeleteVertexArrays(1, &m.second.vao);
  for (auto& b : buffers_) glDeleteBuffers(1, &b.second.id);
  for (Library* lib : {&city_, &char_}) {
    for (auto& t : lib->gpu) glDeleteTextures(1, &t.second.id);
    lib->gpu.clear(); lib->plans.clear(); lib->programs.clear(); lib->textures.clear(); lib->directory.clear();
  }
  clearCharacter();
  for (auto& t : placeholders_) glDeleteTextures(1, &t.second.id);
  for (auto& t : shadowMaps_) glDeleteTextures(1, &t.id);
  if (shadowFbo_) glDeleteFramebuffers(1, &shadowFbo_);
  shadowFbo_ = 0; shadowMaps_.clear();
  placeholders_.clear();
  meshes_.clear(); buffers_.clear(); bindings_.clear(); depthBindings_.clear();
  pools_.clear();
  environment_.clear();
  residentBytes = 0; frame_ = 0;
  info_.clear();
}
