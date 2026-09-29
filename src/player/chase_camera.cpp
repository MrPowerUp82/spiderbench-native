#include "player/chase_camera.h"
#include "player/input.h"
#include "world/world.h"

namespace {
const float OCC_PITCH[] = {0, 0.35f, 0.7f, 1.0f}, OCC_YAW[] = {0, 0.5f, -0.5f, 1.0f, -1.0f, 1.5f, -1.5f};
float angDamp(float a, float b, float rate, float dt) { float d = std::atan2(std::sin(b - a), std::cos(b - a)); return a + d * (1 - std::exp(-rate * dt)); }
float sd(CamSpring& o, float target, float st, float dt) {
  float w = 2 / std::max(1e-4f, st), x = w * dt, e = 1 / (1 + x + 0.48f * x * x + 0.235f * x * x * x);
  if (!std::isfinite(o.v)) { o.v = target; o.vel = 0; return target; }
  float ch = o.v - target, tmp = (o.vel + w * ch) * dt;
  o.vel = (o.vel - w * tmp) * e; o.v = target + (ch + tmp) * e; return o.v;
}
float sdA(CamSpring& o, float target, float st, float dt) { return sd(o, o.v + angWrap(target - o.v), st, dt); }
Vec3 sdV(CamSpringV& o, const Vec3& target, float st, float dt) {
  if (!o.init) { o.v = target; o.vel = {}; o.init = true; return o.v; }
  for (int a = 0; a < 3; a++) { CamSpring s{o.v[a], o.vel[a]}; sd(s, target[a], st, dt); o.v[a] = s.v; o.vel[a] = s.vel; }
  return o.v;
}
float noise(float t, float s) { return std::sin(t * 1.7f + s) * 0.5f + std::sin(t * 3.1f + s * 2.3f) * 0.3f + std::sin(t * 5.3f + s * 4.1f) * 0.2f; }
}  // namespace

void ChaseCamera::reset(const Vec3& pos, float y) {
  target_ = pos; target_.y += 0.55f; yaw = y; pitch = 0.14f; collDist_ = dist_;
  autoYaw_.v = NAN; lagOff_.init = false; jumpOff_.init = false; haveLastGoal_ = false; anchorLean_ = {};
}

void ChaseCamera::applyLook(const InputState& I) {
  if (std::fabs(I.lookDx) + std::fabs(I.lookDy) > 0.5f) lastLook_ = 0;
  yaw -= I.lookDx * sens; pitch = clampf(pitch + I.lookDy * sens, -0.9f, 1.25f);
}

