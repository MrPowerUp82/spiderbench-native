#include "gfx/shader.h"
#include <cstdio>
#include <vector>

static GLuint compile(GLenum type, const std::string& src, const char* name) {
  GLuint s = glCreateShader(type);
  const char* p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint n = 0; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &n);
    std::vector<char> log(n + 1); glGetShaderInfoLog(s, n, nullptr, log.data());
    std::fprintf(stderr, "[shader] %s (%s) compile error:\n%s\n", name, type == GL_VERTEX_SHADER ? "vs" : "fs", log.data());
    glDeleteShader(s); return 0;
  }
  return s;
}

static std::string withDefines(const std::string& src, const std::string& defines) {
  std::string head = "#version 330 core\n" + defines + "\n";
  return head + src;
}

bool Shader::build(const char* name, const std::string& vs, const std::string& fs, const std::string& defines) {
  name_ = name;
  GLuint v = compile(GL_VERTEX_SHADER, withDefines(vs, defines), name), f = compile(GL_FRAGMENT_SHADER, withDefines(fs, defines), name);
  if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return false; }
  id = glCreateProgram();
  glAttachShader(id, v); glAttachShader(id, f);
  glLinkProgram(id);
  glDeleteShader(v); glDeleteShader(f);
  GLint ok = 0; glGetProgramiv(id, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLint n = 0; glGetProgramiv(id, GL_INFO_LOG_LENGTH, &n);
    std::vector<char> log(n + 1); glGetProgramInfoLog(id, n, nullptr, log.data());
    std::fprintf(stderr, "[shader] %s link error:\n%s\n", name, log.data());
    glDeleteProgram(id); id = 0;
    return false;
  }
  return true;
}

GLint Shader::loc(const char* n) {
  auto it = locs_.find(n);
  if (it != locs_.end()) return it->second;
  GLint l = glGetUniformLocation(id, n);
  locs_[n] = l;
  return l;
}
