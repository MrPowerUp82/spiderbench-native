// Animator post layers (character space, after the node blend): web hands + two-handed grip, swing legs, quick-boost
// yank, secondary motion, look-at, breathing, foot IK, air life, helper bones.
#include "anim/animator_util.h"
#include "world/world.h"

using namespace anim;

// ---------------------------------------------------------------- web hands
void Animator::postWeb(float dt, const AnimState& A, float w) {
  bool zipLike = A.mode == "zip" || A.sub == "wallZip";
  const Vec3* anc = zipLike ? &A.zip.target : &A.swing.anchor;
  char hand = A.mode == "zip" ? 'R' : A.swing.hand;
  bool zipOwn = A.mode == "zip" && !A.zip.dash;
  float want = w > 0.01f && !zipOwn ? w : 0;
  for (char S : {'L', 'R'}) { float t = S == hand || A.sub == "wallZip" ? want : 0; float& ww = webWH_[si(S)]; ww = damp(ww, t, t > ww ? 11.f : 6.f, dt); }
  webW_ = std::max(webWH_[0], webWH_[1]);
  twoHandState(dt, A, A.mode == "swing" ? anc : nullptr, hand);
  swLegW_ = damp(swLegW_, A.mode == "swing" ? 1.f : 0.f, A.mode == "swing" ? 12.f : 5.f, dt);
  if (swLegW_ > 0.005f) {
    float tt = A.mode == "swing" ? two_.max * smooth01((twoPh_ + 0.2f) / 0.4f) : 0;
    tuckW_ = damp(tuckW_, tt, tt > tuckW_ ? 8.f : (A.mode == "swing" && twoPh_ > 0.2f ? 1.2f : 4.f), dt);
    swingLegs(smooth01(swLegW_) * lerpf(0.92f, 1, std::max(two_.max, tuckW_)), tuckW_, hand);
  }
  if (webW_ < 0.01f) return;
  PoseBuilder& b = b_;
  bool twoOk = A.mode != "zip" && (two_.max > 0.005f || two_.sw[0] > 0.005f || two_.sw[1] > 0.005f);
  if (twoOk && two_.max > 0.005f) twoHandBody(two_.max * webW_, hand);
  Vec3 a = worldToChar(*anc);
  float tension = clampf(A.swing.tension, 0, 1);
  bool hasPlan = false; Plan plan{};
  for (char S : {'L', 'R'}) {
    float ww = webWH_[si(S)]; if (ww < 0.01f) continue;
    float sx = sxOf(S);
    Vec3 sh = b.pos(K("upperArm", S));
    Vec3 dir = (a - sh).normalized();
    // joint limits: up / forward / out, never far behind the back, below the hip or across the midline
    dir.z = std::max(dir.z, -0.45f); dir.y = std::max(dir.y, -0.15f);
    dir.x = sx > 0 ? std::max(dir.x, -0.3f) : std::min(dir.x, 0.3f);
    dir.normalize();
    float side = clampf(dir.x * sx, -1, 1), rise = clampf(dir.y, 0, 1);
    b.rot("spine", Z, -sx * 0.06f * rise * ww); b.rot("chest", Z, -sx * 0.1f * rise * ww);
    b.rot("chest", Y, sx * 0.08f * (1 - side) * ww);
    int cl = b.i(K("shoulder", S));
    if (cl >= 0) {
      Vec3 cp = b.pos(cl), up0 = (b.pos(K("upperArm", S)) - cp).normalized();
      b.aim(cl, vlerp(up0, dir, 0.3f).normalized(), ww * 0.6f * (1 - 0.6f * two_.max));
    }
    Vec3 sh2 = b.pos(K("upperArm", S));
    float reach = (rd_.a1 + rd_.a2) * lerpf(0.95f, 0.995f, tension);
    Vec3 tgt = sh2 + dir * reach;
    Vec3 pole = sh2 + dir * (reach * 0.5f) + Vec3{sx * 0.35f, -0.05f, -0.25f};
    if (twoOk && !hasPlan) { hasPlan = twoHandPlan(a, plan); if (hasPlan && plan.W != S) hasPlan = false; }
    if (hasPlan) { tgt.lerp(plan.tW, plan.w); pole.lerp(plan.poleW, plan.w); }
    int ids[3] = {b.i(K("upperArm", S)), b.i(K("lowerArm", S)), b.i(K("hand", S))};
    const auto& fing = rd_.fingers[si(S)];
    Quat q0[3]; for (int j = 0; j < 3; j++) q0[j] = b.pose->getQ(ids[j]);
    std::vector<Quat> f0; for (const auto& f : fing) f0.push_back(b.pose->getQ(f.i));
    int s = si(S);
    if (A.mode == "swing" && S == hand) {
      b.ik("arm", S, tgt, pole, 1, true);
      b.aim(K("hand", S), dir, 0.9f);
      fist(S, 1, 1);
      if (hasPlan) gripHand(S, plan.fW, plan.pnW, plan.w, 1.0f);
      for (int j = 0; j < 3; j++) webGripQ_[s][j] = b.pose->getQ(ids[j]);
      webGripF_[s].clear(); for (const auto& f : fing) webGripF_[s].push_back(b.pose->getQ(f.i));
      webGripHas_[s] = true;
    }
    if (webGripHas_[s]) {
      for (int j = 0; j < 3; j++) b.pose->setQ(ids[j], Quat::slerp(q0[j], webGripQ_[s][j], ww));
      for (size_t j = 0; j < fing.size() && j < webGripF_[s].size(); j++) b.pose->setQ(fing[j].i, Quat::slerp(f0[j], webGripF_[s][j], ww));
      b.dirty = true;
    }
  }
  if (A.mode == "swing" || A.mode == "air") {
    char Fr = other(hand); float wf = clampf(webW_ * 1.2f, 0, 1) * (1 - webWH_[si(Fr)]);
    if (wf > 0.01f) freeArmJump(Fr, wf);
  }
  if (!hasPlan && twoOk) hasPlan = twoHandPlan(a, plan);
  if (hasPlan) twoHandIK(plan);
}

