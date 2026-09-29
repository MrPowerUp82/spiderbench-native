#include "world/layout.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace layout {

const std::array<float, 7> AVENUES = {-610, -430, -250, 0, 250, 430, 610};
const char* AV_NAMES[7] = {"12TH AV", "10TH AV", "8TH AV", "6TH AV", "5TH AV", "PARK AV", "2ND AV"};

namespace {
constexpr float WIDE_ST_HALF = 9, NARROW_ST_HALF = 3.75f;
const float WIDE_ROADS[] = {-480, -1200, -1680, -2480, -2800};
const float NARROW_STREETS[] = {-3280, -3040, -2720, -2320, -1920, -1760, -1440, 80, 1600, 1760};
// control polylines [z, x] north -> south (layout.js WEST_CP / EAST_CP)
const float WEST_CP[][2] = {{-3480, -100}, {-3380, -300}, {-3200, -440}, {-2800, -545}, {-2200, -620}, {-1400, -665}, {-600, -700}, {0, -718}, {600, -725},
                            {1200, -708}, {1800, -665}, {2300, -595}, {2700, -490}, {3000, -360}, {3200, -215}, {3330, -60}};
const float EAST_CP[][2] = {{-3480, -100}, {-3380, 110}, {-3200, 320}, {-2800, 555}, {-2200, 660}, {-1400, 690}, {-600, 700}, {0, 708}, {600, 722},
                            {1200, 772}, {1650, 812}, {2050, 775}, {2450, 620}, {2780, 430}, {3050, 205}, {3250, 25}, {3330, -60}};
template <size_t N>
float lerpCP(const float (&cp)[N][2], float z) {
  if (z <= cp[0][0]) return cp[0][1];
  for (size_t i = 1; i < N; i++) if (z <= cp[i][0]) { float za = cp[i - 1][0], xa = cp[i - 1][1], zb = cp[i][0], xb = cp[i][1]; return xa + (xb - xa) * (z - za) / (zb - za); }
  return cp[N - 1][1];
}
constexpr float SHORE_PUSH = 30;
float shorePushAt(float z) { return SHORE_PUSH * std::max(0.f, std::min({1.f, (z - Grid::Z_MIN) / 450.f, (Grid::Z_MAX - z) / 450.f})); }

struct Tables {
  std::vector<float> streets, hw, sz, w0, e0;
  Tables() {
    for (float z = -3440; z <= 3280; z += Grid::ST_SP) streets.push_back(z);
    for (float z : streets) {
      float h = Grid::ST_HALF;
      for (float w : WIDE_ROADS) if (w == z) h = WIDE_ST_HALF;
      for (float w : NARROW_STREETS) if (w == z) h = NARROW_ST_HALF;
      hw.push_back(h);
    }
    std::set<float> s{Grid::Z_MIN, Grid::Z_MAX};
    for (size_t k = 0; k < streets.size(); k++) for (float e : {streets[k] - hw[k], streets[k] + hw[k]}) if (e > Grid::Z_MIN && e < Grid::Z_MAX) s.insert(e);
    sz.assign(s.begin(), s.end());
    for (float z : sz) { w0.push_back(lerpCP(WEST_CP, z)); e0.push_back(lerpCP(EAST_CP, z)); }
  }
  int shoreIdx(float z) const {
    int lo = 0, hi = (int)sz.size() - 1;
    while (hi - lo > 1) { int m = (lo + hi) >> 1; if (sz[m] <= z) lo = m; else hi = m; }
    return lo;
  }
  float at0(const std::vector<float>& A, float z) const {
    int i = shoreIdx(z); float t = (z - sz[i]) / (sz[i + 1] - sz[i]); return A[i] + (A[i + 1] - A[i]) * t;
  }
};
const Tables& T() { static Tables t; return t; }
}  // namespace

const std::vector<float>& streets() { return T().streets; }
float stHalf(int k) { const auto& t = T(); return k >= 0 && k < (int)t.hw.size() ? t.hw[k] : Grid::ST_HALF; }
float shoreW(float z) { z = std::clamp(z, Grid::Z_MIN, Grid::Z_MAX - 1e-3f); return T().at0(T().w0, z) - shorePushAt(z); }
float shoreE(float z) { z = std::clamp(z, Grid::Z_MIN, Grid::Z_MAX - 1e-3f); return T().at0(T().e0, z) + shorePushAt(z); }
bool onLand(float x, float z) { if (z < Grid::Z_MIN || z > Grid::Z_MAX) return false; return x >= shoreW(z) && x <= shoreE(z); }
std::vector<float> shoreZ() { return T().sz; }

