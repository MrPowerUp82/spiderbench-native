#include "world/baked_pools.h"
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

bool BakedPools::open(const std::string& path, size_t meshCount) {
  clear(); if (!archive_.open(path)) return false;
  byMesh_.resize(meshCount, -1);
  for (size_t i = 0; i < archive_.meshCount(); i++) {
    BakedPool pool; pool.meta = archive_.mesh(i);
    const auto& m = pool.meta; size_t mesh = m["objectOrdinal"].asUInt(); uint32_t n = m["count"].asUInt();
    if (mesh >= meshCount || byMesh_[mesh] != -1 || m["max"].asUInt() > 1000000) return false;
    std::vector<uint8_t> bytes;
    if (!archive_.readBlob(m["positions"].asUInt(), bytes) || bytes.size() != size_t(n) * 3 * sizeof(double)) return false;
    pool.positions.resize(size_t(n) * 3); std::memcpy(pool.positions.data(), bytes.data(), bytes.size());
    for (const auto& name : m["attributes"].getMemberNames()) {
      const auto& attr = m["attributes"][name]; BakedPool::Attribute a; a.size = attr["itemSize"].asInt();
      if (a.size < 1 || a.size > 16 || !archive_.readBlob(attr["blob"].asUInt(), bytes) || bytes.size() != size_t(n) * a.size * sizeof(float)) return false;
      a.data.resize(size_t(n) * a.size); std::memcpy(a.data.data(), bytes.data(), bytes.size());
      pool.attributes.emplace(name, std::move(a));
    }
    pool.attributes.emplace("aLod", BakedPool::Attribute{4});
    for (uint32_t j = 0; j < n; j++) {
      int64_t x = int64_t(std::floor(pool.positions[j * 3] / 64)), z = int64_t(std::floor(pool.positions[j * 3 + 2] / 64));
      pool.grid[x * 100003 + z].push_back(j);
    }
    byMesh_[mesh] = int(pools_.size()); pools_.push_back(std::move(pool));
  }
  std::printf("[baked-pools] %zu pools loaded\n", pools_.size()); return true;
}

BakedPool* BakedPools::forMesh(size_t index) {
  if (index >= byMesh_.size() || byMesh_[index] < 0) return nullptr;
  return &pools_[byMesh_[index]];
}

bool BakedPools::update(const Camera& camera) {
  Vec3 forward = camera.direction(); double hl = std::hypot(double(forward.x), double(forward.z));
  double wx = 0, wz = 0, wc = -2;
  if (hl >= .3) {
    wx = forward.x / hl; wz = forward.z / hl;
    constexpr double poolPi = 3.14159265358979323846;
    double tv = std::tan(double(camera.fov) * poolPi / 360), th = tv * camera.aspect, worst = 1;
    Mat4 world = camera.world(); bool valid = true;
    for (int k = 0; k < 4; k++) {
      double x = k & 1 ? th : -th, y = k & 2 ? tv : -tv;
      double dx = world.m[0] * x + world.m[4] * y - world.m[8], dz = world.m[2] * x + world.m[6] * y - world.m[10];
      double h = std::hypot(dx, dz); if (h < 1e-4) { valid = false; break; }
      worst = std::min(worst, (dx * wx + dz * wz) / h);
    }
    double half = std::acos(std::clamp(worst, -1.0, 1.0)) + poolPi / 3;
    if (valid && half < poolPi) wc = std::cos(half);
  }
  for (auto& pool : pools_) {
    const auto& m = pool.meta;
    const double poolNear = m["near"].asDouble(), poolFar = m["far"].asDouble(), shadowFar = m["shadowFar"].asDouble();
    const double threshold = std::max(6.0, std::min(40.0, poolFar * .02));
    bool wedge = wc > -1 && poolFar > shadowFar;
    bool viewDue = wedge != pool.wedgeOn || (wedge && wx * pool.vx + wz * pool.vz < .94);
    if (m["isStatic"].asBool() && pool.written) continue;
    double mx = double(camera.position.x) - pool.last.x, my = double(camera.position.y) - pool.last.y, mz = double(camera.position.z) - pool.last.z;
    if (pool.written && mx * mx + my * my + mz * mz < threshold * threshold && !viewDue) continue;
    pool.last = camera.position;
    pool.wedgeOn = wedge; if (wedge) { pool.vx = wx; pool.vz = wz; }
    struct Candidate { uint32_t id; double distance; size_t order; };
    std::vector<Candidate> candidates;
    if (m["isStatic"].asBool()) {
      for (uint32_t i = 0; i < std::min(m["max"].asUInt(), m["count"].asUInt()); i++) candidates.push_back({i, 0, i});
    } else {
      const double nr = std::max(0.0, poolNear - m["fadeIn"].asDouble() - threshold), fr = poolFar + threshold;
      const double n2 = nr * nr, f2 = fr * fr;
      const int r = int(std::ceil(fr / 64));
      int64_t cx = int64_t(std::floor(camera.position.x / 64)), cz = int64_t(std::floor(camera.position.z / 64));
      for (int64_t gx = cx - r; gx <= cx + r; gx++) for (int64_t gz = cz - r; gz <= cz + r; gz++) {
        auto cell = pool.grid.find(gx * 100003 + gz); if (cell == pool.grid.end()) continue;
        for (uint32_t i : cell->second) {
          double dx = pool.positions[i * 3] - camera.position.x, dz = pool.positions[i * 3 + 2] - camera.position.z;
          double d2 = dx * dx + dz * dz;
          double s2 = (shadowFar + threshold) * (shadowFar + threshold);
          if (d2 >= n2 && d2 < f2 && (!wedge || d2 < s2 || dx * wx + dz * wz >= wc * std::sqrt(d2)))
            candidates.push_back({i, d2, candidates.size()});
        }
      }
    }
    double s2 = (shadowFar + threshold) * (shadowFar + threshold);
    if (m["isStatic"].asBool()) {
      pool.shadowCount = shadowFar >= poolFar ? uint32_t(candidates.size()) : 0;
    } else if (candidates.size() > m["max"].asUInt()) {
      std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
      candidates.resize(m["max"].asUInt());
    } else if (candidates.size() <= 20000) {
      std::sort(candidates.begin(), candidates.end(), [s2](const Candidate& a, const Candidate& b) {
        double ka = (std::floor(a.distance) * 2 + (a.distance < s2 ? 0 : 1)) * 1048576 + a.order;
        double kb = (std::floor(b.distance) * 2 + (b.distance < s2 ? 0 : 1)) * 1048576 + b.order;
        return ka < kb;
      });
    } else std::stable_partition(candidates.begin(), candidates.end(), [s2](const Candidate& c) { return c.distance < s2; });
    pool.selection.clear(); if (!m["isStatic"].asBool()) pool.shadowCount = 0;
    for (const auto& c : candidates) { pool.selection.push_back(c.id); if (!m["isStatic"].asBool() && c.distance < s2) pool.shadowCount++; }
    pool.count = uint32_t(pool.selection.size());
    pool.written = true;
    if (!upload(pool)) return false;
  }
  return true;
}