// quick web boost (Q): one arm snaps toward the far anchor, then yanks the line to the chest
void Animator::postQuickYank(float dt, const AnimState& A) {
  const float Kreach = 0.97f, pullX = 0.17f, pullY = -0.16f, pullZ = 0.2f, holdT = 0.3f, leanK = 0.14f, twistK = 0.2f;
  bool on = A.quick.active && A.mode == "air";
  PoseBuilder& b = b_;
  for (char S : {'L', 'R'}) {
    QY& H = qy_[si(S)]; bool mine = on && A.quick.hand == S;
    if (mine) { H.a = A.quick.anchor; H.has = true; }
    float w = clampf(H.w.step(mine && A.quick.t < A.quick.hitT + holdT ? 1.f : 0.f, dt), 0, 1.05f);
    float p = clampf(H.p.step(mine && A.quick.t < A.quick.hitT ? 0.f : 1.f, dt), -0.15f, 1.12f);
    if (w < 0.01f || !H.has) continue;
    float sx = sxOf(S); Vec3 sh = b.pos(K("upperArm", S));
    Vec3 a = worldToChar(H.a), dir = (a - sh).normalized();
    dir.z = std::max(dir.z, 0.25f); dir.y = clampf(dir.y, -0.2f, 0.8f); dir.x = sx > 0 ? std::max(dir.x, -0.25f) : std::min(dir.x, 0.25f); dir.normalize();
    float reach = (rd_.a1 + rd_.a2) * Kreach, ex = 1 - clampf(p, 0, 1), pc = clampf(p, 0, 1);
    b.rot("spine", X, leanK * 0.5f * w * pc); b.rot("chest", X, leanK * 0.5f * w * pc);
    b.rot("chest", Y, sx * twistK * w * (0.6f * pc - ex));
    b.rot("neck", X, -leanK * 0.4f * w * pc);
    Vec3 sh2 = b.pos(K("upperArm", S));
    Vec3 outP = sh2 + dir * reach, pulled{sx * pullX, sh2.y + pullY, sh2.z + pullZ};
    Vec3 tgt = vlerp(outP, pulled, p);
    Vec3 pole = vlerp(sh2 + dir * (reach * 0.5f), Vec3{sx * 0.55f, sh2.y - 0.3f, sh2.z - 0.3f}, p) + Vec3{sx * 0.25f, -0.2f, -0.2f} * ex;
    b.ik("arm", S, tgt, pole, std::min(1.f, w), true);
    b.aim(K("hand", S), dir, std::min(1.f, w) * ex * 0.8f);
    fist(S, 0.35f + 0.6f * pc, std::min(1.f, w));
    b.dirty = true;
  }
}

// free arm in the jump / air pose (spread, pulled back, hand ~shoulder height, wrist continuing the forearm)
void Animator::freeArmJump(char S, float w) {
  PoseBuilder& b = b_; float sx = sxOf(S), ld = sx == jumpLead_ ? 1.f : 0.f;
  Vec3 sh = b.pos(K("upperArm", S));
  float Rr = rd_.a1 + rd_.a2;
  Vec3 dir = Vec3{sx * 0.6f - sh.x, -(ld ? 0.06f : 0.14f), -0.24f - sh.z}.normalized();
  Vec3 tgt = sh + dir * (Rr * 0.93f), pole = sh + dir * (Rr * 0.5f) + Vec3{0, -0.2f, -0.3f};
  b.ik("arm", S, tgt, pole, w, true);
  int iF = b.i(K("lowerArm", S)), iH = b.i(K("hand", S));
  if (iF >= 0 && iH >= 0) b.setCQ(iH, b.cq(iF) * skel_.bQ(iF).conj() * skel_.bQ(iH), w);
  fist(S, 0.12f, w * 0.9f);
}

