#pragma once
#include "gfx/camera.h"
#include <json/json.h>
#include <vector>

struct BakedCascade {
  Mat4 view, projection, cullProjection, matrix;
  Vec3 position;
  int size = 0;
  float bias = 0, normalBias = 0, radius = 0;
  bool due = false;
};

// Same fitting, texel snapping and staggered updates as render/csm.js.
class BakedCSM {
 public:
  bool configure(const Json::Value& config, const Vec3& sunDirection);
  void update(const Camera& camera);
  std::vector<BakedCascade> cascades;
  uint64_t frame = 0;
 private:
  std::vector<double> splits_;
  Vec3 sun_;
  int size_ = 0;
};
bool validateBakedCSM(const std::string& directory);
