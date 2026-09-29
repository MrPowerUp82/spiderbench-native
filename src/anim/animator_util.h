// internal helpers shared by the animator translation units
#pragma once
#include "anim/animator.h"
#include <string>

namespace anim {
inline std::string K(const char* base, char S) { return std::string(base) + S; }
inline float sxOf(char S) { return S == 'L' ? 1.f : -1.f; }
inline int si(char S) { return S == 'L' ? 0 : 1; }
inline char other(char S) { return S == 'L' ? 'R' : 'L'; }
constexpr float WALL_Z = 0.30f; // wall plane in wall-authored clips (character space +Z)
const Vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
inline bool finite(float v) { return std::isfinite(v); }
}  // namespace anim
