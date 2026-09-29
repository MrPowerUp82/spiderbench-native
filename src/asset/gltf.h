// Minimal glTF 2.0 binary (.glb) loader: node hierarchy, skinned triangle meshes, PBR materials, skins, animation clips.
// Images are not decoded here: tools/convert_assets.py writes each embedded image as <name>_img<N>.tex.
#pragma once
#include "core/math.h"
#include <string>
#include <vector>
#include <cstdint>

struct GltfNode {
  std::string name;
  int parent = -1;
  std::vector<int> children;
  Vec3 t; Quat r; Vec3 s{1, 1, 1};
  int mesh = -1, skin = -1;
};

struct GltfPrimitive {
  std::vector<float> pos, nrm, uv;   // 3, 3, 2 per vertex
  std::vector<uint8_t> joints;        // 4 per vertex
  std::vector<float> weights;         // 4 per vertex
  std::vector<uint32_t> idx;
  int material = -1;
  size_t vertexCount() const { return pos.size() / 3; }
};

struct GltfMesh { std::string name; std::vector<GltfPrimitive> prims; };

struct GltfMaterial {
  std::string name;
  float baseColor[4] = {1, 1, 1, 1};
  float metallic = 1, roughness = 1;
  int baseTex = -1, mrTex = -1, normalTex = -1, occlTex = -1; // image indices
  bool doubleSided = false;
};

struct GltfSkin { std::vector<int> joints; std::vector<Mat4> inverseBind; };

struct GltfChannel {
  int node = -1;
  int path = 0; // 0 translation, 1 rotation, 2 scale
  bool step = false;
  std::vector<float> times, values; // values: 3 or 4 floats per key
};

struct GltfClip { std::string name; float duration = 0; std::vector<GltfChannel> channels; };

struct GltfModel {
  std::vector<GltfNode> nodes;
  std::vector<GltfMesh> meshes;
  std::vector<GltfMaterial> materials;
  std::vector<GltfSkin> skins;
  std::vector<GltfClip> clips;
  std::vector<int> roots;
  int imageCount = 0;
  int findNode(const std::string& n) const { for (size_t i = 0; i < nodes.size(); i++) if (nodes[i].name == n) return (int)i; return -1; }
  int findClip(const std::string& n) const { for (size_t i = 0; i < clips.size(); i++) if (clips[i].name == n) return (int)i; return -1; }
};

bool loadGlb(const std::string& path, GltfModel& out);
