#include "anim/pose.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <regex>

// ================================================================== helpers
Quat frameRot(const Vec3& d0, const Vec3& h0, const Vec3& d1, const Vec3& h1) {
  Vec3 a0 = d0.normalized(), b0 = (h0 - a0 * h0.dot(a0)).normalized(), c0 = a0.cross(b0);
  Vec3 a1 = d1.normalized(), b1 = (h1 - a1 * h1.dot(a1)).normalized(), c1 = a1.cross(b1);
  return (Quat::fromBasis(a1, b1, c1) * Quat::fromBasis(a0, b0, c0).conj()).normalize();
}
Quat basisQ(const Vec3& d, const Vec3& n) {
  Vec3 x = d.normalized(), z = (n - x * n.dot(x)).normalized(), y = z.cross(x);
  return Quat::fromBasis(x, y, z);
}
float noise1(float x, float seed) {
  auto h = [&](float n) { float s = std::sin(n * 127.1f + seed * 311.7f) * 43758.5453f; return s - std::floor(s); };
  float i = std::floor(x), f = x - i, u = f * f * (3 - 2 * f);
  return (h(i) * (1 - u) + h(i + 1) * u) * 2 - 1;
}
Vec3 bez3(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t) {
  float u = 1 - t, a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
  return p0 * a + p1 * b + p2 * c + p3 * d;
}
float Spring::step(float target, float dt) {
  float w = TAU * f; int n = std::max(1, (int)std::ceil(dt / (1.f / 120))); float h = dt / n;
  for (int i = 0; i < n; i++) { float a = w * w * (target - x) - 2 * z * w * v; v += a * h; x += v * h; }
  return x;
}
Vec3 Spring3::step(const Vec3& target, float dt) {
  float w = TAU * f; int n = std::max(1, (int)std::ceil(dt / (1.f / 120))); float h = dt / n;
  for (int i = 0; i < n; i++) { Vec3 a = (target - x) * (w * w) - v * (2 * z * w); v += a * h; x += v * h; }
  return x;
}

void blendPoses(const Pose& a, const Pose& b, float t, Pose& out, const std::vector<float>* mask) {
  const int n = a.n;
  for (int i = 0; i < n; i++) {
    float w = mask ? t * (*mask)[i] : t;
    int k = i * 4, j = i * 3;
    if (w <= 0) { if (&out != &a) { for (int c = 0; c < 4; c++) out.q[k + c] = a.q[k + c]; for (int c = 0; c < 3; c++) out.p[j + c] = a.p[j + c]; } continue; }
    if (w >= 1) { for (int c = 0; c < 4; c++) out.q[k + c] = b.q[k + c]; for (int c = 0; c < 3; c++) out.p[j + c] = b.p[j + c]; continue; }
    float dot = a.q[k] * b.q[k] + a.q[k + 1] * b.q[k + 1] + a.q[k + 2] * b.q[k + 2] + a.q[k + 3] * b.q[k + 3];
    float s = dot < 0 ? -w : w, r = 1 - w;
    float x = a.q[k] * r + b.q[k] * s, y = a.q[k + 1] * r + b.q[k + 1] * s, z = a.q[k + 2] * r + b.q[k + 2] * s, ww = a.q[k + 3] * r + b.q[k + 3] * s;
    float l = 1.f / std::sqrt(x * x + y * y + z * z + ww * ww);
    out.q[k] = x * l; out.q[k + 1] = y * l; out.q[k + 2] = z * l; out.q[k + 3] = ww * l;
    for (int c = 0; c < 3; c++) out.p[j + c] = a.p[j + c] * r + b.p[j + c] * w;
  }
}

// ================================================================== skeleton
static std::string sanitize(const std::string& n) { std::string o; for (char c : n) if (c != '.' && c != ':' && c != '/' && c != '[' && c != ']') o += c == ' ' ? '_' : c; return o; }
static std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