void ChaseCamera::update(float dt, const CamParams& p) {
  dt = std::min(dt, 0.1f);
  time_ += dt; lastLook_ += dt;
  const Vec3& vel = p.vel; float speed = vel.length(), hs = std::hypot(vel.x, vel.z);
  const std::string& m = *p.mode; const std::string& sub = *p.sub;
  bool swinging = m == "swing", air = m == "air" || m == "zip", dive = p.dive;
  bool subLand = sub.rfind("land", 0) == 0, ledge = sub == "ledgeGrab" || sub == "ledgeClimb";
  // ---- auto recenter behind the direction of travel
  float blend = clampf((lastLook_ - 0.9f) * 1.5f, 0, 1);
  if (lastLook_ < 0.05f || !std::isfinite(autoYaw_.v)) { autoYaw_ = {yaw, 0}; autoPitch_ = {pitch, 0}; }
  {
    bool hasYaw = false, hasPitch = false; float wantYaw = 0, wantPitch = 0, rate = 0;
    if (m == "wall") {
      if (p.modeT > 0.3f) { wantYaw = std::atan2(-p.wallNormal.x, -p.wallNormal.z); rate = 1.6f; hasYaw = true; }
      wantPitch = sub == "wallRun" ? -0.08f : -0.05f; hasPitch = true;
    } else if (m == "perch") { wantYaw = p.facing; rate = 1.4f; wantPitch = 0.3f; hasYaw = hasPitch = true; }
    else if (m == "rope") { wantYaw = p.facing; rate = sub == "ropeShoot" ? 0.6f : 1.8f; wantPitch = 0.26f; hasYaw = hasPitch = true; }
    else if (dive && hs <= 2.5f) { wantPitch = 0.75f + 0.4f * smoothstep(-vel.y, 18, 45); hasPitch = true; }
    else if (swinging && p.swingDir) {
      wantYaw = std::atan2(p.swingDir->x, p.swingDir->z); rate = 2.2f; hasYaw = true;
      wantPitch = clampf(0.1f - vel.y * 0.006f, -0.12f, 0.3f); hasPitch = true;
    } else if (hs > 2.5f && !(p.sling > 0) && !p.noAuto) {
      wantYaw = std::atan2(vel.x, vel.z); hasYaw = true;
      rate = clampf((hs - 2) / 10, 0, 1) * (air ? 1.8f : 1.3f);
      wantPitch = dive ? 0.62f + 0.5f * smoothstep(-vel.y, 18, 45) : air ? clampf(0.14f - vel.y * 0.01f, -0.1f, 0.45f) : 0.14f; hasPitch = true;
    }
    occHold_ -= dt;
    if (occHold_ > 0) hasYaw = hasPitch = false;
    if (hasYaw) sdA(autoYaw_, wantYaw, 0.5f, dt);
    else if ((std::isfinite(autoRate_.v) ? autoRate_.v : 0) < 0.05f) autoYaw_ = {yaw, 0};
    if (hasPitch) sd(autoPitch_, wantPitch, 0.5f, dt);
    else if ((std::isfinite(autoPRate_.v) ? autoPRate_.v : 0) < 0.05f) autoPitch_ = {pitch, 0};
    sd(autoRate_, hasYaw ? rate : 0, 0.35f, dt);
    sd(autoPRate_, hasPitch ? (dive ? 2.4f : 1.1f) : 0, 0.35f, dt);
    if (blend > 0) {
      yaw = angDamp(yaw, autoYaw_.v, std::max(0.f, autoRate_.v) * blend, dt);
      pitch = damp(pitch, autoPitch_.v, std::max(0.f, autoPRate_.v) * blend, dt);
    }
  }
  // ---- follow pivot (velocity lag, soft-clamped; unexplained body jumps absorbed and sprung back)
  Vec3 goal = p.pos; goal.y += 0.55f;
  sd(lagK_, swinging || air ? 0.03f : m == "wall" ? 0.02f : 0.012f, 0.4f, dt);
  sd(lagMax_, swinging || air ? 0.8f : 0.5f, 0.4f, dt);
  Vec3 wantLag = vel * -lagK_.v; { float L = wantLag.length(); if (L > 1e-4f) wantLag *= lagMax_.v * std::tanh(L / lagMax_.v) / L; }
  sdV(lagOff_, wantLag, 0.3f, dt);
  if (!jumpOff_.init) { jumpOff_.init = true; jumpOff_.v = {}; jumpOff_.vel = {}; }
  if (haveLastGoal_) {
    Vec3 mv = goal - lastGoal_, jump = mv - vel * dt;
    Vec3 j2 = mv - lastVel_ * dt; if (j2.lengthSq() < jump.lengthSq()) jump = j2;
    float jl = jump.length();
    if (jl > 0.06f && jl < 3) jumpOff_.v -= jump; else if (jl >= 3) { jumpOff_.v = {}; jumpOff_.vel = {}; }
  }
  haveLastGoal_ = true; lastGoal_ = goal; lastVel_ = vel;
  if (jumpOff_.v.length() > 1.2f) jumpOff_.v.setLength(1.2f);
  sdV(jumpOff_, {0, 0, 0}, 0.3f, dt);
  target_ = goal + lagOff_.v + jumpOff_.v;
  float lw = swinging || air ? 0.06f : 0.04f;
  Vec3 leadW{vel.x * lw, vel.y * (swinging || air ? 0.015f : lw), vel.z * lw}; if (leadW.length() > 2) leadW.setLength(2);
  sdV(lead_, leadW, 0.45f, dt);
  // ---- distance / height / FOV by context
  float wantDist = 4.0f, wantH = 0, wantSide = 0.32f;
  if (m == "ground") {
    wantDist = 3.9f + clampf((speed - 8) * 0.07f, 0, 0.7f); wantSide = 0.35f;
    float wk = p.walkK * (1 - smoothstep(speed, 2.2f, 4.5f)); wantDist -= 0.45f * wk; wantH -= 0.1f * wk;
  } else if (swinging) { wantDist = 3.6f + clampf((speed - 12) * 0.025f, 0, 0.7f); wantH = 0.35f + p.tension * 0.25f; wantSide = 0.15f; }
  else if (air) { wantDist = dive ? 3.9f : 3.8f + clampf((speed - 12) * 0.025f, 0, 0.7f); wantH = dive ? 0.9f : 0.15f; wantSide = 0.2f; }
  else if (m == "wall") { wantDist = 4.8f; wantH = sub == "wallRun" ? 0.2f : 0; wantSide = 0; }
  else if (m == "perch") { wantDist = 4.3f; wantH = 0.25f; wantSide = 0.4f; }
  else if (m == "rope") { wantDist = 4.1f; wantH = 0.4f; wantSide = 0.2f; }
  if (m == "land" || subLand) wantDist = 4.2f;
  if (ledge) { wantH = 1.4f; pitch = damp(pitch, 0.42f, 4, dt); }
  float wantFov = 58 + 13 * smoothstep(speed, 12, 44) + (dive ? 5 : 0);
  if (p.sling > 0) { wantDist += 1.6f * p.sling; wantH += 0.3f * p.sling; wantFov += 8 * p.sling * p.sling; }
  dist_ = sd(distS_, wantDist, 0.55f, dt);
  heightOff_ = sd(heightS_, wantH, 0.5f, dt);
  sideOff_ = sd(sideS_, wantSide, 0.6f, dt);
  punchV_ += (-punch_ * 140 - punchV_ * 16) * dt; punch_ += punchV_ * dt;
  dipV_ += (-dip_ * 90 - dipV_ * 13) * dt; dip_ += dipV_ * dt;
  fov_ = sd(fovS_, wantFov, 0.5f, dt);
  kickV_ += (-kickK_ * 55 - kickV_ * 11) * dt; kickK_ += kickV_ * dt;
  // ---- roll: velocity yaw-rate + swing bank
  float velYaw = std::atan2(vel.x, vel.z);
  float dyw = std::isfinite(lastVelYaw_) ? angWrap(velYaw - lastVelYaw_) : 0; lastVelYaw_ = velYaw;
  sd(yawRate_, hs > 3 ? clampf(dyw / std::max(dt, 1e-3f), -3, 3) * std::min(1.f, (hs - 3) / 17) : 0, 0.3f, dt);
  sd(bankS_, swinging ? p.bank : 0, 0.35f, dt);
  float wantRoll = clampf(-yawRate_.v * 0.04f, -0.09f, 0.09f) - bankS_.v * 0.05f;
  roll_ = damp(roll_, wantRoll, 3, dt);
  // ---- compose
  Vec3 fwd = forward();
  Vec3 pivot = target_; pivot.y += heightOff_ + dip_;
  Vec3 right{-std::cos(yaw), 0, std::sin(yaw)};
  pivot.addScaled(right, sideOff_);
  { // the pivot never sits behind a wall relative to the character
    Vec3 eye = p.pos; eye.y += 0.55f; Vec3 d = pivot - eye; float L = d.length();
    if (L > 1e-3f) {
      d /= L; Hit h; float cap = world_.raycast(eye, d, L + 0.25f, h) ? std::max(0.f, h.distance - 0.3f) : L + 0.25f;
      pivCap_ = !std::isfinite(pivCap_) ? cap : cap < pivCap_ ? cap : damp(pivCap_, cap, 4, dt);
      if (pivCap_ < L) pivot = eye + d * pivCap_;
    }
  }
  Vec3 back = -fwd;
  float allowed = dist_;
  auto probe = [&](float ox, float oy) {
    Vec3 o = pivot + right * ox; o.y += oy; Hit h;
    if (world_.raycast(o, back, dist_ + 0.4f, h)) allowed = std::min(allowed, std::max(0.5f, h.distance - 0.35f));
  };
  probe(0, 0); probe(0.3f, 0); probe(-0.3f, 0); probe(0, 0.25f); probe(0, -0.25f);
  { // foliage: never park the camera inside a tree canopy
    std::vector<const TreePt*> cans; world_.treesNear(pivot, 25, cans);
    if (!cans.empty()) for (float t = 0.8f; t < allowed; t += 0.5f) {
      Vec3 o = pivot + back * t; bool in = false;
      for (auto* q : cans) { float dy = o.y - q->cy; if (std::fabs(dy) < q->r * 0.75f && (o.x - q->pos.x) * (o.x - q->pos.x) + (o.z - q->pos.z) * (o.z - q->pos.z) < q->r * q->r) { in = true; break; } }
      if (in) { allowed = std::max(0.8f, t - 0.35f); break; }
    }
  }
  // occluded: search nearby orbit directions with >= ~3 m clearance and move there smoothly
  float minD = std::min(3.0f, dist_ * 0.75f);
  occT_ -= dt; if ((sub == "vault" || sub.rfind("ledge", 0) == 0) && occT_ > 0.03f) occT_ = 0.03f;
  if (allowed < minD && occT_ <= 0) {
    occT_ = 0.12f;
    auto clear = [&](float yw, float pt) {
      float cp = std::cos(pt); Vec3 d{-std::sin(yw) * cp, std::sin(pt), -std::cos(yw) * cp};
      float a = dist_; for (float ox : {0.f, 0.3f, -0.3f}) { Vec3 o = pivot + right * ox; Hit h; if (world_.raycast(o, d, dist_ + 0.4f, h)) a = std::min(a, h.distance - 0.35f); }
      return a;
    };
    bool found = false; float bs = -INF, by = 0, bp = 0;
    float base = m == "perch" ? p.facing : yaw;
    for (float dp : OCC_PITCH) for (float dyw2 : OCC_YAW) {
      float yw = base + dyw2, pt = clampf(pitch + dp, -0.3f, 1.15f);
      float a = clear(yw, pt), sc = std::min(a, dist_) - 0.9f * std::fabs(dyw2) - 0.6f * dp;
      if (a >= minD && sc > bs) { bs = sc; by = yw; bp = pt; found = true; }
    }
    occGoal_ = found; occYaw_ = by; occPitch_ = bp; if (found) occHold_ = 2.0f;
  }
  if (allowed >= minD + 0.5f && occT_ <= -1) occGoal_ = false;
  if (occGoal_) {
    bool fast = sub == "vault" || sub.rfind("ledge", 0) == 0 || (p.modeT < 0.8f && !swinging && !air);
    float r = lastLook_ < 0.4f ? 1.5f : fast ? 11 : swinging || air ? 3 : 5;
    yaw = angDamp(yaw, occYaw_, r, dt); pitch = damp(pitch, occPitch_, r, dt);
    back = {-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    if (std::fabs(angWrap(yaw - occYaw_)) < 0.05f && std::fabs(pitch - occPitch_) < 0.05f) occGoal_ = false;
  }
  if (allowed < collDist_) { collDist_ = damp(collDist_, allowed, 30, dt); collS_ = {collDist_, 0}; }
  else { collS_.v = collDist_; collDist_ = sd(collS_, allowed, 0.45f, dt); }
  camera_.position = pivot + back * std::min(collDist_ + 1.3f * std::max(0.f, kickK_), std::max(collDist_, allowed));
  float gy = world_.groundHeight(camera_.position.x, camera_.position.z, camera_.position.y + 0.3f) + 0.3f;
  if (camera_.position.y < gy) camera_.position.y = gy;
  // look target: ahead of the pivot; while swinging lean toward the (spring-smoothed) anchor offset
  if (swinging && p.anchor) {
    Vec3 want = *p.anchor - pivot;
    if (!std::isfinite(anchorLean_.v) || anchorLean_.v < 0.005f) { leanOff_.v = want; leanOff_.vel = {}; leanOff_.init = true; }
    sdV(leanOff_, want, 0.4f, dt);
  }
  sd(anchorLean_, swinging && p.anchor ? 0.1f : 0, swinging ? 0.5f : 0.7f, dt);
  Vec3 lookAt = pivot + fwd * 10 + lead_.v;
  if (anchorLean_.v > 0.0005f) lookAt.lerp(pivot + leanOff_.v, anchorLean_.v);
  trauma_ = std::max(0.f, trauma_ - dt * 1.5f);
  float sh = trauma_ * trauma_;
  camera_.lookAt(lookAt);
  camera_.rotateZ(roll_ + sh * 0.045f * noise(time_ * 22, 3));
  camera_.rotateX(sh * 0.035f * noise(time_ * 25, 1)); camera_.rotateY(sh * 0.035f * noise(time_ * 24, 7));
  camera_.fov = fov_ + punch_ + 9 * std::max(0.f, kickK_);
  // speed motion blur amount for the post pipeline
  float mbK = (m == "ground" || m == "wall" ? 0.45f : dive ? 1.6f : 1) * smoothstep(speed, 6, 36) * 1.1f;
  motionBlur = std::max(0.f, sd(mbK_, mbK, mbK > (std::isfinite(mbK_.v) ? mbK_.v : 0) ? 0.35f : 0.2f, dt));
}
