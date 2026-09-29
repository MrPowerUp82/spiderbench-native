// Animator nodes (makeNodes in animator.js) + procedural release tricks (trickPose / trickSpin).
#include "anim/animator_util.h"
#include "player/traversal/traversal.h"
#include <map>
#include <random>

using namespace anim;

namespace {
const std::map<std::string, float> PTRICK = {{"layout", 1.3f}, {"corkscrew", 0.78f}, {"tuckFlip", 0.9f}, {"scissor", 0.7f}};
const std::map<std::string, float> NAT = {{"wallCrawl", 1.4f}, {"wallCrawlFast", 2.6f}, {"wallRun", 8.0f}, {"wallRunHorizontal", 7.0f}};
float rnd01() { static std::mt19937 g(1234567); return std::uniform_real_distribution<float>(0, 1)(g); }
bool isUpright(const std::string& n) { return n == "ground" || n == "runStart" || n == "runStop" || n == "turn180" || n == "jumpCharge" || n == "land" || n == "perch" || n == "perchToStand" || n == "slingshot"; }
}  // namespace

// ================================================================== enter / hold
void Animator::enter(Layer& L, const AnimState& A, Layer* prev) {
  LayerData& D = L.data;
  switch (L.name) {
    case NodeK::Ground:
      if (prev && prev->name != NodeK::Ground && speedH_ > 1.5f) matchLocoPhase(P.out, speedH_);
      break;
    case NodeK::RunStart: D.clip = "runStart"; break;
    case NodeK::JumpCharge: D.clip = "jumpCrouch"; D.c = 0; break;
    case NodeK::JumpLaunch: {
      jumpLead_ = -jumpLead_;
      bool high = A.jumpCharge > 0.45f || A.velocity.y > 11.5f || (prev && prev->name == NodeK::JumpCharge && A.jumpCharge > 0.45f);
      D.clip = high && clips_.has("jumpLaunchHigh") ? "jumpLaunchHigh" : clips_.first({"jumpLaunchSmall", "jump"});
      bool run = speedH_ > 2.5f;
      D.t0 = prev && prev->name == NodeK::JumpCharge ? (D.clip == "jumpLaunchHigh" ? 0.1f : 0.06f) : run ? 0.12f : A.grounded ? 0.02f : 0.1f;
      break;
    }
    case NodeK::Air: {
      D.relClip.clear();
      bool fromSwing = prev && (prev->name == NodeK::Swing || prev->name == NodeK::Zip);
      D.hop = prev && prev->name == NodeK::JumpLaunch && prev->data.clip != "jumpLaunchHigh";
      if (fromSwing || A.sub == "release") { if (clips_.has("releaseSpread")) D.relClip = "releaseSpread"; }
      D.dive = 0;
      break;
    }
    case NodeK::Trick: {
      std::string tr = A.trick.empty() ? "layout" : A.trick;
      auto it = PTRICK.find(tr);
      if (it != PTRICK.end()) {
        D.proc = true; D.tr = tr; D.dur = it->second; D.trSide = A.trickSide ? A.trickSide : 1; D.clip = "trick:" + tr; D.sprInit = false;
        D.air = std::make_unique<Layer>(); D.air->name = NodeK::Air; D.air->nameStr = "air";
        if (tr == "layout" && prev && prev->name == NodeK::Swing) { // seamless out of the web: the release pitch moves into the spin
          float hv = std::hypot(A.velocity.x, A.velocity.z);
          float yaw = hv > 1 ? std::atan2(A.velocity.x, A.velocity.z) : yaw_;
          Quat qy = Quat::axisAngle(Y, yaw);
          Quat d = qy.conj() * frameQ_ * spinS_.q;
          Vec3 up = d * UP;
          D.phi0 = std::atan2(up.z, up.y); D.phiEnd = D.trSide * TAU;
          D.resid = Quat::axisAngle(X, -D.phi0) * d;
          frameQ_ = qy; yaw_ = yaw; D.seam = true; D.noHandover = true;
          L.dur = std::min(L.dur, 0.12f);
        }
      } else D.clip = clips_.first({"releaseFlip", "airTrick"});
      break;
    }
    case NodeK::Land: {
      const std::string& sub = A.sub; float sev = A.landing.severity;
      std::string clip = sub == "landRoll" ? "landRoll" : sub == "landHard" ? "landHard" : sub == "landMedium" ? "landMedium" : sub == "landLight" ? "landLight" : sev > 0.75f ? "landHard" : sev > 0.35f ? "landMedium" : "landLight";
      if (!clips_.has(clip)) clip = clips_.first({"land", "landMedium"});
      D.clip = clip; D.t0 = clips_.contactTime(clip);
      D.minHold = clip == "landLight" ? 0.1f : clip == "landMedium" ? 0.22f : clip == "landHard" ? 0.55f : clip == "landRoll" ? clips_.dur("landRoll") - D.t0 - 0.15f : 0.3f;
      break;
    }
    case NodeK::Perch:
      D.landed = !(prev && (prev->name == NodeK::Zip || prev->name == NodeK::Air || prev->name == NodeK::Trick || prev->name == NodeK::PointLaunch || prev->name == NodeK::JumpLaunch));
      D.t0 = prev && prev->name == NodeK::Zip && clips_.has("zipCatchLevel") ? 0.233f : clips_.contactTime("perchLand");
      break;
    case NodeK::PerchToStand: D.clip = "perchToStand"; break;
    case NodeK::Zip:
      D.fg = prev && isUpright(prev->nameStr) && std::fabs(A.velocity.y) < 1;
      D.dash = A.zip.dash || A.mode != "zip";
      D.clip = D.dash ? clips_.first({"webZipPull", "webShoot"}) : "zip:fire";
      D.travelT = NAN;
      break;
    case NodeK::PointLaunch: D.clip = "pointLaunch"; D.t0 = A.grounded ? 0 : 0.1f; break;
    case NodeK::Slingshot:
      D.clip = "slingshot"; D.k = 0; D.ph = 0; D.in = 0; D.mv = 0; D.rel = 0; D.grip = 0;
      D.aim[0] = Vec3{0.6f, 0.5f, 1}.normalized(); D.aim[1] = Vec3{-0.6f, 0.5f, 1}.normalized();
      D.hold[0] = Vec3{0.5f, 0.2f, 1}.normalized(); D.hold[1] = Vec3{-0.5f, 0.2f, 1}.normalized();
      break;
    case NodeK::Swing: {
      size_t h = L.key.find('#'); D.hand = h != std::string::npos && h + 1 < L.key.size() ? L.key[h + 1] : A.swing.hand;
      D.ph = clampf(A.swing.phase, -1, 1); D.angPrev = NAN; D.fwd = 1;
      break;
    }
    case NodeK::WallRun:
      D.side = A.sub == "wallRunSide";
      if (prev && prev->name != NodeK::Ground && prev->name != NodeK::WallRun) matchLocoPhase(P.out, 8);
      break;
    case NodeK::WallJump: D.clip = clips_.first({"wallJump"}) ? "wallJump" : ""; break;
    case NodeK::Ledge: {
      D.climb = A.ledge.variant == "flip" && clips_.has("ledgeClimbFlip") ? "ledgeClimbFlip" : clips_.first({"ledgeClimbQuick", "wallToRoofVault"});
      D.grab = clips_.dur("ledgeGrab");
      Vec3 f = A.ledge.active ? Vec3{A.ledge.inward.x, 0, A.ledge.inward.z}.normalized() : Vec3{std::sin(yaw_), 0, std::cos(yaw_)};
      Vec3 O = A.ledge.active ? A.ledge.point - f * WALL_Z : io_.center;
      if (A.ledge.active) O.y -= 1.95f;
      D.O = O; D.f = f; D.tl = 0;
      break;
    }
    case NodeK::Corner: D.clip = "cornerWrap"; break;
    default: break;
  }
}

