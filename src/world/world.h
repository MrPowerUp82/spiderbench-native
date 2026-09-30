// The city (port of the world/city.js contract used by traversal, camera and HUD):
//   raycast(origin, dir, max) -> {point, normal, distance} | none
//   groundHeight(x, z[, y])   -> highest walkable surface at (x, z) at or below y
//   buildings (AABB list)     -> anchor faces, roof-edge zip points, capsule collision (traversal/collide.js fallback path)
//   getZipPoints(c, r)        -> perch / zip targets (roof edges + corners derived from the boxes, lamp tops)
//   trees                     -> low swing anchors in the park (anchors.js treePoints)
// Buildings are generated procedurally per block (seeded, deterministic); buildings.js / facade.js / rooftops.js detail
// is replaced by a facade shader + simple massing (podium / tower / crown, parapets, water towers, rooftop units).
#pragma once
#include "core/math.h"
#include "gfx/mesh.h"
#include <memory>
#include <string>
#include <vector>

class BakedCollision; class BakedTraversal;

enum BoxKind : uint8_t { K_BUILDING, K_TIER, K_PARAPET, K_ROOFBOX, K_WATERTOWER, K_POLE, K_TRUNK };

struct Box { Vec3 mn, mx; uint8_t kind; };
struct Hit { Vec3 point, normal; float distance = 0; };
struct ZipPoint { Vec3 pos, normal; std::string kind; int box = -1; };
struct TreePt { Vec3 pos; float cy, r; };

class World {
 public:
  World();
  ~World();
  Vec3 spawn{250, 0, 160 + 5 + 2.3f};
  std::vector<Box> boxes;
  std::vector<TreePt> trees;
  std::vector<ZipPoint> lampPoints;
  MeshBuilder cityMesh, farMesh;
  GpuMesh cityGpu, farGpu;

  void build(uint32_t seed = 1234);
  // The original city exported by tools/ref (collision.sbcol + traversal.sbtrv): exact solids, zip points, terrain and
  // tree anchors replace the procedural boxes for every query (collision.js makeQueries / collide.js createCollider).
  bool loadBaked(const std::string& directory);
  const BakedCollision* collision() const { return coll_.get(); }
  void upload() { cityGpu.upload(cityMesh, 256); farGpu.upload(farMesh, 1500); cityMesh.clear(); farMesh.clear(); }

  bool raycast(const Vec3& o, const Vec3& d, float maxDist, Hit& out) const;
  bool raycast(const Vec3& o, const Vec3& d, float maxDist) const { Hit h; return raycast(o, d, maxDist, h); }
  float groundHeight(float x, float z, float y = INF) const;
  // indices of boxes whose XZ footprint intersects the square (x +- r, z +- r)
  void nearBoxes(float x, float z, float r, std::vector<int>& out) const;
  bool inside(const Vec3& p, float m = 0.05f) const;
  void getZipPoints(const Vec3& c, float r, std::vector<ZipPoint>& out) const;
  void treesNear(const Vec3& p, float r, std::vector<const TreePt*>& out) const;
  std::string districtAt(float x, float z) const;
  float terrainAt(float x, float z) const; // ground.js terrainHeight (baked raster or the layout port)

 private:
  std::unique_ptr<BakedCollision> coll_;
  std::unique_ptr<BakedTraversal> trav_;
  std::vector<std::string> zipKinds_;
  std::vector<uint32_t> zipStart_, zipItems_;
  int zipNx_ = 0, zipNz_ = 0; float zipX0_ = 0, zipZ0_ = 0;
  static constexpr float ZIP_CELL = 16;
  bool rayBaked(const Vec3& o, const Vec3& d, float maxDist, Hit& out) const;
  static constexpr float CELL = 24, GX0 = -900, GZ0 = -3600;
  int gw_ = 0, gh_ = 0;
  std::vector<std::vector<int>> grid_;
  mutable std::vector<uint32_t> stamp_; mutable uint32_t frame_ = 1;
  mutable std::vector<std::vector<ZipPoint>> zipCache_; mutable std::vector<uint8_t> zipCached_;
  void index();
  int addBox(const Vec3& mn, const Vec3& mx, BoxKind k) { boxes.push_back({mn, mx, k}); return (int)boxes.size() - 1; }
  void buildGround();
  void buildBlock(float x0, float z0, float x1, float z1, Mulberry32& rng, bool avW, bool avE);
  void buildBuilding(float x0, float z0, float x1, float z1, float H, Mulberry32& rng);
  void buildPark(Mulberry32& rng);
  void buildStreetFurniture(Mulberry32& rng);
  void buildFarShores(Mulberry32& rng);
  void addTree(float x, float z, float h, Mulberry32& rng);
  void boxZipPoints(int i, std::vector<ZipPoint>& out) const;
};