void Skel::build(const Rig& rig) {
  const GltfModel& m = rig.model;
  std::vector<char> isJoint(m.nodes.size(), 0);
  if (!m.skins.empty()) for (int j : m.skins[0].joints) isJoint[j] = 1;
  std::vector<int> boneOf(m.nodes.size(), -1);
  std::function<void(int)> visit = [&](int i) {
    if (isJoint[i]) { boneOf[i] = (int)node.size(); node.push_back(i); }
    for (int c : m.nodes[i].children) visit(c);
  };
  for (int r : m.roots) visit(r);
  N = (int)node.size();
  parent.assign(N, -1); name.resize(N);
  rest = Pose(N);
  Mat4 C = Mat4::translate({0, rig.heightOffset, 0}) * Mat4::scale({rig.modelScale, rig.modelScale, rig.modelScale});
  baseQ.resize(N); baseP.resize(N);
  // rest-pose model-space world matrices of all nodes
  std::vector<Mat4> W(m.nodes.size());
  std::function<void(int, const Mat4&)> fw = [&](int i, const Mat4& P) { W[i] = P * Mat4::compose(m.nodes[i].t, m.nodes[i].r, m.nodes[i].s); for (int c : m.nodes[i].children) fw(c, W[i]); };
  for (int r : m.roots) fw(r, Mat4());
  for (int b = 0; b < N; b++) {
    const GltfNode& nd = m.nodes[node[b]];
    name[b] = sanitize(nd.name); byName[name[b]] = b;
    rest.setQ(b, nd.r); rest.setP(b, nd.t);
    int pn = nd.parent;
    if (pn >= 0 && boneOf[pn] >= 0) parent[b] = boneOf[pn];
    else { // root bone: frame of its parent object in character space
      Mat4 PM = pn >= 0 ? C * W[pn] : C;
      baseP[b] = PM.position(); baseQ[b] = PM.rotation();
      scale = Vec3{PM.m[4], PM.m[5], PM.m[6]}.length();
    }
  }
  // logical keys (rig.js resolveBones for this rig's names; three.js strips the dots: 'upperArm.L' -> 'upperArmL')
  auto find = [&](const char* re, const char* excl = nullptr) {
    std::regex r(re, std::regex::icase), x(excl ? excl : "$^", std::regex::icase);
    for (int b = 0; b < N; b++) if (std::regex_search(name[b], r) && !(excl && std::regex_search(lower(name[b]), x))) return b;
    return -1;
  };
  auto setk = [&](const char* k, int b) { if (b >= 0) key[k] = b; };
  setk("hips", find("^(hips|pelvis|hip)$"));
  setk("spine", find("^spine$|^spine_?0*1$"));
  setk("spine1", find("^spine0*1$|^spine_?01$"));
  setk("chest", find("upper_?chest|spine0*3|spine_03|^spine2$"));
  setk("neck", find("neck"));
  setk("head", find("^head$"));
  for (const char* S : {"L", "R"}) {
    std::string s(S);
    setk(("shoulder" + s).c_str(), find(("^(shoulder|clavicle)_?" + s + "$").c_str()));
    setk(("upperArm" + s).c_str(), find(("upper_?arm_?" + s + "$").c_str()));
    setk(("lowerArm" + s).c_str(), find(("^(fore_?arm|lower_?arm)_?" + s + "$").c_str()));
    setk(("hand" + s).c_str(), find(("^(hand|wrist)_?" + s + "$").c_str()));
    setk(("upperLeg" + s).c_str(), find(("^(thigh|upper_?leg|up_?leg)_?" + s + "$").c_str()));
    setk(("lowerLeg" + s).c_str(), find(("^(shin|calf|lower_?leg)_?" + s + "$").c_str()));
    setk(("foot" + s).c_str(), find(("^(foot|ankle)_?" + s + "$").c_str()));
    setk(("toe" + s).c_str(), find(("^toes?_?" + s + "$").c_str()));
  }
  if (key.count("spine1") && (key["spine1"] == key["spine"] || key["spine1"] == key["chest"])) key.erase("spine1");
  cq.assign(N * 4, 0); cp.assign(N * 3, 0);
  fk(rest);
  bindQ = cq; bindP = cp;
  // child for aiming: last bone child, then logical chain preferences, hand -> middle finger base
  child.assign(N, -1);
  for (int i = N - 1; i >= 0; i--) { int p = parent[i]; if (p >= 0) child[p] = i; }
  const char* pref[][2] = {{"upperArmL", "lowerArmL"}, {"lowerArmL", "handL"}, {"upperArmR", "lowerArmR"}, {"lowerArmR", "handR"},
                           {"upperLegL", "lowerLegL"}, {"lowerLegL", "footL"}, {"upperLegR", "lowerLegR"}, {"lowerLegR", "footR"}, {"neck", "head"}, {"chest", "neck"}};
  for (auto& pr : pref) if (key.count(pr[0]) && key.count(pr[1])) child[key[pr[0]]] = key[pr[1]];
  for (const char* S : {"L", "R"}) {
    int h = idx(std::string("hand") + S); if (h < 0) continue;
    for (int b = 0; b < N; b++) if (parent[b] == h && lower(name[b]).find("middle") != std::string::npos) { child[h] = b; break; }
  }
  // mirror map by name (L <-> R suffix)
  mirror.assign(N, 0);
  for (int b = 0; b < N; b++) {
    const std::string& n = name[b]; int mm = b;
    if (!n.empty() && (n.back() == 'L' || n.back() == 'R')) {
      std::string c = n; c.back() = n.back() == 'L' ? 'R' : 'L';
      auto it = byName.find(c); if (it != byName.end()) mm = it->second;
    }
    mirror[b] = mm;
  }
  len.assign(N, 0);
  for (int b = 0; b < N; b++) if (child[b] >= 0) len[b] = bP(child[b]).distanceTo(bP(b));
}

