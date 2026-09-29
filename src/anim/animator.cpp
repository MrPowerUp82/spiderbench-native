// Animator core: selection, layer stack, main update, visual root frame, wall contact.
#include "anim/animator_util.h"
#include "player/traversal/traversal.h"
#include "world/world.h"
#include <cstdio>
#include <map>

using namespace anim;

namespace {
// per-transition blend times: TRANS[from][to] ?? TRANS[from]['*'] ?? TRANS['*'][to] ?? 0.2
const std::map<std::string, std::map<std::string, float>> TRANS = {
  {"*", {{"land", 0.1f}, {"landHard", 0.08f}, {"landRoll", 0.1f}, {"jumpCharge", 0.16f}, {"jumpLaunch", 0.12f}, {"swing", 0.24f}, {"zip", 0.14f}, {"trick", 0.16f},
         {"perch", 0.14f}, {"crawl", 0.28f}, {"wallRun", 0.28f}, {"wallJump", 0.12f}, {"vault", 0.14f}, {"corner", 0.16f}, {"pointLaunch", 0.12f}, {"air", 0.3f}, {"ground", 0.3f}}},
  {"ground", {{"runStart", 0.16f}, {"runStop", 0.16f}, {"turn180", 0.14f}, {"jumpCharge", 0.18f}, {"jumpLaunch", 0.2f}, {"air", 0.28f}, {"wallRun", 0.3f}, {"slingshot", 0.25f}}},
  {"slingshot", {{"pointLaunch", 0.18f}, {"ground", 0.35f}}},
  {"runStart", {{"ground", 0.26f}}}, {"runStop", {{"ground", 0.34f}}}, {"turn180", {{"ground", 0.22f}}},
  {"jumpCharge", {{"jumpLaunch", 0.14f}, {"ground", 0.24f}}},
  {"jumpLaunch", {{"air", 0.38f}, {"land", 0.12f}}},
  {"air", {{"ground", 0.2f}, {"land", 0.1f}, {"swing", 0.26f}}},
  {"swing", {{"air", 0.34f}, {"trick", 0.18f}, {"swing", 0.3f}}},
  {"trick", {{"air", 0.4f}}},
  {"land", {{"ground", 0.38f}, {"jumpCharge", 0.18f}, {"jumpLaunch", 0.14f}}},
  {"zip", {{"perch", 0.14f}, {"air", 0.34f}}},
  {"perch", {{"ground", 0.34f}, {"perchToStand", 0.18f}}},
  {"perchToStand", {{"ground", 0.3f}}},
  {"crawl", {{"wallRun", 0.3f}, {"air", 0.32f}, {"jumpLaunch", 0.26f}, {"wallJump", 0.24f}, {"ledge", 0.12f}}},
  {"wallRun", {{"crawl", 0.36f}, {"air", 0.34f}, {"jumpLaunch", 0.26f}, {"wallJump", 0.24f}, {"vault", 0.2f}, {"ground", 0.3f}, {"ledge", 0.12f}}},
  {"ledge", {{"ground", 0.2f}}},
  {"wallJump", {{"air", 0.4f}}},
};
float blendTime(const std::string& a, const std::string& b) {
  auto fa = TRANS.find(a);
  if (fa != TRANS.end()) { auto x = fa->second.find(b); if (x != fa->second.end()) return x->second; auto s = fa->second.find("*"); if (s != fa->second.end()) return s->second; }
  const auto& st = TRANS.at("*"); auto y = st.find(b); if (y != st.end()) return y->second;
  return 0.2f;
}
struct Trait { const char* frame; float foot, look, web; };
const std::map<std::string, Trait> TRAITS = {
  {"ground", {"upright", 1, 0.8f, 0}}, {"runStart", {"upright", 0.6f, 0.4f, 0}}, {"runStop", {"upright", 1, 0.5f, 0}}, {"turn180", {"upright", 1, 0.2f, 0}},
  {"jumpCharge", {"upright", 1, 0.5f, 0}}, {"jumpLaunch", {"air", 0, 0.3f, 0}}, {"air", {"air", 0, 0.5f, 0}}, {"trick", {"air", 0, 0, 0}},
  {"land", {"upright", 0.9f, 0.3f, 0}}, {"perch", {"upright", 0.8f, 1, 0}}, {"perchToStand", {"upright", 1, 0.5f, 0}},
  {"swing", {"swing", 0, 0.45f, 1}}, {"zip", {"zip", 0, 0.3f, 1}}, {"pointLaunch", {"air", 0, 0, 0}}, {"slingshot", {"upright", 0.5f, 0.3f, 0}},
  {"crawl", {"wall", 0, 0.6f, 0}}, {"wallRun", {"wallRun", 0, 0.3f, 0}}, {"wallJump", {"wallOut", 0, 0, 0}}, {"ledge", {"ledge", 0, 0, 0}},
  {"vault", {"uprightWall", 0, 0.2f, 0}}, {"corner", {"wall", 0, 0.2f, 0}}, {"frozen", {"upright", 0, 0.5f, 0}},
};
const Trait& traitOf(const std::string& n) { auto it = TRAITS.find(n); return it != TRAITS.end() ? it->second : TRAITS.at("ground"); }
const std::map<std::string, NodeK> NODES = {
  {"frozen", NodeK::Frozen}, {"ground", NodeK::Ground}, {"runStart", NodeK::RunStart}, {"runStop", NodeK::RunStop}, {"turn180", NodeK::Turn180},
  {"jumpCharge", NodeK::JumpCharge}, {"jumpLaunch", NodeK::JumpLaunch}, {"air", NodeK::Air}, {"trick", NodeK::Trick}, {"land", NodeK::Land},
  {"perch", NodeK::Perch}, {"perchToStand", NodeK::PerchToStand}, {"zip", NodeK::Zip}, {"pointLaunch", NodeK::PointLaunch}, {"slingshot", NodeK::Slingshot},
  {"swing", NodeK::Swing}, {"crawl", NodeK::Crawl}, {"wallRun", NodeK::WallRun}, {"wallJump", NodeK::WallJump}, {"vault", NodeK::Vault},
  {"ledge", NodeK::Ledge}, {"corner", NodeK::Corner},
};
const std::map<std::string, float> PTRICK = {{"layout", 1.3f}, {"corkscrew", 0.78f}, {"tuckFlip", 0.9f}, {"scissor", 0.7f}};
bool isTrick(const std::string& t) { return PTRICK.count(t) > 0; }
}  // namespace

