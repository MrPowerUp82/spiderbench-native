#include "world/world.h"
#include "world/layout.h"
#include <cstdio>
#include <algorithm>

using namespace layout;

namespace {
float bump(float z, float c, float w) { float u = (z - c) / w; return std::exp(-u * u); }
// district skyline envelope (m): Midtown and the Financial District peak, the Village stays low-rise
float districtHeight(float x, float z) {
  if (z < -2300) return 24;                                  // Harlem / Washington Heights
  if (z < -600) return std::fabs(x) < 300 ? 45 : 40;         // Upper West / East Side
  if (z < 560) return 40 + 190 * bump(z, -250, 520);         // Midtown
  if (z < 1400) return 20;                                   // Greenwich Village
  if (z < 2350) return 32 + 30 * bump(z, 2300, 200);         // SoHo / Tribeca
  return 60 + 190 * bump(z, 2900, 380);                      // Financial District
}
struct Style { int id; Vec3 tint; float floorH, bayW; };
Style pickStyle(float H, float z, Mulberry32& rng) {
  float r = rng();
  if (H > 120) {
    if (r < 0.6f) { const Vec3 glass[] = {{0.55f, 0.68f, 0.78f}, {0.45f, 0.58f, 0.62f}, {0.62f, 0.66f, 0.7f}, {0.4f, 0.5f, 0.6f}}; return {0, glass[(int)(rng() * 4) & 3], 3.9f + rng() * 0.4f, 1.5f + rng() * 0.3f}; }
    return {4, Vec3{0.86f, 0.8f, 0.7f} * (0.9f + 0.15f * rng()), 3.8f + rng() * 0.3f, 2.2f + rng() * 0.6f};
  }
  if (H > 50) {
    if (r < 0.3f) return {2, Vec3{0.9f, 0.86f, 0.76f} * (0.9f + 0.12f * rng()), 3.7f + rng() * 0.4f, 2.0f + rng() * 0.7f};
    if (r < 0.55f) return {3, Vec3{0.72f, 0.72f, 0.7f} * (0.9f + 0.15f * rng()), 3.6f + rng() * 0.4f, 1.8f + rng() * 0.8f};
    if (r < 0.8f) return {4, Vec3{0.8f, 0.72f, 0.6f} * (0.9f + 0.15f * rng()), 3.7f + rng() * 0.3f, 2.2f + rng() * 0.6f};
    return {0, Vec3{0.5f, 0.62f, 0.7f}, 3.9f, 1.6f};
  }
  if (r < 0.65f) { const Vec3 brick[] = {{0.62f, 0.33f, 0.26f}, {0.55f, 0.36f, 0.3f}, {0.7f, 0.46f, 0.36f}, {0.48f, 0.3f, 0.26f}, {0.66f, 0.56f, 0.46f}};
    return {1, brick[(int)(rng() * 5) % 5], 3.2f + rng() * 0.4f, 2.1f + rng() * 0.6f}; }
  return {2, Vec3{0.88f, 0.84f, 0.74f} * (0.9f + 0.12f * rng()), 3.5f + rng() * 0.4f, 2.0f + rng() * 0.6f};
}
}  // namespace

// ------------------------------------------------------------------------------------------------ build
void World::build(uint32_t seed) {
  Mulberry32 rng(seed);
  buildGround();
  const auto& S = streets();
  int nb = 0;
  for (size_t k = 0; k + 1 < S.size(); k++) {
    float za = S[k] + stHalf((int)k), zb = S[k + 1] - stHalf((int)k + 1);
    auto gr = gridRange(za, zb);
    if (gr[1] - gr[0] < 30) continue;
    float start = gr[0]; bool westAv = false;
    auto emit = [&](float s0, float s1, bool avW, bool avE) {
      if (s1 - s0 < 18) return;
      float cx = (s0 + s1) / 2, cz = (za + zb) / 2;
      if (inParkCells(cx, cz)) return;
      float x0 = s0 + (avW ? Grid::AV_WALK : 3.f), x1 = s1 - (avE ? Grid::AV_WALK : 3.f);
      float z0 = za + Grid::ST_WALK, z1 = zb - Grid::ST_WALK;
      buildBlock(x0, z0, x1, z1, rng, avW, avE); nb++;
    };
    for (float a : AVENUES) {
      if (a - Grid::AV_HALF < gr[0] || a + Grid::AV_HALF > gr[1]) continue;
      emit(start, a - Grid::AV_HALF, westAv, true);
      start = a + Grid::AV_HALF; westAv = true;
    }
    emit(start, gr[1], westAv, false);
  }
  buildPark(rng);
  buildStreetFurniture(rng);
  buildFarShores(rng);
  index();
  std::printf("[world] %d blocks, %zu boxes, %zu trees, %zu lamps, %zu + %zu verts\n", nb, boxes.size(), trees.size(), lampPoints.size(), cityMesh.v.size(), farMesh.v.size());
}

