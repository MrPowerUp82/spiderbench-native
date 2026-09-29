#include "gfx/gl.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>

#define SB_GL_DEF(T, N) T N = nullptr;
SB_GL_FUNCS(SB_GL_DEF)
#undef SB_GL_DEF
PFNGLCLIPCONTROLPROC_ glClipControl_ = nullptr;

bool glLoad() {
  bool ok = true;
#define SB_GL_LOAD(T, N) N = (T)SDL_GL_GetProcAddress(#N); if (!N) { std::fprintf(stderr, "[gl] missing %s\n", #N); ok = false; }
  SB_GL_FUNCS(SB_GL_LOAD)
#undef SB_GL_LOAD
  glClipControl_ = (PFNGLCLIPCONTROLPROC_)SDL_GL_GetProcAddress("glClipControl");
  return ok;
}

bool glHasClipControl() {
  if (!glClipControl_) return false;
  GLint major = 0, minor = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &major); glGetIntegerv(GL_MINOR_VERSION, &minor);
  if (major > 4 || (major == 4 && minor >= 5)) return true;
  GLint n = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &n);
  for (GLint i = 0; i < n; i++) { const char* e = (const char*)glGetStringi(GL_EXTENSIONS, i); if (e && !std::strcmp(e, "GL_ARB_clip_control")) return true; }
  return false;
}