bool Animator::build(Rig& rig) {
  rig_ = &rig;
  skel_.build(rig);
  rd_.build(skel_);
  b_ = PoseBuilder(&skel_);
  clips_.build(rig, skel_);
  const int N = skel_.N;
  for (Pose* p : {&P.a, &P.b, &P.c, &P.d, &P.e, &P.idle, &P.loco, &P.tmp, &P.mir, &P.out, &P.prev, &P.fi}) *p = Pose(N);
  P.out.copy(skel_.rest); P.prev.copy(skel_.rest);
  std::printf("[anim] animator: %d bones, legLen %.3f, arm %.3f, ankleH %.3f, keys %zu\n", N, rd_.legLen, rd_.a1 + rd_.a2, rd_.ankleH, skel_.key.size());
  return N > 0 && skel_.idx("hips") >= 0;
}

// ---------------------------------------------------------------- selection
std::string Animator::trickKey(const std::string& trick) {
  if (trick != lastTrick_) { lastTrick_ = trick; trickSeq_++; }
  return trick + ":" + std::to_string(trickSeq_);
}

std::string Animator::select(const AnimState& A) {
  const std::string &mode = A.mode, &sub = A.sub;
  Layer* top = layers_.empty() ? nullptr : layers_.back().get();
  if (top && hold(*top, A)) return top->key;
  std::string trick = !A.trick.empty() && A.trick != lastTrickDone_ ? A.trick : "";
  float hs = speedH_;
  if (mode == "ground" || mode == "combat") {
    if (sub == "slingshot") return "slingshot";
    if (sub == "jumpCharge") return "jumpCharge";
    if (sub == "jumpLaunch") return "jumpLaunch";
    if (sub == "landRoll" || sub == "landHard" || sub == "landMedium") return "land";
    if (sub == "vault") return "jumpLaunch";
    if (top && top->nameStr == "perch" && hs < 2) return "perchToStand";
    return groundVariant(top, A);
  }
  if (mode == "land") {
    if ((sub == "landLight" || (sub.empty() && A.landing.severity < 0.25f)) && hs > 3) { kickImpact(A); return "ground"; }
    return "land";
  }
  if (mode == "air") {
    if (!trick.empty() && isTrick(trick)) return "trick#" + trickKey(trick);
    if (sub == "jumpLaunch") return "jumpLaunch";
    if (sub == "pointLaunch") return "pointLaunch";
    if (sub == "zipPull") return "zip";
    if (sub == "wallJump") return "wallJump";
    if (sub == "vault") return "jumpLaunch";
    return "air";
  }
  if (mode == "swing") return sub == "release" ? "air" : std::string("swing#") + A.swing.hand;
  if (mode == "zip") return sub == "pointLaunch" ? "pointLaunch" : "zip";
  if (mode == "perch") return "perch";
  if (mode == "wall") {
    if (sub == "wallZip") return "zip";
    if (sub == "wallRun" || sub == "wallRunSide") return "wallRun";
    if (sub == "wallJump") return "wallJump";
    if (sub == "vault") return "jumpLaunch";
    if (sub == "cornerWrap") return "corner";
    if (sub == "ledgeGrab" || sub == "ledgeClimb") return clips_.has("ledgeGrab") || clips_.has("ledgeClimbQuick") ? "ledge" : "jumpLaunch";
    return "crawl";
  }
  return "ground";
}

