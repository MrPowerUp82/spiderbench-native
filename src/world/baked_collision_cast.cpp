// Ray casts and region queries over the baked CollisionGrid (port of collision.js CollisionGrid.cast, _rayBox,
// _rayRamp, _rayHF, _rayCyl, query and topNormal). Double precision like the JS numbers; the stored data is Float32
// exactly as in the original typed arrays.
#include "world/baked_collision.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double DINF = std::numeric_limits<double>::infinity();
constexpr float FINF = std::numeric_limits<float>::infinity();
constexpr uint8_t BOX = 0, CYL = 1, RAMP = 2, HF = 3;
constexpr uint8_t BLOCKONLY = 8;
inline double axisOf(const Vec3& v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
}  // namespace

uint32_t BakedCollision::nextFrame() const {
  if (stamp_.size() != n) { stamp_.assign(n, 0); frame_ = 0; }
  if (++frame_ == 0) { std::fill(stamp_.begin(), stamp_.end(), 0u); frame_ = 1; }
  return frame_;
}

float BakedCollision::rayBox(uint32_t i, const Vec3& o, const Vec3& d, float tMax, Vec3& n) const {
  const float* b = bb.data() + size_t(i) * 6;
  double tn = -DINF, tf = DINF; int ax = -1;
  for (int a = 0; a < 3; a++) {
    const double oa = axisOf(o, a), da = axisOf(d, a), lo = b[a], hi = b[3 + a];
    if (da == 0) { if (oa < lo || oa > hi) return FINF; continue; }
    double t0 = (lo - oa) / da, t1 = (hi - oa) / da;
    if (t0 > t1) std::swap(t0, t1);
    if (t0 > tn) { tn = t0; ax = a; }
    if (t1 < tf) tf = t1;
    if (tn > tf) return FINF;
  }
  if (tn < 0 || tn > tMax) return FINF; // origin inside -> ignored (ray starts within a solid)
  n = {0, 0, 0}; n[ax] = axisOf(d, ax) > 0 ? -1.f : 1.f;
  return float(tn);
}

float BakedCollision::rayRamp(uint32_t i, const Vec3& o, const Vec3& d, float tMax, Vec3& n) const {
  const float* b = bb.data() + size_t(i) * 6; const float* P = par.data() + size_t(i) * 6;
  const int axis = int(P[0]); const double yA = P[1], yB = P[2];
  const double lo = b[axis], hi = b[3 + axis];
  const double s = (yB - yA) / (hi - lo), w = yA - s * lo; // plane: y - s*c <= w with c the axis coordinate
  const double oc = axis == 0 ? o.x : o.z, dc = axis == 0 ? d.x : d.z;
  double tn = -DINF, tf = DINF; int ax = -1;
  for (int a = 0; a < 3; a++) {
    const double oa = axisOf(o, a), da = axisOf(d, a), l = b[a], h = b[3 + a];
    if (da == 0) { if (oa < l || oa > h) return FINF; continue; }
    double t0 = (l - oa) / da, t1 = (h - oa) / da;
    if (t0 > t1) std::swap(t0, t1);
    if (t0 > tn) { tn = t0; ax = a; }
    if (t1 < tf) tf = t1;
  }
  const double f0 = (o.y - s * oc) - w, fd = d.y - s * dc;
  if (std::fabs(fd) < 1e-12) { if (f0 > 0) return FINF; }
  else {
    const double tp = -f0 / fd;
    if (fd < 0) { if (tp > tn) { tn = tp; ax = 3; } } else if (tp < tf) tf = tp;
  }
  const double thk = P[3];
  if (thk > 0) { // lower plane: y - s*c >= w - thk
    const double g0 = f0 + thk;
    if (std::fabs(fd) < 1e-12) { if (g0 < 0) return FINF; }
    else {
      const double tp = -g0 / fd;
      if (fd > 0) { if (tp > tn) { tn = tp; ax = 4; } } else if (tp < tf) tf = tp;
    }
  }
  if (tn > tf || tn < 0 || tn > tMax) return FINF;
  if (ax == 3 || ax == 4) {
    const double l = std::hypot(s, 1.0) * (ax == 4 ? -1 : 1);
    n = {axis == 0 ? float(-s / l) : 0.f, float(1 / l), axis == 2 ? float(-s / l) : 0.f};
  } else {
    n = {0, 0, 0}; n[ax] = axisOf(d, ax) > 0 ? -1.f : 1.f;
  }
  return float(tn);
}