std::array<float, 2> gridRange(float za, float zb) {
  const auto& t = T();
  if (za < Grid::Z_MIN || zb > Grid::Z_MAX) return {1, 0};
  float w = std::max(t.at0(t.w0, std::max(Grid::Z_MIN, za)), t.at0(t.w0, std::min(Grid::Z_MAX - 1e-3f, zb)));
  float e = std::min(t.at0(t.e0, std::max(Grid::Z_MIN, za)), t.at0(t.e0, std::min(Grid::Z_MAX - 1e-3f, zb)));
  for (size_t i = 0; i < t.sz.size(); i++) if (t.sz[i] > za && t.sz[i] < zb) { w = std::max(w, t.w0[i]); e = std::min(e, t.e0[i]); }
  return {w + Grid::PROM, e - Grid::PROM};
}

bool inParkCells(float x, float z) {
  return x > Grid::PARK_X0 - Grid::AV_WALK && x < Grid::PARK_X1 + Grid::AV_WALK && z > Grid::PARK_Z0 - Grid::ST_WALK && z < Grid::PARK_Z1 + Grid::ST_WALK;
}

// row band of z: street k if |z - s_k| < hw_k (returns k, isStreet) else the block row between streets (k = index of the street north)
static void rowOf(float z, int& k, bool& isStreet, float& za, float& zb) {
  const auto& t = T(); const auto& S = t.streets;
  int n = (int)S.size();
  int j = (int)std::floor((z - S[0]) / Grid::ST_SP + 0.5f); j = std::clamp(j, 0, n - 1);
  if (std::fabs(z - S[j]) < t.hw[j]) { k = j; isStreet = true; za = S[j] - t.hw[j]; zb = S[j] + t.hw[j]; return; }
  isStreet = false;
  if (z < S[j]) j--;
  k = j;
  za = j >= 0 ? S[j] + t.hw[j] : Grid::Z_MIN - 1;
  zb = j + 1 < n ? S[j + 1] - t.hw[j + 1] : Grid::Z_MAX + 1;
}

CellInfo classify(float x, float z) {
  if (!onLand(x, z)) return {C_WATER, 0, 0, 0};
  if (inParkCells(x, z)) return {C_PARK, 0, 0, 0};
  int k; bool isSt; float za, zb;
  rowOf(z, k, isSt, za, zb);
  // the street's own grid range is measured over the adjacent block rows (a street exists where blocks exist)
  auto gr = gridRange(isSt ? za - 30 : za, isSt ? zb + 30 : zb);
  if (gr[0] >= gr[1] || x < gr[0] || x > gr[1]) return {C_WALK, 0, 0, 0}; // promenade / tips
  int av = -1;
  for (int i = 0; i < 7; i++) if (std::fabs(x - AVENUES[i]) < Grid::AV_HALF) av = i;
  bool avActive = false;
  if (av >= 0) { auto g = gridRange(za, zb); avActive = AVENUES[av] - Grid::AV_HALF >= g[0] && AVENUES[av] + Grid::AV_HALF <= g[1]; }
  if (isSt && avActive) return {C_ROAD, 2, 0, 0};
  if (isSt) return {C_ROAD, 1, streets()[k], stHalf(k)};
  if (avActive) return {C_ROAD, 0, AVENUES[av], Grid::AV_HALF};
  return {C_WALK, 0, 0, 0};
}

float terrainHeight(float x, float z) {
  CellInfo c = classify(x, z);
  switch (c.type) {
    case C_WATER: return GY_WATER;
    case C_ROAD: return GY_ROAD;
    case C_PARK: return GY_GRASS;
    default: return GY_WALK;
  }
}

std::vector<float> xBands() {
  std::vector<float> b{Grid::X_MIN - 60};
  for (float a : AVENUES) { b.push_back(a - Grid::AV_HALF); b.push_back(a + Grid::AV_HALF); }
  b.push_back(Grid::X_MAX + 60);
  // Central Park edges split the between-avenue bands so park / block cells are separate
  b.push_back(Grid::PARK_X0 - Grid::AV_WALK); b.push_back(Grid::PARK_X1 + Grid::AV_WALK);
  std::sort(b.begin(), b.end()); b.erase(std::unique(b.begin(), b.end()), b.end());
  return b;
}
std::vector<float> zBands() {
  std::vector<float> b = T().sz;
  b.push_back(Grid::PARK_Z0 - Grid::ST_WALK); b.push_back(Grid::PARK_Z1 + Grid::ST_WALK);
  std::sort(b.begin(), b.end()); b.erase(std::unique(b.begin(), b.end()), b.end());
  return b;
}

}  // namespace layout