std::string Animator::groundVariant(Layer* top, const AnimState& A) {
  float v = speedH_; std::string tk = top ? top->nameStr : "";
  if (tk == "runStart" || tk == "runStop" || tk == "turn180") lastOneShot_[tk] = time_;
  auto ok = [&](const char* k) { auto it = lastOneShot_.find(k); return time_ - (it != lastOneShot_.end() ? it->second : -9.f) > 0.7f; };
  if (!ok("runStart") || !ok("runStop") || !ok("turn180")) return "ground";
  // user feedback #14: stopping / reversing settles straight into idle (no runStop skid, no turn180 pivot)
  return tk == "ground" && clips_.has("runStart") && stillT_ > 0.25f && v > 0.25f && A.mode != "combat" && (A.sub == "run" || A.sub == "sprint") && moveIntent_ > 5 ? "runStart" : "ground";
}

void Animator::kickImpact(const AnimState& A) {
  if (impactKicked_) return; impactKicked_ = true;
  impact_.v -= 1.2f + 2.2f * A.landing.severity;
}

Layer* Animator::setTarget(const std::string& key) {
  Layer* top = layers_.empty() ? nullptr : layers_.back().get();
  if (top && top->key == key) return top;
  std::string name = key.substr(0, key.find('#'));
  auto it = NODES.find(name); if (it == NODES.end()) { name = "ground"; it = NODES.find(name); }
  float bt = top ? blendTime(top->nameStr, name) : 0;
  auto L = std::make_unique<Layer>();
  L->key = key; L->name = it->second; L->nameStr = name; L->w = bt <= 0 ? 1 : 0; L->dur = std::max(bt, 1e-3f); L->t = 0;
  if (!pool_.empty()) { L->pose = std::move(pool_.back()); pool_.pop_back(); } else L->pose = Pose(skel_.N);
  enter(*L, *A_, top);
  layers_.push_back(std::move(L));
  if (layers_.size() > 6) { // collapse the two oldest into a frozen snapshot
    Layer& a = *layers_[0]; Layer& b = *layers_[1];
    blendPoses(a.pose, b.pose, smooth01(b.w), a.pose);
    pool_.push_back(std::move(b.pose));
    a.name = NodeK::Frozen; a.key = "frozen"; a.nameStr = "frozen"; a.w = 1;
    layers_.erase(layers_.begin() + 1);
  }
  return layers_.back().get();
}

