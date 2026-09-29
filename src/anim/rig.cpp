#include "anim/rig.h"
#include <cstdio>
#include <functional>

bool Rig::load(const std::string& glbPath) {
  if (!loadGlb(glbPath, model)) return false;
  size_t n = model.nodes.size();
  restT_.resize(n); restR_.resize(n); restS_.resize(n);
  for (size_t i = 0; i < n; i++) { restT_[i] = model.nodes[i].t; restR_[i] = model.nodes[i].r; restS_[i] = model.nodes[i].s; }
  poseT_ = restT_; poseR_ = restR_; poseS_ = restS_;
  local_.resize(n); world_.resize(n);
  std::function<void(int)> visit = [&](int i) { order_.push_back(i); for (int c : model.nodes[i].children) visit(c); };
  for (int r : model.roots) visit(r);
  computeWorld();
  // rest-pose bounds of the skinned mesh: feet to y = 0, height normalised to ~1.78 m if the export is off-scale (rig.js)
  float ymin = INF, ymax = -INF;
  std::vector<Mat4> pal; object_.identity(); skinMatrices(pal);
  for (const auto& m : model.meshes) for (const auto& p : m.prims) for (size_t v = 0; v < p.vertexCount(); v += 7) {
    Vec3 P{p.pos[v * 3], p.pos[v * 3 + 1], p.pos[v * 3 + 2]}, acc;
    for (int k = 0; k < 4; k++) { float w = p.weights[v * 4 + k]; if (w > 0) acc += pal[p.joints[v * 4 + k]].transformPoint(P) * w; }
    ymin = std::min(ymin, acc.y); ymax = std::max(ymax, acc.y);
  }
  float h = ymax - ymin;
  if (h > 0 && (h < 1.2f || h > 2.4f)) { modelScale = 1.78f / h; ymin *= modelScale; }
  heightOffset = -ymin;
  std::printf("[rig] %s: %zu nodes, %zu clips, height %.2f m, feet offset %.3f\n", glbPath.c_str(), n, model.clips.size(), h * modelScale, heightOffset);
  return true;
}

Rig::Action* Rig::startAction(int clip, float fade, bool loop, bool driven) {
  float rate = fade > 1e-3f ? 1.f / fade : 1e6f;
  Action* found = nullptr;
  for (auto& a : actions_) {
    if (a.clip == clip) { found = &a; a.target = 1; a.fadeRate = rate; }
    else { a.target = 0; a.fadeRate = rate; }
  }
  if (!found) {
    actions_.push_back({clip, 0.f, 1.f, 0.f, 1.f, rate, loop, driven});
    found = &actions_.back();
    if (actions_.size() == 1) found->weight = 1;
  }
  found->loop = loop; found->driven = driven;
  return found;
}

bool Rig::play(const std::string& name, float fade, float timeScale, bool loop) {
  int c = model.findClip(name); if (c < 0) return false;
  bool same = curName_ == name && !actions_.empty();
  Action* a = nullptr;
  if (same) { for (auto& x : actions_) if (x.clip == c) a = &x; }
  if (!a || (a->driven)) { a = startAction(c, fade, loop, false); if (!same) a->time = 0; }
  a->timeScale = timeScale; a->driven = false; a->loop = loop;
  curName_ = name;
  return true;
}

bool Rig::drive(const std::string& name, float t01, float fade) {
  int c = model.findClip(name); if (c < 0) return false;
  Action* a = nullptr;
  if (curName_ == name) for (auto& x : actions_) if (x.clip == c) a = &x;
  if (!a) a = startAction(c, fade, false, true);
  a->driven = true;
  a->time = clampf(t01, 0, 1) * model.clips[c].duration;
  curName_ = name;
  return true;
}

float Rig::currentTime01() const {
  for (const auto& a : actions_) if (a.target > 0.5f) return model.clips[a.clip].duration > 0 ? a.time / model.clips[a.clip].duration : 0;
  return 0;
}
void Rig::setTimeScale(float ts) { for (auto& a : actions_) if (a.target > 0.5f) a.timeScale = ts; }

void Rig::sample(const GltfClip& c, float t, std::vector<Vec3>& T, std::vector<Quat>& R, std::vector<Vec3>& S) const {
  for (const auto& ch : c.channels) {
    const auto& tm = ch.times; size_t n = tm.size(); if (!n) continue;
    size_t i1 = std::upper_bound(tm.begin(), tm.end(), t) - tm.begin();
    size_t i0 = i1 == 0 ? 0 : i1 - 1; if (i1 >= n) i1 = n - 1;
    float u = (i1 == i0 || ch.step) ? 0.f : clampf((t - tm[i0]) / (tm[i1] - tm[i0]), 0, 1);
    const float* v = ch.values.data();
    if (ch.path == 1) {
      Quat a{v[i0 * 4], v[i0 * 4 + 1], v[i0 * 4 + 2], v[i0 * 4 + 3]}, b{v[i1 * 4], v[i1 * 4 + 1], v[i1 * 4 + 2], v[i1 * 4 + 3]};
      R[ch.node] = Quat::slerp(a, b, u);
    } else {
      Vec3 a{v[i0 * 3], v[i0 * 3 + 1], v[i0 * 3 + 2]}, b{v[i1 * 3], v[i1 * 3 + 1], v[i1 * 3 + 2]};
      (ch.path == 0 ? T : S)[ch.node] = vlerp(a, b, u);
    }
  }
}

