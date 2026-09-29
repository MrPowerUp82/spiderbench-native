// Animator pose helpers: locomotion blend space, walk form, arm pump, run track, zip pose, perch squat, landings,
// jump arms / tuck, wall cling / climb, stride warps.
#include "anim/animator_util.h"
#include "player/traversal/traversal.h"
#include "world/world.h"

using namespace anim;

namespace {
const std::pair<const char*, float> LOCO[] = {{"walk", 1.6f}, {"jog", 4.5f}, {"run", 8.5f}, {"sprint", 14.f}};
}

bool Animator::oneShot(const std::string& name, float t, Pose& out, int lock) {
  return clips_.sample(name, std::min(t, clips_.dur(name) - 1e-3f), out, false, lock);
}

const std::vector<float>& Animator::maskArms() {
  if (armMask_.empty()) {
    armMask_.assign(skel_.N, 0.f);
    auto under = [&](int i, int root) { while (i >= 0) { if (i == root) return true; i = skel_.parent[i]; } return false; };
    for (int i = 0; i < skel_.N; i++) for (const char* r : {"shoulderL", "shoulderR", "upperArmL", "upperArmR"}) { int ri = skel_.idx(r); if (ri >= 0 && under(i, ri)) armMask_[i] = 1; }
  }
  return armMask_;
}

void Animator::widenStance(Pose& pose, float w) {
  PoseBuilder& b = b_.begin(pose);
  b.moveHips(0, -0.025f * w, 0);
  for (char S : {'L', 'R'}) {
    float sx = sxOf(S); Vec3 a = b.pos(K("foot", S));
    Quat fq = b.cq(K("foot", S)); Vec3 knee = b.pos(K("lowerLeg", S));
    a.x += sx * 0.045f * w; a.y = std::max(a.y, rd_.ankleH);
    b.ik("leg", S, a, knee + Vec3{sx * 0.15f, 0, 0.4f}, 1, false);
    b.setCQ(K("foot", S), fq);
  }
}

ClipLib::LocoMeta Animator::locoMeta(const std::string& n) {
  ClipLib::LocoMeta m = clips_.loco(n);
  if (n != "walk" || !m.ok) return m;
  if (walkMetaOk_) return walkMeta_;
  const int N = 120; int fl = skel_.idx("footL"), fr = skel_.idx("footR");
  std::vector<float> ys, zs;
  for (int j = 0; j < N; j++) { clips_.sample(n, m.dur * j / N, P.tmp); skel_.fk(P.tmp); ys.push_back(skel_.cp[fl * 3 + 1]); zs.push_back(skel_.cp[fl * 3 + 2]); }
  float mn = *std::min_element(ys.begin(), ys.end()); std::vector<float> vs;
  for (int j = 0; j < N; j++) { int b = (j + 1) % N; if (ys[j] < mn + 0.006f && ys[b] < mn + 0.006f) vs.push_back((zs[j] - zs[b]) / (m.dur / N)); }
  std::sort(vs.begin(), vs.end());
  float v = vs.size() > 4 ? std::fabs(vs[vs.size() >> 1]) : m.v;
  float best = 0, bestE = 1e9f;
  for (int j = 0; j < N; j++) {
    clips_.sample(n, m.dur * j / N, P.tmp); skel_.fk(P.tmp);
    float dz = std::fabs(skel_.cp[fl * 3 + 2] - skel_.cp[fr * 3 + 2]), up = skel_.cp[fl * 3 + 1] - skel_.cp[fr * 3 + 1];
    if (up > 0.01f && dz < bestE) { bestE = dz; best = (float)j / N; }
  }
  walkMeta_ = m; walkMeta_.v = v; walkMeta_.pass = std::fmod(std::fmod(best - m.phase0, 1.f) + 1, 1.f);
  walkMetaOk_ = true;
  return walkMeta_;
}

// phase-aligned locomotion blend space at the current loco phase (cadence + stride split so feet never slide)
void Animator::locoPose(Pose& out, float v, const float* ovrK, const float* ovrRate) {
  std::vector<std::pair<const char*, float>> avail;
  for (auto& l : LOCO) if (clips_.has(l.first)) avail.push_back(l);
  if (avail.empty()) { out.copy(skel_.rest); locoInfo_ = {}; return; }
  size_t i = 0; while (i + 2 < avail.size() && v > avail[i + 1].second) i++;
  auto A0 = avail[i], A1 = avail[std::min(i + 1, avail.size() - 1)];
  float w = A0.first == A1.first ? 0 : smooth01((v - A0.second) / (A1.second - A0.second));
  ClipLib::LocoMeta m0 = locoMeta(A0.first), m1 = locoMeta(A1.first);
  float ph = locoPhase_;
  clips_.sample(A0.first, std::fmod(ph + m0.phase0, 1.f) * m0.dur, P.a);
  if (w > 0.001f) { clips_.sample(A1.first, std::fmod(ph + m1.phase0, 1.f) * m1.dur, P.b); blendPoses(P.a, P.b, w, out); }
  else out.copy(P.a);
  float vN = lerpf(m0.v, m1.v, w), fN = lerpf(1 / m0.dur, 1 / m1.dur, w);
  float ratio = std::max(v, 0.05f) / std::max(vN, 0.1f);
  float runK = smooth01((v - 5) / 3);
  float k = clampf(std::pow(ratio, lerpf(0.32f, 0.6f, runK)), 0.75f, lerpf(1.2f, 1.42f, runK));
  float rate = clampf(ratio / k, 0.55f, 2.2f);
  float wkC = 1 - smooth01((v - 1.8f) / 1.6f);
  if (wkC > 1e-3f) {
    float kW = clampf(ratio / ((0.535f + 0.285f * v) / fN), 0.3f, 1.3f);
    k = std::exp(lerpf(std::log(k), std::log(kW), wkC)); rate = ratio / k;
  }
  if (ovrK) { k = *ovrK; rate = *ovrRate / fN; }
  locoInfo_.rate = fN * rate; locoInfo_.k = k; locoInfo_.vN = vN; locoInfo_.clipA = A0.first; locoInfo_.clipB = A1.first; locoInfo_.w = w;
  if (std::fabs(k - 1) > 0.02f) strideWarp(out, k);
}