// ---------------------------------------------------------------- main update
void Animator::update(float dt, const AnimIO& io) {
  const AnimState& A = *io.anim; A_ = io.anim; io_ = io; world_ = io.world;
  dt = std::min(dt, 1.f / 15);
  time_ += dt; dt_ = dt;
  if (A.mode != "land") impactKicked_ = false;
  // kinematics
  const Vec3& vel = A.velocity;
  velS_.lerp(vel, 1 - std::exp(-20 * dt));
  accel_.lerp((vel - prevVel_) / std::max(dt, 1e-3f), 1 - std::exp(-10 * dt));
  prevVel_ = vel;
  prevSpeed_ = speedH_;
  float hs = std::hypot(vel.x, vel.z);
  speedH_ = hs;
  stillT_ = hs < 0.2f ? stillT_ + dt : (hs > 0.25f && prevSpeed_ <= 0.25f ? stillT_ : 0);
  runSpeedMem_ = std::max(hs, runSpeedMem_ - dt * 8);
  decel_ = damp(decel_, (prevSpeed_ - hs) / std::max(dt, 1e-3f), 12, dt);
  moveIntent_ = A.sub == "sprint" ? 15 : A.sub == "run" ? 8.5f : hs;
  if (!yawInit_) { yaw_ = std::atan2(A.lookDir.x, A.lookDir.z); if (hs > 0.5f) yaw_ = std::atan2(vel.x, vel.z); yawInit_ = true; }
  bool upMode = A.mode == "ground" || A.mode == "land" || A.mode == "combat" || A.mode == "perch";
  float wantYaw = upMode ? A.facing : hs > 0.4f ? std::atan2(vel.x, vel.z) : yaw_;
  wantYaw_ = wantYaw;
  turnErr_ = std::fabs(angWrap(wantYaw - yaw_));
  // state machine
  std::string key = select(A);
  setTarget(key);
  idleT_ += dt; airT_ = A.mode == "air" ? airT_ + dt : 0;
  for (auto& L : layers_) { L->t += dt; L->w = std::min(1.f, L->w + dt / L->dur); }
  int drop = 0;
  for (int i = (int)layers_.size() - 1; i > 0; i--) if (layers_[i]->w >= 1) { drop = i; break; }
  if (drop) { for (int i = 0; i < drop; i++) pool_.push_back(std::move(layers_[i]->pose)); layers_.erase(layers_.begin(), layers_.begin() + drop); }
  Layer& top = *layers_.back();
  if (top.name != NodeK::Turn180) {
    float rate = top.name == NodeK::Ground ? lerpf(7, 11, clampf(hs / 8, 0, 1)) : 9;
    float d = angWrap(wantYaw - yaw_);
    float yr = clampf(d * rate, -14, 14);
    yawRate_ = hs > 0.4f ? yr : 0;
    yaw_ += yr * dt;
  } else yawRate_ = 0;
  Pose& out = P.out;
  bool first = true;
  for (auto& L : layers_) {
    eval(*L, A, L->pose);
    if (first) { out.copy(L->pose); first = false; }
    else blendPoses(out, L->pose, smooth01(L->w), out);
  }
  // trait weights (blended across layers)
  float foot = 0, look = 0, web = 0;
  { float acc = 1; for (int i = (int)layers_.size() - 1; i >= 0; i--) { Layer& L = *layers_[i]; float w = i == 0 ? acc : acc * smooth01(L.w);
      const Trait& T = traitOf(L.nameStr); foot += T.foot * w; look += T.look * w; web += T.web * w; acc -= w; if (acc <= 1e-4f) break; } }
  // visual root
  updateFrame(dt, A, top);
  // procedural post layers (character space)
  b_.begin(out);
  postWeb(dt, A, web);
  postQuickYank(dt, A);
  postSecondary(dt, A);
  postLook(dt, A, look * (1 - 0.8f * two_.max));
  postBreath(dt, top);
  if (A.grounded && (A.mode == "land" || A.mode == "ground")) foot = std::max(foot, 0.95f);
  postFeet(dt, A, foot);
  postAirLife(dt, A, top);
  helpers(out);
  // NaN guard: keep the last good pose
  { bool bad = false; for (float v : out.q) if (v != v) { bad = true; break; } if (!bad) for (float v : out.p) if (v != v) { bad = true; break; }
    if (bad) { if (!nanWarned_) { nanWarned_ = true; std::fprintf(stderr, "[anim] NaN pose in %s\n", top.key.c_str()); } out.copy(P.prev); } else P.prev.copy(out); }
  skel_.apply(out, *rig_);
  rig_->setObjectMatrix(charToWorld);
  rig_->updateWorld();
  wallContact(dt, A, top);
  prevMode_ = A.mode; prevSub_ = A.sub;
  if (top.name == NodeK::Trick && !A.trick.empty()) lastTrickDone_ = A.trick;
  if (A.trick.empty()) { lastTrickDone_.clear(); lastTrick_.clear(); }
  debugNode = top.key; debugClip = top.data.clip.empty() ? top.nameStr : top.data.clip;
  debugLayers.clear(); for (auto& L : layers_) { char buf[64]; std::snprintf(buf, sizeof buf, "%s:%.2f ", L->key.c_str(), L->w); debugLayers += buf; }
}

