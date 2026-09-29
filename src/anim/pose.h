// Animation-layer foundations (ports of player/anim/skeleton.js, builder.js, rigdata.js, clips.js):
//  - "character space" = the local frame of the rig object (X = char left, Y = up, Z = forward, feet at y = 0).
//  - Pose: LOCAL quaternions + positions for every skeleton bone.
//  - Skel: bone list / parents / bind pose (character space), FK, mirroring.
//  - ClipLib: clip sampling into poses, hips lock, automatic clip analysis (loco speed / phase, contact time, yaw).
//  - PoseBuilder: FK-aware character-space editing incl. analytic 2-bone IK.
//  - RigData: derived metrics + finger curl.
#pragma once
#include "anim/rig.h"
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// ------------------------------------------------------------------ math helpers (three.js semantics)
constexpr float TAU = 2 * PI;
inline float smooth01(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
inline float smoother01(float t) { t = clampf(t, 0, 1); return t * t * t * (t * (t * 6 - 15) + 10); }
inline float remapf(float x, float a, float b, float c = 0, float d = 1) { return lerpf(c, d, clampf((x - a) / (b - a), 0, 1)); }
inline Quat qEulerYXZ(float x, float y, float z) { return Quat::axisAngle({0, 1, 0}, y) * Quat::axisAngle({1, 0, 0}, x) * Quat::axisAngle({0, 0, 1}, z); }
inline Vec3 applyAxisAngle(const Vec3& v, const Vec3& axis, float a) { return Quat::axisAngle(axis, a) * v; }
inline float angleTo(const Vec3& a, const Vec3& b) { float d = std::sqrt(a.lengthSq() * b.lengthSq()); if (d < 1e-12f) return PI / 2; return std::acos(clampf(a.dot(b) / d, -1, 1)); }
inline float lerpAngle(float a, float b, float t) { float d = std::atan2(std::sin(b - a), std::cos(b - a)); return a + d * t; }
inline Quat slerpTo(const Quat& a, const Quat& b, float t) { return Quat::slerp(a, b, t); }
Quat frameRot(const Vec3& d0, const Vec3& h0, const Vec3& d1, const Vec3& h1);
Quat basisQ(const Vec3& d, const Vec3& n);  // maps X -> d, Z -> n (n orthogonalised against d)
float noise1(float x, float seed = 0);
Vec3 bez3(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t);

struct Spring {
  float x = 0, v = 0, f = 4, z = 0.6f;
  Spring() = default;
  Spring(float x0, float freq, float zeta) : x(x0), f(freq), z(zeta) {}
  float step(float target, float dt);
};
struct Spring3 {
  Vec3 x, v; float f = 4, z = 0.6f;
  Spring3() = default;
  Spring3(float freq, float zeta) : f(freq), z(zeta) {}
  Vec3 step(const Vec3& target, float dt);
};

// ------------------------------------------------------------------ pose
struct Pose {
  int n = 0;
  std::vector<float> q, p;
  Pose() = default;
  explicit Pose(int n_) : n(n_), q(n_ * 4, 0.f), p(n_ * 3, 0.f) {}
  Pose& copy(const Pose& o) { q = o.q; p = o.p; n = o.n; return *this; }
  Quat getQ(int i) const { const float* a = &q[i * 4]; return {a[0], a[1], a[2], a[3]}; }
  void setQ(int i, const Quat& v) { float* a = &q[i * 4]; a[0] = v.x; a[1] = v.y; a[2] = v.z; a[3] = v.w; }
  Vec3 getP(int i) const { const float* a = &p[i * 3]; return {a[0], a[1], a[2]}; }
  void setP(int i, const Vec3& v) { float* a = &p[i * 3]; a[0] = v.x; a[1] = v.y; a[2] = v.z; }
};
// out = a*(1-t) + b*t (nlerp, hemisphere fix); mask scales t per bone
void blendPoses(const Pose& a, const Pose& b, float t, Pose& out, const std::vector<float>* mask = nullptr);

// ------------------------------------------------------------------ skeleton
class Skel {
 public:
  int N = 0;
  std::vector<int> node;              // bone -> rig node index
  std::vector<std::string> name;      // sanitized (three.js: dots removed)
  std::vector<int> parent, child, mirror;
  std::unordered_map<std::string, int> byName, key;
  std::vector<Quat> baseQ; std::vector<Vec3> baseP; float scale = 1;
  Pose rest;
  std::vector<float> cq, cp, bindQ, bindP, len;
  unsigned gen = 0;

  void build(const Rig& rig);
  int idx(const std::string& k) const { auto it = key.find(k); if (it != key.end()) return it->second; auto b = byName.find(k); return b != byName.end() ? b->second : -1; }
  void fk(const Pose& pose) { fk(pose, cq, cp); gen++; }
  void fk(const Pose& pose, std::vector<float>& q, std::vector<float>& p) const;
  Quat parentCQ(int i) const { int pi = parent[i]; if (pi < 0) return baseQ[i]; return cQ(pi); }
  Quat cQ(int i) const { const float* a = &cq[i * 4]; return {a[0], a[1], a[2], a[3]}; }
  Vec3 cP(int i) const { const float* a = &cp[i * 3]; return {a[0], a[1], a[2]}; }
  Quat bQ(int i) const { const float* a = &bindQ[i * 4]; return {a[0], a[1], a[2], a[3]}; }
  Vec3 bP(int i) const { const float* a = &bindP[i * 3]; return {a[0], a[1], a[2]}; }
  void mirrorPose(const Pose& src, Pose& out);
  // write a pose into the rig's node locals
  void apply(const Pose& pose, Rig& rig) const;
};

// ------------------------------------------------------------------ clips
class ClipLib {
 public:
  struct LocoMeta { float v = 1, phase0 = 0, dur = 1, pass = 0; bool ok = false; };
  void build(const Rig& rig, Skel& skel);
  bool has(const std::string& n) const { return ids_.count(n) > 0; }
  float dur(const std::string& n) const { auto it = ids_.find(n); return it == ids_.end() ? 0 : rig_->model.clips[it->second].duration; }
  const char* first(std::initializer_list<const char*> names) const { for (const char* n : names) if (n && has(n)) return n; return nullptr; }
  // lock: 0 none, 1 'xz' (default), 2 'xyz'
  bool sample(const std::string& n, float t, Pose& out, bool loop = true, int lock = 1) const;
  void sampleRaw(const std::string& n, float t, Pose& out, bool loop) const;
  void lockHips(Pose& pose, int lock) const;
  LocoMeta loco(const std::string& n, bool wall = false);
  float contactTime(const std::string& n);
  float yawDelta(const std::string& n);
 private:
  const Rig* rig_ = nullptr;
  Skel* s_ = nullptr;
  std::unordered_map<std::string, int> ids_;
  std::vector<int> nodeToBone_;
  std::map<std::string, LocoMeta> locoMeta_;
  std::map<std::string, float> meta_;
  mutable Pose tmp_;
};

// ------------------------------------------------------------------ pose builder
class PoseBuilder {
 public:
  Skel* s = nullptr;
  Pose* pose = nullptr;
  bool dirty = true;
  unsigned gen = 0;
  explicit PoseBuilder(Skel* sk = nullptr) : s(sk) {}
  PoseBuilder& begin(Pose& p) { pose = &p; dirty = true; return *this; }
  void fk() { if (dirty || gen != s->gen) { s->fk(*pose); dirty = false; gen = s->gen; } }
  int i(const std::string& k) const { return s->idx(k); }
  Vec3 pos(const std::string& k) { return pos(i(k)); }
  Vec3 pos(int i) { if (i < 0) return {}; fk(); return s->cP(i); }
  Quat cq(const std::string& k) { return cq(i(k)); }
  Quat cq(int i) { if (i < 0) return {}; fk(); return s->cQ(i); }
  PoseBuilder& setCQ(int i, const Quat& Q, float w = 1);
  PoseBuilder& setCQ(const std::string& k, const Quat& Q, float w = 1) { return setCQ(i(k), Q, w); }
  PoseBuilder& rot(int i, const Vec3& axis, float angle);
  PoseBuilder& rot(const std::string& k, const Vec3& axis, float angle) { return rot(i(k), axis, angle); }
  PoseBuilder& rotE(int i, float pitch, float yaw = 0, float roll = 0);
  PoseBuilder& rotE(const std::string& k, float pitch, float yaw = 0, float roll = 0) { return rotE(i(k), pitch, yaw, roll); }
  PoseBuilder& twist(const std::string& k, float angle);
  PoseBuilder& aim(int i, const Vec3& d, float w = 1);
  PoseBuilder& aim(const std::string& k, const Vec3& d, float w = 1) { return aim(i(k), d, w); }
  PoseBuilder& fromBind(int i, const Quat& R, float w = 1);
  PoseBuilder& fromBind(const std::string& k, const Quat& R, float w = 1) { return fromBind(i(k), R, w); }
  PoseBuilder& moveHips(float dx, float dy, float dz);
  PoseBuilder& setHipsChar(const Vec3& p) { Vec3 c = pos("hips"); return moveHips(p.x - c.x, p.y - c.y, p.z - c.z); }
  // analytic 2-bone IK (kind 'arm' / 'leg'); absolute = hinge frames rebuilt from bind (no candy-wrapper twist)
  float ik(bool arm, char S, const Vec3& target, const Vec3* pole, float w = 1, bool absolute = true, bool keepEnd = false);
  float ik(const char* kind, char S, const Vec3& target, const Vec3& pole, float w = 1, bool absolute = true) { return ik(kind[0] == 'a', S, target, &pole, w, absolute); }
  PoseBuilder& orient(const std::string& k, const Vec3& d, const Vec3& up, const Vec3& refAxisBind, float w = 1);
 private:
  void aimTo(int i, Vec3 from, Vec3 to);
};

// ------------------------------------------------------------------ rig metrics
class RigData {
 public:
  Skel* s = nullptr;
  Vec3 hipsP; float hipY = 0.95f;
  Vec3 thigh[2], knee[2], ankle[2], shoulder[2], elbow[2], wrist[2], toe[2]; // [0] = L, [1] = R
  float l1 = 0, l2 = 0, legLen = 0, a1 = 0, a2 = 0, ankleH = 0, hipHalfW = 0, shoulderHalfW = 0;
  Vec3 ballOff, heelOff;
  struct Finger { int i; Vec3 axis; float amt; };
  std::vector<Finger> fingers[2];
  void build(Skel& sk);
  void curl(Pose& pose, char S, float amount, float w = 1) const;
  static int side(char S) { return S == 'L' ? 0 : 1; }
};
struct ArmTarget { Vec3 elbow, hand; };
ArmTarget armTarget(const RigData& rd, char S, float flex, float abd, float elbow, float twist = 0);