// natural walk form: the pelvis rises as far as the legs allow (inverted-pendulum walk), hip sway over the stance foot
void Animator::walkForm(Pose& pose, float w) {
  if (w < 0.01f) return;
  PoseBuilder& b = b_.begin(pose);
  Vec3 a[2]; Quat q[2];
  for (char S : {'L', 'R'}) { a[si(S)] = b.pos(K("foot", S)); q[si(S)] = b.cq(K("foot", S)); }
  float hy = b.pos("hips").y;
  float cap = rd_.hipY - 0.006f - hy;
  float Lmax = rd_.legLen * 0.99f;
  float aMin = std::min(a[0].y, a[1].y);
  auto smin = [](float x, float c, float r = 0.02f) { float hh = clampf(0.5f + 0.5f * (c - x) / r, 0, 1); return lerpf(c, x, hh) - r * hh * (1 - hh); };
  float raise = cap;
  for (char S : {'L', 'R'}) {
    Vec3 hp = b.pos(K("upperLeg", S)); const Vec3& A = a[si(S)];
    float d2 = (hp.x - A.x) * (hp.x - A.x) + (hp.z - A.z) * (hp.z - A.z);
    float relax = A.z < hp.z ? 0.035f * smooth01((A.y - aMin - 0.015f) / 0.045f) : 0;
    raise = smin(raise, A.y + std::sqrt(std::max(0.f, Lmax * Lmax - d2)) - hp.y + relax);
  }
  raise = std::max(0.f, raise) * w;
  float st = clampf((a[1].y - a[0].y) / 0.05f, -1, 1);
  b.moveHips(0.016f * st * w, raise, 0);
  b.rot("hips", Z, -0.035f * st * w); b.rot("spine", Z, 0.02f * st * w); b.rot("chest", Z, 0.015f * st * w);
  for (char S : {'L', 'R'}) {
    float sx = sxOf(S); Vec3 knee = b.pos(K("lowerLeg", S));
    b.ik("leg", S, a[si(S)], knee + Vec3{sx * 0.02f, 0, 0.4f}, 1, false);
    b.setCQ(K("foot", S), q[si(S)]);
  }
}

// ---- web-zip phase weights (fire / yank / flight / catch), all continuous
void Animator::zipPhases(Layer& L, const AnimState& A) {
  const std::string& sub = A.sub; float zt = clampf(A.zip.t, 0, 1);
  bool travel = sub == "zipTravel" || sub == "zipFlight" || sub == "zipCatch" || zt > 0.001f;
  LayerData& D = L.data;
  if (travel && !finite(D.travelT)) D.travelT = L.t;
  float tt = L.t;
  float fire = smooth01(tt / 0.07f);
  float yu = sub == "zipYank" ? std::max(clampf(A.t / 0.12f, 0, 1), 0.2f) : clampf((tt - 0.075f) / 0.13f, 0, 1);
  if (travel) yu = std::max(yu, clampf((tt - D.travelT) / 0.08f + 0.6f, 0, 1));
  float y = yu <= 0 ? 0 : 1 + 2.2f * std::pow(yu - 1, 3.f) + 1.2f * std::pow(yu - 1, 2.f);
  float g = travel ? std::max(smooth01((tt - D.travelT - 0.02f) / 0.16f), smooth01((zt - 0.04f) / 0.16f)) : 0;
  float dT = std::max(0.f, io_.center.distanceTo(A.zip.target) - io_.H);
  float sp = std::max(4.f, A.velocity.length());
  float rem = io_.trav && io_.trav->zip.dur > 0 && A.mode == "zip" ? (1 - clampf(io_.trav->zip.u, 0, 1)) * io_.trav->zip.dur : dT / sp;
  float c = travel ? smooth01(1 - rem / 0.22f) : 0;
  c = D.cMax = std::max(D.cMax, c);
  D.zph = {fire, y, g, c}; D.hasZph = true;
  D.clip = c > 0.5f ? "zip:catch" : g > 0.5f ? "zip:flight" : y > 0.5f ? "zip:yank" : "zip:fire";
}

