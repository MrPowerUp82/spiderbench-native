#pragma once
#include <json/json.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Random-access reader for the scene buffers emitted by bake_geometry.mjs.
// Keeps only the manifest resident; vertex/index buffers inflate on request.
class BakedGeometryArchive {
 public:
  bool open(const std::string& manifestPath);
  size_t meshCount() const { return root_["meshes"].size(); }
  const Json::Value& meshes() const { return root_["meshes"]; }
  size_t materialCount() const { return root_["materials"].size(); }
  const Json::Value& mesh(size_t i) const { return root_["meshes"][Json::ArrayIndex(i)]; }
  const Json::Value& material(size_t i) const { return root_["materials"][Json::ArrayIndex(i)]; }
  const Json::Value& blob(size_t i) const { return root_["blobs"][Json::ArrayIndex(i)]; }
  bool readBlob(uint32_t id, std::vector<uint8_t>& out) const;

 private:
  Json::Value root_;
  std::string archivePath_;
};
