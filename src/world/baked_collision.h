#pragma once
#include "core/math.h"
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// Exact numeric data emitted by tools/ref/bake_collision.mjs from the original
// CollisionGrid. Separate from the current procedural World until rendering and
// all traversal queries use the baked scene together.
struct BakedField {
  uint32_t nx = 0, nz = 0;
  float cell = 0, hMax = 0, loMin = 0;
  std::vector<float> h, lo;
};
struct BakedZip { Vec3 pos, normal; uint8_t kind = 0; };
struct BakedBox { Vec3 mn, mx; };
struct BakedTop { float y = -std::numeric_limits<float>::infinity(); int id = -1; };

class BakedCollision {
 public:
  uint32_t n = 0, nx = 0, nz = 0;
  float cell = 0, ox = 0, oz = 0;
  Vec3 spawn;
  std::vector<uint8_t> type, flags, kind;
  std::vector<float> bb, par;
  std::vector<uint32_t> start, items;
  std::vector<BakedField> fields;
  std::vector<BakedZip> zips;
  std::vector<BakedBox> boxes;

  bool load(const std::string& path);
  float top(uint32_t i, float x, float z) const;
  BakedTop topAt(float x, float z, float yMax = std::numeric_limits<float>::infinity(), bool skipOverhang = false) const;
  bool inside(float x, float y, float z) const;
};
