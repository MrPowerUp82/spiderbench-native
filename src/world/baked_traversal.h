#pragma once
#include "core/math.h"
#include <cstdint>
#include <string>
#include <vector>

// Data emitted by tools/ref/bake_traversal.mjs: the original analytic terrain (ground.js terrainHeight) as a
// hierarchical palette raster and the tree swing anchors (anchors.js treePoints).
struct BakedTree { Vec3 pos; float cy = 0, r = 0; };

class BakedTraversal {
 public:
  std::vector<BakedTree> trees;
  bool load(const std::string& path);
  // terrainHeight(x, z): 0.25 m samples inside the collision grid, 16 m outside, open water beyond
  float terrain(float x, float z) const;

 private:
  float fx0_ = 0, fz0_ = 0, fine_ = 0.25f; uint32_t sub_ = 8, top_ = 8, tnx_ = 0, tnz_ = 0;
  float cx0_ = 0, cz0_ = 0, coarseCell_ = 16; uint32_t cnx_ = 0, cnz_ = 0;
  float water_ = -1.6f;
  std::vector<float> palette_;
  std::vector<uint32_t> topTiles_, subTiles_;
  std::vector<uint8_t> blocks_, coarse_;
};

class World;
// Compares World queries over the baked city with the answers of the original JS (traversal_queries.json).
bool validateBakedTraversal(const World& world, const std::string& queries);