void World::buildGround() {
  MeshBuilder& M = cityMesh;
  auto xb = xBands(), zb = zBands();
  for (size_t j = 0; j + 1 < zb.size(); j++) {
    float za = zb[j], zc = zb[j + 1];
    if (zc <= Grid::Z_MIN || za >= Grid::Z_MAX) continue;
    za = std::max(za, Grid::Z_MIN); zc = std::min(zc, Grid::Z_MAX);
    float wa = shoreW(za), ea = shoreE(za), wc = shoreW(zc), ec = shoreE(zc);
    for (size_t i = 0; i + 1 < xb.size(); i++) {
      float xa = xb[i], xc = xb[i + 1];
      float a0 = std::max(xa, wa), a1 = std::min(xc, ea), c0 = std::max(xa, wc), c1 = std::min(xc, ec);
      if (a1 - a0 < 0.05f && c1 - c0 < 0.05f) continue;
      a1 = std::max(a1, a0); c1 = std::max(c1, c0);
      float cx = (a0 + a1 + c0 + c1) / 4, cz = (za + zc) / 2;
      CellInfo ci = classify(cx, cz);
      if (ci.type == C_WATER) continue;
      float h = ci.type == C_ROAD ? GY_ROAD : ci.type == C_PARK ? GY_GRASS : GY_WALK;
      M.tint = {1, 1, 1};
      M.mat = ci.type == C_ROAD ? S_ROAD : ci.type == C_PARK ? S_GRASS : S_WALK;
      M.p1 = (float)ci.axis; M.p2 = ci.centre; M.p3 = ci.half;
      std::vector<Vec2> poly;
      poly.push_back({a0, za}); if (a1 - a0 > 0.01f) poly.push_back({a1, za});
      poly.push_back({c1, zc}); if (c1 - c0 > 0.01f) poly.push_back({c0, zc});
      M.polyTop(poly, h);
      if (h > 0.01f) { // curb faces
        M.mat = S_CONCRETE; M.p1 = M.p2 = M.p3 = 0;
        M.quad({a0, 0, za}, {a1, 0, za}, {a1, h, za}, {a0, h, za}, {0, 0, -1}, a0, 0, a1, h);
        M.quad({c0, 0, zc}, {c1, 0, zc}, {c1, h, zc}, {c0, h, zc}, {0, 0, 1}, c0, 0, c1, h);
        M.quad({a0, 0, za}, {c0, 0, zc}, {c0, h, zc}, {a0, h, za}, {-1, 0, 0}, za, 0, zc, h);
        M.quad({a1, 0, za}, {c1, 0, zc}, {c1, h, zc}, {a1, h, za}, {1, 0, 0}, za, 0, zc, h);
      }
    }
  }
  // seawall / bulkhead along the shore, down into the river
  M.mat = S_CONCRETE; M.tint = {0.8f, 0.78f, 0.74f}; M.p1 = M.p2 = M.p3 = 0;
  auto sz = shoreZ();
  for (size_t i = 0; i + 1 < sz.size(); i++) {
    float z0 = sz[i], z1 = sz[i + 1];
    Vec2 w0{shoreW(z0), z0}, w1{shoreW(z1 - 1e-3f), z1}, e0{shoreE(z0), z0}, e1{shoreE(z1 - 1e-3f), z1};
    M.quad({w0.x, -4, w0.y}, {w1.x, -4, w1.y}, {w1.x, 0.15f, w1.y}, {w0.x, 0.15f, w0.y}, Vec3{-(w1.y - w0.y), 0, (w1.x - w0.x)}.normalized(), z0, -4, z1, 0.15f);
    M.quad({e0.x, -4, e0.y}, {e1.x, -4, e1.y}, {e1.x, 0.15f, e1.y}, {e0.x, 0.15f, e0.y}, Vec3{(e1.y - e0.y), 0, -(e1.x - e0.x)}.normalized(), z0, -4, z1, 0.15f);
  }
}

