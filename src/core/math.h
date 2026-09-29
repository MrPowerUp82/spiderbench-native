// Small linear-algebra kit with three.js semantics (right-handed, y up, column-major matrices, quaternions x,y,z,w),
// so the traversal / camera code ports line for line from the JS original.
#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>

constexpr float PI = 3.14159265358979323846f;
constexpr float INF = 1e30f;

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float damp(float a, float b, float rate, float dt) { return a + (b - a) * (1.f - std::exp(-rate * dt)); }
inline float angWrap(float a) { return std::atan2(std::sin(a), std::cos(a)); }
inline float smoothstep(float x, float lo, float hi) { if (x <= lo) return 0; if (x >= hi) return 1; x = (x - lo) / (hi - lo); return x * x * (3 - 2 * x); }
inline float signf(float v) { return v > 0 ? 1.f : (v < 0 ? -1.f : 0.f); }

struct Vec2 {
  float x = 0, y = 0;
  Vec2() = default;
  Vec2(float x_, float y_) : x(x_), y(y_) {}
  Vec2& set(float a, float b) { x = a; y = b; return *this; }
};

struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec3() = default;
  constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  Vec3& set(float a, float b, float c) { x = a; y = b; z = c; return *this; }
  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
  Vec3 operator-() const { return {-x, -y, -z}; }
  Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
  Vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
  float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
  float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
  float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
  Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
  float lengthSq() const { return x * x + y * y + z * z; }
  float length() const { return std::sqrt(lengthSq()); }
  Vec3& normalize() { float l = length(); if (l > 1e-12f) { x /= l; y /= l; z /= l; } return *this; }
  Vec3 normalized() const { Vec3 v = *this; return v.normalize(); }
  float distanceTo(const Vec3& o) const { return (*this - o).length(); }
  float distanceToSquared(const Vec3& o) const { return (*this - o).lengthSq(); }
  Vec3& addScaled(const Vec3& o, float s) { x += o.x * s; y += o.y * s; z += o.z * s; return *this; }
  Vec3& lerp(const Vec3& o, float t) { x += (o.x - x) * t; y += (o.y - y) * t; z += (o.z - z) * t; return *this; }
  Vec3& setLength(float l) { normalize(); return *this *= l; }
  Vec3& setY(float v) { y = v; return *this; }
  float hlen() const { return std::hypot(x, z); }
};
inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline Vec3 vlerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3 vmin(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 vmax(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
constexpr Vec3 UP{0, 1, 0};

struct Vec4 { float x = 0, y = 0, z = 0, w = 0; };

struct Mat4;
struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
  Quat() = default;
  Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
  static Quat axisAngle(const Vec3& a, float ang) { float s = std::sin(ang / 2); Vec3 n = a.normalized(); return {n.x * s, n.y * s, n.z * s, std::cos(ang / 2)}; }
  Quat operator*(const Quat& b) const { // this * b (apply b first)
    return {x * b.w + w * b.x + y * b.z - z * b.y, y * b.w + w * b.y + z * b.x - x * b.z, z * b.w + w * b.z + x * b.y - y * b.x, w * b.w - x * b.x - y * b.y - z * b.z};
  }
  Quat conj() const { return {-x, -y, -z, w}; }
  Quat inverse() const { float l = x * x + y * y + z * z + w * w; return {-x / l, -y / l, -z / l, w / l}; }
  float dot(const Quat& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
  Quat& normalize() { float l = std::sqrt(x * x + y * y + z * z + w * w); if (l < 1e-12f) { x = y = z = 0; w = 1; } else { x /= l; y /= l; z /= l; w /= l; } return *this; }
  Vec3 rotate(const Vec3& v) const { // v' = q v q*
    Vec3 u{x, y, z}; Vec3 t = u.cross(v) * 2.f; return v + t * w + u.cross(t);
  }
  static Quat slerp(Quat a, Quat b, float t) {
    float c = a.dot(b); if (c < 0) { b = {-b.x, -b.y, -b.z, -b.w}; c = -c; }
    if (c > 0.9995f) { Quat r{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}; return r.normalize(); }
    float th = std::acos(c), s = std::sin(th), wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
  }
  static Quat fromUnitVectors(const Vec3& a, const Vec3& b) {
    float r = a.dot(b) + 1; Quat q;
    if (r < 1e-6f) { r = 0; if (std::fabs(a.x) > std::fabs(a.z)) q = {-a.y, a.x, 0, r}; else q = {0, -a.z, a.y, r}; }
    else { Vec3 c = a.cross(b); q = {c.x, c.y, c.z, r}; }
    return q.normalize();
  }
  // rotation whose columns are the basis vectors x, y, z (orthonormal)
  static Quat fromBasis(const Vec3& X, const Vec3& Y, const Vec3& Z) {
    float m11 = X.x, m12 = Y.x, m13 = Z.x, m21 = X.y, m22 = Y.y, m23 = Z.y, m31 = X.z, m32 = Y.z, m33 = Z.z;
    float tr = m11 + m22 + m33; Quat q;
    if (tr > 0) { float s = 0.5f / std::sqrt(tr + 1); q = {(m32 - m23) * s, (m13 - m31) * s, (m21 - m12) * s, 0.25f / s}; }
    else if (m11 > m22 && m11 > m33) { float s = 2 * std::sqrt(1 + m11 - m22 - m33); q = {0.25f * s, (m12 + m21) / s, (m13 + m31) / s, (m32 - m23) / s}; }
    else if (m22 > m33) { float s = 2 * std::sqrt(1 + m22 - m11 - m33); q = {(m12 + m21) / s, 0.25f * s, (m23 + m32) / s, (m13 - m31) / s}; }
    else { float s = 2 * std::sqrt(1 + m33 - m11 - m22); q = {(m13 + m31) / s, (m23 + m32) / s, 0.25f * s, (m21 - m12) / s}; }
    return q.normalize();
  }
};
inline Vec3 operator*(const Quat& q, const Vec3& v) { return q.rotate(v); }

struct Mat4 {
  float m[16]; // column-major: m[col*4 + row]
  Mat4() { identity(); }
  Mat4& identity() { for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 1.f : 0.f; return *this; }
  float& at(int r, int c) { return m[c * 4 + r]; }
  float at(int r, int c) const { return m[c * 4 + r]; }
  Mat4 operator*(const Mat4& b) const {
    Mat4 r; for (int c = 0; c < 4; c++) for (int i = 0; i < 4; i++) { float s = 0; for (int k = 0; k < 4; k++) s += at(i, k) * b.at(k, c); r.at(i, c) = s; } return r;
  }
  Vec3 transformPoint(const Vec3& p) const { return {at(0, 0) * p.x + at(0, 1) * p.y + at(0, 2) * p.z + at(0, 3), at(1, 0) * p.x + at(1, 1) * p.y + at(1, 2) * p.z + at(1, 3), at(2, 0) * p.x + at(2, 1) * p.y + at(2, 2) * p.z + at(2, 3)}; }
  Vec3 transformDir(const Vec3& p) const { return {at(0, 0) * p.x + at(0, 1) * p.y + at(0, 2) * p.z, at(1, 0) * p.x + at(1, 1) * p.y + at(1, 2) * p.z, at(2, 0) * p.x + at(2, 1) * p.y + at(2, 2) * p.z}; }
  Vec4 mul4(float x, float y, float z, float w) const {
    return {at(0, 0) * x + at(0, 1) * y + at(0, 2) * z + at(0, 3) * w, at(1, 0) * x + at(1, 1) * y + at(1, 2) * z + at(1, 3) * w,
            at(2, 0) * x + at(2, 1) * y + at(2, 2) * z + at(2, 3) * w, at(3, 0) * x + at(3, 1) * y + at(3, 2) * z + at(3, 3) * w};
  }
  Vec3 position() const { return {m[12], m[13], m[14]}; }
  static Mat4 compose(const Vec3& t, const Quat& q, const Vec3& s) {
    Mat4 r; float x = q.x, y = q.y, z = q.z, w = q.w, x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2, yy = y * y2, yz = y * z2, zz = z * z2, wx = w * x2, wy = w * y2, wz = w * z2;
    r.m[0] = (1 - (yy + zz)) * s.x; r.m[1] = (xy + wz) * s.x; r.m[2] = (xz - wy) * s.x; r.m[3] = 0;
    r.m[4] = (xy - wz) * s.y; r.m[5] = (1 - (xx + zz)) * s.y; r.m[6] = (yz + wx) * s.y; r.m[7] = 0;
    r.m[8] = (xz + wy) * s.z; r.m[9] = (yz - wx) * s.z; r.m[10] = (1 - (xx + yy)) * s.z; r.m[11] = 0;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; r.m[15] = 1; return r;
  }
  static Mat4 translate(const Vec3& t) { Mat4 r; r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
  static Mat4 scale(const Vec3& s) { Mat4 r; r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r; }
  static Mat4 perspective(float fovyDeg, float aspect, float n, float f) {
    Mat4 r; float t = 1.f / std::tan(fovyDeg * PI / 360.f);
    for (float& v : r.m) v = 0;
    r.m[0] = t / aspect; r.m[5] = t; r.m[10] = (f + n) / (n - f); r.m[11] = -1; r.m[14] = 2 * f * n / (n - f); return r;
  }
  // reversed-Z infinite-ish projection for glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE): depth 1 at near, 0 at far
  static Mat4 perspectiveRevZ(float fovyDeg, float aspect, float n, float f) {
    Mat4 r; float t = 1.f / std::tan(fovyDeg * PI / 360.f);
    for (float& v : r.m) v = 0;
    r.m[0] = t / aspect; r.m[5] = t; r.m[10] = n / (f - n); r.m[11] = -1; r.m[14] = f * n / (f - n); return r;
  }
  static Mat4 ortho(float l, float rr, float b, float t, float n, float f) {
    Mat4 r; r.m[0] = 2 / (rr - l); r.m[5] = 2 / (t - b); r.m[10] = -2 / (f - n);
    r.m[12] = -(rr + l) / (rr - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -(f + n) / (f - n); return r;
  }
  static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) { // view matrix
    Vec3 f = (target - eye).normalized(), s = f.cross(up).normalized(), u = s.cross(f);
    Mat4 r; r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z; r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z; r.m[12] = -s.dot(eye); r.m[13] = -u.dot(eye); r.m[14] = f.dot(eye); return r;
  }
  Mat4 inverse() const {
    const float* a = m; float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    Mat4 r; if (std::fabs(det) < 1e-20f) return r; det = 1.f / det; for (int i = 0; i < 16; i++) r.m[i] = inv[i] * det; return r;
  }
  Quat rotation() const { // assumes no shear
    Vec3 X{m[0], m[1], m[2]}, Y{m[4], m[5], m[6]}, Z{m[8], m[9], m[10]};
    return Quat::fromBasis(X.normalized(), Y.normalized(), Z.normalized());
  }
};

// Deterministic PRNG (same as layout.js mulberry32)
struct Mulberry32 {
  uint32_t a;
  explicit Mulberry32(uint32_t seed) : a(seed) {}
  float operator()() {
    a += 0x6D2B79F5u; uint32_t t = a;
    t = (t ^ (t >> 15)) * (1u | t);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (float)((t ^ (t >> 14)) / 4294967296.0);
  }
};
inline float hash2(int x, int z) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (float)((h ^ (h >> 16)) / 4294967296.0);
}