bool Animator::hold(Layer& L, const AnimState& A) {
  const LayerData& D = L.data;
  switch (L.name) {
    case NodeK::RunStart: return A.mode == "ground" && speedH_ > 0.2f && (L.t < 0.2f || speedH_ > 2.5f) && L.t < clips_.dur("runStart") / 1.1f - 0.12f;
    case NodeK::JumpLaunch: return (A.mode == "air" || A.mode == "ground") && L.t + D.t0 < clips_.dur(D.clip) - 0.1f && A.velocity.y > -3 && A.trick.empty();
    case NodeK::Trick: return A.mode == "air" && L.t < (D.proc ? D.dur - 0.03f : clips_.dur(D.clip) - 0.12f);
    case NodeK::Land: {
      if (!(A.mode == "land" || A.mode == "ground")) return false;
      if (A.sub == "jumpCharge" || A.sub == "jumpLaunch") return L.t < 0.08f;
      float rem = clips_.dur(D.clip) - D.t0 - L.t;
      if (speedH_ > 2) return L.t < D.minHold;
      return rem > 0.25f;
    }
    case NodeK::PerchToStand: return A.mode == "ground" && speedH_ < 2.5f && L.t < clips_.dur("perchToStand") - 0.2f;
    case NodeK::PointLaunch: return (A.mode == "air" || A.mode == "zip") && L.t + D.t0 < clips_.dur("pointLaunch") - 0.15f;
    case NodeK::WallJump: return (A.mode == "air" || A.mode == "wall") && L.t < clips_.dur("wallJump") - 0.15f && A.trick.empty();
    case NodeK::Ledge: return A.mode != "swing" && A.mode != "zip" && (A.ledge.active || D.tl < D.grab + clips_.dur(D.climb) - 0.08f);
    case NodeK::Corner: return A.mode == "wall" && L.t < clips_.dur("cornerWrap") - 0.1f;
    default: return false;
  }
}