void Skel::fk(const Pose& pose, std::vector<float>& q, std::vector<float>& p) const {
  const float S = scale;
  for (int i = 0; i < N; i++) {
    int k = i * 4, j = i * 3, pi = parent[i];
    Quat pq; Vec3 pp;
    if (pi < 0) { pq = baseQ[i]; pp = baseP[i]; }
    else { pq = {q[pi * 4], q[pi * 4 + 1], q[pi * 4 + 2], q[pi * 4 + 3]}; pp = {p[pi * 3], p[pi * 3 + 1], p[pi * 3 + 2]}; }
    Quat l = pose.getQ(i);
    Quat c = pq * l;
    q[k] = c.x; q[k + 1] = c.y; q[k + 2] = c.z; q[k + 3] = c.w;
    Vec3 v = pq * (pose.getP(i) * S) + pp;
    p[j] = v.x; p[j + 1] = v.y; p[j + 2] = v.z;
  }
}

void Skel::mirrorPose(const Pose& src, Pose& out) {
  fk(src);
  std::vector<Quat> D(N), newC(N);
  for (int i = 0; i < N; i++) D[i] = cQ(i) * bQ(i).conj();
  for (int i = 0; i < N; i++) { const Quat& d = D[mirror[i]]; newC[i] = Quat(d.x, -d.y, -d.z, d.w) * bQ(i); }
  for (int i = 0; i < N; i++) {
    int pi = parent[i];
    Quat pc = pi < 0 ? baseQ[i] : newC[pi];
    out.setQ(i, pc.conj() * newC[i]);
    if (pi < 0) {
      int mm = mirror[i];
      Vec3 v = baseQ[mm] * (src.getP(mm) * scale) + baseP[mm]; v.x = -v.x;
      v = baseQ[i].conj() * (v - baseP[i]) / scale;
      out.setP(i, v);
    } else out.setP(i, src.getP(i));
  }
}

void Skel::apply(const Pose& pose, Rig& rig) const {
  for (int i = 0; i < N; i++) rig.setLocal(node[i], pose.getP(i), pose.getQ(i));
}

// ================================================================== clips
void ClipLib::build(const Rig& rig, Skel& skel) {
  rig_ = &rig; s_ = &skel;
  for (size_t i = 0; i < rig.model.clips.size(); i++) ids_[rig.model.clips[i].name] = (int)i;
  nodeToBone_.assign(rig.model.nodes.size(), -1);
  for (int b = 0; b < skel.N; b++) nodeToBone_[skel.node[b]] = b;
  tmp_ = Pose(skel.N);
}

