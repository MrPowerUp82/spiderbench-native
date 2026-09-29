#pragma once
#include "gfx/gl.h"
#include "core/math.h"
#include <string>
#include <unordered_map>

// GLSL 330 program with a cached uniform-location map. `defines` is inserted after the #version line.
class Shader {
 public:
  GLuint id = 0;
  bool build(const char* name, const std::string& vs, const std::string& fs, const std::string& defines = "");
  void use() const { glUseProgram(id); }
  GLint loc(const char* n);
  void set(const char* n, int v) { glUniform1i(loc(n), v); }
  void set(const char* n, float v) { glUniform1f(loc(n), v); }
  void set(const char* n, const Vec2& v) { glUniform2f(loc(n), v.x, v.y); }
  void set(const char* n, const Vec3& v) { glUniform3f(loc(n), v.x, v.y, v.z); }
  void set(const char* n, float x, float y, float z, float w) { glUniform4f(loc(n), x, y, z, w); }
  void set(const char* n, const Mat4& m) { glUniformMatrix4fv(loc(n), 1, GL_FALSE, m.m); }
  void setMats(const char* n, const Mat4* m, int count) { glUniformMatrix4fv(loc(n), count, GL_FALSE, m[0].m); }
 private:
  std::unordered_map<std::string, GLint> locs_;
  std::string name_;
};