// Wall-plane contact: shift the visual root along the wall normal so no joint penetrates the facade (push-out always),
// and pull in a little while crawling so hands / feet stay on it.
void Animator::wallContact(float dt, const AnimState& A, Layer& top) {
  bool ledge = top.name == NodeK::Ledge && A.ledge.active;
  bool onWall = ledge || (A.mode == "wall" && A.sub != "ledgeGrab" && A.sub != "ledgeClimb" && top.name != NodeK::Vault);
  float wd = A.wall.dist; const Vec3& C = io_.center;
  float corr = 0;
  if (onWall && (ledge || (finite(wd) && wd > 0.05f))) {
    Vec3 n = ledge ? -A.ledge.inward : A.wall.normal;
    float pd = ledge ? A.ledge.point.dot(n) : C.dot(n) - wd;
    float maxY = ledge ? A.ledge.point.y - 0.06f : INF;
    struct J { const char* k; float r; bool contact; };
    static const J JS[] = {{"footL", 0.055f, true}, {"footR", 0.055f, true}, {"handL", 0.04f, true}, {"handR", 0.04f, true}, {"lowerLegL", 0.075f, false}, {"lowerLegR", 0.075f, false},
                           {"lowerArmL", 0.05f, false}, {"lowerArmR", 0.05f, false}, {"hips", 0.1f, false}, {"chest", 0.1f, false}, {"head", 0.1f, false}, {"toeL", 0.025f, true}, {"toeR", 0.025f, true}};
    float pen = 0, gap = INF;
    skel_.fk(P.out);
    for (const auto& j : JS) {
      int i = skel_.idx(j.k); if (i < 0) continue;
      Vec3 v = charToWorld.transformPoint(skel_.cP(i)); if (v.y > maxY) continue;
      float d = v.dot(n) - pd - j.r;
      pen = std::max(pen, -d); if (j.contact) gap = std::min(gap, d);
    }
    corr = pen > 0 ? pen : (top.name == NodeK::Crawl && finite(gap) && gap > 0 ? -std::min(gap, 0.06f) : 0);
  }
  float k = corr > wallCorr_ ? 25.f : 8.f;
  wallCorr_ = damp(wallCorr_, corr, k, dt);
  if (corr > 0) wallCorr_ = std::max(wallCorr_, corr * 0.9f);
  if (std::fabs(wallCorr_) < 1e-4f) return;
  if (onWall) wallCorrN_ = ledge ? -A.ledge.inward : A.wall.normal;
  visP.addScaled(wallCorrN_, wallCorr_);
  charToWorld = Mat4::compose(visP, visQ, {1, 1, 1}); charToWorldInv_ = charToWorld.inverse();
  rig_->setObjectMatrix(charToWorld);
  rig_->updateWorld();
}