// web-zip from the authored clips (webZipFire -> webZipYank -> zipFlight -> zipCatch), blended by the phase weights
void Animator::zipPose(Pose& out, Layer& L, const AnimState& A) {
  const ZipPh& Zp = L.data.zph; LayerData& D = L.data;
  if (A.sub == "zipYank" && !finite(D.yankT0)) D.yankT0 = L.t;
  if (A.sub == "zipCatch" && !finite(D.catchT0)) D.catchT0 = L.t;
  clips_.sample("webZipFire", std::min(L.t + 0.04f, clips_.dur("webZipFire") - 1e-3f), out, false);
  float y = clampf(Zp.y, 0, 1);
  if (y > 1e-3f) { clips_.sample("webZipYank", std::min(finite(D.yankT0) ? L.t - D.yankT0 : 0.06f * y, clips_.dur("webZipYank") - 1e-3f), P.c, false); blendPoses(out, P.c, y, out); }
  if (Zp.g > 1e-3f) { clips_.sample("zipFlight", finite(D.travelT) ? L.t - D.travelT : 0, P.c); blendPoses(out, P.c, Zp.g, out); }
  if (Zp.c > 1e-3f) {
    float u = std::max(Zp.c, A.mode != "zip" ? 1.f : 0.f);
    const char* cn = clips_.first({"zipCatchLevel", "zipCatch"}); float cdd = clips_.dur(cn);
    clips_.sample(cn, std::min(u, 1.f) * (cdd - 1e-3f), P.c, false);
    blendPoses(out, P.c, Zp.c, out);
  }
  PoseBuilder& b = b_.begin(out);
  float wa = clampf(Zp.f, 0, 1) * (1 - y) * (1 - Zp.g);
  if (wa > 0.01f) { // fire: both arms thrust at the actual target
    Vec3 t = worldToChar(A.zip.target);
    for (char S : {'L', 'R'}) {
      float sx = sxOf(S); Vec3 sh = b.pos(K("upperArm", S));
      Vec3 d = t - sh; d.z = std::max(d.z, 0.3f * d.length()); d.normalize();
      Vec3 hnd = sh + d * ((rd_.a1 + rd_.a2) * 0.96f); hnd.x = lerpf(hnd.x, 0, 0.25f);
      Vec3 el = sh + d * 0.25f + Vec3{sx * 0.25f, -0.2f, -0.1f};
      b.ik("arm", S, hnd, el, wa, true);
      b.aim(K("hand", S), d, wa);
    }
  }
  float gw = finite(D.gw) ? D.gw : 0;
  if (gw > 1e-3f) { // grounded start: planted staggered stance, coil on the yank
    b.moveHips(0, -gw * (0.04f + 0.12f * y), -gw * 0.05f * y);
    for (char S : {'L', 'R'}) {
      float sx = sxOf(S); const Vec3& th = rd_.thigh[si(S)];
      Quat fq = b.cq(K("foot", S));
      b.ik("leg", S, {sx * 0.14f, rd_.ankleH, S == 'L' ? 0.12f : -0.14f}, {sx * 0.3f, th.y - 0.2f, 0.9f}, gw, true);
      b.setCQ(K("foot", S), fq); b.fromBind(K("foot", S), Quat(), gw);
    }
  }
  D.clip = Zp.c > 0.5f ? "zipCatch" : Zp.g > 0.5f ? "zipFlight" : y > 0.5f ? "webZipYank" : "webZipFire";
}

float Animator::idleFootX() {
  if (finite(idleFX_)) return idleFX_;
  const char* n = clips_.first({"idle"});
  if (!n || !clips_.sample(n, 0, P.tmp)) return idleFX_ = 0.14f;
  widenStance(P.tmp, 1); skel_.fk(P.tmp);
  return idleFX_ = std::fabs(skel_.cp[skel_.idx("footL") * 3]);
}

// run: two foot tracks ~hip width apart, torso pitched forward, rear leg pushes from behind
void Animator::runTrack(Pose& pose, float v) {
  float k = smooth01((v - 2.5f) / 3); if (k < 0.01f) return;
  PoseBuilder& b = b_.begin(pose);
  float lean = k * lerpf(0.08f, 0.14f, smooth01((v - 8) / 5));
  b.rot("spine", X, lean * 0.45f); b.rot("spine1", X, lean * 0.3f); b.rot("chest", X, lean * 0.25f);
  b.rot("head", X, -lean * 0.6f);
  for (char S : {'L', 'R'}) {
    float sx = sxOf(S);
    Vec3 f = b.pos(K("foot", S)), hip = b.pos(K("upperLeg", S));
    float want = sx * std::max(sx * f.x, lerpf(std::fabs(hip.x), idleFootX(), 0.3f));
    Quat fq = b.cq(K("foot", S));
    f.x = lerpf(f.x, want, k);
    float behind = smooth01((hip.z - f.z - 0.05f) / 0.35f) * (1 - smooth01((f.y - 0.25f) / 0.2f));
    if (behind > 0.01f) { f.z -= 0.12f * k * behind; f.y += 0.03f * k * behind; fq = Quat::axisAngle(X, 0.45f * k * behind) * fq; }
    Vec3 knee = b.pos(K("lowerLeg", S));
    b.ik("leg", S, f, knee + Vec3{sx * 0.03f, 0, 0.3f}, 1, true);
    b.setCQ(K("foot", S), fq);
  }
}

