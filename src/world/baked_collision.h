#pragma once
#include "core/math.h"
#include <cstdint>
#include <functional>
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
struct BakedCast { float t = 0; int id = -1; Vec3 n; };

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
  // collision.js CollisionGrid.cast: nearest solid along a normalized ray (origins inside a solid are ignored)
  bool cast(const Vec3& o, const Vec3& d, float tMax, BakedCast& out) const;
  // every solid whose XZ bounds overlap the rectangle (each reported once)
  void query(float x0, float z0, float x1, float z1, const std::function<void(uint32_t)>& fn) const;
  Vec3 topNormal(uint32_t i, float x, float z) const;

 private:
  mutable std::vector<uint32_t> stamp_;
  mutable uint32_t frame_ = 0;
  uint32_t nextFrame() const;
  float rayBox(uint32_t i, const Vec3& o, const Vec3& d, float tMax, Vec3& n) const;
  float rayRamp(uint32_t i, const Vec3& o, const Vec3& d, float tMax, Vec3& n) const;
  float rayHF(uint32_t i, const Vec3& o, const Vec3& d, float tMax, Vec3& n) const;
  float rayCyl(uint32_t i, const Vec3& o, const Vec3& d, float tMax, Vec3& n) const;
};
