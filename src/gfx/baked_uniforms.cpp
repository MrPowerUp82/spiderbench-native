#include "gfx/baked_programs.h"
#include <vector>
#include <algorithm>

namespace {
void flatten(const Json::Value& value, std::vector<float>& out) {
  if (value.isArray()) for (const auto& v : value) flatten(v, out);
  else if (value.isNumeric() || value.isBool()) out.push_back(value.isBool() ? float(value.asBool()) : value.asFloat());
}
Json::Value uniformAt(const Json::Value& root, const std::string& name) {
  Json::Value value = root; size_t p = 0;
  while (p < name.size()) {
    if (name[p] == '.') { p++; continue; }
    if (name[p] == '[') {
      size_t end = name.find(']', p); if (end == std::string::npos || !value.isArray()) return {};
      int i = std::stoi(name.substr(p + 1, end - p - 1));
      if (i == 0 && end + 1 == name.size()) return value;
      value = value[Json::ArrayIndex(i)]; p = end + 1;
    } else {
      size_t end = name.find_first_of(".[", p);
      const std::string key = name.substr(p, end == std::string::npos ? name.size() - p : end - p);
      if (!value.isObject() || !value.isMember(key)) return {};
      value = value[key]; p = end == std::string::npos ? name.size() : end;
    }
  }
  return value;
}
int components(GLenum type) {
  switch (type) {
    case GL_FLOAT: case GL_BOOL: case GL_INT: return 1;
    case GL_FLOAT_VEC2: return 2;
    case GL_FLOAT_VEC3: return 3;
    case GL_FLOAT_VEC4: return 4;
    case GL_FLOAT_MAT3: return 9;
    case GL_FLOAT_MAT4: return 16;
    default: return 0;
  }
}
}
void uploadCapturedUniforms(Shader& shader, const Json::Value& values) {
  GLint count = 0; glGetProgramiv(shader.id, GL_ACTIVE_UNIFORMS, &count);
  for (GLint i = 0; i < count; i++) {
    char name[256]; GLsizei len; GLint size; GLenum type;
    glGetActiveUniform(shader.id, i, sizeof(name), &len, &size, &type, name);
    int c = components(type); if (!c) continue;
    std::vector<float> v; flatten(uniformAt(values, name), v);
    if (v.empty() || v.size() % c) continue;
    GLsizei n = std::min(size, GLint(v.size() / c)); GLint loc = shader.loc(name);
    switch (type) {
      case GL_FLOAT: glUniform1fv(loc, n, v.data()); break;
      case GL_FLOAT_VEC2: glUniform2fv(loc, n, v.data()); break;
      case GL_FLOAT_VEC3: glUniform3fv(loc, n, v.data()); break;
      case GL_FLOAT_VEC4: glUniform4fv(loc, n, v.data()); break;
      case GL_FLOAT_MAT3: glUniformMatrix3fv(loc, n, GL_FALSE, v.data()); break;
      case GL_FLOAT_MAT4: glUniformMatrix4fv(loc, n, GL_FALSE, v.data()); break;
      case GL_BOOL: case GL_INT: { std::vector<GLint> ints(v.begin(), v.end()); glUniform1iv(loc, n, ints.data()); break; }
    }
  }
}
