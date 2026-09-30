#include "world/baked_collision.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
static_assert(std::endian::native == std::endian::little, "SBCOLL1 stores little-endian numbers");
constexpr float NEG_INF = -std::numeric_limits<float>::infinity();
constexpr uint8_t BOX = 0, CYL = 1, RAMP = 2, HF = 3;
constexpr uint8_t OVERHANG = 1, BLOCKONLY = 8;

template<class T> bool readOne(std::ifstream& f, T& v) {
  return bool(f.read(reinterpret_cast<char*>(&v), sizeof(T)));
}
template<class T> bool readArray(std::ifstream& f, std::vector<T>& out, size_t n) {
  out.resize(n);
  return !n || bool(f.read(reinterpret_cast<char*>(out.data()), n * sizeof(T)));
}
}  // namespace

bool BakedCollision::load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  char magic[8];
  if (!f.read(magic, 8) || std::memcmp(magic, "SBCOLL1\0", 8)) return false;
  uint32_t version, fieldCount, zipCount, boxCount, itemCount;
  if (!readOne(f, version) || version != 1 || !readOne(f, n) || !readOne(f, fieldCount) ||
      !readOne(f, zipCount) || !readOne(f, boxCount) || !readOne(f, nx) ||
      !readOne(f, nz) || !readOne(f, itemCount)) return false;
  if (n > 3000000 || fieldCount > 100000 || zipCount > 1000000 || boxCount > 1000000 ||
      !nx || !nz || uint64_t(nx) * nz > 2000000 || itemCount > 20000000) return false;
  if (!readOne(f, cell) || !readOne(f, ox) || !readOne(f, oz) ||
      !readOne(f, spawn.x) || !readOne(f, spawn.y) || !readOne(f, spawn.z) || cell <= 0) return false;
  if (!readArray(f, type, n) || !readArray(f, flags, n) || !readArray(f, kind, n) ||
      !readArray(f, bb, size_t(n) * 6) || !readArray(f, par, size_t(n) * 6) ||
      !readArray(f, start, size_t(nx) * nz + 1) || !readArray(f, items, itemCount)) return false;
  if (start.back() != itemCount) return false;
  for (size_t i = 1; i < start.size(); i++) if (start[i] < start[i - 1]) return false;
  for (uint32_t i : items) if (i >= n) return false;
  fields.resize(fieldCount);
  uint64_t totalCells = 0;
  for (auto& field : fields) {
    if (!readOne(f, field.nx) || !readOne(f, field.nz) || !readOne(f, field.cell) ||
        !readOne(f, field.hMax) || !readOne(f, field.loMin)) return false;
    const uint64_t cells = uint64_t(field.nx) * field.nz;
    totalCells += cells;
    if (!field.nx || !field.nz || field.cell <= 0 || cells > 2000000 || totalCells > 20000000) return false;
    if (!readArray(f, field.h, size_t(cells)) || !readArray(f, field.lo, size_t(cells))) return false;
  }
  std::vector<float> zipP, boxP;
  std::vector<uint8_t> zipK;
  if (!readArray(f, zipP, size_t(zipCount) * 6) || !readArray(f, zipK, zipCount) ||
      !readArray(f, boxP, size_t(boxCount) * 6)) return false;
  zips.resize(zipCount);
  for (size_t i = 0; i < zips.size(); i++) {
    const float* p = zipP.data() + i * 6;
    zips[i] = {{p[0], p[1], p[2]}, {p[3], p[4], p[5]}, zipK[i]};
  }
  boxes.resize(boxCount);
  for (size_t i = 0; i < boxes.size(); i++) {
    const float* p = boxP.data() + i * 6;
    boxes[i] = {{p[0], p[1], p[2]}, {p[3], p[4], p[5]}};
  }
  if (uint64_t(f.tellg()) != std::filesystem::file_size(path)) return false;
  std::printf("[bake] %u solids, %zu fields, %zu zip points, %zu building boxes\n",
              n, fields.size(), zips.size(), boxes.size());
  return true;
}

