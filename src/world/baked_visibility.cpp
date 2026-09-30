#include "world/baked_visibility.h"
#include <filesystem>
#include <fstream>
#include <cstdio>

bool BakedVisibility::open(const Json::Value& meshes, const Json::Value& objects) {
  items_.clear(); states_.clear(); near_.clear();
  if (meshes.size() != objects.size()) return false;
  for (Json::ArrayIndex i = 0; i < meshes.size(); i++) {
    const auto& mesh = meshes[i]; const auto& center = objects[i]["tileCenter"];
    const std::string name = mesh["name"].asString(); size_t space = name.find(' ');
    Item item; item.kind = name.substr(0, space);
    item.managed = center.isArray() && center.size() == 2 && !name.ends_with(" super");
    if (item.managed) {
      item.x = center[0].asDouble(); item.z = center[1].asDouble();
      const bool roof = item.kind == "roofs" || item.kind == "roofAO" || item.kind == "roofStreaks";
      item.group = std::string(roof ? "roof " : "city ") + name.substr(space == std::string::npos ? name.size() : space + 1);
      if (item.kind == "facadeLod" || item.kind == "roofs") near_[item.group] = true;
    }
    states_.push_back({mesh["visible"].asBool(), mesh["castShadow"].asBool()}); items_.push_back(item);
  }
  return true;
}

void BakedVisibility::update(const Vec3& position) {
  // Update each hysteresis state once, then share it with the matching facade LOD.
  for (const auto& item : items_) if (item.managed && (item.kind == "facadeLod" || item.kind == "roofs")) {
    const double d = std::hypot(std::max(0.0, std::abs(position.x - item.x) - 128), std::max(0.0, std::abs(position.z - item.z) - 128));
    bool& close = near_.at(item.group); close = d < (close ? 690 : 650);
  }
  for (size_t i = 0; i < items_.size(); i++) {
    const auto& item = items_[i]; if (!item.managed) continue;
    const bool sign = item.kind == "signage" || item.kind == "signageGhost";
    const double half = sign ? 256 : 128;
    const double d = std::hypot(std::max(0.0, std::abs(position.x - item.x) - half), std::max(0.0, std::abs(position.z - item.z) - half));
    auto& state = states_[i];
    if (item.kind == "facade") { if (near_.contains(item.group)) state.visible = near_.at(item.group); }
    else if (item.kind == "facadeLod") state.visible = !near_.at(item.group);
    else if (item.kind == "detail") { state.visible = d < 450; state.shadow = d < 160; }
    else if (item.kind == "roofs") { state.visible = near_.at(item.group); state.shadow = d < 230; }
    else if (item.kind == "roofAO") state.visible = d < 520;
    else if (item.kind == "roofStreaks") state.visible = d < 690;
    else if (item.kind == "signage") { state.visible = d < 850; state.shadow = d < 120; }
    else if (item.kind == "signageGhost") state.visible = d < 520;
  }
}

bool validateBakedVisibility(const std::string& directory, const std::string& shaderDirectory) {
  const auto read = [](const std::filesystem::path& path, Json::Value& value) {
    std::ifstream file(path); Json::CharReaderBuilder builder; std::string error;
    return file && Json::parseFromStream(builder, file, &value, &error);
  };
  Json::Value geometry, shaders, reference;
  if (!read(std::filesystem::path(directory) / "geometry.json", geometry) || !read(std::filesystem::path(shaderDirectory) / "manifest.json", shaders) ||
      !read(std::filesystem::path(directory) / "tile-queries.json", reference) || reference["format"].asString() != "SBTILECHECK1") return false;
  BakedVisibility visibility; if (!visibility.open(geometry["meshes"], shaders["shadowObjects"])) return false;
  size_t count = 0;
  for (const auto& frame : reference["frames"]) {
    const auto& p = frame["position"]; visibility.update({p[0].asFloat(), p[1].asFloat(), p[2].asFloat()});
    for (const auto& item : frame["states"]) {
      const auto& actual = visibility.at(item["objectOrdinal"].asUInt());
      if (actual.visible != item["visible"].asBool() || actual.shadow != item["shadow"].asBool()) {
        std::fprintf(stderr, "[baked-tiles] mismatch at sample %zu mesh %u: native %d/%d JS %d/%d\n", count, item["objectOrdinal"].asUInt(), actual.visible, actual.shadow, item["visible"].asBool(), item["shadow"].asBool()); return false;
      }
      count++;
    }
  }
  std::printf("[baked-tiles] %zu visibility and shadow states match the original JS\n", count); return true;
}