// ---------------------------------------------------------------- two-handed swing grip
void Animator::twoHandState(float dt, const AnimState& A, const Vec3* anc, char hand) {
  Two& T = two_;
  bool swinging = A.mode == "swing" && A.sub != "release" && anc;
  if (swinging) {
    if (T.hand != hand || T.anc.distanceToSquared(*anc) > 0.25f) { T.t = 0; T.on = false; T.dropped = false; T.anc = *anc; T.hand = hand; }
    T.t += dt;
  } else { T.t = 0; T.hand = 0; }
  twoPh_ = swingPhHand_ == hand ? swingPh_ : 0;
  const Vec3& v = A.velocity; float hs = std::hypot(v.x, v.z), hdg = hs > 3 ? std::atan2(v.x, v.z) : NAN;
  float hRate = 0;
  if (finite(hdg) && finite(T.hdg) && dt > 0) hRate = angWrap(hdg - T.hdg) / dt;
  T.hdg = hdg; T.hRate = damp(T.hRate, std::fabs(hRate) < 8 ? hRate : 0, 10, dt);
  float away = hand == 'R' ? 1.f : -1.f;
  float tBank = A.swing.bank * away, tRate = T.hRate * away;
  if (swinging && T.t > 0.25f && (tBank > 0.25f || tRate > 0.9f)) T.dropped = true;
  T.on = swinging && !T.dropped;
  char F = other(hand);
  for (char S : {'L', 'R'}) {
    int s = si(S); bool joining = S == F && T.on;
    T.spP[s] = clampf(T.spP[s] + (joining ? dt / 0.45f : -dt / 0.5f), 0, 1);
    float e = smoother01(T.spP[s]) + (joining ? 0.035f * std::sin(PI * clampf((T.spP[s] - 0.75f) / 0.25f, 0, 1)) : 0);
    T.spX[s] = damp(T.spX[s], e, 22, dt); if (T.spP[s] == 0 && T.spX[s] < 0.002f) T.spX[s] = 0;
    (S == 'L' ? T.L : T.R) = clampf(T.spX[s], 0, 1);
    T.sw[s] = 0;
  }
  T.max = smooth01(std::max(T.L, T.R));
}

void Animator::twoHandBody(float w, char hand) {
  PoseBuilder& b = b_; float sxW = hand == 'L' ? 1.f : -1.f;
  b.rotE("spine", 0.12f * w, sxW * 0.12f * w, sxW * 0.07f * w); b.rotE("chest", 0.06f * w, sxW * 0.1f * w, sxW * 0.03f * w);
  if (b.i("neck") >= 0) b.rotE("neck", -0.14f * w, 0, 0);
  b.rotE("head", 0.12f * w, 0, 0);
  for (char S : {'L', 'R'}) {
    int cl = b.i(K("shoulder", S)); if (cl < 0) continue;
    Vec3 cp = b.pos(cl), up0 = (b.pos(K("upperArm", S)) - cp).normalized();
    b.aim(cl, vlerp(up0, Y, 0.2f).normalized(), w * 0.25f);
  }
}

// swing legs: together, hanging below the hips, knees slightly bent, toes pointed; tucked on the two-handed upswing
void Animator::swingLegs(float w, float tuck, char hand) {
  if (w < 0.005f) return;
  PoseBuilder& b = b_; float L = rd_.legLen, l1 = rd_.l1, l2 = rd_.l2;
  Vec3 hL = b.pos("upperLegL"), hR = b.pos("upperLegR");
  Vec3 mid = (hL + hR) * 0.5f;
  if (tuck > 0.01f) { b.rotE("spine", 0.3f * tuck * w, 0, 0); if (b.i("spine1") >= 0) b.rotE("spine1", 0.12f * tuck * w, 0, 0); mid = (b.pos("upperLegL") + b.pos("upperLegR")) * 0.5f; }
  for (char S : {'L', 'R'}) {
    float sx = sxOf(S);
    Vec3 hip = b.pos(K("upperLeg", S));
    Vec3 foot0 = hip; foot0.x = mid.x + sx * 0.075f;
    float lead = (S == hand ? -1.f : 1.f) * 0.07f;
    Vec3 hang = foot0 + Vec3{0, -L * 0.94f + std::fabs(lead) * 0.3f + (lead < 0 ? 0.02f : 0.f), L * 0.1f + lead};
    Vec3 th = Vec3{0, 0.62f, 1}.normalized() * l1, sh = Vec3{0, -1, -0.4f}.normalized() * (l2 * 0.98f);
    Vec3 tk = foot0 + th + sh;
    Vec3 ank = vlerp(hang, tk, tuck);
    Vec3 knee = vlerp(hip, ank, 0.5f) + Vec3{0, 0.25f * tuck, 0.5f + 0.2f * tuck};
    b.ik("leg", S, ank, knee, w, true);
    Vec3 shin = (b.pos(K("foot", S)) - b.pos(K("lowerLeg", S))).normalized();
    b.aim(K("foot", S), (shin * 0.7f + Vec3{0, 0, lerpf(0.7f, 0.45f, tuck)}).normalized(), w * 0.85f);
  }
}

