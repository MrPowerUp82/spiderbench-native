#include "world/baked_geometry.h"
#include <zlib.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
uint32_t le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
}

bool BakedGeometryArchive::open(const std::string& manifestPath) {
  std::ifstream mf(manifestPath);
  if (!mf) return false;
  Json::CharReaderBuilder builder;
  std::string err;
  Json::Value root;
  if (!Json::parseFromStream(builder, mf, &root, &err) || root["format"].asString() != "SBGEO01" ||
      root["version"].asInt() != 1 || !root["blobs"].isArray() ||
      !root["meshes"].isArray() || !root["materials"].isArray()) return false;
  const std::filesystem::path archive = std::filesystem::path(manifestPath).parent_path() / root["archive"].asString();
  std::ifstream f(archive, std::ios::binary);
  char magic[8];
  if (!f.read(magic, 8) || std::memcmp(magic, "SBGEO01\0", 8)) return false;
  root_ = std::move(root);
  archivePath_ = archive.string();
  return true;
}

bool BakedGeometryArchive::readBlob(uint32_t id, std::vector<uint8_t>& out) const {
  if (id >= root_["blobs"].size() || archivePath_.empty()) return false;
  const Json::Value& meta = root_["blobs"][id];
  const uint64_t offset = meta["offset"].asUInt64();
  const uint64_t expectedRaw = meta["bytes"].asUInt64();
  const uint64_t expectedPacked = meta["compressedBytes"].asUInt64();
  if (expectedRaw > 512ull * 1024 * 1024 || expectedPacked > 512ull * 1024 * 1024 ||
      offset > uint64_t(std::numeric_limits<std::streamoff>::max())) return false;
  std::ifstream f(archivePath_, std::ios::binary);
  if (!f.seekg(std::streamoff(offset))) return false;
  uint8_t header[8];
  if (!f.read(reinterpret_cast<char*>(header), 8)) return false;
  const uint32_t rawSize = le32(header), packedSize = le32(header + 4);
  if (rawSize != expectedRaw || packedSize != expectedPacked) return false;
  std::vector<uint8_t> packed(packedSize);
  if (packedSize && !f.read(reinterpret_cast<char*>(packed.data()), packedSize)) return false;
  if (rawSize == 0) {
    uint8_t dummy = 0; uLongf n = 1;
    const bool ok = uncompress(&dummy, &n, packed.data(), packedSize) == Z_OK && n == 0;
    out.clear(); return ok;
  }
  out.resize(rawSize);
  uLongf n = rawSize;
  if (uncompress(out.data(), &n, packed.data(), packedSize) != Z_OK || n != rawSize) {
    out.clear(); return false;
  }
  return true;
}