void ClipLib::sampleRaw(const std::string& n, float t, Pose& out, bool loop) const {
  auto it = ids_.find(n); if (it == ids_.end()) return;
  const GltfClip& c = rig_->model.clips[it->second];
  float d = c.duration;
  if (loop && d > 0) { t = std::fmod(t, d); if (t < 0) t += d; } else t = clampf(t, 0, d);
  for (const auto& ch : c.channels) {
    int b = nodeToBone_[ch.node]; if (b < 0 || ch.path == 2) continue;
    const auto& tm = ch.times; size_t cnt = tm.size(); if (!cnt) continue;
    size_t i1 = std::upper_bound(tm.begin(), tm.end(), t) - tm.begin();
    size_t i0 = i1 == 0 ? 0 : i1 - 1; if (i1 >= cnt) i1 = cnt - 1;
    float u = (i1 == i0 || ch.step) ? 0.f : clampf((t - tm[i0]) / (tm[i1] - tm[i0]), 0, 1);
    const float* v = ch.values.data();
    if (ch.path == 1) out.setQ(b, Quat::slerp({v[i0 * 4], v[i0 * 4 + 1], v[i0 * 4 + 2], v[i0 * 4 + 3]}, {v[i1 * 4], v[i1 * 4 + 1], v[i1 * 4 + 2], v[i1 * 4 + 3]}, u));
    else out.setP(b, vlerp({v[i0 * 3], v[i0 * 3 + 1], v[i0 * 3 + 2]}, {v[i1 * 3], v[i1 * 3 + 1], v[i1 * 3 + 2]}, u));
  }
}

bool ClipLib::sample(const std::string& n, float t, Pose& out, bool loop, int lock) const {
  if (!has(n)) return false;
  out.copy(s_->rest);
  sampleRaw(n, t, out, loop);
  if (lock) lockHips(out, lock);
  return true;
}

void ClipLib::lockHips(Pose& pose, int lock) const {
  int i = s_->idx("hips"); if (i < 0 || s_->parent[i] >= 0) return;
  float S = s_->scale; const Quat& bq = s_->baseQ[i]; const Vec3& bp = s_->baseP[i];
  Vec3 v = bq * (pose.getP(i) * S) + bp;
  Vec3 r = s_->bP(i);
  v.x = r.x; v.z = r.z; if (lock == 2) v.y = r.y;
  pose.setP(i, bq.conj() * (v - bp) / S);
}

ClipLib::LocoMeta ClipLib::loco(const std::string& n, bool wall) {
  std::string k = n + (wall ? ":wall" : ":z");
  auto it = locoMeta_.find(k); if (it != locoMeta_.end()) return it->second;
  LocoMeta m; if (!has(n)) return m;
  const int Nn = 90; int fl = s_->idx("footL"); float d = dur(n);
  std::vector<float> ys, zs;
  for (int j = 0; j < Nn; j++) { tmp_.copy(s_->rest); sampleRaw(n, d * j / Nn, tmp_, true); lockHips(tmp_, 1); s_->fk(tmp_); ys.push_back(s_->cp[fl * 3 + 1]); zs.push_back(s_->cp[fl * 3 + 2]); }
  std::vector<char> contact(Nn); std::vector<float>* travel;
  if (!wall) { float mn = *std::min_element(ys.begin(), ys.end()); for (int j = 0; j < Nn; j++) contact[j] = ys[j] < mn + 0.03f; travel = &zs; }
  else { float mx = *std::max_element(zs.begin(), zs.end()); for (int j = 0; j < Nn; j++) contact[j] = zs[j] > mx - 0.03f; travel = &ys; }
  float td = 0; int best = -1; std::vector<float> vs;
  for (int j = 0; j < Nn; j++) {
    int a = j, b = (j + 1) % Nn;
    if (contact[a] && contact[b]) vs.push_back(((*travel)[a] - (*travel)[b]) / (d / Nn));
    if (!contact[(j + Nn - 1) % Nn] && contact[j] && best < 0) { best = j; td = (float)j / Nn; }
  }
  std::sort(vs.begin(), vs.end());
  float med = vs.empty() ? 1 : std::fabs(vs[vs.size() / 2]);
  m.v = med > 0 ? med : 1; m.phase0 = td; m.dur = d; m.ok = true;
  locoMeta_[k] = m; return m;
}