bool Animator::handKids(char S, int& idx, int& mid, int& pin, float& len) {
  int s = si(S);
  if (!handKidsInit_[s]) {
    handKidsInit_[s] = true;
    int h = skel_.idx(K("hand", S)), i1 = -1, m1 = -1, p1 = -1, r1 = -1;
    if (h >= 0) for (int i = 0; i < skel_.N; i++) if (skel_.parent[i] == h) {
      std::string n = skel_.name[i]; for (char& c : n) c = (char)std::tolower((unsigned char)c);
      if (i1 < 0 && n.find("index") != std::string::npos) i1 = i;
      if (m1 < 0 && n.find("middle") != std::string::npos) m1 = i;
      if (p1 < 0 && n.find("pinky") != std::string::npos) p1 = i;
      if (r1 < 0 && n.find("ring") != std::string::npos) r1 = i;
    }
    if (p1 < 0) p1 = r1;
    if (h >= 0 && i1 >= 0 && m1 >= 0 && p1 >= 0) { handKids_[s][0] = i1; handKids_[s][1] = m1; handKids_[s][2] = p1; handLen_[s] = skel_.bP(m1).distanceTo(skel_.bP(h)); handKidsOk_[s] = true; }
  }
  if (!handKidsOk_[s]) return false;
  idx = handKids_[s][0]; mid = handKids_[s][1]; pin = handKids_[s][2]; len = handLen_[s];
  return true;
}

Animator::Grip Animator::gripFrame(char S, const Vec3& g, const Vec3& d, const Vec3& sh, const Vec3& fwd, float beta) {
  int i, m, p; float hl = 0.09f; handKids(S, i, m, p, hl);
  Vec3 mm = g - sh; mm -= d * mm.dot(d); if (mm.lengthSq() < 1e-8f) mm = {S == 'L' ? -1.f : 1.f, 0, 0}; mm.normalize();
  Vec3 f = (d * std::cos(beta) + mm * std::sin(beta)).normalized();
  Vec3 n = fwd - f * fwd.dot(f); if (n.lengthSq() < 1e-6f) n = mm.cross(f); n.normalize();
  return {f, n, g - f * (hl * 0.55f) - n * 0.02f};
}

Animator::Grip Animator::overlapFrame(char S, const Vec3& g, const Grip& fw) {
  int i, m, p; float hl = 0.09f; handKids(S, i, m, p, hl);
  Vec3 f = Vec3{-fw.f.x, fw.f.y, fw.f.z}.normalized();
  Vec3 n{-fw.n.x, fw.n.y, fw.n.z}; n -= f * n.dot(f); n.normalize();
  Vec3 sock = g + n * twoOffAdd_ - dLine_ * 0.045f;
  return {f, n, sock - f * (hl * 0.55f) - n * 0.02f};
}

