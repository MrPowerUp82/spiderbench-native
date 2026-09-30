#pragma once
#include "world/baked_geometry.h"
#include "gfx/gl.h"
#include "gfx/camera.h"
#include <unordered_map>

struct BakedPool {
  Json::Value meta;
  struct Attribute { int size = 0; std::vector<float> data; GLuint gpu = 0; };
  std::unordered_map<std::string, Attribute> attributes;
  std::vector<double> positions;
  std::unordered_map<int64_t, std::vector<uint32_t>> grid;
  std::vector<uint32_t> selection;
  Vec3 last{1e9f, 0, 0};
  double vx = 0, vz = 0;
  bool wedgeOn = false, written = false;
  uint32_t count = 0, shadowCount = 0;
};

// Pool selection and upload follow world/pool.js. Stored transforms were
// composed by Three.js and rounded to Float32 exactly as setMatrixAt does.
class BakedPools {
 public:
  bool open(const std::string& manifest, size_t meshCount);
  bool update(const Camera& camera);
  BakedPool* forMesh(size_t index);
  const std::vector<BakedPool>& entries() const { return pools_; }
  void clear();
 private:
  BakedGeometryArchive archive_;
  std::vector<BakedPool> pools_;
  std::vector<int> byMesh_;
  bool upload(BakedPool& pool);
};

bool validateBakedPools(const std::string& directory);