bool BakedPools::upload(BakedPool& pool) {
  for (auto& entry : pool.attributes) {
    auto& attr = entry.second;
    if (!attr.gpu) { glGenBuffers(1, &attr.gpu); glBindBuffer(GL_ARRAY_BUFFER, attr.gpu);
      glBufferData(GL_ARRAY_BUFFER, size_t(pool.meta["max"].asUInt()) * attr.size * 4, nullptr, GL_DYNAMIC_DRAW); }
    if (!pool.count) continue;
    std::vector<float> data(size_t(pool.count) * attr.size);
    for (uint32_t i = 0; i < pool.count; i++) {
      if (entry.first == "aLod") {
        data[i * 4] = float(pool.meta["near"].asDouble() - pool.meta["fadeIn"].asDouble());
        data[i * 4 + 1] = pool.meta["near"].asFloat();
        data[i * 4 + 2] = float(pool.meta["far"].asDouble() - pool.meta["fadeOut"].asDouble());
        data[i * 4 + 3] = pool.meta["far"].asFloat();
      } else std::copy_n(attr.data.data() + size_t(pool.selection[i]) * attr.size, attr.size, data.data() + size_t(i) * attr.size);
    }
    glBindBuffer(GL_ARRAY_BUFFER, attr.gpu); glBufferSubData(GL_ARRAY_BUFFER, 0, data.size() * 4, data.data());
  }
  return glGetError() == GL_NO_ERROR;
}

void BakedPools::clear() {
  for (auto& p : pools_) for (auto& a : p.attributes) if (a.second.gpu) glDeleteBuffers(1, &a.second.gpu);
  pools_.clear(); byMesh_.clear();
}

bool validateBakedPools(const std::string& directory) {
  const auto dir = std::filesystem::path(directory);
  std::ifstream file(dir / "pool-queries.json"); Json::Value reference;
  Json::CharReaderBuilder builder; std::string error;
  if (!file || !Json::parseFromStream(builder, file, &reference, &error) || reference["format"].asString() != "SBPOOLCHECK1") return false;
  BakedPools pools; if (!pools.open((dir / "pool-data.json").string(), 100000)) return false;
  size_t frames = 0, compared = 0;
  for (const auto& frame : reference["frames"]) {
    Camera camera; const auto& p = frame["camera"]["position"], q = frame["camera"]["quaternion"];
    camera.position = {p[0].asFloat(), p[1].asFloat(), p[2].asFloat()};
    camera.quaternion = {q[0].asFloat(), q[1].asFloat(), q[2].asFloat(), q[3].asFloat()};
    if (!pools.update(camera)) { pools.clear(); return false; }
    for (const auto& expected : frame["pools"]) {
      auto* pool = pools.forMesh(expected["objectOrdinal"].asUInt());
      bool ok = pool && pool->shadowCount == expected["shadowCount"].asUInt() && pool->count == expected["selection"].size();
      if (ok) for (uint32_t j = 0; j < pool->count; j++) if (pool->selection[j] != expected["selection"][j].asUInt()) { ok = false; break; }
      if (!ok) {
        std::fprintf(stderr, "[baked-pools] mismatch at frame %zu, mesh %u: native %u/%u, JS %u/%u\n", frames, expected["objectOrdinal"].asUInt(), pool ? pool->count : 0, pool ? pool->shadowCount : 0, expected["selection"].size(), expected["shadowCount"].asUInt());
        pools.clear(); return false;
      }
      compared += pool->count;
    }
    frames++;
  }
  std::printf("[baked-pools] %zu camera frames match the original JS exactly (%zu ordered instances and shadow prefixes)\n", frames, compared);
  pools.clear(); return true;
}