bool Animator::twoHandPlan(const Vec3& a, Plan& pl) {
  PoseBuilder& b = b_; Two& T = two_;
  char F = std::max(T.L, T.sw[0]) >= std::max(T.R, T.sw[1]) ? 'L' : 'R', W = other(F);
  float ww = clampf(webWH_[si(W)] * 1.1f, 0, 1);
  float w = smooth01(T.get(F)) * ww, raw = T.spX[si(F)] * ww, sw = T.sw[si(F)] * ww;
  if (w < 0.005f && sw < 0.005f) return false;
  float Rr = rd_.a1 + rd_.a2;
  Vec3 shW = b.pos(K("upperArm", W)), shF = b.pos(K("upperArm", F));
  Vec3 M = (shW + shF) * 0.5f;
  Vec3 d = (a - M).normalized();
  if (d.y < 0.5f) { d.y = 0.5f; d.normalize(); }
  Vec3 u = shW - shF; u -= d * u.dot(d); u.normalize();
  Vec3 fwd = u.cross(d); if (fwd.z < 0) fwd = -fwd;
  dLine_ = d;
  struct Fr { Vec3 g; Grip fw, fo; };
  auto frames = [&](float t) { Vec3 g = M + d * t; Grip fw = gripFrame(W, g, d, shW, fwd, 1.35f); return Fr{g, fw, overlapFrame(F, g, fw)}; };
  auto bis = [](auto fn, float lo, float hi) { for (int i = 0; i < 20; i++) { float m = (lo + hi) / 2; if (fn(m)) hi = m; else lo = m; } return (lo + hi) / 2; };
  auto reach = [&](float t) { Fr fr = frames(t); return std::max(fr.fw.wrist.distanceTo(shW), fr.fo.wrist.distanceTo(shF)); };
  float t = bis([&](float tt) { return reach(tt) > 0.9f * Rr; }, 0, 1.4f);
  Vec3 hc = headCentre(); float clr = 0.12f + 0.05f + 0.06f;
  float tHead = bis([&](float tt) { Vec3 g = frames(tt).g; return g.distanceTo(hc) >= clr && (g - hc).dot(d) > 0; }, 0, 1.4f);
  if (tHead > t) t = std::min(tHead, bis([&](float tt) { return reach(tt) > 0.99f * Rr; }, 0, 1.4f));
  Fr fr = frames(t); float sxW = W == 'L' ? 1.f : -1.f;
  pl.F = F; pl.W = W; pl.joining = T.on && T.hand == W; pl.w = w; pl.raw = raw; pl.sw = sw; pl.d = d; pl.g = fr.g;
  pl.tW = fr.fw.wrist; pl.tF = fr.fo.wrist; pl.fW = fr.fw.f; pl.fF = fr.fo.f; pl.pnW = fr.fw.n; pl.pnF = fr.fo.n;
  pl.poleW = vlerp(shW, fr.fw.wrist, 0.5f) + Vec3{sxW * 0.35f, 0.05f, -0.3f};
  pl.poleF = vlerp(shF, fr.fo.wrist, 0.5f) + Vec3{-sxW * 0.35f, 0.05f, -0.3f};
  pl.shF = shF;
  return true;
}

// closed fist: strong knuckle flexion, thumb folded over; amount 0 open .. 1 tight
void Animator::fist(char S, float amount, float w) {
  if (w <= 0.001f) return;
  if (fist_.empty()) {
    fist_.resize(2);
    for (char s2 : {'L', 'R'}) for (const auto& f : rd_.fingers[si(s2)]) {
      std::string n = skel_.name[f.i]; for (char& c : n) c = (char)std::tolower((unsigned char)c);
      int seg = 0; for (char c : n) if (std::isdigit((unsigned char)c)) { seg = std::clamp(c - '1', 0, 2); break; }
      bool thumb = n.find("thumb") != std::string::npos;
      float mk = n.rfind("index", 0) == 0 ? 0.9f : n.rfind("pinky", 0) == 0 ? 1.1f : 1.f;
      const float TA[3] = {1.05f, 1.0f, 0.9f}, FA[3] = {2.6f, 1.8f, 1.1f};
      float a = thumb ? TA[seg] : FA[seg] * mk;
      fist_[si(s2)].push_back({f.i, {f.axis, a * sxOf(s2)}});
    }
  }
  for (const auto& e : fist_[si(S)]) {
    Quat q = skel_.rest.getQ(e.first) * Quat::axisAngle(e.second.first, e.second.second * amount);
    if (w < 1) q = Quat::slerp(b_.pose->getQ(e.first), q, w);
    b_.pose->setQ(e.first, q);
  }
  b_.dirty = true;
}

void Animator::gripHand(char S, const Vec3& f0, const Vec3& pN, float w, float curl) {
  PoseBuilder& b = b_; float sx = sxOf(S);
  Vec3 el = b.pos(K("lowerArm", S)), wr = b.pos(K("hand", S));
  Vec3 fa = (wr - el).normalized();
  Vec3 f = f0; float ang = angleTo(fa, f), maxA = 0.95f;
  if (ang > maxA) { Vec3 ax = fa.cross(f); if (ax.lengthSq() > 1e-8f) f = applyAxisAngle(fa, ax.normalized(), maxA); }
  int i, m, p; float len;
  if (handKids(S, i, m, p, len)) {
    Vec3 fc = b.pos(m) - wr, ac = b.pos(p) - b.pos(i);
    Vec3 pc = ac.cross(fc) * sx;
    Quat R = frameRot(fc, pc, f, pN);
    if (w < 0.999f) R = Quat::slerp(R, Quat(), 1 - w);
    b.setCQ(K("hand", S), R * b.cq(K("hand", S)));
  } else b.aim(K("hand", S), f, w);
  fist(S, curl, w);
}