// ================================================================== eval
void Animator::eval(Layer& L, const AnimState& A, Pose& out) {
  LayerData& D = L.data;
  switch (L.name) {
    case NodeK::Frozen: return;
    case NodeK::Ground: case NodeK::RunStop: case NodeK::Turn180: evalGround(L, A, out); return;
    case NodeK::RunStart: oneShot("runStart", L.t * 1.1f, out); return;
    case NodeK::JumpCharge: {
      D.c = damp(D.c, clampf(A.jumpCharge, 0, 1), 12, dt_);
      float c = D.c, v = speedH_;
      evalGround(L, A, P.c);
      if (!clips_.sample("jumpCrouch", 0.1f + L.t * 0.8f, P.a)) fallback(P.a);
      float moving = smooth01((v - 1) / 3);
      float wC = lerpf(0.35f + 0.6f * smooth01(c), 0.22f + 0.45f * smooth01(c), moving) * smooth01(L.t / 0.12f + 0.3f);
      blendPoses(P.c, P.a, wC, out);
      D.clip = std::string("jumpCrouch+") + (v > 1 ? "loco" : "idle");
      return;
    }
    case NodeK::JumpLaunch: {
      if (!oneShot(D.clip, L.t + D.t0, out)) { fallback(out); return; }
      if (D.clip != "jumpLaunchHigh" && clips_.has("jumpCrouch")) { clips_.sample("jumpCrouch", 0.3f, P.c); blendPoses(out, P.c, 0.6f, out, &maskArms()); }
      if (D.clip == "jumpLaunchHigh" && clips_.has("airRise")) { clips_.sample("airRise", L.t, P.c); blendPoses(out, P.c, 0.6f * smooth01((L.t + D.t0 - 0.04f) / 0.14f), out, &maskArms()); }
      jumpArms(out, smooth01((L.t + D.t0 - 0.02f) / 0.12f) * 0.9f, 1, 0);
      jumpTuck(out, smooth01((L.t + D.t0 - 0.1f) / 0.18f) * 0.8f);
      return;
    }
    case NodeK::Air: evalAir(L, A, out); return;
    case NodeK::Trick: {
      if (D.proc) {
        D.air->t = L.t; evalAir(*D.air, A, out);
        float u = clampf(L.t / D.dur, 0, 1);
        bool lay = D.tr == "layout";
        trickPose(out, D, u, (D.seam ? 1.f : smooth01(L.t / 0.15f)) * (1 - smooth01((u - (lay ? 0.84f : 0.76f)) / (lay ? 0.16f : 0.22f))));
        return;
      }
      if (!oneShot(D.clip, L.t, out)) fallback(out);
      return;
    }
    case NodeK::Land:
      if (!oneShot(D.clip, D.t0 + L.t, out)) fallback(out);
      if (D.clip == "landHard" && landHardNeedsFix()) threePoint(out);
      groundContact(out, 1);
      return;
    case NodeK::Perch: evalPerch(L, A, out); return;
    case NodeK::PerchToStand: if (!oneShot("perchToStand", L.t, out)) fallback(out); return;
    case NodeK::Zip: {
      if (D.dash) {
        float d = clips_.dur(D.clip);
        float u = clampf(A.zip.t, 0, 1) * d;
        if (!oneShot(D.clip, std::max(u, std::min(L.t, 0.25f)), out)) fallback(out);
        return;
      }
      zipPhases(L, A);
      float gT = D.fg && (A.sub == "zipFire" || A.sub == "zipYank") ? 1.f : 0.f;
      D.gw = !finite(D.gw) ? gT : damp(D.gw, gT, gT ? 20.f : 14.f, dt_);
      zipPose(out, L, A);
      return;
    }
    case NodeK::PointLaunch: if (!oneShot("pointLaunch", L.t + D.t0, out)) fallback(out); return;
    case NodeK::Slingshot: evalSlingshot(L, A, out); return;
    case NodeK::Swing: evalSwing(L, A, out); return;
    case NodeK::Crawl: evalCrawl(L, A, out); return;
    case NodeK::WallRun: evalWallRun(L, A, out); return;
    case NodeK::WallJump: if (D.clip.empty() || !oneShot(D.clip, L.t, out)) fallback(out); return;
    case NodeK::Vault: fallback(out); return;
    case NodeK::Ledge: {
      D.tl = A.ledge.active ? std::max(D.tl, A.ledge.t) : D.tl + dt_;
      float t = D.tl;
      if (!clips_.has("ledgeGrab")) { oneShot(D.climb, t, out, 2); return; }
      if (t < D.grab) { oneShot("ledgeGrab", t, out, 0); D.clip = "ledgeGrab"; }
      else {
        oneShot(D.climb, t - D.grab, out, 0); D.clip = D.climb;
        float bw = 1 - smooth01((t - D.grab) / 0.06f);
        if (bw > 0.001f) { oneShot("ledgeGrab", D.grab, P.c, 0); blendPoses(out, P.c, bw, out); }
      }
      return;
    }
    case NodeK::Corner: if (!oneShot("cornerWrap", L.t, out)) fallback(out); return;
  }
}

bool Animator::frame(Layer& L, const AnimState& A, FrameInfo& f) {
  LayerData& D = L.data;
  switch (L.name) {
    case NodeK::Zip: {
      f.kind = "zip";
      if (D.dash) { f.tilt = lerpf(0.15f, 0.6f, smooth01(L.t / 0.25f)); return true; }
      ZipPh Z = D.hasZph ? D.zph : ZipPh{};
      float gw = finite(D.gw) ? D.gw : 0;
      f.tilt = lerpf(lerpf(lerpf(0.12f, 0.3f, Z.y), clips_.has("zipFlight") ? 1.f : 0.92f, Z.g), 0.f, Z.c) * (1 - 0.75f * gw);
      f.back = 0.4f * Z.c; f.useVel = false; f.aim = Z.g > 0.05f; f.lay = Z.g * (1 - Z.c); f.catchK = Z.c; f.pivot = 1 - gw;
      return true;
    }
    case NodeK::Crawl: {
      f.kind = "wall";
      const Vec3& n = A.wall.normal; const Vec3& v = A.velocity;
      if (v.lengthSq() > 0.09f) { Vec3 h = v - n * v.dot(n); if (h.lengthSq() > 0.01f) { f.heading = h.normalized(); f.hasHeading = true; } }
      return true;
    }
    case NodeK::WallRun: {
      const Vec3& n = A.wall.normal; const Vec3& v = A.velocity;
      Vec3 h = v - n * v.dot(n); if (h.lengthSq() > 0.25f) { f.heading = h.normalized(); f.hasHeading = true; }
      f.kind = "wallRun";
      return true;
    }
    case NodeK::WallJump: f.kind = "wallOut"; return true;
    case NodeK::Ledge: f.kind = "ledge"; f.O = D.O; f.fwd = D.f; f.w = smooth01(L.t / 0.1f); return true;
    default: return false;
  }
}

bool Animator::spin(Layer& L, const AnimState& A, Quat& out) {
  LayerData& D = L.data;
  if (L.name == NodeK::Trick && D.proc) return trickSpin(D, clampf(L.t / D.dur, 0, 1), L.t, out);
  if (L.name == NodeK::Swing) {
    float bank = clampf(A.swing.bank, -1, 1);
    D.roll = damp(D.roll, -bank * 0.45f, 5, dt_);
    if (std::fabs(D.roll) > 1e-3f) { out = Quat::axisAngle(Z, D.roll); return true; }
  }
  return false;
}

