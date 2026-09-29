#include "asset/gltf.h"
#include <json/json.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {
struct Ctx {
  Json::Value j;
  std::vector<uint8_t> bin;
};

int compCount(const std::string& type) {
  if (type == "SCALAR") return 1; if (type == "VEC2") return 2; if (type == "VEC3") return 3;
  if (type == "VEC4") return 4; if (type == "MAT4") return 16; return 0;
}
int compSize(int ct) { return ct == 5126 || ct == 5125 ? 4 : (ct == 5123 || ct == 5122 ? 2 : 1); }

float readComp(const uint8_t* p, int ct, bool normalized) {
  switch (ct) {
    case 5126: { float f; std::memcpy(&f, p, 4); return f; }
    case 5121: return normalized ? *p / 255.f : (float)*p;
    case 5120: return normalized ? std::max(*(const int8_t*)p / 127.f, -1.f) : (float)*(const int8_t*)p;
    case 5123: { uint16_t v; std::memcpy(&v, p, 2); return normalized ? v / 65535.f : (float)v; }
    case 5122: { int16_t v; std::memcpy(&v, p, 2); return normalized ? std::max(v / 32767.f, -1.f) : (float)v; }
    case 5125: { uint32_t v; std::memcpy(&v, p, 4); return (float)v; }
  }
  return 0;
}

// read accessor as floats (count * comps)
std::vector<float> readFloats(const Ctx& c, int ai, int* compsOut = nullptr) {
  const Json::Value& a = c.j["accessors"][ai];
  int comps = compCount(a["type"].asString()), ct = a["componentType"].asInt(), count = a["count"].asInt();
  bool norm = a.get("normalized", false).asBool();
  if (compsOut) *compsOut = comps;
  std::vector<float> out((size_t)count * comps, 0.f);
  if (!a.isMember("bufferView")) return out;
  const Json::Value& bv = c.j["bufferViews"][a["bufferView"].asInt()];
  size_t off = bv.get("byteOffset", 0).asUInt() + a.get("byteOffset", 0).asUInt();
  int stride = bv.get("byteStride", 0).asInt(); if (!stride) stride = comps * compSize(ct);
  for (int i = 0; i < count; i++) for (int k = 0; k < comps; k++)
    out[(size_t)i * comps + k] = readComp(c.bin.data() + off + (size_t)i * stride + k * compSize(ct), ct, norm);
  return out;
}

std::vector<uint32_t> readUints(const Ctx& c, int ai) {
  const Json::Value& a = c.j["accessors"][ai];
  int comps = compCount(a["type"].asString()), ct = a["componentType"].asInt(), count = a["count"].asInt();
  std::vector<uint32_t> out((size_t)count * comps);
  const Json::Value& bv = c.j["bufferViews"][a["bufferView"].asInt()];
  size_t off = bv.get("byteOffset", 0).asUInt() + a.get("byteOffset", 0).asUInt();
  int stride = bv.get("byteStride", 0).asInt(); if (!stride) stride = comps * compSize(ct);
  for (int i = 0; i < count; i++) for (int k = 0; k < comps; k++) {
    const uint8_t* p = c.bin.data() + off + (size_t)i * stride + k * compSize(ct);
    uint32_t v = 0;
    if (ct == 5121) v = *p; else if (ct == 5123) { uint16_t s; std::memcpy(&s, p, 2); v = s; } else std::memcpy(&v, p, 4);
    out[(size_t)i * comps + k] = v;
  }
  return out;
}

int texImage(const Json::Value& j, const Json::Value& texRef) {
  if (!texRef.isObject() || !texRef.isMember("index")) return -1;
  const Json::Value& t = j["textures"][texRef["index"].asInt()];
  if (t.isMember("source")) return t["source"].asInt();
  const Json::Value& ext = t["extensions"];
  for (const char* e : {"EXT_texture_webp", "KHR_texture_basisu", "EXT_texture_avif"})
    if (ext.isMember(e)) return ext[e]["source"].asInt();
  return -1;
}
}  // namespace