void Animator::twoHandIK(const Plan& pl) {
  PoseBuilder& b = b_; char F = pl.F, W = pl.W; float w2 = pl.w;
  float k = clampf(pl.raw, 0, 1.04f); if (k < 0.003f && w2 < 0.003f) return;
  int ids[3] = {b.i(K("upperArm", F)), b.i(K("lowerArm", F)), b.i(K("hand", F))};
  const auto& fing = rd_.fingers[si(F)];
  Quat q0[3]; for (int j = 0; j < 3; j++) q0[j] = b.pose->getQ(ids[j]);
  std::vector<Quat> f0; for (const auto& f : fing) f0.push_back(b.pose->getQ(f.i));
  auto grip = [&](const Vec3& tF) {
    for (int j = 0; j < 3; j++) b.pose->setQ(ids[j], q0[j]); b.dirty = true;
    b.ik("arm", F, tF, pl.poleF, 1, true);
    gripHand(F, pl.fF, pl.pnF, 1, 1.15f);
  };
  int s = si(F);
  Quat q1[3]; std::vector<Quat> f1;
  if ((pl.joining && A_->mode == "swing") || !twoGripHas_[s]) {
    grip(pl.tF);
    float pr = twoProbe(F, W);
    if (pr < -0.015f) { float need = -pr - 0.012f; twoOffAdd_ = std::min(0.01f, twoOffAdd_ + need); grip(pl.tF + pl.pnW * need); }
    else if (pr > -0.006f && twoOffAdd_ > 0) twoOffAdd_ = std::max(0.f, twoOffAdd_ - 0.001f);
    for (int j = 0; j < 3; j++) q1[j] = b.pose->getQ(ids[j]);
    for (const auto& f : fing) f1.push_back(b.pose->getQ(f.i));
    for (int j = 0; j < 3; j++) twoGripQ_[s][j] = q1[j];
    twoGripF_[s] = f1; twoGripHas_[s] = true;
  } else { for (int j = 0; j < 3; j++) q1[j] = twoGripQ_[s][j]; f1 = twoGripF_[s]; }
  float kmin = std::min(k, 1.f);
  float kj[3] = {std::pow(k, 0.8f), std::pow(kmin, 1.25f) * (k > 1 ? k : 1), std::pow(kmin, 1.6f)};
  for (int j = 0; j < 3; j++) b.pose->setQ(ids[j], Quat::slerp(q0[j], q1[j], std::min(kj[j], 1.04f)));
  float cw = smooth01((kmin - 0.7f) / 0.3f);
  for (size_t j = 0; j < fing.size() && j < f1.size(); j++) b.pose->setQ(fing[j].i, Quat::slerp(f0[j], f1[j], cw));
  b.dirty = true;
}

Vec3 Animator::headCentre() {
  PoseBuilder& b = b_; Vec3 h = b.pos("head");
  Quat hq = b.cq("head") * skel_.bQ(b.i("head")).conj();
  return h + hq * Vec3{0, 0.1f, 0.02f};
}

// min distance between the second hand and the web hand + forearm (minus rough radii)
float Animator::twoProbe(char F, char W) {
  PoseBuilder& b = b_;
  struct Pt { Vec3 p; float r; }; struct Sg { Vec3 a, b; float r; };
  std::vector<Pt> pts{{b.pos(K("hand", F)), 0.025f}};
  for (const auto& f : rd_.fingers[si(F)]) pts.push_back({b.pos(f.i), 0.009f});
  std::vector<Sg> segs{{b.pos(K("lowerArm", W)), b.pos(K("hand", W)), 0.03f}};
  for (const auto& f : rd_.fingers[si(W)]) { int c = skel_.child[f.i]; segs.push_back({b.pos(f.i), b.pos(c >= 0 ? c : f.i), 0.009f}); }
  int i, m, p; float len; int midW = handKids(W, i, m, p, len) ? m : b.i(K("hand", W));
  segs.push_back({b.pos(K("hand", W)), b.pos(midW), 0.025f});
  float best = INF;
  for (const auto& P0 : pts) for (const auto& S : segs) {
    Vec3 ab = S.b - S.a; float L2 = ab.lengthSq(); float t = L2 > 1e-10f ? clampf((P0.p - S.a).dot(ab) / L2, 0, 1) : 0;
    float dd = P0.p.distanceTo(S.a + ab * t) - S.r - P0.r; best = std::min(best, dd);
  }
  return best;
}

// ---------------------------------------------------------------- secondary / look / breath
void Animator::postSecondary(float dt, const AnimState& A) {
  bool inAir = A.mode == "swing" || A.mode == "air" || A.mode == "zip";
  Vec3 acc = dirToChar(accel_) * (inAir ? 1.f : 0.f);
  if (acc.length() > 40) acc.setLength(40);
  Vec3 s = legSpring_.step({-acc.x * 0.012f, 0, -acc.z * 0.012f}, dt);
  Vec3 a = armSpring_.step({-acc.x * 0.018f, 0, -acc.z * 0.02f}, dt);
  if (!inAir && s.lengthSq() < 1e-6f) return;
  PoseBuilder& b = b_;
  float lx = clampf(s.x, -0.35f, 0.35f), lz = clampf(s.z, -0.4f, 0.4f);
  for (char S : {'L', 'R'}) { b.rotE(K("upperLeg", S), -lz, 0, lx); b.rotE(K("lowerLeg", S), lz * 0.4f, 0, 0); }
  auto arm = [&](char S) {
    float fk = (1 - std::max(smooth01(two_.get(S)), two_.sw[si(S)])) * (A.mode == "swing" ? 1.f : 1 - webWH_[si(S)]);
    b.rotE(K("upperArm", S), -clampf(a.z, -0.5f, 0.5f) * fk, 0, clampf(a.x, -0.4f, 0.4f) * fk);
  };
  if (A.mode == "swing") arm(other(A.swing.hand)); else { arm('L'); arm('R'); }
}