// procedural arm pump: elastic spring driven by the leg phase (read from the thigh angles), in the chest frame
void Animator::armPump(Pose& pose, float v, float w) {
  if (w < 0.01f) return;
  PoseBuilder& b = b_.begin(pose);
  float th[2];
  for (char S : {'L', 'R'}) { Vec3 h = b.pos(K("upperLeg", S)), k = b.pos(K("lowerLeg", S)); th[si(S)] = std::atan2(k.z - h.z, h.y - k.y); }
  float s0 = (th[0] - th[1]) * 0.5f;
  if (!armSprInit_) { armSpr_ = Spring(s0, 3, 0.42f); armSprInit_ = true; }
  if (armT_ != time_) {
    armT_ = time_;
    armAmp_ = std::max(damp(armAmp_, 0.05f, 1.5f, dt_), std::fabs(s0));
    float sn = clampf(s0 / std::max(armAmp_, 0.05f), -1, 1);
    float tgt = signf(sn) * std::pow(std::fabs(sn), 0.7f) * armAmp_;
    armSpr_.f = clampf(locoInfo_.rate * 3.6f, 3, 9); armSpr_.z = 0.78f;
    if (!(std::fabs(armSpr_.x - tgt) < 1.5f)) { armSpr_.x = tgt; armSpr_.v = 0; }
    armSpr_.step(tgt, dt_);
  }
  float s = armSpr_.x * 0.9f, sVel = armSpr_.v * 0.9f;
  float u = smooth01((v - 1.4f) / 3.6f), sp = smooth01((v - 9) / 5);
  float gain = lerpf(0.8f, 1.45f, u) * lerpf(1, 1.1f, sp), bias = lerpf(-0.02f, 0.1f, u), abd = lerpf(0.1f, 0.36f, u);
  {
    float tw = s * w * lerpf(0.35f, 0.55f, u);
    b.rot("hips", Y, -tw * 0.35f);
    b.rot("spine", Y, tw * 0.3f); b.rot("spine1", Y, tw * 0.35f); b.rot("chest", Y, tw * 0.45f);
    b.rot("chest", Z, tw * 0.18f);
    b.rot("neck", Y, -tw * 0.45f); b.rot("head", Y, -tw * 0.4f);
  }
  int ci = b.i("chest");
  Quat chestD = b.cq(ci) * skel_.bQ(ci).conj();
  for (char S : {'L', 'R'}) {
    float sw = S == 'L' ? -s : s;
    float flex = clampf(bias + gain * sw, lerpf(-0.5f, -0.78f, u), lerpf(0.45f, 0.85f, u));
    float fwdAmt = clampf((flex + 0.95f) / 1.8f, 0, 1);
    float elbow = lerpf(lerpf(0.3f, 1.2f, u), lerpf(0.55f, 0.85f, u), fwdAmt);
    float armV = S == 'L' ? -sVel : sVel;
    elbow = clampf(elbow - armV * 0.08f * u, 0.2f, 1.6f);
    ArmTarget at = armTarget(rd_, S, flex, abd, elbow, lerpf(0, 0.18f, u));
    Vec3 sh = b.pos(K("upperArm", S));
    Vec3 el = chestD * at.elbow + sh, hd = chestD * at.hand + sh;
    float sx = sxOf(S);
    b.ik("arm", S, hd, el + chestD * Vec3{sx * 0.05f, 0, -0.08f}, w, true);
    rd_.curl(pose, S, lerpf(0.45f, 0.7f, u), w); b.dirty = true;
  }
}

float Animator::kneeSpread(const std::string& name) {
  auto it = kneeSpread_.find(name); if (it != kneeSpread_.end()) return it->second;
  if (!clips_.sample(name, 0.5f, P.tmp)) return kneeSpread_[name] = 0;
  skel_.fk(P.tmp); int a = skel_.idx("lowerLegL"), b = skel_.idx("lowerLegR");
  return kneeSpread_[name] = std::fabs(skel_.cp[a * 3] - skel_.cp[b * 3]);
}

// perch: deep frog squat on the point (procedural when the authored clip is the legacy hunched one) + landing absorb
void Animator::perchSquat(Pose& pose, Layer& L, float land) {
  const char* clip = clips_.first({"perchIdle"});
  bool proc = !(clip && kneeSpread(clip) > 0.5f);
  PoseBuilder& b = b_.begin(pose); float t = L.t, T = time_;
  float imp = clampf(A_->perch.impact.length() / 20, 0.4f, 1.3f);
  float absorb = land ? imp * std::sin(PI * clampf(t / 0.42f, 0, 1)) * std::exp(-t * 2.5f) : 0;
  if (!proc) { if (absorb > 1e-3f) b.moveHips(0, -0.07f * absorb, 0.02f * absorb); return; }
  float shift = noise1(T * 0.23f, 3) * 0.035f, bob = std::sin(T * TAU * 0.3f) * 0.008f;
  b.setHipsChar({shift, 0.35f - 0.08f * absorb + bob, -0.08f});
  const std::tuple<const char*, float, float> parts[] = {{"hips", 0.62f + 0.1f * absorb, -shift * 2.2f}, {"spine", 0.86f + 0.1f * absorb, -shift}, {"spine1", 0.96f + 0.1f * absorb, 0},
                                                         {"chest", 1.02f + 0.08f * absorb, 0}, {"neck", 0.5f, 0}, {"head", -0.12f - 0.06f * absorb, 0}};
  for (const auto& [k, pitch, roll] : parts) { if (b.i(k) < 0) continue; b.fromBind(k, qEulerYXZ(pitch, 0, roll)); }
  for (char S : {'L', 'R'}) {
    float sx = sxOf(S);
    b.ik("leg", S, {sx * 0.13f, rd_.ankleH + 0.035f, 0}, {sx * 1.3f, 0.5f, 0.3f}, 1, true);
    b.aim(K("foot", S), Vec3{sx * 0.35f, -0.42f, 1}.normalized(), 1);
    b.ik("arm", S, {sx * (0.06f + 0.02f * shift * sx), 0.06f, 0.13f}, {sx * 0.3f, 0.6f, -0.1f}, 1, true);
    b.aim(K("hand", S), Vec3{-sx * 0.12f, -0.75f, 1}.normalized(), 1);
    rd_.curl(pose, S, 0.62f, 1); b.dirty = true;
  }
}