float BakedCollision::rayHF(uint32_t i, const Vec3& o, const Vec3& d, float tMax, Vec3& n) const {
  const float* b = bb.data() + size_t(i) * 6; const float* P = par.data() + size_t(i) * 6;
  const uint32_t fid = uint32_t(P[0]);
  if (fid >= fields.size()) return FINF;
  const BakedField& f = fields[fid];
  const double base = P[1], x0 = b[0], z0 = b[2], c = f.cell;
  const int nx = int(f.nx), nz = int(f.nz);
  double t0 = 0, t1 = tMax; int entryAx = -1;
  for (int a = 0; a < 3; a++) {
    const double oa = axisOf(o, a), da = axisOf(d, a), lo = b[a], hi = b[3 + a];
    if (std::fabs(da) < 1e-12) { if (oa < lo || oa > hi) return FINF; continue; }
    double ta = (lo - oa) / da, tb = (hi - oa) / da; if (ta > tb) std::swap(ta, tb);
    if (ta > t0) { t0 = ta; entryAx = a; }
    if (tb < t1) t1 = tb;
    if (t0 > t1) return FINF;
  }
  const double dx = d.x, dy = d.y, dz = d.z;
  const double px = o.x + dx * t0 - x0, pz = o.z + dz * t0 - z0;
  int ix = std::min(nx - 1, std::max(0, int(std::floor(px / c)))), iz = std::min(nz - 1, std::max(0, int(std::floor(pz / c))));
  const int sx = dx > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
  const double tdx = std::fabs(dx) < 1e-12 ? DINF : c / std::fabs(dx), tdz = std::fabs(dz) < 1e-12 ? DINF : c / std::fabs(dz);
  double tnx = std::fabs(dx) < 1e-12 ? DINF : (x0 + (ix + (sx > 0 ? 1 : 0)) * c - o.x) / dx;
  double tnz = std::fabs(dz) < 1e-12 ? DINF : (z0 + (iz + (sz > 0 ? 1 : 0)) * c - o.z) / dz;
  double tc = t0;
  for (int guard = 0; guard < 8192; guard++) {
    const double tEnd = std::min({tnx, tnz, t1});
    const size_t k = size_t(iz) * nx + ix; const double h = f.h[k];
    if (h > -1e30) {
      const double top = base + h, bot = base + f.lo[k], yIn = o.y + dy * tc;
      if (yIn <= top && yIn >= bot) { // entered through a side face (the origin inside a column is ignored)
        if (tc <= 1e-9) return FINF;
        n = {0, 0, 0};
        if (entryAx == 0) n.x = float(-sx); else if (entryAx == 2) n.z = float(-sz); else n.y = dy > 0 ? -1.f : 1.f;
        return float(tc);
      }
      if (dy < 0 && yIn > top) { const double tt = (top - o.y) / dy; if (tt <= tEnd) { n = {0, 1, 0}; return float(tt); } }
      if (dy > 0 && yIn < bot) { const double tt = (bot - o.y) / dy; if (tt <= tEnd) { n = {0, -1, 0}; return float(tt); } }
    }
    if (tEnd >= t1) return FINF;
    if (tnx < tnz) { tc = tnx; ix += sx; tnx += tdx; entryAx = 0; if (ix < 0 || ix >= nx) return FINF; }
    else { tc = tnz; iz += sz; tnz += tdz; entryAx = 2; if (iz < 0 || iz >= nz) return FINF; }
  }
  return FINF;
}