float ClipLib::contactTime(const std::string& n) {
  std::string k = n + ":contact"; auto it = meta_.find(k); if (it != meta_.end()) return it->second;
  if (!has(n)) return 0;
  int fl = s_->idx("footL"), fr = s_->idx("footR"); float ah = s_->bindP[fl * 3 + 1], t0 = 0;
  for (float t = 0; t < std::min(0.5f, dur(n)); t += 1.f / 60) {
    tmp_.copy(s_->rest); sampleRaw(n, t, tmp_, false); s_->fk(tmp_);
    if (std::min(s_->cp[fl * 3 + 1], s_->cp[fr * 3 + 1]) < ah + 0.05f) { t0 = t; break; }
  }
  meta_[k] = t0; return t0;
}

float ClipLib::yawDelta(const std::string& n) {
  std::string k = n + ":yaw"; auto it = meta_.find(k); if (it != meta_.end()) return it->second;
  if (!has(n)) return 0;
  int h = s_->idx("hips"); float d = dur(n);
  auto fwdAt = [&](float t) { tmp_.copy(s_->rest); sampleRaw(n, t, tmp_, false); s_->fk(tmp_); Vec3 f = (s_->cQ(h) * s_->bQ(h).conj()) * Vec3{0, 0, 1}; return std::atan2(f.x, f.z); };
  float acc = 0, prev = fwdAt(0);
  for (int j = 1; j <= 30; j++) { float a = fwdAt(d * j / 30); acc += angWrap(a - prev); prev = a; }
  meta_[k] = acc; return acc;
}