// ---- idle + locomotion (walk / jog / run / sprint)
void Animator::evalGround(Layer& L, const AnimState& A, Pose& out) {
  LayerData& D = L.data; float vr = speedH_;
  D.vp = !finite(D.vp) || vr >= D.vp ? vr : damp(D.vp, vr, 9, dt_);
  // walk stop = one short finishing step onto the passing pose
  ClipLib::LocoMeta wm = locoMeta("walk");
  bool slowing = vr < (finite(D.vrPrev) ? D.vrPrev : vr) - 1e-4f || vr < 0.02f;
  D.vrPrev = vr;
  if (wm.ok && !D.stopOn && slowing && vr < 1.75f && vr > 0.05f && D.vp < 2.2f && decel_ < 9 && A.mode != "combat" && locoInfo_.clipA == "walk") {
    float dP = std::fmod(std::fmod(wm.pass - locoPhase_, 0.5f) + 0.5f, 0.5f);
    if (dP < 0.2f) dP += 0.5f;
    D.stopOn = true; D.stopRem = dP; D.stopDone = false;
  }
  bool hasOvr = false; float ovrK = 1, ovrRate = 1, ovrFc = 1;
  if (D.stopOn) {
    if (!slowing) D.stopOn = false;
    else if (!D.stopDone) {
      float aEst = std::max(1.5f, decel_), Drem = std::max(vr * vr / (2 * aEst), 0.02f);
      float fc = clampf(D.stopRem * std::max(vr, 0.25f) / Drem, 0.35f, 2.4f);
      ovrK = clampf(std::max(vr, 0.f) / (wm.v * wm.dur * fc), 0.12f, 1.3f); ovrRate = fc; ovrFc = fc; hasOvr = true;
      D.vp = std::max(D.vp, 1.3f);
    }
  }
  float v = D.vp;
  D.cw = damp(D.cw, A.mode == "combat" && clips_.has("fightIdle") ? 1.f : 0.f, 6, dt_);
  const char* idleName = clips_.first({"idle"});
  float wl = smooth01((v - 0.15f) / 1.1f);
  if (wl < 0.999f) { if (!idleName || !clips_.sample(idleName, idleT_, P.idle)) fallback(P.idle); }
  else idleT_ = 0;
  if (wl > 0.001f && D.wlPrev <= 0.001f && wm.ok && vr >= v - 1e-3f && !D.stopOn) locoPhase_ = wm.pass;
  D.wlPrev = wl;
  if (wl <= 0.001f) D.stopOn = false;
  if (wl > 0.001f) {
    locoPose(P.loco, v, hasOvr ? &ovrK : nullptr, hasOvr ? &ovrRate : nullptr);
    walkForm(P.loco, (1 - smooth01((v - 1.9f) / 1.8f)) * smooth01((v - 0.05f) / 0.6f + (hasOvr ? 1 : 0)));
    armPump(P.loco, v, smooth01((v - 0.4f) / 1.2f));
    runTrack(P.loco, v);
    if (hasOvr) { float adv = std::min(dt_ * ovrFc, D.stopRem); D.stopRem -= adv; locoPhase_ = std::fmod(locoPhase_ + adv, 1.f); if (D.stopRem <= 1e-3f) D.stopDone = true; }
    else if (!(D.stopOn && D.stopDone)) locoPhase_ = std::fmod(locoPhase_ + dt_ * locoInfo_.rate * clampf(vr / std::max(v, 0.1f), 0, 1), 1.f);
    char buf[96]; std::snprintf(buf, sizeof buf, "%s>%s@%.2f r%.2f k%.2f", locoInfo_.clipA.c_str(), locoInfo_.clipB.c_str(), locoInfo_.w, locoInfo_.rate, locoInfo_.k); D.clip = buf;
    if (wl < 0.999f) blendPoses(P.idle, P.loco, wl, out); else out.copy(P.loco);
  } else { out.copy(P.idle); D.clip = idleName ? idleName : "rest"; }
  if (wl < 0.999f) widenStance(out, 1 - wl);
}

// ---- air: rise / apex / fall / dive blend space + release one-shot + jump arms / tuck + landing reach
void Animator::evalAir(Layer& L, const AnimState& A, Pose& out) {
  LayerData& D = L.data;
  if (A.sub == "flight") { if (clips_.has("zipFlight")) clips_.sample("zipFlight", 0.18f, out); else fallback(out); D.clip = "zipFlight"; return; }
  float vy = A.velocity.y, t = airT_;
  float wr = smooth01((vy - 1) / 5), wf = smooth01((-vy - 1) / 6);
  bool diving = A.sub == "dive" || A.dive || A.glide || (vy < -17 && t > 0.9f);
  D.dive = damp(D.dive, diving ? 1.f : 0.f, 2.5f, dt_);
  const char* rise = clips_.first({"airRise", "jump"}); const char* apex = clips_.first({"airApex", "airRise"});
  const char* fall = clips_.first({"fallCalm", "airApex", "fall"}); const char* dive = clips_.first({"fallFast", "fall"});
  if (!(rise && apex && fall)) { fallback(out); return; }
  clips_.sample(apex, t, P.a);
  if (wr > 0.001f) { clips_.sample(rise, t, P.b); blendPoses(P.a, P.b, wr, P.a); }
  if (wf > 0.001f) { clips_.sample(fall, t, P.b); blendPoses(P.a, P.b, wf, P.a); }
  if (D.dive > 0.001f && dive) { clips_.sample(dive, t, P.b); blendPoses(P.a, P.b, smooth01(D.dive), P.a); }
  D.clip = wr > 0.5f ? rise : wf > 0.5f ? (D.dive > 0.5f ? dive : fall) : apex;
  if (!D.relClip.empty()) {
    float d = clips_.dur(D.relClip), wRel = 1 - smooth01((L.t - (d - 0.35f)) / 0.35f);
    if (wRel > 0.001f) { oneShot(D.relClip, L.t, P.b); blendPoses(P.a, P.b, wRel, P.a); D.clip = D.relClip; }
    else D.relClip.clear();
  }
  out.copy(P.a);
  { float dk = smooth01(D.dive) * smooth01((-vy - 10) / 22) * (A.glide ? 0.f : 1.f);
    if (dk > 0.001f) b_.begin(out).rot("hips", X, 1.2f * dk); }
  if (D.hop && clips_.has("jumpCrouch")) { clips_.sample("jumpCrouch", 0.3f, P.c); blendPoses(out, P.c, 0.5f, out, &maskArms()); }
  jumpArms(out, (1 - smooth01(D.dive)) * 0.85f, wr, wf);
  jumpTuck(out, (1 - smooth01(D.dive)) * (D.relClip.empty() ? 1.f : 0.f) * (1 - smooth01((-vy - 3) / 6)) * 0.8f);
  groundReach(out, A);
}