float BakedCollision::rayCyl(uint32_t i, const Vec3& o, const Vec3& d, float tMax, Vec3& n) const {
  const float* b = bb.data() + size_t(i) * 6; const float* P = par.data() + size_t(i) * 6;
  const double cx = P[0], cz = P[1], r0 = P[2], r1 = P[3], y0 = b[1], y1 = b[4];
  const double h = y1 - y0, k = (r1 - r0) / h;
  const double X = o.x - cx, Z = o.z - cz, dx = d.x, dy = d.y, dz = d.z;
  if (o.y >= y0 && o.y <= y1) { const double r = r0 + k * (o.y - y0); if (X * X + Z * Z <= r * r) return FINF; } // inside -> ignore
  double best = DINF, nx = 0, ny = 0, nz = 0;
  const double a0 = r0 + k * (o.y - y0), bk = k * dy; // side
  const double A = dx * dx + dz * dz - bk * bk, B = 2 * (X * dx + Z * dz - a0 * bk), C = X * X + Z * Z - a0 * a0;
  if (std::fabs(A) > 1e-12) {
    const double disc = B * B - 4 * A * C;
    if (disc >= 0) {
      const double sq = std::sqrt(disc);
      for (double t : {(-B - sq) / (2 * A), (-B + sq) / (2 * A)}) {
        if (t < 0 || t >= best || t > tMax) continue;
        const double y = o.y + dy * t; if (y < y0 || y > y1) continue;
        const double R = a0 + bk * t; if (R < 0) continue;
        const double gx = X + dx * t, gy = -R * k, gz = Z + dz * t;
        if (gx * dx + gy * dy + gz * dz >= 0) continue; // must be entering (normal against the ray)
        best = t; double l = std::sqrt(gx * gx + gy * gy + gz * gz); if (l == 0) l = 1;
        nx = gx / l; ny = gy / l; nz = gz / l;
      }
    }
  }
  if (dy < 0 && r1 > 0) { const double t = (y1 - o.y) / dy; if (t >= 0 && t < best && t <= tMax) { const double px = X + dx * t, pz = Z + dz * t; if (px * px + pz * pz <= r1 * r1) { best = t; nx = 0; ny = 1; nz = 0; } } }
  if (dy > 0 && r0 > 0) { const double t = (y0 - o.y) / dy; if (t >= 0 && t < best && t <= tMax) { const double px = X + dx * t, pz = Z + dz * t; if (px * px + pz * pz <= r0 * r0) { best = t; nx = 0; ny = -1; nz = 0; } } }
  if (best == DINF) return FINF;
  n = {float(nx), float(ny), float(nz)};
  return float(best);
}