// ================================================================== pose builder
PoseBuilder& PoseBuilder::setCQ(int i, const Quat& Q, float w) {
  if (i < 0 || w <= 0) return *this;
  fk();
  Quat l = s->parentCQ(i).conj() * Q;
  if (w < 1) l = Quat::slerp(pose->getQ(i), l, w);
  pose->setQ(i, l.normalize());
  dirty = true; return *this;
}
PoseBuilder& PoseBuilder::rot(int i, const Vec3& axis, float angle) {
  if (i < 0 || !angle) return *this;
  fk(); return setCQ(i, Quat::axisAngle(axis, angle) * s->cQ(i));
}
PoseBuilder& PoseBuilder::rotE(int i, float pitch, float yaw, float roll) {
  if (!pitch && !yaw && !roll) return *this;
  if (i < 0) return *this;
  fk(); return setCQ(i, qEulerYXZ(pitch, yaw, roll) * s->cQ(i));
}
PoseBuilder& PoseBuilder::twist(const std::string& k, float angle) {
  int ii = i(k); if (ii < 0 || !angle) return *this;
  int c = s->child[ii]; if (c < 0) return *this;
  fk(); Vec3 a = (s->cP(c) - s->cP(ii)).normalized();
  return rot(ii, a, angle);
}
PoseBuilder& PoseBuilder::aim(int ii, const Vec3& d, float w) {
  if (ii < 0 || w <= 0) return *this;
  int c = s->child[ii]; if (c < 0) return *this;
  fk();
  Vec3 cur = (s->cP(c) - s->cP(ii)).normalized();
  Quat q = Quat::fromUnitVectors(cur, d.normalized());
  if (w < 1) q = Quat::slerp(q, Quat(), 1 - w);
  return setCQ(ii, q * s->cQ(ii));
}
PoseBuilder& PoseBuilder::fromBind(int ii, const Quat& R, float w) {
  if (ii < 0) return *this;
  return setCQ(ii, R * s->bQ(ii), w);
}
PoseBuilder& PoseBuilder::moveHips(float dx, float dy, float dz) {
  int ii = i("hips"); if (ii < 0) return *this;
  float S = s->scale; int pi = s->parent[ii];
  Vec3 v{dx, dy, dz};
  if (pi < 0) v = s->baseQ[ii].conj() * v;
  else { fk(); v = s->cQ(pi).conj() * v; }
  pose->setP(ii, pose->getP(ii) + v / S);
  dirty = true; return *this;
}
void PoseBuilder::aimTo(int ii, Vec3 from, Vec3 to) {
  Quat q = Quat::fromUnitVectors(from.normalized(), to.normalized());
  fk(); setCQ(ii, q * s->cQ(ii));
}
float PoseBuilder::ik(bool arm, char S, const Vec3& target, const Vec3* pole, float w, bool absolute, bool keepEnd) {
  std::string sd(1, S);
  int a = i((arm ? "upperArm" : "upperLeg") + sd), b = i((arm ? "lowerArm" : "lowerLeg") + sd), c = i((arm ? "hand" : "foot") + sd);
  if (a < 0 || b < 0 || c < 0 || w <= 0) return 0;
  fk();
  Vec3 A = s->cP(a), B0 = s->cP(b), C0 = s->cP(c);
  float l1 = A.distanceTo(B0), l2 = B0.distanceTo(C0);
  Vec3 T = target; if (w < 1) T = vlerp(C0, target, w);
  Vec3 toT = T - A; float dRaw = toT.length();
  float d = clampf(dRaw, std::fabs(l1 - l2) + 1e-3f, (l1 + l2) * 0.9995f);
  Vec3 dir = toT.normalized();
  Vec3 pv = (pole ? *pole : B0) - A; pv -= dir * pv.dot(dir);
  if (pv.lengthSq() < 1e-8f) { pv = B0 - A; pv -= dir * pv.dot(dir); if (pv.lengthSq() < 1e-8f) pv = {0, 0, 1}; }
  pv.normalize();
  float x = (l1 * l1 - l2 * l2 + d * d) / (2 * d), h = std::sqrt(std::max(0.f, l1 * l1 - x * x));
  Vec3 mid = A + dir * x + pv * h, end = A + dir * d;
  Quat endQ = s->cQ(c);
  if (absolute) {
    Vec3 bindPole = arm ? Vec3{0, 0, -1} : Vec3{0, 0, 1};
    Vec3 u0 = (s->bP(b) - s->bP(a)).normalized(), lo0 = (s->bP(c) - s->bP(b)).normalized();
    Vec3 h0 = u0.cross(bindPole).normalized();
    Vec3 u1 = (mid - A).normalized(), lo1 = (end - mid).normalized();
    Vec3 h1 = u1.cross(pv).normalized();
    if (h1.lengthSq() < 0.5f) h1 = h0;
    Quat Ru = frameRot(u0, h0, u1, h1), Rl = frameRot(lo0, h0, lo1, h1);
    fromBind(a, Ru, w < 1 ? w : 1); fromBind(b, Rl, w < 1 ? w : 1);
    if (keepEnd) setCQ(c, endQ);
    return dRaw / (l1 + l2);
  }
  aimTo(a, B0 - A, mid - A);
  fk();
  Vec3 B1 = s->cP(b), C1 = s->cP(c);
  aimTo(b, C1 - B1, end - B1);
  if (keepEnd) setCQ(c, endQ);
  return dRaw / (l1 + l2);
}
PoseBuilder& PoseBuilder::orient(const std::string& k, const Vec3& d, const Vec3& up, const Vec3& refAxisBind, float w) {
  int ii = i(k); if (ii < 0 || w <= 0) return *this;
  int c = s->child[ii];
  Vec3 f0 = c >= 0 ? (s->bP(c) - s->bP(ii)).normalized() : Vec3{0, 0, 1};
  Vec3 u0 = (refAxisBind - f0 * refAxisBind.dot(f0)).normalized();
  Vec3 f1 = d.normalized(), u1 = (up - f1 * up.dot(f1)).normalized();
  Quat R = Quat::fromBasis(f1, u1, f1.cross(u1)) * Quat::fromBasis(f0, u0, f0.cross(u0)).conj();
  return fromBind(ii, R.normalize(), w);
}