// ---- perch: land -> idle (deep squat), rounded back, feet seated on the point
void Animator::evalPerch(Layer& L, const AnimState& A, Pose& out) {
  LayerData& D = L.data;
  const char* idle = clips_.first({"perchIdle", "wallPerch"});
  if (!idle || !clips_.sample(idle, L.t, P.a)) fallback(P.a);
  D.clip = idle ? idle : "";
  float land = 0;
  if (!D.landed && clips_.has("perchLand")) {
    float d = clips_.dur("perchLand") - D.t0, w = 1 - smooth01((L.t - (d - 0.3f)) / 0.3f);
    if (w > 0.001f) { oneShot("perchLand", D.t0 + L.t, P.b); blendPoses(P.a, P.b, w, P.a); D.clip = "perchLand"; }
    land = 1;
  }
  out.copy(P.a);
  perchSquat(out, L, land);
  { PoseBuilder& bb = b_.begin(out);
    bb.rot("hips", X, -0.4f); bb.rot("spine", X, 0.2f); bb.rot("spine1", X, 0.14f); bb.rot("chest", X, 0.08f); bb.rot("neck", X, -0.02f); }
  perchSeat(out, L, A);
}

// ---- web slingshot: hold the strands, shoot per click, lean back with tension, forward snap on release
void Animator::evalSlingshot(Layer& L, const AnimState& A, Pose& out) {
  LayerData& D = L.data; float dt = dt_;
  const auto& anc = A.sling.anchors;
  D.k = damp(D.k, clampf(A.sling.tension, 0, 1), 9, dt);
  D.in = damp(D.in, 1, 7, dt);
  D.mv = damp(D.mv, clampf(std::fabs(A.sling.moving), 0, 1), 8, dt);
  D.rel = damp(D.rel, A.sling.release >= 0 ? 1.f : 0.f, A.sling.release >= 0 ? 30.f : 6.f, dt);
  D.grip = damp(D.grip, !anc.empty() ? 1.f : 0.35f, 8, dt);
  float k = D.k * (1 - D.rel), mv = D.mv, rel = D.rel, e = smooth01(D.in);
  D.ph = std::fmod(D.ph + dt * (1.1f + 0.9f * (1 - D.k)) * mv, 1.f);
  for (char S : {'L', 'R'}) {
    int s = si(S), side = S == 'L' ? -1 : 1; const AnimState::SlingAnchor* young = nullptr; int n = 0; Vec3 m;
    for (const auto& a : anc) if (a.side == side) { n++; if (!young || a.t < young->t) young = &a; m += (worldToChar(a.p) - Vec3{0, 1.35f, 0}).normalized(); }
    if (young) { Vec3 d = worldToChar(young->p) - Vec3{0, 1.4f, 0}; d.z = std::max(d.z, 0.15f); D.aim[s] = d.normalized(); }
    if (n && m.lengthSq() > 1e-4f) { m.normalize(); m.y = clampf(m.y, -0.1f, 0.35f); m.z = std::max(m.z, 0.5f); D.hold[s].lerp(m.normalized(), 1 - std::exp(-6 * dt)); D.hold[s].normalize(); }
    float tgt = young && young->t < 0.15f ? 1.f : 0.f, w = 30, z = 0.55f;
    D.gv[s] += (w * w * (tgt - D.g[s]) - 2 * z * w * D.gv[s]) * dt; D.g[s] += D.gv[s] * dt;
  }
  const char* idle = clips_.first({"idle"}); if (!idle || !clips_.sample(idle, idleT_, out)) fallback(out);
  PoseBuilder& b = b_.begin(out);
  b.moveHips(0, e * (-0.03f - 0.15f * k) + rel * 0.05f, e * (-0.02f - 0.1f * k) + rel * 0.1f);
  b.rot("hips", X, e * (-0.06f - 0.32f * k) + rel * 0.32f);
  b.rot("spine", X, e * (0.03f + 0.04f * k) + rel * 0.1f); if (b.i("spine1") >= 0) b.rot("spine1", X, e * 0.03f + rel * 0.06f);
  b.rot("chest", X, e * 0.02f * k);
  b.rot("neck", X, e * (0.08f + 0.18f * k) - rel * 0.12f); b.rot("head", X, e * (0.05f + 0.12f * k) - rel * 0.1f);
  for (char S : {'L', 'R'}) {
    float sx = sxOf(S); const Vec3& th = rd_.thigh[si(S)];
    float cyc = (D.ph + (S == 'L' ? 0 : 0.5f)) * 2 * PI;
    float z = e * ((S == 'L' ? 0.2f : 0.02f) + 0.18f * k) + 0.08f * mv * std::cos(cyc) - rel * (S == 'L' ? 0.1f : 0.2f);
    float lift = 0.06f * mv * std::max(0.f, std::sin(cyc));
    Quat fq = b.cq(K("foot", S));
    b.ik("leg", S, {sx * (0.15f + 0.04f * k), rd_.ankleH + lift + 0.03f * k, z}, {sx * 0.25f, th.y - 0.1f, 0.9f}, e, true);
    b.setCQ(K("foot", S), fq); b.fromBind(K("foot", S), Quat(), e);
    b.rot(K("foot", S), X, e * (-0.4f * k - 0.25f * mv * std::max(0.f, std::sin(cyc))) * (1 - rel) + rel * 0.35f);
  }
  Vec3 ch = b.pos("chest"); float reach = rd_.a1 + rd_.a2;
  for (char S : {'L', 'R'}) {
    int s = si(S); float sx = sxOf(S), g = clampf(D.g[s], -0.2f, 1.25f);
    Vec3 sh = b.pos(K("upperArm", S));
    Vec3 holdP = Vec3{sx * 0.17f, ch.y + 0.05f, ch.z} + D.hold[s] * (reach * (0.55f + 0.25f * k));
    Vec3 shoot = sh + D.aim[s] * (reach * 0.97f);
    Vec3 pull{sx * 0.18f, ch.y - 0.05f, ch.z + 0.2f};
    Vec3 hand = vlerp(vlerp(holdP, shoot, clampf(g, 0, 1.1f)), pull, rel);
    Vec3 el = vlerp({sx * 0.45f, sh.y - 0.3f, sh.z - 0.15f}, {sx * 0.3f, sh.y - 0.25f, sh.z - 0.3f}, rel);
    float w = e * std::max(D.grip, clampf(g, 0, 1));
    b.ik("arm", S, hand, el, w, true);
    if (g > 0.02f) { Vec3 ax = Y.cross(D.aim[s]); if (ax.lengthSq() > 1e-4f) { b.aim(K("hand", S), D.aim[s], clampf(g, 0, 1)); b.rot(K("hand", S), ax.normalized(), -0.75f * clampf(g, 0, 1.2f)); } }
    rd_.curl(out, S, lerpf(D.grip * e, 0.25f, clampf(g, 0, 1))); b.dirty = true;
  }
}