float BakedCollision::top(uint32_t i, float x, float z) const {
  if (i >= n) return NEG_INF;
  const size_t j = size_t(i) * 6;
  const float* b = bb.data() + j;
  const float* p = par.data() + j;
  if (x < b[0] || x > b[3] || z < b[2] || z > b[5]) return NEG_INF;
  if (type[i] == BOX) return b[4];
  if (type[i] == HF) {
    const uint32_t fid = uint32_t(p[0]);
    if (fid >= fields.size()) return NEG_INF;
    const auto& field = fields[fid];
    const int ix = std::clamp(int(std::floor((x - b[0]) / field.cell)), 0, int(field.nx) - 1);
    const int iz = std::clamp(int(std::floor((z - b[2]) / field.cell)), 0, int(field.nz) - 1);
    const float h = field.h[size_t(iz) * field.nx + ix];
    return h > -1e30f ? p[1] + h : NEG_INF;
  }
  if (type[i] == RAMP) {
    const int axis = int(p[0]);
    if (axis != 0 && axis != 2) return NEG_INF;
    const float lo = b[axis], hi = b[3 + axis];
    return p[1] + (p[2] - p[1]) * ((axis == 0 ? x : z) - lo) / (hi - lo);
  }
  if (type[i] == CYL) {
    const float dx = x - p[0], dz = z - p[1], d = std::sqrt(dx * dx + dz * dz);
    if (d <= p[3]) return b[4];
    if (d <= p[2]) return b[1] + (p[2] - d) / (p[2] - p[3]) * (b[4] - b[1]);
  }
  return NEG_INF;
}

BakedTop BakedCollision::topAt(float x, float z, float yMax, bool skipOverhang) const {
  BakedTop out;
  const int cx = int(std::floor((x - ox) / cell)), cz = int(std::floor((z - oz) / cell));
  if (cx < 0 || cz < 0 || cx >= int(nx) || cz >= int(nz)) return out;
  const size_t c = size_t(cz) * nx + cx;
  for (uint32_t k = start[c]; k < start[c + 1]; k++) {
    const uint32_t i = items[k];
    if ((skipOverhang && (flags[i] & OVERHANG)) || (flags[i] & BLOCKONLY)) continue;
    const float y = top(i, x, z);
    if (y > out.y && y <= yMax) { out.y = y; out.id = int(i); }
  }
  return out;
}

bool BakedCollision::inside(float x, float y, float z) const {
  const int cx = int(std::floor((x - ox) / cell)), cz = int(std::floor((z - oz) / cell));
  if (cx < 0 || cz < 0 || cx >= int(nx) || cz >= int(nz)) return false;
  const size_t c = size_t(cz) * nx + cx;
  for (uint32_t k = start[c]; k < start[c + 1]; k++) {
    const uint32_t i = items[k];
    const size_t j = size_t(i) * 6;
    const float* b = bb.data() + j; const float* p = par.data() + j;
    if (y < b[1] || y > b[4]) continue;
    const float t = top(i, x, z);
    if (t < y) continue;
    if (type[i] == RAMP && p[3] > 0 && y < t - p[3]) continue;
    if (type[i] == HF) {
      const uint32_t fid = uint32_t(p[0]); if (fid >= fields.size()) continue;
      const auto& field = fields[fid];
      const int ix = std::clamp(int(std::floor((x - b[0]) / field.cell)), 0, int(field.nx) - 1);
      const int iz = std::clamp(int(std::floor((z - b[2]) / field.cell)), 0, int(field.nz) - 1);
      if (y < p[1] + field.lo[size_t(iz) * field.nx + ix]) continue;
    }
    if (type[i] == CYL) {
      const float r = p[2] + (p[3] - p[2]) * (y - b[1]) / (b[4] - b[1]);
      if (std::hypot(x - p[0], z - p[1]) > r) continue;
    }
    return true;
  }
  return false;
}