bool loadGlb(const std::string& path, GltfModel& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { std::fprintf(stderr, "[gltf] cannot open %s\n", path.c_str()); return false; }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (data.size() < 20 || std::memcmp(data.data(), "glTF", 4) != 0) { std::fprintf(stderr, "[gltf] not a GLB %s\n", path.c_str()); return false; }
  uint32_t jlen; std::memcpy(&jlen, data.data() + 12, 4);
  Ctx c;
  {
    std::string js((const char*)data.data() + 20, jlen);
    Json::CharReaderBuilder b; std::string err;
    std::istringstream ss(js);
    if (!Json::parseFromStream(b, ss, &c.j, &err)) { std::fprintf(stderr, "[gltf] json: %s\n", err.c_str()); return false; }
    size_t off = 20 + jlen;
    if (off + 8 <= data.size()) { uint32_t blen; std::memcpy(&blen, data.data() + off, 4); c.bin.assign(data.begin() + off + 8, data.begin() + off + 8 + blen); }
  }
  const Json::Value& j = c.j;
  out.imageCount = (int)j["images"].size();

  // nodes
  out.nodes.resize(j["nodes"].size());
  for (Json::ArrayIndex i = 0; i < j["nodes"].size(); i++) {
    const Json::Value& n = j["nodes"][i]; GltfNode& N = out.nodes[i];
    N.name = n.get("name", "").asString();
    if (n.isMember("translation")) N.t = {n["translation"][0].asFloat(), n["translation"][1].asFloat(), n["translation"][2].asFloat()};
    if (n.isMember("rotation")) N.r = {n["rotation"][0].asFloat(), n["rotation"][1].asFloat(), n["rotation"][2].asFloat(), n["rotation"][3].asFloat()};
    if (n.isMember("scale")) N.s = {n["scale"][0].asFloat(), n["scale"][1].asFloat(), n["scale"][2].asFloat()};
    if (n.isMember("matrix")) { // decompose (rare in Blender exports)
      Mat4 m; for (int k = 0; k < 16; k++) m.m[k] = n["matrix"][k].asFloat();
      N.t = m.position(); N.s = {Vec3{m.m[0], m.m[1], m.m[2]}.length(), Vec3{m.m[4], m.m[5], m.m[6]}.length(), Vec3{m.m[8], m.m[9], m.m[10]}.length()};
      N.r = m.rotation();
    }
    N.mesh = n.get("mesh", -1).asInt(); N.skin = n.get("skin", -1).asInt();
    for (const auto& ch : n["children"]) N.children.push_back(ch.asInt());
  }
  for (size_t i = 0; i < out.nodes.size(); i++) for (int ch : out.nodes[i].children) out.nodes[ch].parent = (int)i;
  int scene = j.get("scene", 0).asInt();
  if (j["scenes"].size()) for (const auto& r : j["scenes"][scene]["nodes"]) out.roots.push_back(r.asInt());
  else for (size_t i = 0; i < out.nodes.size(); i++) if (out.nodes[i].parent < 0) out.roots.push_back((int)i);

  // materials
  for (const auto& m : j["materials"]) {
    GltfMaterial M; M.name = m.get("name", "").asString();
    const Json::Value& pbr = m["pbrMetallicRoughness"];
    if (pbr.isMember("baseColorFactor")) for (int k = 0; k < 4; k++) M.baseColor[k] = pbr["baseColorFactor"][k].asFloat();
    M.metallic = pbr.get("metallicFactor", 1.0).asFloat(); M.roughness = pbr.get("roughnessFactor", 1.0).asFloat();
    M.baseTex = texImage(j, pbr["baseColorTexture"]); M.mrTex = texImage(j, pbr["metallicRoughnessTexture"]);
    M.normalTex = texImage(j, m["normalTexture"]); M.occlTex = texImage(j, m["occlusionTexture"]);
    M.doubleSided = m.get("doubleSided", false).asBool();
    out.materials.push_back(M);
  }

  // meshes
  for (const auto& m : j["meshes"]) {
    GltfMesh M; M.name = m.get("name", "").asString();
    for (const auto& p : m["primitives"]) {
      if (p.get("mode", 4).asInt() != 4) continue;
      GltfPrimitive P; const Json::Value& at = p["attributes"];
      P.pos = readFloats(c, at["POSITION"].asInt());
      size_t n = P.pos.size() / 3;
      P.nrm = at.isMember("NORMAL") ? readFloats(c, at["NORMAL"].asInt()) : std::vector<float>(n * 3, 0.f);
      P.uv = at.isMember("TEXCOORD_0") ? readFloats(c, at["TEXCOORD_0"].asInt()) : std::vector<float>(n * 2, 0.f);
      if (at.isMember("JOINTS_0")) { auto ju = readUints(c, at["JOINTS_0"].asInt()); P.joints.assign(ju.begin(), ju.end()); }
      else P.joints.assign(n * 4, 0);
      P.weights = at.isMember("WEIGHTS_0") ? readFloats(c, at["WEIGHTS_0"].asInt()) : std::vector<float>(n * 4, 0.f);
      if (!at.isMember("WEIGHTS_0")) for (size_t v = 0; v < n; v++) P.weights[v * 4] = 1;
      if (p.isMember("indices")) P.idx = readUints(c, p["indices"].asInt());
      else { P.idx.resize(n); for (size_t v = 0; v < n; v++) P.idx[v] = (uint32_t)v; }
      P.material = p.get("material", -1).asInt();
      M.prims.push_back(std::move(P));
    }
    out.meshes.push_back(std::move(M));
  }

  // skins
  for (const auto& s : j["skins"]) {
    GltfSkin S;
    for (const auto& jn : s["joints"]) S.joints.push_back(jn.asInt());
    if (s.isMember("inverseBindMatrices")) {
      auto f = readFloats(c, s["inverseBindMatrices"].asInt());
      for (size_t k = 0; k < S.joints.size(); k++) { Mat4 m; std::memcpy(m.m, &f[k * 16], 64); S.inverseBind.push_back(m); }
    } else S.inverseBind.assign(S.joints.size(), Mat4());
    out.skins.push_back(std::move(S));
  }

  // animations
  for (const auto& a : j["animations"]) {
    GltfClip C; C.name = a.get("name", "").asString();
    for (const auto& ch : a["channels"]) {
      const Json::Value& smp = a["samplers"][ch["sampler"].asInt()];
      std::string path = ch["target"]["path"].asString();
      if (path != "translation" && path != "rotation" && path != "scale") continue;
      GltfChannel K; K.node = ch["target"]["node"].asInt();
      K.path = path == "translation" ? 0 : path == "rotation" ? 1 : 2;
      K.step = smp.get("interpolation", "LINEAR").asString() == "STEP";
      K.times = readFloats(c, smp["input"].asInt());
      K.values = readFloats(c, smp["output"].asInt());
      if (smp.get("interpolation", "").asString() == "CUBICSPLINE") { // keep only the value (middle) of each in/value/out triplet
        int w = K.path == 1 ? 4 : 3; std::vector<float> v; v.reserve(K.times.size() * w);
        for (size_t k = 0; k < K.times.size(); k++) for (int q = 0; q < w; q++) v.push_back(K.values[(k * 3 + 1) * w + q]);
        K.values.swap(v);
      }
      if (!K.times.empty()) C.duration = std::max(C.duration, K.times.back());
      C.channels.push_back(std::move(K));
    }
    out.clips.push_back(std::move(C));
  }
  return true;
}
