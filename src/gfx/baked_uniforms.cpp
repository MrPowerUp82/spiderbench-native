#include "gfx/baked_programs.h"
#include <vector>
#include <algorithm>

namespace {
void flatten(const Json::Value& value, std::vector<float>& out) {
  if (value.isArray()) for (const auto& v : value) flatten(v, out);
  else if (value.isNumeric() || value.isBool()) out.push_back(value.isBool() ? float(value.asBool()) : value.asFloat());
}
// Resolves a GLSL uniform name ("a.b[2].c", "arr[0]") inside the captured Three.js uniform values.
const Json::Value* uniformAt(const Json::Value& root, const std::string& name) {
  const Json::Value* value = &root; size_t p = 0;
  while (p < name.size()) {
    if (name[p] == '.') { p++; continue; }
    if (name[p] == '[') {
      size_t end = name.find(']', p); if (end == std::string::npos || !value->isArray()) return nullptr;
      int i = std::stoi(name.substr(p + 1, end - p - 1));
      if (i == 0 && end + 1 == name.size()) return value;
      value = &(*value)[Json::ArrayIndex(i)]; p = end + 1;
    } else {
      size_t end = name.find_first_of(".[", p);
      const std::string key = name.substr(p, end == std::string::npos ? name.size() - p : end - p);
      if (!value->isObject() || !value->isMember(key)) return nullptr;
      value = &(*value)[key]; p = end == std::string::npos ? name.size() : end;
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
}  // namespace

void CapturedUniforms::build(Shader& shader, const Json::Value& values) {
  list.clear();
  GLint count = 0; glGetProgramiv(shader.id, GL_ACTIVE_UNIFORMS, &count);
  for (GLint i = 0; i < count; i++) {
    char name[256]; GLsizei len; GLint size; GLenum type;
    glGetActiveUniform(shader.id, i, sizeof(name), &len, &size, &type, name);
    const int c = components(type); if (!c) continue;
    const Json::Value* value = uniformAt(values, name); if (!value) continue;
    Entry e; flatten(*value, e.values);
    if (e.values.empty() || e.values.size() % c) continue;
    e.count = std::min(size, GLint(e.values.size() / c)); e.type = type; e.location = shader.loc(name);
    if (e.location >= 0) list.push_back(std::move(e));
  }
}

void CapturedUniforms::upload() const {
  for (const Entry& e : list) {
    const float* v = e.values.data();
    switch (e.type) {
      case GL_FLOAT: glUniform1fv(e.location, e.count, v); break;
      case GL_FLOAT_VEC2: glUniform2fv(e.location, e.count, v); break;
      case GL_FLOAT_VEC3: glUniform3fv(e.location, e.count, v); break;
      case GL_FLOAT_VEC4: glUniform4fv(e.location, e.count, v); break;
      case GL_FLOAT_MAT3: glUniformMatrix3fv(e.location, e.count, GL_FALSE, v); break;
      case GL_FLOAT_MAT4: glUniformMatrix4fv(e.location, e.count, GL_FALSE, v); break;
      case GL_BOOL: case GL_INT: { GLint ints[64]; const GLsizei n = std::min<GLsizei>(e.count, 64);
        for (GLsizei k = 0; k < n; k++) ints[k] = GLint(v[k]);
        glUniform1iv(e.location, n, ints); break; }
    }
  }
}

void uploadCapturedUniforms(Shader& shader, const Json::Value& values) {
  CapturedUniforms u; u.build(shader, values); u.upload();
}