void Animator::postLook(float dt, const AnimState& A, float w) {
  float yaw = 0, pitch = 0;
  if (w > 0.01f) {
    Vec3 d = dirToChar(A.lookDir);
    yaw = std::atan2(d.x, d.z); pitch = -std::atan2(d.y, std::hypot(d.x, d.z));
    float behind = smooth01((std::fabs(yaw) - 1.6f) / 0.6f);
    yaw = clampf(yaw, -1.2f, 1.2f) * (1 - behind); pitch = clampf(pitch, -0.6f, 0.7f) * (1 - behind);
  }
  float y = lookYaw_.step(yaw * w, dt), p = lookPitch_.step(pitch * w, dt);
  if (std::fabs(y) + std::fabs(p) < 1e-3f) return;
  PoseBuilder& b = b_;
  Vec3 ax = (b.cq("chest") * skel_.bQ(b.i("chest")).conj()) * UP;
  const std::pair<const char*, float> parts[] = {{"spine1", 0.15f}, {"chest", 0.2f}, {"neck", 0.3f}, {"head", 0.35f}};
  for (const auto& [k, f] : parts) {
    if (b.i(k) < 0) continue;
    b.rot(k, ax, y * f);
    std::string ks(k);
    if (ks == "neck" || ks == "head") { Vec3 side = (b.cq(k) * skel_.bQ(b.i(k)).conj()) * X; b.rot(k, side, p * (ks == "head" ? 0.6f : 0.4f)); }
  }
}

void Animator::postBreath(float dt, Layer& top) {
  float exert = clampf(runSpeedMem_ / 12, 0, 1);
  float f = lerpf(0.28f, 0.55f, exert), amp = lerpf(0.012f, 0.028f, exert) * (top.name == NodeK::Ground && speedH_ < 1 ? 1.f : top.name == NodeK::Perch ? 1.f : 0.3f);
  float s = std::sin(time_ * TAU * f);
  b_.rotE("chest", -s * amp, 0, 0);
  if (b_.i("shoulderL") >= 0) { b_.rotE("shoulderL", 0, 0, s * amp * 0.5f); b_.rotE("shoulderR", 0, 0, -s * amp * 0.5f); }
  impact_.step(0, dt);
}

// foot IK: raycast the ground under each foot, pelvis drops to let the lower foot reach, planted feet follow the slope
void Animator::postFeet(float dt, const AnimState& A, float w) {
  PoseBuilder& b = b_;
  float dip = clampf(impact_.x, -0.25f, 0.05f);
  if (w < 0.02f && std::fabs(dip) < 1e-3f) { pelvisOff_ = damp(pelvisOff_, 0, 10, dt); return; }
  const Vec3& O = visP;
  Vec3 upC = dirToChar(UP);
  bool upright = upC.y > 0.85f;
  float offs[2] = {0, 0}, plantW[2]; Vec3 ank[2];
  for (char S : {'L', 'R'}) {
    int s = si(S);
    Vec3 a = b.pos(K("foot", S)); ank[s] = a;
    float lift = std::max(0.f, a.y - rd_.ankleH);
    plantW[s] = 1 - smooth01(lift / 0.25f);
    float off = 0;
    if (upright && world_ && w > 0.02f) {
      Vec3 wp = charToWorld.transformPoint(a); Hit h;
      if (world_->raycast({wp.x, O.y + 0.6f, wp.z}, {0, -1, 0}, 1.6f, h) && h.normal.y > 0.5f) {
        float gy = h.point.y;
        off = clampf(gy - O.y, -0.45f, 0.35f);
        footN_[s].lerp(dirToChar(h.normal), 1 - std::exp(-15 * dt)); footN_[s].normalize();
      } else { footN_[s].lerp(Y, 1 - std::exp(-10 * dt)); footN_[s].normalize(); }
    } else { footN_[s].lerp(Y, 1 - std::exp(-10 * dt)); footN_[s].normalize(); }
    footOff_[s] = damp(footOff_[s], off, 18, dt);
    offs[s] = footOff_[s];
  }
  float pTarget = clampf(std::min(offs[0], offs[1]), -0.45f, 0.08f) * w;
  pelvisOff_ = damp(pelvisOff_, pTarget, 12, dt);
  float pOff = pelvisOff_ + dip;
  if (std::fabs(pOff) > 1e-4f) b.moveHips(0, pOff, 0);
  for (char S : {'L', 'R'}) {
    int s = si(S);
    Vec3 tgt = ank[s]; tgt.y += offs[s] * w;
    if (A.grounded) tgt.y = std::max(tgt.y, rd_.ankleH * 0.97f + offs[s] * w);
    Vec3 knee = b.pos(K("lowerLeg", S)), hip = b.pos(K("upperLeg", S));
    Vec3 pole = knee + (knee - (hip + ank[s]) * 0.5f).normalized() * 0.5f;
    Quat fq = b.cq(K("foot", S));
    b.ik("leg", S, tgt, pole, 1, false);
    b.setCQ(K("foot", S), fq);
    const Vec3& n = footN_[s];
    if (n.y < 0.9999f) { Quat aq = Quat::fromUnitVectors(Y, n) * fq; b.setCQ(K("foot", S), Quat::slerp(fq, aq, plantW[s] * w)); }
  }
}

