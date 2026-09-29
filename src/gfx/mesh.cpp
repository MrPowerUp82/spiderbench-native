#include "gfx/mesh.h"
#include <cstddef>
#include <map>

void MeshBuilder::box(const Vec3& a, const Vec3& b, int faces, float topMat) {
  const float x0 = a.x, y0 = a.y, z0 = a.z, x1 = b.x, y1 = b.y, z1 = b.z;
  if (faces & 1) quad({x0, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {-1, 0, 0}, -z1, y0, -z0, y1);
  if (faces & 2) quad({x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}, {1, 0, 0}, z0, y0, z1, y1);
  if (faces & 4) quad({x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, {0, 0, -1}, -x0, y0, -x1, y1);
  if (faces & 8) quad({x1, y0, z1}, {x0, y0, z1}, {x0, y1, z1}, {x1, y1, z1}, {0, 0, 1}, x1, y0, x0, y1);
  float m0 = mat;
  if (faces & 16) { if (topMat >= 0) mat = topMat; quad({x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, {0, 1, 0}, x0, z1, x1, z0); mat = m0; }
  if (faces & 32) quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {0, -1, 0}, x0, z0, x1, z1);
}

void MeshBuilder::polyTop(const std::vector<Vec2>& pts, float y) {
  if (pts.size() < 3) return;
  uint32_t base = (uint32_t)v.size();
  for (const auto& p : pts) vert({p.x, y, p.y}, {0, 1, 0}, p.x, p.y);
  for (uint32_t i = 1; i + 1 < pts.size(); i++) tri(base, base + i, base + i + 1, {0, 1, 0}); // convex fan, faces up
}

void MeshBuilder::wall(const Vec2& a, const Vec2& b, float y0, float y1) {
  Vec3 A{a.x, 0, a.y}, B{b.x, 0, b.y};
  Vec3 d = B - A; float L = d.length(); if (L < 1e-4f) return;
  Vec3 n = Vec3{-d.z, 0, d.x} / L; // caller passes the outline so that this side is outward
  quad({A.x, y0, A.z}, {B.x, y0, B.z}, {B.x, y1, B.z}, {A.x, y1, A.z}, n, 0, y0, L, y1);
}

void MeshBuilder::cylinder(const Vec3& base, float r0, float r1, float h, int segs, bool cap) {
  uint32_t start = (uint32_t)v.size();
  float slope = (r0 - r1) / h;
  for (int i = 0; i <= segs; i++) {
    float a = (float)i / segs * 2 * PI, c = std::cos(a), s = std::sin(a);
    Vec3 n = Vec3{c, slope, s}.normalized();
    vert({base.x + c * r0, base.y, base.z + s * r0}, n, (float)i / segs * 2 * PI * r0, base.y);
    vert({base.x + c * r1, base.y + h, base.z + s * r1}, n, (float)i / segs * 2 * PI * r0, base.y + h);
  }
  for (int i = 0; i < segs; i++) {
    uint32_t a = start + i * 2;
    float am = ((float)i + 0.5f) / segs * 2 * PI;
    Vec3 out{std::cos(am), 0, std::sin(am)};
    tri(a, a + 1, a + 3, out); tri(a, a + 3, a + 2, out);
  }
  if (cap && r1 > 1e-3f) {
    uint32_t c = vert({base.x, base.y + h, base.z}, {0, 1, 0}, base.x, base.z);
    for (int i = 0; i < segs; i++) {
      float a0 = (float)i / segs * 2 * PI, a1 = (float)(i + 1) / segs * 2 * PI;
      uint32_t p = vert({base.x + std::cos(a0) * r1, base.y + h, base.z + std::sin(a0) * r1}, {0, 1, 0}, 0, 0);
      uint32_t q = vert({base.x + std::cos(a1) * r1, base.y + h, base.z + std::sin(a1) * r1}, {0, 1, 0}, 0, 0);
      tri(c, q, p, {0, 1, 0});
    }
  }
}

Frustum::Frustum(const Mat4& m) {
  auto row = [&](int r) { return Vec4{m.at(r, 0), m.at(r, 1), m.at(r, 2), m.at(r, 3)}; };
  Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
  auto add = [](Vec4 a, Vec4 b, float s) { return Vec4{a.x + b.x * s, a.y + b.y * s, a.z + b.z * s, a.w + b.w * s}; };
  p[0] = add(r3, r0, 1); p[1] = add(r3, r0, -1); p[2] = add(r3, r1, 1); p[3] = add(r3, r1, -1); p[4] = add(r3, r2, 1); p[5] = add(r3, r2, -1);
}
bool Frustum::visible(const Vec3& mn, const Vec3& mx) const {
  for (const Vec4& q : p) {
    Vec3 v{q.x >= 0 ? mx.x : mn.x, q.y >= 0 ? mx.y : mn.y, q.z >= 0 ? mx.z : mn.z}; // corner furthest along the normal
    if (q.x * v.x + q.y * v.y + q.z * v.z + q.w < 0) return false;
  }
  return true;
}

void GpuMesh::upload(const MeshBuilder& b, float tile) {
  if (!vao) { glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); glGenBuffers(1, &ibo); }
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, b.v.size() * sizeof(SVert), b.v.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
  chunks.clear();
  if (tile > 0) {
    // bucket triangles by the XZ tile of their centroid; each bucket becomes one contiguous index range
    std::map<std::pair<int, int>, std::vector<uint32_t>> buckets;
    for (size_t t = 0; t + 2 < b.idx.size(); t += 3) {
      const SVert &A = b.v[b.idx[t]], &B = b.v[b.idx[t + 1]], &C = b.v[b.idx[t + 2]];
      int tx = (int)std::floor((A.px + B.px + C.px) / 3 / tile), tz = (int)std::floor((A.pz + B.pz + C.pz) / 3 / tile);
      auto& L = buckets[{tx, tz}]; L.insert(L.end(), {b.idx[t], b.idx[t + 1], b.idx[t + 2]});
    }
    std::vector<uint32_t> all; all.reserve(b.idx.size());
    for (auto& [k, L] : buckets) {
      Chunk c{(uint32_t)all.size(), (uint32_t)L.size(), {INF, INF, INF}, {-INF, -INF, -INF}};
      for (uint32_t i : L) { Vec3 p{b.v[i].px, b.v[i].py, b.v[i].pz}; c.mn = vmin(c.mn, p); c.mx = vmax(c.mx, p); }
      all.insert(all.end(), L.begin(), L.end());
      chunks.push_back(c);
    }
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, all.size() * 4, all.data(), GL_STATIC_DRAW);
  } else glBufferData(GL_ELEMENT_ARRAY_BUFFER, b.idx.size() * 4, b.idx.data(), GL_STATIC_DRAW);
  const GLsizei st = sizeof(SVert);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(SVert, px));
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(SVert, nx));
  glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, st, (void*)offsetof(SVert, u));
  glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(SVert, r));
  glEnableVertexAttribArray(4); glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, st, (void*)offsetof(SVert, mat));
  glBindVertexArray(0);
  count = (GLsizei)b.idx.size();
}

void GpuMesh::draw() const {
  if (!count) return;
  glBindVertexArray(vao);
  glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
}

int GpuMesh::drawCulled(const Frustum& f) const {
  if (chunks.empty()) { draw(); return 1; }
  glBindVertexArray(vao);
  int n = 0;
  for (const auto& c : chunks) {
    if (!f.visible(c.mn, c.mx)) continue;
    glDrawElements(GL_TRIANGLES, (GLsizei)c.count, GL_UNSIGNED_INT, (void*)(size_t)(c.first * 4));
    n++;
  }
  return n;
}
