// Traversal spatial helpers (ports of player/traversal/collide.js, anchors.js, zippoints.js) over the World box list.
#pragma once
#include "world/world.h"
#include "gfx/camera.h"
#include <optional>
#include <unordered_map>

// ---------------------------------------------------------------- collide.js
struct Contact { Vec3 normal, point; int box = -1; float depth = 0, top = 0; };
// Push a vertical capsule (feet position, radius r, height h) out of every box it overlaps horizontally. Only the part of
// the body above stepH over the feet collides (lower overlaps are floors / steps handled by ground snapping).
std::optional<Contact> pushOutCapsule(const World& w, Vec3& feet, float r, float h, float stepH);

// ---------------------------------------------------------------- anchors.js
struct Anchor { Vec3 point, normal; float L = 0, lat = 0; std::string kind; };
class AnchorFinder {
 public:
  explicit AnchorFinder(const World& w) : world_(w) {}
  // pos: body centre; fwd: horizontal travel dir (unit); turn: horizontal steer dir or null; speed: m/s
  std::optional<Anchor> find(const Vec3& pos, const Vec3& fwd, const Vec3* turn, float speed, float floorY);
 private:
  const World& world_;
  struct Cand { Vec3 point, normal; float L, score, lat; };
  std::vector<Cand> cands_;
  std::vector<int> near_;
  struct Pass { float ahead, up, radius, minAbove, minL, maxL, minAhead, maxLat, elevLo, elevHi; const Vec3* turn = nullptr; };
  void faceCandidates(const Vec3& pos, const Vec3& D, const Vec3& fwd, const Vec3& right, const Pass& o);
  std::optional<Anchor> confirm(const Vec3& pos, const Cand& c, bool strict);
  bool arcClear(const Vec3& pos, float pivotY, const Vec3& pivot, float rope);
  std::optional<Anchor> coneRays(const Vec3& pos, const Vec3& fwd);
};

// ---------------------------------------------------------------- zippoints.js (aim targeting for the reticle)
struct ZipTarget { Vec3 pos, normal; std::string kind; float dist = 0, sx = 0, sy = 0; };
struct ZipCandidate { ZipTarget t; float ang = 0, score = 0; bool best = false; };
class ZipTargeting {
 public:
  explicit ZipTargeting(const World& w) : world_(w) {}
  struct Opts { bool enabled = true; const Vec3* exclude = nullptr; bool air = false; const Vec3* perchOut = nullptr; };
  const ZipTarget* update(float dt, const Camera& camera, const Vec3& eye, const Opts& o);
  const ZipTarget* best() const { return hasBest_ ? &best_ : nullptr; }
  const std::vector<ZipCandidate>& candidates() const { return shown_; }
 private:
  const World& world_;
  static constexpr float RANGE = 58;
  std::vector<ZipPoint> pool_;
  float poolT_ = 99, time_ = 0;
  bool hasBest_ = false; ZipTarget best_; int64_t bestKey_ = 0;
  struct Meta { float h; bool ok; };
  struct Vis { float t; bool ok; };
  std::unordered_map<int64_t, Meta> meta_;
  std::unordered_map<int64_t, Vis> vis_;
  std::vector<ZipCandidate> shown_;
  static int64_t key(const Vec3& p);
  Meta pointMeta(const ZipPoint& p);
  bool visible(const ZipPoint& p, const Vec3& eye);
};