// ---- swing: low / bottom / high by the travel-relative phase, corner bank, slack tuck, wall kick
void Animator::evalSwing(Layer& L, const AnimState& A, Pose& out) {
  LayerData& D = L.data; const auto& sw = A.swing;
  float ang = sw.angle;
  if (finite(D.angPrev) && dt_ > 0) {
    float rate = (ang - D.angPrev) / dt_;
    if (std::fabs(rate) < 6) { if (rate > 0.12f) D.fwd = 1; else if (rate < -0.12f) D.fwd = -1; }
  }
  D.angPrev = ang; if (!D.fwd) D.fwd = 1;
  float phT = clampf(sw.phase, -1, 1) * D.fwd;
  D.ph = damp(D.ph, phT, 8, dt_);
  float ph = D.ph, bank = clampf(sw.bank, -1, 1), t = L.t;
  if (D.hand == sw.hand) { swingPh_ = ph; swingPhHand_ = D.hand; }
  bool nat = D.hand == 'L' && clips_.has("swingLowL") && clips_.has("swingBottomL") && clips_.has("swingHighL");
  D.nat = nat;
  std::string sfx = nat ? "L" : "";
  std::string lo = "swingLow" + sfx, bo = "swingBottom" + sfx, hi = "swingHigh" + sfx;
  if (clips_.has(lo) && clips_.has(bo) && clips_.has(hi)) {
    float wl = smooth01(-ph), wh = smooth01(ph);
    clips_.sample(bo, t, P.a);
    if (wl > 0.001f) { clips_.sample(lo, t, P.b); blendPoses(P.a, P.b, wl, P.a); }
    if (wh > 0.001f) { clips_.sample(hi, t, P.b); blendPoses(P.a, P.b, wh, P.a); }
    D.clip = ph < -0.35f ? lo : ph > 0.35f ? hi : bo;
  } else if (clips_.has("swing")) { float f = ph < 0 ? 18 * (ph + 1) : 18 + 14 * ph; clips_.sample("swing", f / 30, P.a, false); D.clip = "swing"; D.nat = false; }
  else fallback(P.a);
  float two = clips_.has("swingTwoHanded") ? smooth01(two_.get(other(D.hand))) * 0.5f : 0;
  if (two > 0.01f) { clips_.sample("swingTwoHanded", t, P.b); if (D.nat) mirror(P.b); blendPoses(P.a, P.b, two, P.a); }
  if (D.nat && clips_.has("swingCornerBankL") && std::fabs(bank) > 0.02f) {
    clips_.sample("swingCornerBankL", t, P.b); if (bank < 0) mirror(P.b);
    blendPoses(P.a, P.b, smooth01(std::fabs(bank)) * 0.85f, P.a);
  } else if (clips_.has("swingCornerBank") && std::fabs(bank) > 0.02f) {
    clips_.sample("swingCornerBank", t, P.b);
    float bk = D.hand == 'L' ? -bank : bank;
    if (bk * mirrorBank_ < 0) mirror(P.b);
    blendPoses(P.a, P.b, smooth01(std::fabs(bank)) * 0.85f, P.a);
  }
  float slack = smooth01(sw.slack), kick = smooth01(sw.kick);
  if (slack > 0.01f) { const char* f = clips_.first({"releaseTuck", "airApex"}); if (f) { clips_.sample(f, std::string(f) == "releaseTuck" ? 0.3f : L.t, P.b, false); blendPoses(P.a, P.b, slack * 0.6f, P.a); } }
  if (kick > 0.01f && clips_.has("wallJump")) { clips_.sample("wallJump", 0.14f + 0.1f * (1 - sw.kick), P.b); blendPoses(P.a, P.b, kick * 0.7f, P.a); }
  out.copy(P.a);
  if (D.hand == 'L' && !D.nat) mirror(out);
}

// ---- wall crawl (idle / slow / fast) -> procedural climb; settles into the side-on cling when he stops
void Animator::evalCrawl(Layer& L, const AnimState& A, Pose& out) {
  LayerData& D = L.data; const auto& wv = A.wall;
  float mvIn = std::hypot(wv.move.x, wv.move.y);
  float sp = A.speed;
  bool moving = mvIn > 0.1f || sp > 0.3f, fast = wv.fast;
  D.mv = damp(D.mv, moving ? (fast ? 2.f : 1.f) : 0.f, 6, dt_);
  float m = D.mv;
  const char* slow = clips_.first({"wallCrawl"}); const char* quick = clips_.first({"wallCrawlFast", "wallCrawl"}); const char* idle = clips_.first({"wallIdle", "wallCrawl"});
  if (!slow) { fallback(out); return; }
  ClipLib::LocoMeta ms = clips_.loco(slow, true), mf = clips_.loco(quick, true);
  if (NAT.count(slow)) ms.v = NAT.at(slow); if (NAT.count(quick)) mf.v = NAT.at(quick);
  float wFast = std::max(smooth01(m - 1), smooth01((sp - 1.2f) / 1.2f));
  float vN = lerpf(ms.v, mf.v, wFast), fN = lerpf(1 / ms.dur, 1 / mf.dur, wFast);
  float ratio = std::max(sp, 0.2f) / std::max(vN, 0.2f);
  float k = clampf(std::pow(ratio, 0.4f), 0.8f, 1.45f), rate = clampf(ratio / k, 0.5f, 2.6f);
  D.k = moving ? k : 1;
  if (moving) wallPhase_ = std::fmod(wallPhase_ + dt_ * fN * rate, 1.f);
  clips_.sample(slow, std::fmod(wallPhase_ + ms.phase0, 1.f) * ms.dur, P.a);
  if (wFast > 0.001f) { clips_.sample(quick, std::fmod(wallPhase_ + mf.phase0, 1.f) * mf.dur, P.b); blendPoses(P.a, P.b, wFast, P.a); }
  float wIdle = 1 - smooth01(m);
  if (wIdle > 0.001f) { clips_.sample(idle, L.t, P.b); blendPoses(P.a, P.b, wIdle, P.a); }
  D.clip = wIdle > 0.5f ? idle : wFast > 0.5f ? quick : slow;
  out.copy(P.a);
  climbPose(out, dt_, sp, moving);
  D.cw = damp(D.cw, moving ? 0.f : 1.f, moving ? 10.f : 5.f, dt_);
  if (D.cw < 0.04f || !D.cling) D.cling = pickClingSide(A, D.cling);
  clingPose(out, smooth01(D.cw), D.cling);
}

