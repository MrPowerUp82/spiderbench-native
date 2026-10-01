#pragma once
#include "core/math.h"

// three.js-style perspective camera: looks down its local -Z, quaternion = world orientation
struct Camera {
  Vec3 position{0, 5, 10};
  Quat quaternion;
  float fov = 55, aspect = 16.f / 9, zNear = 0.1f, zFar = 150000; // main.js PerspectiveCamera(55, aspect, 0.1, 150000)
  Vec2 projectionJitter; // NDC offset, zero outside the baked TAA path
  void lookAt(const Vec3& target, const Vec3& up = UP) {
    Vec3 z = (position - target).normalized(); // camera +Z points away from the target
    if (z.lengthSq() < 1e-8f) return;
    Vec3 x = up.cross(z); if (x.lengthSq() < 1e-8f) x = Vec3{1, 0, 0}; x.normalize();
    Vec3 y = z.cross(x);
    quaternion = Quat::fromBasis(x, y, z);
  }
  void rotateX(float a) { quaternion = quaternion * Quat::axisAngle({1, 0, 0}, a); }
  void rotateY(float a) { quaternion = quaternion * Quat::axisAngle({0, 1, 0}, a); }
  void rotateZ(float a) { quaternion = quaternion * Quat::axisAngle({0, 0, 1}, a); }
  Vec3 direction() const { return quaternion * Vec3{0, 0, -1}; }
  Mat4 world() const { return Mat4::compose(position, quaternion, {1, 1, 1}); }
  Mat4 view() const { return world().inverse(); }
  Mat4 proj(bool revZ) const {
    Mat4 p = revZ ? Mat4::perspectiveRevZ(fov, aspect, zNear, zFar) : Mat4::perspective(fov, aspect, zNear, zFar);
    p.m[8] += projectionJitter.x; p.m[9] += projectionJitter.y; return p;
  }
  // NDC projection (x, y in -1..1, z > 1 behind / beyond)
  Vec3 project(const Vec3& p) const {
    Mat4 vp = Mat4::perspective(fov, aspect, zNear, zFar) * view();
    Vec4 c = vp.mul4(p.x, p.y, p.z, 1);
    if (c.w <= 1e-6f) return {0, 0, 2};
    return {c.x / c.w, c.y / c.w, c.z / c.w};
  }
};