// landing / recovery clips: the lowest body point must touch char-space y = 0
void Animator::groundContact(Pose& pose, float w) {
  if (w < 0.01f) return;
  PoseBuilder& b = b_.begin(pose); b.fk();
  float lo = INF;
  auto probe = [&](const char* k, float r) { int i = skel_.idx(k); if (i >= 0) lo = std::min(lo, skel_.cp[i * 3 + 1] - r); };
  probe("footL", rd_.ankleH * 0.85f); probe("footR", rd_.ankleH * 0.85f); probe("toeL", 0.025f); probe("toeR", 0.025f);
  probe("handL", 0.04f); probe("handR", 0.04f); probe("lowerLegL", 0.07f); probe("lowerLegR", 0.07f);
  probe("hips", 0.13f); probe("spine", 0.14f); probe("chest", 0.15f); probe("head", 0.13f);
  if (!finite(lo) || std::fabs(lo) < 0.005f) return;
  b.moveHips(0, -lo * w, 0);
}

// late-fall landing anticipation: legs reach down so the soles meet the ground at contact
void Animator::groundReach(Pose& pose, const AnimState& A) {
  float vy = A.velocity.y; const Vec3& C = io_.center;
  bool landed = A.mode == "land" || A.mode == "ground";
  float h = 0, w = 1;
  if (!landed) {
    if (vy > -2 || !world_) return;
    float g = world_->groundHeight(C.x, C.z, C.y);
    h = C.y - io_.H - g;
    if (h < -0.2f) return;
    float tti = h / std::max(2.f, -vy);
    if (tti > 0.3f) return;
    w = smooth01((0.3f - tti) / 0.22f);
  }
  PoseBuilder& b = b_.begin(pose);
  for (char S : {'L', 'R'}) {
    Vec3 a = b.pos(K("foot", S));
    float maxY = rd_.ankleH + std::max(0.f, h) * 0.85f;
    if (a.y <= maxY) continue;
    Vec3 tgt = a; tgt.y = lerpf(a.y, maxY, w); tgt.z = lerpf(a.z, a.z * 0.6f, w);
    Quat fq = b.cq(K("foot", S)); Vec3 knee = b.pos(K("lowerLeg", S));
    b.ik("leg", S, tgt, knee + Vec3{0, 0, 0.4f}, 1, false);
    b.setCQ(K("foot", S), fq);
  }
}

// seat the balls of the feet exactly on the perch top; pin the feet after the landing; hands never below the plane
void Animator::perchSeat(Pose& pose, Layer& L, const AnimState& A) {
  const Vec3& pt = A.perch.point; if (pt.lengthSq() < 1e-6f) return;
  PoseBuilder& b = b_.begin(pose); b.fk();
  float py = worldToChar(pt).y;
  float lo = INF;
  for (const char* k : {"toeL", "toeR"}) { int i = skel_.idx(k); if (i >= 0) lo = std::min(lo, skel_.cp[i * 3 + 1] - 0.022f); }
  if (!finite(lo)) for (const char* k : {"footL", "footR"}) { int i = skel_.idx(k); if (i >= 0) lo = std::min(lo, skel_.cp[i * 3 + 1] - rd_.ankleH); }
  if (!finite(lo)) return;
  float d = clampf(py - lo, -0.6f, 0.6f);
  L.data.seat = !finite(L.data.seat) ? d : damp(L.data.seat, d, 14, dt_);
  if (std::fabs(L.data.seat) > 1e-3f) b.moveHips(0, L.data.seat, 0);
  if (L.t > 0.8f) {
    if (!L.data.hasPin) { L.data.pin[0] = b.pos("footL"); L.data.pin[1] = b.pos("footR"); L.data.hasPin = true; }
    for (char S : {'L', 'R'}) {
      Quat fq = b.cq(K("foot", S)); Vec3 knee = b.pos(K("lowerLeg", S));
      b.ik("leg", S, L.data.pin[si(S)], knee + Vec3{S == 'L' ? 0.3f : -0.3f, 0.1f, 0.3f}, 1, false);
      b.setCQ(K("foot", S), fq);
    }
  }
  for (char S : {'L', 'R'}) {
    Vec3 h = b.pos(K("hand", S)); float mn = py + 0.045f;
    if (h.y < mn) { Quat hq = b.cq(K("hand", S)); Vec3 el = b.pos(K("lowerArm", S)); h.y = mn; b.ik("arm", S, h, el + Vec3{0, 0.1f, -0.2f}, 1, false); b.setCQ(K("hand", S), hq); }
  }
}

bool Animator::landHardNeedsFix() {
  if (lhf_ >= 0) return lhf_ == 1;
  bool bad = false;
  for (float t : {0.35f, 0.55f}) {
    clips_.sample("landHard", t, P.tmp, false); groundContact(P.tmp, 1); skel_.fk(P.tmp);
    for (const char* k : {"footL", "footR"}) if (skel_.cp[skel_.idx(k) * 3 + 1] > rd_.ankleH + 0.12f) bad = true;
    for (const char* k : {"lowerLegL", "lowerLegR"}) if (skel_.cp[skel_.idx(k) * 3 + 1] < 0.1f) bad = true;
  }
  lhf_ = bad ? 1 : 0; return bad;
}