// ---- wall run = the ground run cycle rotated onto the wall (frame 'wallRun', up = wall normal)
void Animator::evalWallRun(Layer& L, const AnimState& A, Pose& out) {
  LayerData& D = L.data;
  float sp = clampf(A.speed, 4, 14);
  D.side = A.sub == "wallRunSide"; D.useClip = false;
  float ap = std::min(sp, 9.8f);
  locoPose(out, ap);
  armPump(out, ap, 1);
  runTrack(out, ap);
  locoPhase_ = std::fmod(locoPhase_ + dt_ * locoInfo_.rate, 1.f);
  if (!D.side) { // vertical: leaning ~50 deg up the wall, the stride mapped under the hips so each foot strikes the facade
    PoseBuilder& bb = b_.begin(out);
    const float PITCH = 0.87f;
    Vec3 fp[2]; Quat fq[2];
    for (char S : {'L', 'R'}) { fp[si(S)] = bb.pos(K("foot", S)); fq[si(S)] = bb.cq(K("foot", S)); }
    bb.rot("hips", X, PITCH);
    bb.rot("chest", X, -0.12f); bb.rot("neck", X, -0.25f); bb.rot("head", X, -0.45f);
    float hy = bb.pos("hips").y;
    bb.moveHips(0, 0.68f - hy, 0);
    for (char S : {'L', 'R'}) {
      Vec3 f = fp[si(S)]; f.z = f.z * 0.6f - 0.36f; f.y = 0.1f + std::max(0.f, f.y - 0.1f) * 0.55f;
      Vec3 hip = bb.pos(K("upperLeg", S));
      bb.ik("leg", S, f, vlerp(hip, f, 0.5f) + Vec3{0, 0.1f, 0.6f}, 1, true);
      bb.setCQ(K("foot", S), fq[si(S)]);
    }
    solesAbove(out, 0);
    D.clip = "run@wall(vertical)";
    return;
  }
  solesAbove(out, 0);
  D.clip = "run@wall " + locoInfo_.clipA + ">" + locoInfo_.clipB;
}

// ================================================================== procedural release tricks
bool Animator::trickSpin(LayerData& D, float u, float t, Quat& out) {
  float sd = (float)D.trSide;
  if (D.tr == "layout") {
    if (D.seam) {
      float e = remapf(u, 0, 0.9f), p = 0.7f * smooth01(e) + 0.3f * e;
      out = Quat::axisAngle(X, lerpf(D.phi0, D.phiEnd, p)) * Quat::slerp(D.resid, Quat(), smooth01(t / 0.3f));
      return true;
    }
    out = Quat::axisAngle(X, sd * TAU * smooth01(remapf(u, 0.06f, 0.92f))); return true;
  }
  if (D.tr == "corkscrew") {
    float p = 1.2f * smooth01(u / 0.2f) * (1 - smooth01((u - 0.7f) / 0.28f));
    out = Quat::axisAngle(X, p) * Quat::axisAngle(Y, -sd * TAU * smoother01(remapf(u, 0.12f, 0.86f))); return true;
  }
  if (D.tr == "tuckFlip") { out = Quat::axisAngle(X, TAU * smooth01(remapf(u, 0.05f, 0.86f))); return true; }
  if (D.tr == "scissor") { out = Quat::axisAngle(X, 0.3f * smooth01(u / 0.25f) * (1 - smooth01((u - 0.68f) / 0.3f))); return true; }
  return false;
}