void World::buildBlock(float x0, float z0, float x1, float z1, Mulberry32& rng, bool avW, bool avE) {
  if (x1 - x0 < 10 || z1 - z0 < 10) return;
  float cz = (z0 + z1) / 2, depth = z1 - z0;
  // rows: two halves (north / south street frontage) on deep blocks
  std::vector<std::array<float, 2>> rows;
  if (depth > 36) { float m = z0 + depth * (0.45f + 0.1f * rng()); rows.push_back({z0, m}); rows.push_back({m, z1}); }
  else rows.push_back({z0, z1});
  bool tower = false;
  float x = x0;
  while (x < x1 - 1) {
    float env = districtHeight((x + x1) / 2, cz);
    bool big = env > 90 && rng() < 0.28f;
    float w = big ? 40 + 30 * rng() : 12 + 24 * rng();
    if (x1 - (x + w) < 10) w = x1 - x;
    float xe = std::min(x1, x + w);
    bool frontAv = (avW && x - x0 < 1) || (avE && x1 - xe < 1);
    if (big && depth > 30) { // full-depth tower lot
      float H = std::max(env * (0.8f + 0.6f * rng()), 20.f) * (frontAv ? 1.15f : 1.f);
      buildBuilding(x, z0, xe, z1, H, rng); tower = true;
    } else {
      for (auto& r : rows) {
        float H = env * (0.45f + 0.75f * rng());
        if (rng() < 0.06f && env > 30) H *= 1.8f;           // an odd taller one
        H = std::max(H, 9.f) * (frontAv ? 1.12f : 1.f);
        buildBuilding(x, r[0], xe, r[1], H, rng);
      }
    }
    x = xe;
  }
}