// ================================================================== rig data
void RigData::build(Skel& sk) {
  s = &sk;
  auto P = [&](const std::string& k) { int i = sk.idx(k); return i < 0 ? Vec3{} : sk.bP(i); };
  hipsP = sk.idx("hips") >= 0 ? P("hips") : Vec3{0, 0.95f, 0}; hipY = hipsP.y;
  for (int si = 0; si < 2; si++) {
    std::string S = si ? "R" : "L";
    thigh[si] = P("upperLeg" + S); knee[si] = P("lowerLeg" + S); ankle[si] = P("foot" + S);
    shoulder[si] = P("upperArm" + S); elbow[si] = P("lowerArm" + S); wrist[si] = P("hand" + S);
    toe[si] = sk.idx("toe" + S) >= 0 ? P("toe" + S) : ankle[si] + Vec3{0, -0.06f, 0.13f};
  }
  l1 = thigh[0].distanceTo(knee[0]); l2 = knee[0].distanceTo(ankle[0]); legLen = l1 + l2;
  a1 = shoulder[0].distanceTo(elbow[0]); a2 = elbow[0].distanceTo(wrist[0]);
  ankleH = ankle[0].y;
  ballOff = toe[0] - ankle[0];
  heelOff = {0, -ankleH, -0.06f};
  hipHalfW = std::fabs(thigh[0].x - thigh[1].x) / 2; shoulderHalfW = std::fabs(shoulder[0].x - shoulder[1].x) / 2;
  for (int si = 0; si < 2; si++) {
    char S = si ? 'R' : 'L';
    int h = sk.idx(std::string("hand") + S); if (h < 0) continue;
    std::vector<int> kids;
    for (int i = 0; i < sk.N; i++) { int p = sk.parent[i]; while (p >= 0 && p != h) p = sk.parent[p]; if (p == h) kids.push_back(i); }
    auto nm = [&](int i) { return lower(sk.name[i]); };
    int idx1 = -1, pin1 = -1, mid = -1;
    for (int i : kids) if (sk.parent[i] == h) {
      if (idx1 < 0 && nm(i).find("index") != std::string::npos) idx1 = i;
      if (pin1 < 0 && (nm(i).find("pinky") != std::string::npos || nm(i).find("ring") != std::string::npos)) pin1 = i;
      if (mid < 0 && nm(i).find("middle") != std::string::npos) mid = i;
    }
    Vec3 across = idx1 >= 0 && pin1 >= 0 ? (sk.bP(pin1) - sk.bP(idx1)).normalized() : Vec3{S == 'L' ? -1.f : 1.f, 0, 0};
    float sgn = S == 'L' ? 1.f : -1.f;
    for (int i : kids) {
      std::string n = nm(i); bool thumb = n.find("thumb") != std::string::npos;
      int seg = 0; for (char c : n) if (std::isdigit((unsigned char)c)) { seg = c - '1'; break; }
      seg = std::clamp(seg, 0, 2);
      std::string mname = n.rfind("index", 0) == 0 ? "index" : n.rfind("pinky", 0) == 0 ? "pinky" : "middle";
      Quat inv = sk.bQ(i).conj();
      Vec3 axisChar = across;
      if (thumb) axisChar = mid >= 0 ? (sk.bP(mid) - sk.bP(h)).normalized() : Vec3{0, -1, 0};
      Vec3 local = (inv * axisChar).normalized();
      const float TA[3] = {0, 0.5f, 0.6f}, FA[3] = {1.2f, 1.5f, 1.0f};
      float segA = thumb ? -TA[seg] : FA[seg] * (mname == "index" ? 0.92f : mname == "pinky" ? 1.08f : 1.f);
      fingers[si].push_back({i, local, segA * sgn});
    }
  }
}
void RigData::curl(Pose& pose, char S, float amount, float w) const {
  for (const auto& f : fingers[side(S)]) {
    Quat q = s->rest.getQ(f.i) * Quat::axisAngle(f.axis, f.amt * amount);
    if (w < 1) q = Quat::slerp(pose.getQ(f.i), q, w);
    pose.setQ(f.i, q);
  }
}

ArmTarget armTarget(const RigData& rd, char S, float flex, float abd, float elbow, float twist) {
  float sx = S == 'L' ? 1.f : -1.f;
  Vec3 u = Vec3{std::sin(abd) * sx, -std::cos(abd) * std::cos(flex), std::cos(abd) * std::sin(flex)}.normalized();
  Vec3 h = u.cross({0, 0, 1});
  if (h.lengthSq() < 1e-3f) h = {-1, 0, 0};
  h.normalize();
  if (twist) h = applyAxisAngle(h, u, twist * sx);
  Vec3 l = applyAxisAngle(u, h, elbow);
  ArmTarget t; t.elbow = u * rd.a1; t.hand = t.elbow + l * rd.a2;
  return t;
}