// superhero landing: crouched 3-point, both feet planted (staggered)
void Animator::threePoint(Pose& pose) {
  PoseBuilder& b = b_.begin(pose);
  float hy = b.pos("hips").y, w = 1 - smooth01((hy - 0.62f) / 0.25f);
  if (w < 0.01f) return;
  for (char S : {'L', 'R'}) {
    float sx = sxOf(S); bool lead = S == 'L';
    b.ik("leg", S, {sx * 0.2f, rd_.ankleH + (lead ? 0 : 0.03f), lead ? 0.3f : -0.12f}, {sx * 0.45f, 0.6f, 1.0f}, w, true);
    b.fromBind(K("foot", S), Quat::axisAngle(Y, sx * 0.25f) * Quat::axisAngle(X, lead ? 0 : 0.35f), w);
  }
  b.dirty = true;
}

void Animator::solesAbove(Pose& pose, float y0) {
  PoseBuilder& b = b_.begin(pose);
  for (char S : {'L', 'R'}) {
    Vec3 a = b.pos(K("foot", S)); float mn = y0 + rd_.ankleH * 0.92f;
    if (a.y >= mn) continue;
    Quat fq = b.cq(K("foot", S)); Vec3 knee = b.pos(K("lowerLeg", S));
    a.y = mn; b.ik("leg", S, a, knee + Vec3{0, 0, 0.4f}, 1, false);
    b.setCQ(K("foot", S), fq);
  }
}

void Animator::strideWarp(Pose& pose, float k) {
  PoseBuilder& b = b_.begin(pose);
  float hz = b.pos("hips").z;
  for (char S : {'L', 'R'}) {
    Vec3 a = b.pos(K("foot", S)), tgt = a; tgt.z = hz + (a.z - hz) * k;
    float lift = std::max(0.f, a.y - rd_.ankleH); tgt.y += lift * (k - 1) * 0.5f;
    Quat fq = b.cq(K("foot", S)); Vec3 knee = b.pos(K("lowerLeg", S));
    b.ik("leg", S, tgt, knee + Vec3{0, 0, 0.4f}, 1, false);
    b.setCQ(K("foot", S), fq);
  }
  if (k > 1) b.moveHips(0, -0.06f * (k - 1), 0);
}

// jump / air arms: spread, pulled back, hands ~shoulder height, wrists continuing the forearm
void Animator::jumpArms(Pose& pose, float w, float rise, float fall) {
  if (w < 0.01f) return;
  PoseBuilder& b = b_.begin(pose); float T = time_;
  for (char S : {'L', 'R'}) {
    float sx = sxOf(S), ld = sx == jumpLead_ ? 1.f : 0.f; Vec3 sh = b.pos(K("upperArm", S));
    float flut = std::sin(T * 5.3f + (S == 'L' ? 0 : 1.7f)) * 0.025f * fall;
    Vec3 tgt = vlerp({sx * 0.6f, sh.y - (ld ? 0.04f : 0.12f), -0.24f}, {sx * 0.66f, sh.y + flut, -0.12f}, fall * 0.7f);
    b.ik("arm", S, tgt, {sx * 0.45f, sh.y - 0.5f, sh.z - 0.45f}, w, true);
    int iF = b.i(K("lowerArm", S)), iH = b.i(K("hand", S));
    if (iF >= 0 && iH >= 0) b.setCQ(iH, b.cq(iF) * skel_.bQ(iF).conj() * skel_.bQ(iH), w);
  }
}

void Animator::jumpTuck(Pose& pose, float w) {
  if (w < 0.01f) return;
  PoseBuilder& b = b_.begin(pose);
  for (char S : {'L', 'R'}) {
    float sx = sxOf(S); bool hi = sx != jumpLead_;
    Vec3 hip = b.pos(K("upperLeg", S)); Quat fq = b.cq(K("foot", S));
    b.ik("leg", S, {sx * 0.13f, hip.y - (hi ? 0.34f : 0.5f), hi ? 0.1f : -0.06f}, {sx * 0.2f, hip.y - 0.1f, hip.z + 0.8f}, w, true);
    b.setCQ(K("foot", S), fq);
  }
}