bool BakedCollision::cast(const Vec3& o, const Vec3& d, float tMax, BakedCast& out) const {
  if (!n) return false;
  const double c = cell, dx = d.x, dz = d.z;
  const uint32_t fr = nextFrame();
  double t0 = 0, t1 = tMax;
  const double gx0 = ox, gz0 = oz, gx1 = ox + nx * c, gz1 = oz + nz * c;
  if (std::fabs(dx) < 1e-12) { if (o.x < gx0 || o.x > gx1) return false; }
  else { double a = (gx0 - o.x) / dx, b = (gx1 - o.x) / dx; if (a > b) std::swap(a, b); t0 = std::max(t0, a); t1 = std::min(t1, b); }
  if (std::fabs(dz) < 1e-12) { if (o.z < gz0 || o.z > gz1) return false; }
  else { double a = (gz0 - o.z) / dz, b = (gz1 - o.z) / dz; if (a > b) std::swap(a, b); t0 = std::max(t0, a); t1 = std::min(t1, b); }
  if (t0 > t1) return false;
  const double px = o.x + dx * t0, pz = o.z + dz * t0;
  int cx = std::min(int(nx) - 1, std::max(0, int(std::floor((px - gx0) / c))));
  int cz = std::min(int(nz) - 1, std::max(0, int(std::floor((pz - gz0) / c))));
  const int sx = dx > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
  const double tdx = std::fabs(dx) < 1e-12 ? DINF : c / std::fabs(dx), tdz = std::fabs(dz) < 1e-12 ? DINF : c / std::fabs(dz);
  double tnx = std::fabs(dx) < 1e-12 ? DINF : ((gx0 + (cx + (sx > 0 ? 1 : 0)) * c) - o.x) / dx;
  double tnz = std::fabs(dz) < 1e-12 ? DINF : ((gz0 + (cz + (sz > 0 ? 1 : 0)) * c) - o.z) / dz;
  float best = tMax; int bi = -1; Vec3 bn, hn;
  for (;;) {
    const size_t cl = size_t(cz) * nx + cx;
    for (uint32_t k = start[cl], e = start[cl + 1]; k < e; k++) {
      const uint32_t i = items[k];
      if (stamp_[i] == fr) continue;
      stamp_[i] = fr;
      if (flags[i] & BLOCKONLY) continue; // trunks: capsule blockers only
      const uint8_t ty = type[i];
      const float t = ty == BOX ? rayBox(i, o, d, best, hn) : ty == CYL ? rayCyl(i, o, d, best, hn) : ty == HF ? rayHF(i, o, d, best, hn) : rayRamp(i, o, d, best, hn);
      if (t < best) { best = t; bi = int(i); bn = hn; }
    }
    const double tExit = std::min(tnx, tnz);
    if (bi >= 0 && best <= tExit) break;
    if (tExit > t1) break;
    if (tnx < tnz) { cx += sx; tnx += tdx; if (cx < 0 || cx >= int(nx)) break; }
    else { cz += sz; tnz += tdz; if (cz < 0 || cz >= int(nz)) break; }
  }
  if (bi < 0) return false;
  out.t = best; out.id = bi; out.n = bn;
  return true;
}

void BakedCollision::query(float x0, float z0, float x1, float z1, const std::function<void(uint32_t)>& fn) const {
  if (!n) return;
  const uint32_t fr = nextFrame();
  const int a = std::max(0, int(std::floor((x0 - ox) / cell))), b = std::min(int(nx) - 1, int(std::floor((x1 - ox) / cell)));
  const int e = std::max(0, int(std::floor((z0 - oz) / cell))), f = std::min(int(nz) - 1, int(std::floor((z1 - oz) / cell)));
  for (int cz = e; cz <= f; cz++) for (int cx = a; cx <= b; cx++) {
    const size_t cl = size_t(cz) * nx + cx;
    for (uint32_t k = start[cl], kk = start[cl + 1]; k < kk; k++) {
      const uint32_t i = items[k];
      if (stamp_[i] == fr) continue;
      stamp_[i] = fr;
      const float* q = bb.data() + size_t(i) * 6;
      if (q[3] < x0 || q[0] > x1 || q[5] < z0 || q[2] > z1) continue;
      fn(i);
    }
  }
}

Vec3 BakedCollision::topNormal(uint32_t i, float x, float z) const {
  const float* b = bb.data() + size_t(i) * 6; const float* P = par.data() + size_t(i) * 6;
  if (type[i] == RAMP) {
    const int axis = int(P[0]); const float s = (P[2] - P[1]) / (b[3 + axis] - b[axis]);
    return (axis == 0 ? Vec3{-s, 1, 0} : Vec3{0, 1, -s}).normalized();
  }
  if (type[i] == CYL) {
    const float dx = x - P[0], dz = z - P[1], d = std::hypot(dx, dz), r0 = P[2], r1 = P[3];
    if (d > r1 && r0 > r1 && d > 1e-6f) { const float k = (b[4] - b[1]) / (r0 - r1); return Vec3{dx / d * k, 1, dz / d * k}.normalized(); }
  }
  return {0, 1, 0};
}
