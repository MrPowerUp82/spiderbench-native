// Static geometry: one interleaved vertex format for the whole city (uber "world" shader picks the surface by aMat),
// a CPU builder with box / quad / polygon helpers, and the GPU mesh (VAO + VBO + IBO).
#pragma once
#include "gfx/gl.h"
#include "core/math.h"
#include <vector>
#include <cstdint>

// surface ids (world.glsl)
enum Surf : int { S_FACADE = 0, S_ROOF = 1, S_ROAD = 2, S_WALK = 3, S_GRASS = 4, S_CONCRETE = 5, S_BARK = 6, S_LEAVES = 7,
                  S_WOOD = 8, S_METAL = 9, S_GLASS = 10, S_FARLAND = 11 };

struct SVert {
  float px, py, pz;
  float nx, ny, nz;
  float u, v;        // surface coordinates in metres (facade: along-face, height)
  float r, g, b;     // tint
  float mat, p1, p2, p3; // surface id + per-surface params (facade: style, floorH, bayW / road: axis, centre, half width)
};

struct MeshBuilder {
  std::vector<SVert> v;
  std::vector<uint32_t> idx;
  Vec3 tint{1, 1, 1};
  float mat = 0, p1 = 0, p2 = 0, p3 = 0;
  void clear() { v.clear(); idx.clear(); }
  uint32_t vert(const Vec3& p, const Vec3& n, float u, float vv) {
    v.push_back({p.x, p.y, p.z, n.x, n.y, n.z, u, vv, tint.x, tint.y, tint.z, mat, p1, p2, p3});
    return (uint32_t)v.size() - 1;
  }
  // quad a b c d counter-clockwise seen from the normal side
  void quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& n, float u0, float v0, float u1, float v1) {
    uint32_t i = vert(a, n, u0, v0); vert(b, n, u1, v0); vert(c, n, u1, v1); vert(d, n, u0, v1);
    if ((b - a).cross(c - a).dot(n) >= 0) idx.insert(idx.end(), {i, i + 1, i + 2, i, i + 2, i + 3});
    else idx.insert(idx.end(), {i, i + 2, i + 1, i, i + 3, i + 2}); // winding follows the requested normal
  }
  void tri(uint32_t a, uint32_t b, uint32_t c, const Vec3& n) {
    Vec3 A{v[a].px, v[a].py, v[a].pz}, B{v[b].px, v[b].py, v[b].pz}, C{v[c].px, v[c].py, v[c].pz};
    if ((B - A).cross(C - A).dot(n) >= 0) idx.insert(idx.end(), {a, b, c}); else idx.insert(idx.end(), {a, c, b});
  }
  // axis-aligned box; faces bit mask: 1 -x, 2 +x, 4 -z, 8 +z, 16 top, 32 bottom. Facade uv: u along the face, v = world y.
  void box(const Vec3& mn, const Vec3& mx, int faces = 31, float topMat = -1);
  // horizontal convex polygon (CCW from above) at height y, uv = world xz
  void polyTop(const std::vector<Vec2>& pts, float y);
  // vertical wall strip along polygon edge (a -> b), from y0 to y1, facing outward (right of a->b seen from above)
  void wall(const Vec2& a, const Vec2& b, float y0, float y1);
  // vertical cylinder (open top unless capped), segs around
  void cylinder(const Vec3& base, float r0, float r1, float h, int segs, bool cap = true);
};

// frustum planes (from a view-projection matrix, GL clip conventions) for AABB culling
struct Frustum {
  Vec4 p[6];
  explicit Frustum(const Mat4& vp);
  bool visible(const Vec3& mn, const Vec3& mx) const;
};

struct GpuMesh {
  GLuint vao = 0, vbo = 0, ibo = 0;
  GLsizei count = 0;
  struct Chunk { uint32_t first, count; Vec3 mn, mx; };
  std::vector<Chunk> chunks; // triangles grouped by XZ tile (upload with tile > 0)
  void upload(const MeshBuilder& b, float tile = 0);
  void draw() const;
  int drawCulled(const Frustum& f) const; // returns chunks drawn
};