// wall cling: body turned out from the facade, wall-side hand flat on the wall, knee up, head looking out
void Animator::clingPose(Pose& pose, float w, char side) {
  if (w < 0.01f) return;
  PoseBuilder& b = b_.begin(pose); float T = time_;
  const float turn = 2.1f, hipY = 0.6f, hipO = 0.38f, leanK = 0.12f, pitch = 0.14f, fold = 0.2f, chestLift = 0.15f, chestTurn = -0.65f, chestSide = -0.05f,
              headTurn = 1.45f, headTilt = -0.05f, headRoll = 0.05f, neckUp = 0.1f, shDrop = 0.2f, shDropF = 0.3f, curlF = 0.55f, curlW = 0.15f, spreadW = 1.1f;
  const float hW[3] = {0.44f, 0.52f, 0.015f}, hF[3] = {-0.07f, -0.06f, 0.86f}, fW[3] = {0.3f, -0.12f, 0.075f}, fF[3] = {-0.08f, -0.57f, 0.075f},
              pW[3] = {-0.1f, -0.45f, 0.45f}, pF[3] = {-0.35f, 0.05f, 0.85f}, kW[3] = {0.4f, 0.5f, -0.05f}, kF[3] = {0.6f, -0.2f, 0.6f},
              toeW[2] = {0.6f, 1}, toeF[2] = {1, 0.3f}, fingW[2] = {0.6f, 0.9f};
  float m = side == 'R' ? -1.f : 1.f; char W_ = side == 'R' ? 'R' : 'L', F_ = other(W_);
  float th = -m * turn, c = std::cos(th), sn = std::sin(th);
  float br = std::sin(T * 1.7f) * 0.006f, sway = std::sin(T * 0.9f) * 0.01f;
  auto D = [&](float a0, float a1, float a2) { return Vec3{m * a0 * c + a2 * sn, a1, -m * a0 * sn + a2 * c}; };
  Vec3 O{0, hipY + br, WALL_Z - hipO};
  auto V = [&](const float* a) { return Vec3{-m * a[0], O.y + a[1], WALL_Z - a[2]}; };
  auto Wd = [&](const float* a) { return Vec3{-m * a[0], a[1], 0}; };
  for (const char* k : {"spine", "spine1", "chest", "neck", "head"}) b.fromBind(k, Quat(), w);
  Vec3 hips = b.pos("hips");
  Quat hq = Quat::axisAngle(Y, th) * Quat::axisAngle(Z, m * (leanK + sway)) * Quat::axisAngle(X, pitch);
  b.fromBind("hips", hq, w);
  b.moveHips(-hips.x * w, (O.y - hips.y) * w, (O.z - hips.z) * w);
  Vec3 fwd = D(0, 0, 1), bodyX = D(1, 0, 0) * m;
  b.rot("spine", Y, -m * chestTurn * 0.4f * w); b.rot("chest", Y, -m * chestTurn * 0.6f * w);
  b.rot("chest", fwd, -m * chestSide * w);
  Vec3 lat = applyAxisAngle(bodyX, Y, -m * chestTurn);
  b.rot("spine", bodyX, fold * w); b.rot("chest", lat, (fold - chestLift) * w);
  b.rot("neck", lat, -neckUp * w); b.rot("head", lat, neckUp * w);
  b.rot("head", Y, -m * headTurn * w); b.rot("head", applyAxisAngle(bodyX, Y, -m * (chestTurn + headTurn)), headTilt * w);
  b.rot("head", fwd, m * headRoll * w);
  b.rot(K("shoulder", W_), fwd, -m * shDrop * w);
  b.rot(K("shoulder", F_), fwd, m * shDropF * w);
  b.ik("arm", W_, V(hW), V(pW), w, true);
  b.ik("arm", F_, V(hF), V(pF), w, true);
  b.ik("leg", W_, V(fW), V(kW), w, true);
  b.ik("leg", F_, V(fF), V(kF), w, true);
  for (char S : {'L', 'R'}) {
    int iF = b.i(K("lowerArm", S)), iH = b.i(K("hand", S)); if (iF < 0 || iH < 0) continue;
    b.setCQ(iH, b.cq(iF) * skel_.bQ(iF).conj() * skel_.bQ(iH), w);
  }
  palmToWall(W_, Wd(fingW), w);
  soleToWall(W_, Wd(toeW), w);
  soleToWall(F_, Wd(toeF), w);
  rd_.curl(pose, W_, curlW, w);
  { // splay the wall hand's fingers in the wall plane
    int iH = b.i(K("hand", W_)); auto im = skel_.byName.find(K("middle1", W_)); b.dirty = true;
    if (iH >= 0 && im != skel_.byName.end()) {
      Vec3 h = b.pos(iH), md = b.pos(im->second) - h;
      const std::pair<const char*, float> spr[] = {{"index1", 1}, {"ring1", 0.5f}, {"pinky1", 1}, {"thumb1", 2.2f}};
      for (const auto& [n, k] : spr) {
        auto it = skel_.byName.find(K(n, W_)); if (it == skel_.byName.end()) continue;
        Vec3 fd = b.pos(it->second) - h; float sg = signf(md.x * fd.y - md.y * fd.x); if (sg == 0) sg = 1;
        b.rot(it->second, Z, sg * spreadW * 0.25f * k * w);
      }
    }
  }
  rd_.curl(pose, F_, curlF, w); b.dirty = true;
}

char Animator::pickClingSide(const AnimState& A, char prev) {
  if (A.lookDir.lengthSq() < 1e-6f) return prev ? prev : 'L';
  Vec3 c = frameQ_.conj() * -A.lookDir;
  float h = std::hypot(c.x, c.z); if (h < 1e-6f) h = 1; float x = c.x / h;
  if (x < -0.2f) return 'L'; if (x > 0.2f) return 'R';
  return prev ? prev : 'L';
}

void Animator::palmToWall(char S, const Vec3& dir, float w) {
  PoseBuilder& b = b_;
  int iH = b.i(K("hand", S)); auto iM = skel_.byName.find(K("middle1", S)), iI = skel_.byName.find(K("index1", S)), iP = skel_.byName.find(K("pinky1", S));
  if (iH < 0 || iM == skel_.byName.end() || iI == skel_.byName.end() || iP == skel_.byName.end()) return;
  Vec3 h = b.pos(iH), d = (b.pos(iM->second) - h).normalized();
  Vec3 a = b.pos(iI->second) - b.pos(iP->second);
  Vec3 n = d.cross(a).normalized(); if (S == 'R') n = -n;
  Vec3 d2 = dir; d2.z = 0; if (d2.lengthSq() < 1e-4f) d2 = {0, 1, 0}; d2.normalize();
  Quat R = basisQ(d2, {0, 0, 1}) * basisQ(d, n).conj();
  b.setCQ(iH, R * b.cq(iH), w);
}