void Animator::trickPose(Pose& out, LayerData& D, float u, float w) {
  if (w < 0.005f) return;
  PoseBuilder& b = b_.begin(out); const std::string& tr = D.tr; float sd = (float)D.trSide, T = time_;
  if (!D.lenArm) {
    D.lenArm = b.pos("upperArmL").distanceTo(b.pos("lowerArmL")) + b.pos("lowerArmL").distanceTo(b.pos("handL"));
    D.lenLeg = b.pos("upperLegL").distanceTo(b.pos("lowerLegL")) + b.pos("lowerLegL").distanceTo(b.pos("footL"));
  }
  auto seg = [](float uu, float a, float bb) { return smoother01((uu - a) / (bb - a)); };
  float mid = smooth01(u / 0.2f) * (1 - smooth01((u - 0.72f) / 0.26f));
  float arch = 0, head = 0, lean = 0, grab = 0;
  Vec3 armO[2], legO[2]; float armR[2], legR[2];
  for (char S : {'L', 'R'}) {
    int s = si(S); float sx = sxOf(S), lead = sx == sd ? 1.f : 0.f;
    float fl = std::sin(T * 6.1f + sx) * 0.04f;
    if (tr == "layout") {
      float k1 = seg(u, 0.22f, 0.42f), k2 = seg(u, 0.64f, 0.84f), e0 = D.seam ? 1 : smooth01(u / 0.15f);
      armO[s] = vlerp(vlerp({sx * 0.3f, 1, 0.2f}, {sx * 1, 0.28f + fl, -0.12f}, k1), {sx * 0.5f, -0.5f, 0.5f}, k2); armR[s] = lerpf(lerpf(0.95f, 0.92f, k1), 0.86f, k2);
      legO[s] = vlerp(vlerp({sx * 0.07f, -1, -0.04f}, {sx * 0.09f, -0.92f, lead ? 0.34f : -0.28f}, k1), {sx * 0.07f, -1, 0.1f}, k2); legR[s] = lerpf(lerpf(0.97f, 0.9f, k1), 0.97f, k2);
      arch = lerpf(lerpf(sd < 0 ? 0.32f : 0.22f, 0, k1), 0.02f, k2) * e0;
      head = lerpf(lerpf(sd < 0 ? 0.35f : 0.1f, -0.22f, k1), 0, k2) * e0;
    } else if (tr == "corkscrew") {
      armO[s] = vlerp(vlerp({sx * 1, 0.1f, 0.05f}, {sx * 0.12f, -0.42f, 0.5f}, seg(u, 0.12f, 0.3f)), {sx * 0.9f, 0.3f + fl, -0.15f}, seg(u, 0.6f, 0.82f)); armR[s] = 0.95f;
      legO[s] = {-sx * 0.025f * mid, -1, lead ? 0.03f : -0.03f}; legR[s] = 0.985f;
      arch = 0.08f * mid; head = 0.1f * mid;
    } else if (tr == "tuckFlip") {
      float g = seg(u, 0.03f, 0.16f) * (1 - seg(u, 0.6f, 0.8f)); grab = g;
      legO[s] = vlerp({sx * 0.08f, -1, 0.06f}, {sx * 0.13f, -0.16f, 0.4f}, g); legR[s] = 0.97f;
      armO[s] = vlerp({sx * 0.6f, 0.05f, 0.35f}, {sx * 0.95f, 0.15f + fl, 0.05f}, seg(u, 0.62f, 0.86f)); armR[s] = 0.92f;
      arch = -0.5f * g; head = -0.35f * g;
    } else { // scissor
      float ph = TAU * 1.5f * u + (sx > 0 ? 0 : PI), fwd = std::sin(ph);
      legO[s] = {sx * 0.08f, -0.8f, 0.78f * fwd}; legR[s] = fwd > 0 ? 0.96f : 0.8f;
      armO[s] = {sx * 0.55f, -0.15f + 0.25f * std::max(0.f, -fwd), -0.6f * fwd}; armR[s] = 0.85f;
      lean = 0.12f * mid; head = 0.2f * mid;
    }
  }
  for (const auto& [k, f] : std::initializer_list<std::pair<const char*, float>>{{"spine", 0.35f}, {"spine1", 0.65f}, {"chest", 1}}) b.fromBind(k, Quat::axisAngle(X, lean - arch * f), w);
  b.fromBind("neck", Quat::axisAngle(X, lean - arch - head * 0.4f), w);
  b.fromBind("head", Quat::axisAngle(X, lean - arch - head), w);
  if (!D.sprInit) {
    D.sprInit = true;
    for (int k = 0; k < 4; k++) { bool arm = k < 2; D.spr[k] = Spring3(arm ? 2.8f : 3.4f, 0.55f); D.spr[k].x = arm ? armO[k] : legO[k - 2]; }
    if (D.seam) {
      PoseBuilder pb(&skel_); pb.begin(P.prev);
      for (char S : {'L', 'R'}) {
        D.spr[si(S)].x = (pb.pos(K("hand", S)) - pb.pos(K("upperArm", S))) / D.lenArm;
        D.spr[2 + si(S)].x = (pb.pos(K("foot", S)) - pb.pos(K("upperLeg", S))) / D.lenLeg;
      }
      b_.begin(out); skel_.gen++;
    }
  }
  float dt = dt_;
  for (char S : {'L', 'R'}) {
    int s = si(S); float sx = sxOf(S);
    Vec3 sl = D.spr[2 + s].step(legO[s], dt);
    Vec3 hp = b.pos(K("upperLeg", S));
    Vec3 lt = sl.normalized() * (legR[s] * D.lenLeg * clampf(sl.length(), 0.3f, 1)) + hp;
    Vec3 kp; { Vec3 d = lt - hp; float yz = std::hypot(d.y, d.z); if (yz < 1e-6f) yz = 1; kp = Vec3{sx * 0.15f, d.z / yz, -d.y / yz} + hp; }
    b.ik("leg", S, lt, kp, w, true);
    if (tr != "scissor") b.rot(K("foot", S), X, 0.35f * w * mid);
    Vec3 sa = D.spr[s].step(armO[s], dt);
    Vec3 sh = b.pos(K("upperArm", S));
    Vec3 at = sa.normalized() * (armR[s] * D.lenArm * clampf(sa.length(), 0.4f, 1)) + sh;
    if (grab > 0.001f) { Vec3 kn = b.pos(K("lowerLeg", S)), an = b.pos(K("foot", S)); at.lerp(vlerp(kn, an, 0.4f) + Vec3{sx * 0.06f, 0, 0.07f}, grab); }
    if (sx * at.x < 0.05f) at.x = sx * 0.05f;
    Vec3 pole;
    { Vec3 d = (at - sh).normalized(), p1 = Vec3{sx * 0.6f, -0.25f, -0.45f}.normalized(), p2 = Vec3{sx * 0.2f, -0.9f, 0.35f}.normalized();
      if (tr == "scissor") p1 = Vec3{sx * 0.3f, -1, 0}.normalized();
      float kk = tr == "scissor" ? 0 : smooth01((std::fabs(d.dot(p1)) - 0.7f) / 0.2f); pole = vlerp(p1, p2, kk) + sh; }
    b.ik("arm", S, at, pole, w, true);
    int iF = b.i(K("lowerArm", S)), iH = b.i(K("hand", S));
    if (iF >= 0 && iH >= 0) b.setCQ(iH, b.cq(iF) * skel_.bQ(iF).conj() * skel_.bQ(iH), w);
    if (grab > 0.01f) { rd_.curl(out, S, 0.8f, grab * w); b.dirty = true; }
  }
}