void World::buildBuilding(float x0, float z0, float x1, float z1, float H, Mulberry32& rng) {
  MeshBuilder& M = cityMesh;
  Style st = pickStyle(H, (z0 + z1) / 2, rng);
  H = std::round(H / st.floorH) * st.floorH + 1.2f; // whole floors + roof slab
  auto setMat = [&](float tintK = 1) { M.mat = S_FACADE; M.tint = st.tint * tintK; M.p1 = (float)st.id + rng() * 0.5f; M.p2 = st.floorH; M.p3 = st.bayW; };
  struct Tier { float x0, z0, x1, z1, y0, y1; };
  std::vector<Tier> tiers;
  float w = x1 - x0, d = z1 - z0;
  if (H > 70 && rng() < 0.75f && std::min(w, d) > 24) {
    float hp = clampf(H * (0.18f + 0.17f * rng()), 12, 45);
    tiers.push_back({x0, z0, x1, z1, -0.3f, hp});
    float ins = std::max(2.5f, std::min(w, d) * (0.12f + 0.13f * rng()));
    float tx0 = x0 + ins, tz0 = z0 + ins, tx1 = x1 - ins, tz1 = z1 - ins;
    if (H > 160 && rng() < 0.55f && std::min(tx1 - tx0, tz1 - tz0) > 20) {
      float hc = H * (0.86f + 0.06f * rng());
      tiers.push_back({tx0, tz0, tx1, tz1, hp, hc});
      float c = 2 + 2 * rng();
      tiers.push_back({tx0 + c, tz0 + c, tx1 - c, tz1 - c, hc, H});
    } else tiers.push_back({tx0, tz0, tx1, tz1, hp, H});
  } else tiers.push_back({x0, z0, x1, z1, -0.3f, H});

  for (size_t t = 0; t < tiers.size(); t++) {
    const Tier& T = tiers[t];
    setMat();
    M.box({T.x0, T.y0, T.z0}, {T.x1, T.y1, T.z1}, 31, S_ROOF);
    addBox({T.x0, T.y0, T.z0}, {T.x1, T.y1, T.z1}, t == 0 ? K_BUILDING : K_TIER);
    // parapet (masonry styles): a 1.1 m wall round the exposed roof
    if (st.id != 0 && T.x1 - T.x0 > 6 && T.z1 - T.z0 > 6) {
      float th = 0.35f, ph = T.y1 + 1.1f;
      setMat(0.92f); M.mat = S_CONCRETE;
      const float B[4][4] = {{T.x0, T.z0, T.x1, T.z0 + th}, {T.x0, T.z1 - th, T.x1, T.z1}, {T.x0, T.z0 + th, T.x0 + th, T.z1 - th}, {T.x1 - th, T.z0 + th, T.x1, T.z1 - th}};
      for (auto& b : B) { M.box({b[0], T.y1, b[1]}, {b[2], ph, b[3]}, 31); addBox({b[0], T.y1, b[1]}, {b[2], ph, b[3]}, K_PARAPET); }
    }
  }
  // rooftop units + water tower on the top roof
  const Tier& top = tiers.back();
  float rw = top.x1 - top.x0, rd = top.z1 - top.z0;
  if (rw > 12 && rd > 12) {
    int n = (int)(rng() * 3);
    for (int i = 0; i < n; i++) {
      float bw = 2 + 3 * rng(), bd = 2 + 3 * rng(), bh = 1.4f + 1.6f * rng();
      float bx = top.x0 + 2.5f + (rw - 5 - bw) * rng(), bz = top.z0 + 2.5f + (rd - 5 - bd) * rng();
      M.mat = S_METAL; M.tint = Vec3{0.72f, 0.73f, 0.74f} * (0.85f + 0.2f * rng()); M.p1 = M.p2 = M.p3 = 0;
      M.box({bx, top.y1, bz}, {bx + bw, top.y1 + bh, bz + bd}, 31);
      addBox({bx, top.y1, bz}, {bx + bw, top.y1 + bh, bz + bd}, K_ROOFBOX);
    }
    if ((st.id == 1 || st.id == 2) && H > 15 && H < 120 && rng() < 0.45f) {
      float r = 1.8f, cx = top.x0 + 3 + (rw - 6) * rng(), cz = top.z0 + 3 + (rd - 6) * rng(), y = top.y1;
      M.mat = S_METAL; M.tint = {0.25f, 0.24f, 0.23f};
      for (int lx = -1; lx <= 1; lx += 2) for (int lz = -1; lz <= 1; lz += 2)
        M.box({cx + lx * r * 0.6f - 0.1f, y, cz + lz * r * 0.6f - 0.1f}, {cx + lx * r * 0.6f + 0.1f, y + 2.3f, cz + lz * r * 0.6f + 0.1f}, 15);
      M.mat = S_WOOD; M.tint = Vec3{0.52f, 0.4f, 0.3f} * (0.8f + 0.3f * rng());
      M.cylinder({cx, y + 2.3f, cz}, r, r, 3.8f, 14, false);
      M.mat = S_METAL; M.tint = {0.3f, 0.3f, 0.3f};
      M.cylinder({cx, y + 6.1f, cz}, r + 0.1f, 0.08f, 1.3f, 14, false);
      addBox({cx - r, y, cz - r}, {cx + r, y + 6.4f, cz + r}, K_WATERTOWER);
    }
  }
}

