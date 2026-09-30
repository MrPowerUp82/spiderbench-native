#include "world/baked_traversal.h"
#include <zlib.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace {
constexpr uint32_t UNIFORM = 0x80000000u;
struct Reader {
  const uint8_t* p; size_t left;
  template<class T> bool one(T& v) { if (left < sizeof(T)) return false; std::memcpy(&v, p, sizeof(T)); p += sizeof(T); left -= sizeof(T); return true; }
  template<class T> bool array(std::vector<T>& out, size_t n) {
    if (n > left / sizeof(T)) return false;
    out.resize(n); if (n) std::memcpy(out.data(), p, n * sizeof(T));
    p += n * sizeof(T); left -= n * sizeof(T); return true;
  }
};
}  // namespace

bool BakedTraversal::load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (file.size() < 16 || std::memcmp(file.data(), "SBTRV1\0\0", 8)) return false;
  uint32_t version, rawSize;
  std::memcpy(&version, file.data() + 8, 4); std::memcpy(&rawSize, file.data() + 12, 4);
  if (version != 1 || rawSize > (1u << 30)) return false;
  std::vector<uint8_t> raw(rawSize);
  uLongf got = rawSize;
  if (uncompress(raw.data(), &got, file.data() + 16, uLong(file.size() - 16)) != Z_OK || got != rawSize) return false;

  Reader r{raw.data(), raw.size()};
  uint32_t paletteCount, subCount, blockCount, treeCount;
  if (!r.one(fx0_) || !r.one(fz0_) || !r.one(fine_) || !r.one(sub_) || !r.one(top_) || !r.one(tnx_) || !r.one(tnz_)) return false;
  if (!r.one(cx0_) || !r.one(cz0_) || !r.one(coarseCell_) || !r.one(cnx_) || !r.one(cnz_)) return false;
  if (fine_ <= 0 || coarseCell_ <= 0 || !sub_ || !top_ || sub_ * sub_ > 4096 || top_ * top_ > 4096) return false;
  if (uint64_t(tnx_) * tnz_ > 4000000 || uint64_t(cnx_) * cnz_ > 16000000) return false;
  if (!r.one(paletteCount) || paletteCount == 0 || paletteCount > 256 || !r.array(palette_, paletteCount)) return false;
  if (!r.array(topTiles_, size_t(tnx_) * tnz_) || !r.one(subCount) || !r.array(subTiles_, subCount)) return false;
  if (!r.one(blockCount) || !r.array(blocks_, size_t(blockCount) * sub_ * sub_) || !r.array(coarse_, size_t(cnx_) * cnz_)) return false;
  std::vector<float> treeData;
  if (!r.one(treeCount) || !r.array(treeData, size_t(treeCount) * 5) || r.left) return false;
  // every reference must stay inside its table
  const size_t subsPerTop = size_t(top_) * top_;
  for (uint32_t t : topTiles_) if (t & UNIFORM ? (t & 0xff) >= paletteCount : size_t(t) + subsPerTop > subTiles_.size()) return false;
  for (uint32_t s : subTiles_) if (s & UNIFORM ? (s & 0xff) >= paletteCount : s >= blockCount) return false;
  for (uint8_t v : blocks_) if (v >= paletteCount) return false;
  for (uint8_t v : coarse_) if (v >= paletteCount) return false;
  water_ = palette_[coarse_[0]]; // the coarse raster's corner lies in open water
  trees.resize(treeCount);
  for (size_t i = 0; i < trees.size(); i++) {
    const float* t = treeData.data() + i * 5;
    trees[i] = {{t[0], t[1], t[2]}, t[3], t[4]};
  }
  std::printf("[bake] terrain %ux%u top tiles (%zu mixed sub-tiles), %zu palette heights, %zu tree anchors\n",
              tnx_, tnz_, size_t(blockCount), palette_.size(), trees.size());
  return true;
}

float BakedTraversal::terrain(float x, float z) const {
  if (palette_.empty()) return water_;
  const int64_t ix = int64_t(std::floor((double(x) - fx0_) / fine_)), iz = int64_t(std::floor((double(z) - fz0_) / fine_));
  const int64_t n = int64_t(sub_) * top_;
  if (ix >= 0 && iz >= 0 && ix < int64_t(tnx_) * n && iz < int64_t(tnz_) * n) {
    const uint32_t t = topTiles_[size_t(iz / n) * tnx_ + size_t(ix / n)];
    if (t & UNIFORM) return palette_[t & 0xff];
    const uint32_t s = subTiles_[t + size_t((iz / sub_) % top_) * top_ + size_t((ix / sub_) % top_)];
    if (s & UNIFORM) return palette_[s & 0xff];
    return palette_[blocks_[size_t(s) * sub_ * sub_ + size_t(iz % sub_) * sub_ + size_t(ix % sub_)]];
  }
  const int64_t cx = int64_t(std::floor((double(x) - cx0_) / coarseCell_)), cz = int64_t(std::floor((double(z) - cz0_) / coarseCell_));
  if (cx >= 0 && cz >= 0 && cx < cnx_ && cz < cnz_) return palette_[coarse_[size_t(cz) * cnx_ + size_t(cx)]];
  return water_;
}