// airborne secondary life: wind flutter on the limbs, slow leg cycling in long falls
void Animator::postAirLife(float dt, const AnimState& A, Layer& top) {
  bool air = A.mode == "air" && top.name == NodeK::Air;
  airLifeW_ = damp(airLifeW_, air ? 1.f : 0.f, 4, dt);
  float w = airLifeW_; if (w < 0.01f) return;
  PoseBuilder& b = b_; float T = time_, sp = A.velocity.length();
  float fl = w * clampf((sp - 6) / 30, 0, 1);
  float fall = w * smooth01((-A.velocity.y - 6) / 8) * smooth01((airT_ - 0.5f) / 0.5f);
  for (char S : {'L', 'R'}) {
    float sx = sxOf(S), ph = S == 'L' ? 0 : PI;
    b.rotE(K("upperArm", S), noise1(T * 3.1f, sx * 3) * 0.12f * fl, 0, noise1(T * 2.7f, sx * 5 + 1) * 0.1f * fl);
    b.rotE(K("lowerArm", S), noise1(T * 4.3f, sx * 7) * 0.1f * fl, 0, 0);
    b.rotE(K("upperLeg", S), std::sin(T * TAU * 0.7f + ph) * 0.22f * fall + noise1(T * 2.3f, sx * 9) * 0.06f * fl, 0, 0);
    b.rotE(K("lowerLeg", S), (0.5f + 0.5f * std::sin(T * TAU * 0.7f + ph + 1.2f)) * 0.3f * fall, 0, 0);
  }
  b.rotE("head", noise1(T * 2, 11) * 0.05f * fl, noise1(T * 1.6f, 12) * 0.08f * fl, 0);
}

// half-joint helper bones (deltoid / glute: 50 % of the base bone) + forearm twist helpers (half the hand's roll)
void Animator::helpers(Pose& pose) {
  if (nHelp_ < 0) {
    nHelp_ = 0;
    const char* H[4][2] = {{"deltoidL", "upperArmL"}, {"deltoidR", "upperArmR"}, {"gluteL", "upperLegL"}, {"gluteR", "upperLegR"}};
    for (auto& h : H) { auto it = skel_.byName.find(h[0]); int bi = skel_.idx(h[1]); if (it != skel_.byName.end() && bi >= 0 && skel_.parent[it->second] == skel_.parent[bi]) { helpBone_[nHelp_][0] = it->second; helpBone_[nHelp_][1] = bi; nHelp_++; } }
  }
  for (int k = 0; k < nHelp_; k++) pose.setQ(helpBone_[k][0], Quat::slerp(skel_.rest.getQ(helpBone_[k][0]), pose.getQ(helpBone_[k][1]), 0.5f));
  if (!twInit_) {
    twInit_ = true;
    for (char S : {'L', 'R'}) { auto it = skel_.byName.find(K("forearmTwist", S)); int hi = skel_.idx(K("hand", S));
      if (it != skel_.byName.end() && hi >= 0) { Vec3 ax = skel_.rest.getP(hi); if (ax.lengthSq() > 1e-8f) twH_.push_back({it->second, hi, ax.normalized()}); } }
  }
  for (const auto& t : twH_) {
    Quat qb = skel_.rest.getQ(t.hi).conj() * pose.getQ(t.hi);
    float d = qb.x * t.ax.x + qb.y * t.ax.y + qb.z * t.ax.z;
    Quat qa{t.ax.x * d, t.ax.y * d, t.ax.z * d, qb.w}; float l = std::sqrt(qa.x * qa.x + qa.y * qa.y + qa.z * qa.z + qa.w * qa.w);
    if (l < 1e-6f) continue; qa = {qa.x / l, qa.y / l, qa.z / l, qa.w / l};
    Quat half = Quat::slerp(Quat(), qa, 0.5f);
    pose.setQ(t.ti, half * skel_.rest.getQ(t.ti));
  }
}
