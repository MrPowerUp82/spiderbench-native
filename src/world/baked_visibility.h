#pragma once
#include "gfx/camera.h"
#include <json/json.h>
#include <string>
#include <unordered_map>
#include <vector>

class BakedVisibility {
 public:
  struct State { bool visible = false, shadow = false; };
  // lodScale: render/quality.js lodScale() of the baked preset (tile LOD / detail / roof distances scale with it)
  bool open(const Json::Value& meshes, const Json::Value& objects, double lodScale = 1);
  void update(const Vec3& position);
  const State& at(size_t i) const { return states_[i]; }
 private:
  struct Item { std::string kind, group; double x = 0, z = 0; bool managed = false; };
  std::vector<Item> items_;
  std::vector<State> states_;
  std::unordered_map<std::string, bool> near_;
  double lk_ = 1;
};
bool validateBakedVisibility(const std::string& directory, const std::string& shaderDirectory);