void Animator::soleToWall(char S, const Vec3& dir, float w) {
  Vec3 d2 = dir; d2.z = 0; if (d2.lengthSq() < 1e-4f) d2 = {0, 1, 0}; d2.normalize();
  Quat R = basisQ(d2, {0, 0, 1}) * basisQ({0, 0, 1}, {0, -1, 0}).conj();
  b_.fromBind(K("foot", S), R, w);
}

// natural wall climb: diagonal limb pairs, planted limbs fixed on the wall while the body travels past them
void Animator::climbPose(Pose& pose, float dt, float v, bool moving) {
  PoseBuilder& b = b_.begin(pose); float T = time_;
  Quat rots[4]; const char* ends[4] = {"footL", "footR", "handL", "handR"};
  for (int i = 0; i < 4; i++) rots[i] = b.cq(ends[i]);
  float Ls = clampf(0.4f + 0.14f * v, 0.6f, 0.85f);
  climbW_ = damp(climbW_, moving ? 1.f : 0.f, moving ? 8.f : 4.f, dt);
  if (moving) climbPh_ = std::fmod(climbPh_ + dt * std::max(v, 0.3f) / Ls, 1.f);
  else climbPh_ = damp(climbPh_, std::round(climbPh_ * 2) / 2, 3, dt);
  float ph = climbPh_;
  auto limb = [&](float p, float& y, float& lift) { p = std::fmod(std::fmod(p, 1.f) + 1, 1.f); const float SW = 0.35f;
    if (p < 1 - SW) { float u = p / (1 - SW); y = Ls * (0.5f - u); lift = 0; return; }
    float u = (p - (1 - SW)) / SW, e = u * u * (3 - 2 * u); y = Ls * (-0.5f + e); lift = std::sin(PI * u); };
  float sway = std::sin(ph * PI * 2) * 0.045f * climbW_;
  float breathe = std::sin(T * 1.9f) * 0.008f;
  Vec3 hips = b.pos("hips");
  b.moveHips(sway - hips.x * 0.6f, 0.93f + breathe - hips.y, (WALL_Z - 0.42f) - hips.z);
  b.rot("spine", X, -0.04f); b.rot("chest", X, -0.04f);
  b.rot("spine", Z, -sway * 1.6f);
  b.rot("chest", Y, std::sin(ph * PI * 2 - 0.6f) * 0.14f * climbW_);
  const float zc = WALL_Z;
  for (char S : {'L', 'R'}) {
    float sx = sxOf(S), hy, hl, fy, fl;
    limb(ph + (S == 'R' ? 0 : 0.5f), hy, hl); limb(ph + (S == 'L' ? 0 : 0.5f), fy, fl);
    Vec3 sh = b.pos(K("upperArm", S));
    b.ik("arm", S, {sx * 0.4f, sh.y + 0.12f + hy * 0.75f, zc - 0.045f - 0.1f * hl}, {sx * 0.7f, sh.y - 0.35f, zc - 0.55f}, 1, true);
    b.setCQ(K("hand", S), rots[S == 'L' ? 2 : 3]);
    Vec3 foot{sx * 0.3f, 0.38f + fy * 0.85f, zc - 0.08f - 0.1f * fl};
    Vec3 hipJ = b.pos(K("upperLeg", S));
    b.ik("leg", S, foot, vlerp(hipJ, foot, 0.5f) + Vec3{sx * 0.35f, -0.05f, -0.4f}, 1, true);
    b.setCQ(K("foot", S), rots[S == 'L' ? 0 : 1]);
  }
  b.rot("head", X, -0.25f); b.rot("neck", X, -0.1f);
}

// pose-match: pick the loco phase whose feet best match the current pose (entering locomotion from any state)
void Animator::matchLocoPhase(Pose& pose, float v) {
  bool any = false; for (auto& l : LOCO) if (clips_.has(l.first)) any = true;
  if (!any) return;
  skel_.fk(pose);
  int fl = skel_.idx("footL"), fr = skel_.idx("footR");
  float zl = skel_.cp[fl * 3 + 2], zr = skel_.cp[fr * 3 + 2], yl = skel_.cp[fl * 3 + 1], yr = skel_.cp[fr * 3 + 1];
  float best = locoPhase_, bestE = INF;
  for (int j = 0; j < 20; j++) {
    locoPhase_ = j / 20.f; locoPose(P.tmp, std::max(v, 3.f)); skel_.fk(P.tmp);
    float dzl = skel_.cp[fl * 3 + 2] - zl, dzr = skel_.cp[fr * 3 + 2] - zr, dyl = skel_.cp[fl * 3 + 1] - yl, dyr = skel_.cp[fr * 3 + 1] - yr;
    float e = dzl * dzl + dzr * dzr + 2 * (dyl * dyl + dyr * dyr);
    if (e < bestE) { bestE = e; best = j / 20.f; }
  }
  locoPhase_ = best; b_.dirty = true;
}
