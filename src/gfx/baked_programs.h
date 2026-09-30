#pragma once
#include "gfx/shader.h"
#include <json/json.h>
#include <string>
#include <unordered_map>

#include <vector>

// Captured Three.js uniform values resolved once against a linked program (locations + flattened values).
struct CapturedUniforms {
  struct Entry { GLint location = -1; GLenum type = 0; GLsizei count = 0; std::vector<float> values; };
  std::vector<Entry> list;
  void build(Shader& shader, const Json::Value& values);
  void upload() const; // program must be in use
};
void uploadCapturedUniforms(Shader& shader, const Json::Value& values);

// Captured Three.js programs, compiled by the native driver. Conversion only
// changes the GLSL version and ES precision syntax; material patches stay intact.
class BakedPrograms {
 public:
  Json::Value manifest;
  bool open(const std::string& directory);
  Shader* get(const std::string& id);
  bool validateAll();
  void clear(); // call while the GL context is current
 private:
  std::string directory_;
  std::unordered_map<std::string, Shader> programs_;
};