// ---------------------------------------------------------------- visual root (orientation + placement)
void Animator::updateFrame(float dt, const AnimState& A, Layer& top) {
  const Vec3& C = io_.center; float H = io_.H;
  const Vec3& vel = velS_;
  Vec3 fwd{std::sin(yaw_), 0, std::cos(yaw_)}, up = UP;
  float rate = 12, pivot = 0; bool wall = false;
  FrameInfo f; bool hasF = frame(top, A, f);
  std::string kind = hasF ? f.kind : traitOf(top.nameStr).frame;
  if (kind == "ledge") { fwd = f.fwd; yaw_ = std::atan2(fwd.x, fwd.z); rate = 30; pivot = 0; }
  else if (kind == "upright" || kind == "uprightWall") {
    if (kind == "uprightWall") { fwd = -A.wall.normal; fwd.y = 0; if (fwd.lengthSq() < 1e-4f) fwd = {std::sin(yaw_), 0, std::cos(yaw_)}; fwd.normalize(); yaw_ = std::atan2(fwd.x, fwd.z); }
    rate = 16; pivot = 0;
  } else if (kind == "air") {
    float hv = std::hypot(vel.x, vel.z);
    if (hv > 1) yaw_ = lerpAngle(yaw_, std::atan2(vel.x, vel.z), 1 - std::exp(-5 * dt));
    fwd = {std::sin(yaw_), 0, std::cos(yaw_)};
    rate = 7; pivot = 1;
  } else if (kind == "swing") {
    up = (A.swing.anchor - C).normalized();
    Vec3 v = vel; if (v.lengthSq() < 1) v = fwd;
    fwd = v - up * v.dot(up);
    if (fwd.lengthSq() < 1e-4f) fwd = {std::sin(yaw_), 0, std::cos(yaw_)};
    fwd.normalize();
    float yv = std::atan2(vel.x, vel.z); if (yv != 0) yaw_ = yv;
    rate = 10; pivot = 1;
  } else if (kind == "zip") {
    bool hasTgt = !(A.zip.dash || A.mode != "zip"); Vec3 tgt = A.zip.target;
    bool useV = f.useVel && vel.lengthSq() > 9;
    Vec3 dir = useV ? vel : hasTgt ? tgt - C : vel;
    if (f.aim && hasTgt) { if (dir.lengthSq() > 1.2f) zipDir_ = dir.normalized(); dir = zipDir_; }
    if (dir.lengthSq() > 1e-3f) {
      dir.normalize();
      float k = f.tilt;
      Vec3 dh{dir.x, 0, dir.z}; float hd = dh.length(); if (hd > 1e-3f) dh /= hd;
      up = (vlerp(UP, dir, k) - dh * f.back).normalized();
      if (up.y < -0.15f) { up.y = -0.15f; float hl = std::hypot(up.x, up.z); if (hl < 1e-6f) hl = 1; float r = std::sqrt(1 - 0.0225f) / hl; up.x *= r; up.z *= r; }
      float yawT = A.mode == "zip" ? A.facing : hd > 0.35f ? std::atan2(dir.x, dir.z) : yaw_;
      float yT = yawT;
      if (f.catchK > 0 && io_.trav) { const Vec3& pn = io_.trav->zip.normal; if (std::hypot(pn.x, pn.z) > 0.3f) yT = lerpAngle(yawT, std::atan2(pn.x, pn.z), smooth01(f.catchK)); }
      yaw_ = lerpAngle(yaw_, yT, 1 - std::exp(-(f.catchK > 0 ? 8.f : 6.f) * smooth01((hd - 0.35f) / 0.3f + f.catchK) * dt));
      if (A.sub == "wallZip") yaw_ = lerpAngle(yaw_, std::atan2(-A.wall.normal.x, -A.wall.normal.z), 1 - std::exp(-14 * dt));
      Vec3 Hh{std::sin(yaw_), 0, std::cos(yaw_)};
      float g = clampf(f.lay, 0, 1) * smooth01((hd - 0.15f) / 0.4f);
      fwd = Hh * (1 - g) + UP * -g;
      fwd -= up * fwd.dot(up);
      if (fwd.lengthSq() < 1e-3f) { fwd = Hh - up * Hh.dot(up); if (fwd.lengthSq() < 1e-4f) fwd = Vec3{0, -1, 0} - up * (-up.y); }
      fwd.normalize();
    }
    rate = f.useVel ? 12.f : 10.f; pivot = f.pivot;
  } else if (kind == "wall" || kind == "wallOut") {
    Vec3 n = A.mode == "wall" || true ? A.wall.normal : lastWallN_;
    lastWallN_ = n;
    fwd = -n;
    Vec3 wu = UP - n * UP.dot(n);
    if (wu.lengthSq() < 1e-3f) wu = Vec3{0, 0, 1} - n * n.z;
    wu.normalize();
    if (kind == "wall" && f.hasHeading) wallUp_.lerp(f.heading, 1 - std::exp(-6 * dt));
    else wallUp_.lerp(wu, 1 - std::exp(-4 * dt));
    wallUp_ -= n * wallUp_.dot(n); wallUp_.normalize();
    if (wallUp_.lengthSq() < 0.5f) wallUp_ = wu;
    up = wallUp_;
    rate = kind == "wallOut" ? 8.f : 12.f; pivot = 1; wall = kind == "wall";
    yaw_ = std::atan2(fwd.x, fwd.z);
  } else if (kind == "wallRun") {
    Vec3 n = A.wall.normal; lastWallN_ = n;
    up = n;
    if (A.sub == "wallRunSide") up = vlerp(up, UP, 0.42f).normalized();
    if (f.hasHeading) wrHead_.lerp(f.heading, 1 - std::exp(-8 * dt));
    else wrHead_.lerp(UP - n * UP.dot(n), 1 - std::exp(-4 * dt));
    wrHead_ -= n * wrHead_.dot(n);
    if (wrHead_.lengthSq() < 1e-4f) wrHead_ = UP - n * UP.dot(n);
    wrHead_.normalize();
    fwd = wrHead_;
    rate = 10; pivot = 0;
  }
  if (kind != frameKind_) { frameKind_ = kind; kindT_ = 0; } else kindT_ += dt;
  if (kindT_ < 0.45f && kind != "swing" && kind != "air") rate = lerpf(std::min(rate, 4.5f), rate, smooth01(kindT_ / 0.45f));
  Vec3 z = (fwd - up * fwd.dot(up)).normalized(), x = up.cross(z).normalized();
  Quat q = Quat::fromBasis(x, up, z);
  if (!frameInit_) { frameQ_ = q; frameInit_ = true; }
  else { if (frameQ_.dot(q) < 0) q = {-q.x, -q.y, -q.z, -q.w}; frameQ_ = Quat::slerp(frameQ_, q, 1 - std::exp(-rate * dt)); }
  pivotW_ = damp(pivotW_, pivot, 10, dt);
  // lean into turns + acceleration (upright / air only)
  float upright = kind == "upright" || kind == "air" ? 1.f : 0.f, hs = speedH_;
  float latA = hs * yawRate_;
  lean_.step(upright * smooth01((hs - 2.5f) / 3) * clampf(-std::atan(latA / 9.81f) * 0.6f, -0.2f, 0.2f), dt);
  float fa = accel_.x * std::sin(yaw_) + accel_.z * std::cos(yaw_);
  pitchLean_.step(upright * (kind == "upright" ? clampf(fa / 9.81f * 0.25f, -0.08f, 0.12f) : 0.f), dt);
  Quat R = frameQ_ * Quat::axisAngle(Z, lean_.x) * Quat::axisAngle(X, pitchLean_.x);
  Quat sp; bool hasSpin = spin(top, A, sp);
  if (top.key != spinS_.key) { spinS_.key = top.key; spinS_.from = spinS_.q; spinS_.t = top.data.noHandover ? 9 : 0; } else spinS_.t += dt;
  Quat tgt = hasSpin ? sp : Quat();
  spinS_.q = spinS_.t < 0.25f ? Quat::slerp(spinS_.from, tgt, smooth01(spinS_.t / 0.25f)) : tgt;
  if (spinS_.q.w < 0.999999f) R = R * spinS_.q;
  visQ = R;
  // placement: feet pivot vs centre pivot
  Vec3 Of = C + UP * (-H + A.stepOffset);
  Vec3 upB = R * UP;
  Vec3 Oc = C - upB * H;
  Vec3 O = vlerp(Of, Oc, pivotW_);
  wrW_ = damp(wrW_, kind == "wallRun" ? 1.f : 0.f, kind == "wallRun" ? 9.f : 6.f, dt);
  if (wrW_ > 1e-3f) {
    const Vec3& n = lastWallN_;
    if (kind == "wallRun") {
      float wd = A.wall.dist;
      if (finite(wd) && wd > 0.05f) wrOff_ = n * -(wd - 0.02f);
      else if (world_) { Hit h; if (world_->raycast(C, -n, 3.0f, h)) wrOff_ = h.point - C + n * 0.02f; else if (!wrOffOk_) wrOff_ = n * -0.44f; }
      wrOffOk_ = true;
    }
    float dn = wrOff_.dot(n);
    O.lerp(C + n * dn, wrW_);
  } else wrOffOk_ = false;
  float kT = 0;
  if (wall || kind == "wallOut") {
    float d0 = A.wall.dist;
    if (!finite(d0) && world_) { Hit h; if (world_->raycast(C, -lastWallN_, 2.5f, h)) d0 = h.distance; }
    if (finite(d0)) kT = clampf(WALL_Z - d0, -1.2f, 0.6f);
    if (!wallKInit_) { wallK_ = kT; wallKInit_ = true; }
  } else wallKInit_ = false;
  wallK_ = damp(wallK_, kT, 10, dt);
  if (std::fabs(wallK_) > 1e-4f) O.addScaled(lastWallN_, wallK_);
  if (kind == "ledge") O.lerp(f.O, f.w);
  visP = O;
  charToWorld = Mat4::compose(visP, visQ, {1, 1, 1});
  charToWorldInv_ = charToWorld.inverse();
}
