#include "gfx/baked_programs.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <cstdio>

namespace {
std::string desktopSource(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  std::ostringstream text; text << file.rdbuf();
  std::string src = text.str();
  // Shader::build supplies #version 330 core. Keep all original defines.
  src = std::regex_replace(src, std::regex(R"(^\s*#version[^\n]*\n)"), "");
  src = std::regex_replace(src, std::regex(R"(\bprecision\s+(highp|mediump|lowp)\s+\w+\s*;)"), "");
  return std::regex_replace(src, std::regex(R"(\b(highp|mediump|lowp)\b)"), "");
}
}

bool BakedPrograms::open(const std::string& directory) {
  std::ifstream file(std::filesystem::path(directory) / "manifest.json");
  Json::CharReaderBuilder builder; std::string error;
  Json::Value root;
  if (!file || !Json::parseFromStream(builder, file, &root, &error) ||
      !root["entries"].isArray() || !root["usages"].isArray()) return false;
  clear(); manifest = std::move(root); directory_ = directory;
  return true;
}

Shader* BakedPrograms::get(const std::string& id) {
  auto found = programs_.find(id);
  if (found != programs_.end()) return &found->second;
  bool known = false;
  for (const auto& entry : manifest["entries"]) if (entry["id"].asString() == id) known = true;
  if (!known) return nullptr;
  const auto dir = std::filesystem::path(directory_);
  const auto vs = desktopSource(dir / (id + ".vert.glsl"));
  const auto fs = desktopSource(dir / (id + ".frag.glsl"));
  if (vs.empty() || fs.empty()) return nullptr;
  Shader program;
  if (!program.build(("baked-" + id).c_str(), vs, fs)) return nullptr;
  return &programs_.emplace(id, std::move(program)).first->second;
}

bool BakedPrograms::validateAll() {
  size_t passed = 0;
  for (const auto& entry : manifest["entries"]) {
    if (!get(entry["id"].asString())) return false;
    passed++;
    if (passed % 20 == 0) std::printf("[baked-shaders] %zu / %u compiled\n", passed, manifest["entries"].size());
  }
  std::printf("[baked-shaders] %zu programs compiled and linked on the GPU\n", passed);
  return true;
}

void BakedPrograms::clear() {
  for (auto& entry : programs_) glDeleteProgram(entry.second.id);
  programs_.clear(); manifest.clear(); directory_.clear();
}