void Rig::update(float dt) {
  // clocks + fades
  for (auto& a : actions_) {
    const GltfClip& c = model.clips[a.clip];
    if (!a.driven) {
      a.time += dt * a.timeScale;
      if (a.loop && c.duration > 0) { a.time = std::fmod(a.time, c.duration); if (a.time < 0) a.time += c.duration; }
      else a.time = clampf(a.time, 0, c.duration);
    }
    float d = a.target - a.weight, step = a.fadeRate * dt;
    a.weight = std::fabs(d) <= step ? a.target : a.weight + (d > 0 ? step : -step);
  }
  actions_.erase(std::remove_if(actions_.begin(), actions_.end(), [](const Action& a) { return a.weight <= 0 && a.target <= 0; }), actions_.end());
  // blend: weighted sum over actions, remainder = rest pose
  size_t n = model.nodes.size();
  static std::vector<Vec3> T, S, accT, accS; static std::vector<Quat> R, accR;
  accT.assign(n, Vec3()); accS.assign(n, Vec3()); accR.assign(n, Quat(0, 0, 0, 0));
  float tot = 0;
  for (const auto& a : actions_) {
    if (a.weight <= 0) continue;
    T = restT_; R = restR_; S = restS_;
    sample(model.clips[a.clip], a.time, T, R, S);
    for (size_t i = 0; i < n; i++) {
      accT[i] += T[i] * a.weight; accS[i] += S[i] * a.weight;
      Quat q = R[i]; if (tot > 0 && q.dot(accR[i]) < 0) q = {-q.x, -q.y, -q.z, -q.w};
      accR[i] = {accR[i].x + q.x * a.weight, accR[i].y + q.y * a.weight, accR[i].z + q.z * a.weight, accR[i].w + q.w * a.weight};
    }
    tot += a.weight;
  }
  for (size_t i = 0; i < n; i++) {
    if (tot <= 1e-5f) { poseT_[i] = restT_[i]; poseR_[i] = restR_[i]; poseS_[i] = restS_[i]; continue; }
    float rem = std::max(0.f, 1.f - tot), k = 1.f / std::max(tot, 1.f);
    poseT_[i] = accT[i] * k + restT_[i] * rem; poseS_[i] = accS[i] * k + restS_[i] * rem;
    Quat q = accR[i], r = restR_[i]; if (rem > 0 && q.dot(r) < 0) r = {-r.x, -r.y, -r.z, -r.w};
    poseR_[i] = Quat(q.x * k + r.x * rem, q.y * k + r.y * rem, q.z * k + r.z * rem, q.w * k + r.w * rem).normalize();
  }
  computeWorld();
}

void Rig::computeWorld() {
  for (int i : order_) {
    local_[i] = Mat4::compose(poseT_[i], poseR_[i], poseS_[i]);
    int p = model.nodes[i].parent;
    world_[i] = p >= 0 ? world_[p] * local_[i] : local_[i];
  }
}

Vec3 Rig::boneWorld(const std::string& bone) const {
  int i = node(bone); if (i < 0) return object_.position();
  return (object_ * world_[i]).position();
}

Vec3 Rig::handWorld(char side) const {
  std::string S = side == 'L' ? ".L" : ".R";
  int h = node("hand" + S); if (h < 0) return object_.transformPoint({0, 1.5f, 0});
  Vec3 p = (object_ * world_[h]).position();
  int m = node("middle1" + S);
  if (m >= 0) p.lerp((object_ * world_[m]).position(), 0.6f);
  return p;
}

// Rotate a bone (world space) so the direction to its first child points along worldDir. Weighted (rig.js aimBone).
void Rig::aimBone(const std::string& bone, const Vec3& worldDir, float w) {
  int b = node(bone); if (b < 0 || w <= 0 || model.nodes[b].children.empty()) return;
  int child = model.nodes[b].children[0];
  // pick the child along the limb (forearm for the upper arm, hand for the forearm) when there are several
  for (int ch : model.nodes[b].children) if (model.nodes[ch].name.find("Twist") == std::string::npos && model.nodes[ch].children.size()) { child = ch; break; }
  Mat4 W = object_ * world_[b], Wc = object_ * world_[child];
  Vec3 cur = (Wc.position() - W.position()).normalized();
  Quat d = Quat::fromUnitVectors(cur, worldDir.normalized());
  Quat wq = W.rotation();
  Quat target = d * wq;
  int p = model.nodes[b].parent;
  Quat pw = p >= 0 ? (object_ * world_[p]).rotation() : object_.rotation();
  Quat localTarget = pw.inverse() * target;
  poseR_[b] = Quat::slerp(poseR_[b], localTarget.normalize(), w);
  computeWorld();
}

void Rig::skinMatrices(std::vector<Mat4>& out) const {
  if (model.skins.empty()) { out.clear(); return; }
  const GltfSkin& s = model.skins[0];
  out.resize(s.joints.size());
  for (size_t k = 0; k < s.joints.size(); k++) out[k] = object_ * world_[s.joints[k]] * s.inverseBind[k];
}