void World::addTree(float x, float z, float h, Mulberry32& rng) {
  MeshBuilder& M = cityMesh;
  float base = terrainHeight(x, z);
  float tr = 0.18f + 0.02f * h;
  M.mat = S_BARK; M.tint = {0.9f, 0.85f, 0.8f}; M.p1 = M.p2 = M.p3 = 0;
  M.cylinder({x, base, z}, tr, tr * 0.6f, h * 0.55f, 6, false);
  // canopy: a few overlapping low-poly blobs (S_LEAVES tint varies per tree)
  M.mat = S_LEAVES;
  Vec3 tint = Vec3{0.33f, 0.47f, 0.2f} * (0.8f + 0.4f * rng());
  float R = h * (0.3f + 0.06f * rng()), cy = base + h * 0.64f;
  // icosahedron blobs, slightly squashed and jittered, smooth normals (reads as a rounded crown)
  static const float ph = 1.618034f;
  static const Vec3 IV[12] = {{-1, ph, 0}, {1, ph, 0}, {-1, -ph, 0}, {1, -ph, 0}, {0, -1, ph}, {0, 1, ph}, {0, -1, -ph}, {0, 1, -ph}, {ph, 0, -1}, {ph, 0, 1}, {-ph, 0, -1}, {-ph, 0, 1}};
  static const int IF[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
  for (int b = 0; b < 3; b++) {
    float bx = x + (rng() - 0.5f) * R * 0.8f, bz = z + (rng() - 0.5f) * R * 0.8f, by = cy + (rng() - 0.3f) * R * 0.45f, br = R * (0.6f + 0.3f * rng());
    M.tint = tint * (0.88f + 0.24f * rng());
    Vec3 C{bx, by, bz};
    uint32_t base = (uint32_t)M.v.size();
    for (const Vec3& iv : IV) {
      Vec3 n = iv.normalized();
      float j = 0.85f + 0.3f * rng();
      M.vert(C + Vec3{n.x * br * j, n.y * br * 0.8f * j, n.z * br * j}, n, bx + n.x, by + n.y);
    }
    for (const auto& f : IF) {
      Vec3 fn = (IV[f[0]] + IV[f[1]] + IV[f[2]]).normalized();
      M.tri(base + f[0], base + f[1], base + f[2], fn);
    }
  }
  addBox({x - 0.25f, base, z - 0.25f}, {x + 0.25f, base + std::min(3.f, h * 0.4f), z + 0.25f}, K_TRUNK);
  float top = h * 0.95f;
  if (top >= 6) trees.push_back({{x, base + top * 0.82f, z}, base + top * 0.72f, top * 0.34f});
}

void World::buildPark(Mulberry32& rng) {
  // Central Park: loose groves on a jittered 13 m lattice, with open meadows (low-frequency hash) left clear
  for (float x = Grid::PARK_X0 + 8; x < Grid::PARK_X1 - 6; x += 13)
    for (float z = Grid::PARK_Z0 + 8; z < Grid::PARK_Z1 - 6; z += 13) {
      float meadow = hash2((int)std::floor(x / 90), (int)std::floor(z / 90));
      if (meadow < 0.3f) continue;
      if (rng() < 0.45f) continue;
      addTree(x + (rng() - 0.5f) * 9, z + (rng() - 0.5f) * 9, 9 + 9 * rng(), rng);
    }
}

void World::buildStreetFurniture(Mulberry32& rng) {
  MeshBuilder& M = cityMesh;
  const auto& S = streets();
  for (size_t k = 0; k + 1 < S.size(); k++) {
    float za = S[k] + stHalf((int)k), zb = S[k + 1] - stHalf((int)k + 1);
    auto gr = gridRange(za, zb);
    for (float a : AVENUES) {
      if (a - Grid::AV_HALF < gr[0] || a + Grid::AV_HALF > gr[1]) continue;
      for (int side = -1; side <= 1; side += 2) {
        float x = a + side * (Grid::AV_HALF + 0.9f);
        for (float z = za + 8; z < zb - 6; z += 32) {
          if (inParkCells(x, z)) continue;
          float base = GY_WALK, h = 8.6f;
          M.mat = S_METAL; M.tint = {0.28f, 0.33f, 0.3f}; M.p1 = M.p2 = M.p3 = 0;
          M.cylinder({x, base, z}, 0.13f, 0.08f, h, 6, false);
          float ax = -side * 2.2f; // arm reaches over the roadway
          M.box({std::min(x, x + ax) - 0.05f, base + h - 0.2f, z - 0.06f}, {std::max(x, x + ax) + 0.05f, base + h - 0.05f, z + 0.06f}, 63);
          M.box({x + ax - 0.35f, base + h - 0.35f, z - 0.18f}, {x + ax + 0.35f, base + h - 0.1f, z + 0.18f}, 63);
          addBox({x - 0.15f, base, z - 0.15f}, {x + 0.15f, base + h, z + 0.15f}, K_POLE);
          lampPoints.push_back({{x, base + h, z}, {0, 1, 0}, "lampTop", -1});
        }
      }
    }
    // street trees on residential blocks
    float cz = (za + zb) / 2;
    bool resid = (cz < -600 && cz > -2300) || (cz > 560 && cz < 2350);
    if (resid) for (int side = 0; side < 2; side++) {
      float z = side ? zb - 1.6f : za + 1.6f;
      for (float x = gr[0] + 6; x < gr[1] - 6; x += 11) {
        if (rng() > 0.35f) continue;
        bool nearAv = false; for (float a : AVENUES) if (std::fabs(x - a) < Grid::AV_HALF + 6) nearAv = true;
        if (nearAv || inParkCells(x, z)) continue;
        addTree(x, z, 7 + 5 * rng(), rng);
      }
    }
  }
}

void World::buildFarShores(Mulberry32& rng) {
  MeshBuilder& M = farMesh;
  // New Jersey (west) and Brooklyn / Queens (east): low land slabs + a coarse low-rise field, a few tower clusters
  struct Land { float x0, z0, x1, z1; };
  const Land lands[] = {{-9000, -9000, -1950, 9000}, {1350, -9000, 9000, 2500}, {950, 2500, 9000, 9000}};
  for (auto& L : lands) {
    M.mat = S_FARLAND; M.tint = {1, 1, 1}; M.p1 = M.p2 = M.p3 = 0;
    M.box({L.x0, -4, L.z0}, {L.x1, 1.2f, L.z1}, 31);
  }
  auto field = [&](float x0, float x1, float z0, float z1, float hmin, float hmax, float sp) {
    for (float x = x0; x < x1; x += sp) for (float z = z0; z < z1; z += sp) {
      if (rng() < 0.25f) continue;
      float w = sp * (0.35f + 0.4f * rng()), d = sp * (0.35f + 0.4f * rng()), h = hmin + (hmax - hmin) * std::pow(rng(), 2.2f);
      Style st = pickStyle(h, z, rng);
      M.mat = S_FACADE; M.tint = st.tint; M.p1 = (float)st.id; M.p2 = st.floorH; M.p3 = st.bayW;
      float bx = x + (sp - w) * rng(), bz = z + (sp - d) * rng();
      M.box({bx, 1.2f, bz}, {bx + w, 1.2f + h, bz + d}, 31, S_ROOF);
    }
  };
  field(-3600, -1990, -4200, 4200, 8, 30, 70);
  field(-2350, -1990, 1400, 2900, 40, 170, 55);   // Jersey City waterfront towers
  field(1390, 3200, -4000, 2400, 8, 28, 70);
  field(1390, 1850, -1600, -700, 40, 140, 55);    // Long Island City
  field(1000, 2600, 2550, 4200, 10, 60, 70);      // Downtown Brooklyn
}

// ------------------------------------------------------------------------------------------------ spatial index
void World::index() {
  gw_ = (int)std::ceil((1000 - GX0) / CELL); gh_ = (int)std::ceil((3500 - GZ0) / CELL);
  grid_.assign((size_t)gw_ * gh_, {});
  for (size_t i = 0; i < boxes.size(); i++) {
    const Box& b = boxes[i];
    int x0 = std::max(0, (int)std::floor((b.mn.x - GX0) / CELL)), x1 = std::min(gw_ - 1, (int)std::floor((b.mx.x - GX0) / CELL));
    int z0 = std::max(0, (int)std::floor((b.mn.z - GZ0) / CELL)), z1 = std::min(gh_ - 1, (int)std::floor((b.mx.z - GZ0) / CELL));
    for (int x = x0; x <= x1; x++) for (int z = z0; z <= z1; z++) grid_[(size_t)z * gw_ + x].push_back((int)i);
  }
  stamp_.assign(boxes.size(), 0);
  zipCache_.assign(boxes.size(), {}); zipCached_.assign(boxes.size(), 0);
}

void World::nearBoxes(float x, float z, float r, std::vector<int>& out) const {
  out.clear(); uint32_t fr = ++frame_;
  int x0 = std::max(0, (int)std::floor((x - r - GX0) / CELL)), x1 = std::min(gw_ - 1, (int)std::floor((x + r - GX0) / CELL));
  int z0 = std::max(0, (int)std::floor((z - r - GZ0) / CELL)), z1 = std::min(gh_ - 1, (int)std::floor((z + r - GZ0) / CELL));
  for (int cx = x0; cx <= x1; cx++) for (int cz = z0; cz <= z1; cz++)
    for (int i : grid_[(size_t)cz * gw_ + cx]) {
      if (stamp_[i] == fr) continue; stamp_[i] = fr;
      const Box& b = boxes[i];
      if (b.mx.x < x - r || b.mn.x > x + r || b.mx.z < z - r || b.mn.z > z + r) continue;
      out.push_back(i);
    }
}

// groundHeight(x, z)    highest surface at (x, z)
// groundHeight(x, z, y) highest surface whose top is <= y + 0.5 (step-up tolerance, collision.js makeQueries)
float World::groundHeight(float x, float z, float y) const {
  if (y < INF) y += 0.5f;
  float h = terrainHeight(x, z);
  int cx = (int)std::floor((x - GX0) / CELL), cz = (int)std::floor((z - GZ0) / CELL);
  if (cx < 0 || cz < 0 || cx >= gw_ || cz >= gh_) return h;
  for (int i : grid_[(size_t)cz * gw_ + cx]) {
    const Box& b = boxes[i];
    if (x < b.mn.x || x > b.mx.x || z < b.mn.z || z > b.mx.z) continue;
    if (b.mx.y <= y + 1e-4f && b.mx.y > h) h = b.mx.y;
  }
  return h;
}

bool World::inside(const Vec3& p, float m) const {
  int cx = (int)std::floor((p.x - GX0) / CELL), cz = (int)std::floor((p.z - GZ0) / CELL);
  if (cx < 0 || cz < 0 || cx >= gw_ || cz >= gh_) return false;
  for (int i : grid_[(size_t)cz * gw_ + cx]) {
    const Box& b = boxes[i];
    if (p.x > b.mn.x + m && p.x < b.mx.x - m && p.y > b.mn.y + m && p.y < b.mx.y - m && p.z > b.mn.z + m && p.z < b.mx.z - m) return true;
  }
  return false;
}

static bool rayBox(const Vec3& o, const Vec3& d, const Box& b, float& tHit, int& axis) {
  float tn = -INF, tf = INF; axis = -1;
  for (int a = 0; a < 3; a++) {
    float oa = o[a], da = d[a], mn = b.mn[a], mx = b.mx[a];
    if (std::fabs(da) < 1e-9f) { if (oa < mn || oa > mx) return false; continue; }
    float t1 = (mn - oa) / da, t2 = (mx - oa) / da;
    if (t1 > t2) std::swap(t1, t2);
    if (t1 > tn) { tn = t1; axis = a; }
    if (t2 < tf) tf = t2;
    if (tn > tf) return false;
  }
  if (tn < 0 || axis < 0) return false; // origin inside (single-sided surfaces): no hit
  tHit = tn; return true;
}

bool World::raycast(const Vec3& o, const Vec3& d, float maxDist, Hit& out) const {
  float best = maxDist; bool found = false;
  // terrain (piecewise flat: road 0 / walk 0.15 / grass / water)
  if (d.y < -1e-5f) {
    float t = (GY_GRASS - o.y) / d.y;
    if (t < 0) t = 0;
    for (int it = 0; it < 3 && t <= best; it++) {
      Vec3 p = o + d * t; float h = terrainHeight(p.x, p.z);
      float t2 = (h - o.y) / d.y;
      if (t2 < 0) break;
      if (std::fabs(t2 - t) < 1e-3f || it == 2) { if (t2 <= best) { best = t2; out.point = o + d * t2; out.normal = {0, 1, 0}; found = true; } break; }
      t = t2;
    }
  }
  // boxes: 2D DDA over the index cells
  uint32_t fr = ++frame_;
  float gx = (o.x - GX0) / CELL, gz = (o.z - GZ0) / CELL;
  int cx = (int)std::floor(gx), cz = (int)std::floor(gz);
  int sx = d.x > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
  float dtx = std::fabs(d.x) > 1e-9f ? CELL / std::fabs(d.x) : INF, dtz = std::fabs(d.z) > 1e-9f ? CELL / std::fabs(d.z) : INF;
  float tmx = std::fabs(d.x) > 1e-9f ? ((sx > 0 ? (cx + 1 - gx) : (gx - cx)) * CELL) / std::fabs(d.x) : INF;
  float tmz = std::fabs(d.z) > 1e-9f ? ((sz > 0 ? (cz + 1 - gz) : (gz - cz)) * CELL) / std::fabs(d.z) : INF;
  float tCell = 0;
  for (int steps = 0; steps < 4096; steps++) {
    if (cx >= 0 && cz >= 0 && cx < gw_ && cz < gh_) {
      for (int i : grid_[(size_t)cz * gw_ + cx]) {
        if (stamp_[i] == fr) continue; stamp_[i] = fr;
        float t; int ax;
        if (rayBox(o, d, boxes[i], t, ax) && t <= best) {
          best = t; found = true; out.point = o + d * t;
          out.normal = {0, 0, 0}; out.normal[ax] = d[ax] > 0 ? -1.f : 1.f;
        }
      }
    } else if ((cx < 0 && sx < 0) || (cz < 0 && sz < 0) || (cx >= gw_ && sx > 0) || (cz >= gh_ && sz > 0)) break;
    float tNext = std::min(tmx, tmz);
    if (found && best <= tNext) break;
    if (tNext > best) break;
    tCell = tNext;
    if (tmx < tmz) { tmx += dtx; cx += sx; } else { tmz += dtz; cz += sz; }
  }
  if (found) out.distance = best;
  return found;
}

// ------------------------------------------------------------------------------------------------ zip points
// roof edges / corners / rooftop boxes derived from the box list (port of traversal/zippoints.js boxPoints)
void World::boxZipPoints(int i, std::vector<ZipPoint>& pts) const {
  const Box& b = boxes[i];
  float top = b.mx.y;
  if (top < 3 || b.kind == K_POLE || b.kind == K_TRUNK) return;
  float w = b.mx.x - b.mn.x, d = b.mx.z - b.mn.z;
  bool onRoof = b.mn.y > 3, small = w < 8 && d < 8;
  const char* kindEdge = onRoof && small ? (top - b.mn.y > 2.5f ? "waterTower" : "ledge") : "roofEdge";
  const char* kindCorner = onRoof && small ? kindEdge : "roofCorner";
  const float ins = 0.3f;
  auto add = [&](float x, float z, float nx, float nz, const char* kind) {
    if (groundHeight(x, z) > top + 0.35f) return;                    // covered
    float ox = x + nx * 1.6f, oz = z + nz * 1.6f;
    if (groundHeight(ox, oz) > top - 1.8f) return;                    // no real drop outward
    pts.push_back({{x, top, z}, Vec3{nx, 0, nz}.normalized(), kind, i});
  };
  const float s2 = 0.70710678f;
  add(b.mn.x + ins, b.mn.z + ins, -s2, -s2, kindCorner); add(b.mx.x - ins, b.mn.z + ins, s2, -s2, kindCorner);
  add(b.mn.x + ins, b.mx.z - ins, -s2, s2, kindCorner); add(b.mx.x - ins, b.mx.z - ins, s2, s2, kindCorner);
  auto edge = [&](float len, auto cb) { if (len < 5) return; int n = std::max(1, (int)std::round(len / 9)); for (int k = 0; k < n; k++) cb((k + 0.5f) / n); };
  edge(w, [&](float t) { float x = b.mn.x + w * t; add(x, b.mn.z + ins, 0, -1, kindEdge); add(x, b.mx.z - ins, 0, 1, kindEdge); });
  edge(d, [&](float t) { float z = b.mn.z + d * t; add(b.mn.x + ins, z, -1, 0, kindEdge); add(b.mx.x - ins, z, 1, 0, kindEdge); });
}

void World::getZipPoints(const Vec3& c, float r, std::vector<ZipPoint>& out) const {
  out.clear();
  std::vector<int> ids; nearBoxes(c.x, c.z, r, ids);
  for (int i : ids) {
    if (!zipCached_[i]) { boxZipPoints(i, zipCache_[i]); zipCached_[i] = 1; }
    for (const auto& p : zipCache_[i]) if (p.pos.distanceToSquared(c) < r * r) out.push_back(p);
  }
  for (const auto& p : lampPoints) if (p.pos.distanceToSquared(c) < r * r) out.push_back(p);
}

void World::treesNear(const Vec3& p, float r, std::vector<const TreePt*>& out) const {
  out.clear();
  for (const auto& t : trees) { float dx = t.pos.x - p.x, dz = t.pos.z - p.z; if (dx * dx + dz * dz < r * r) out.push_back(&t); }
}

std::string World::districtAt(float x, float z) const {
  if (!onLand(x, z)) return x < 0 ? "HUDSON RIVER" : "EAST RIVER";
  if (inParkCells(x, z)) return "CENTRAL PARK";
  if (z < -2300) return "HARLEM";
  if (z < -560) return x < 0 ? "UPPER WEST SIDE" : "UPPER EAST SIDE";
  if (z < 560) return "MIDTOWN";
  if (z < 1400) return "GREENWICH VILLAGE";
  if (z < 2350) return "SOHO";
  return "FINANCIAL DISTRICT";
}
