#include "player/traversal/traversal.h"
#include "player/chase_camera.h"
#include "player/web.h"
#include "anim/rig.h"
#include <cstdio>
#include <random>
#include <map>

namespace {
constexpr float H = TRAV_H, R = TRAV_R, HEIGHT = TRAV_HEIGHT;
constexpr float STEP = 0.55f;
constexpr float G = 24, GS = 25;
constexpr float WALK = 2.6f, RUN = 9.8f, SPRINT = 15.5f;
constexpr float WALK_SLOW = 1.4f, WALK_EASE = 7, WALK_ACC = 5.5f, WALK_DEC = 3.5f, WALK_TURN = 4.5f;
constexpr float WALLRUN = 14;
constexpr float VMAX = 45;
constexpr float CHAIN_BUF = 1.6f, CHAIN_CAP = 2.5f, CHAIN_REL = 1.5f; constexpr int CHAIN_MAX = 6;
struct { float dur = 0.95f, v0 = 46, v1 = 16, reach = 42, snap = 0.75f, cd = 1.2f; } WZIP;
constexpr float SWING_JUMP = 4.0f, SWING_JUMP_UP = 16, SWING_JUMP_VY = 22, REL_NOTRICK = 4.0f, REL_UP = 9, REL_UP_VY = 16;
constexpr float SWING_DIP = 6, SWING_GAIN = 5, RELEASE_BOOST = 1.5f, SWING_DRAG = 0.0022f, PUMP_MAX_ANG = 1.15f;
constexpr float JUMP = 11.2f, JUMP_MAX = 19.5f;
constexpr float WATER_Y = -1.0f;
struct TrickDef { float dur, snap, boost, up, steer, side; };
const std::map<std::string, TrickDef> TRICK_DEF = {
  {"tuckFlip", {0.9f, 0.35f, 5.5f, 0.8f, 0, 0}},
  {"layout", {1.3f, 0.35f, 4.0f, 2.5f, 0, 0}},
  {"corkscrew", {0.78f, 0.35f, 5.0f, 0.6f, 0.35f, 2.5f}},
  {"scissor", {0.7f, 0.32f, 3.5f, 1.4f, 0, 0}},
};
const char* TRICKS[] = {"tuckFlip", "layout", "corkscrew", "scissor"};
// web slingshot
constexpr float SL_MAX = 2.8f, SL_BACK = 3.0f, SL_FLOOR = 0.3f, SL_ELEV = 40 * PI / 180, SL_MIN = 0.2f, SL_REL = 0.12f; constexpr int SL_CAP = 4;
// zip
constexpr float ZIP_EXP = 1.8f, ZIP_BRAKE = 200, ZIP_VEND = 5, ZIP_RAMP = 0.07f, ZIP_SPEED = 0.83f;
float zipEase(float u) { return 1 - std::pow(1 - u, ZIP_EXP); }
// quick web boost
struct { float dv = 12, hCap = 40, cd = 0.55f, minD = 25, maxD = 80, nearD = 12, web = 0.26f, dur = 0.62f; } QUICK;
bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
float sm01(float x) { x = clampf(x, 0, 1); return x * x * (3 - 2 * x); }
}  // namespace

Traversal::Traversal(const World& w, ChaseCamera& cam, WebSystem& web, Rig& rig, const Camera& camera)
    : targeting(w), anchors(w), world_(w), cam_(cam), web_(web), rig_(rig), camera_(camera) {}

float Traversal::rnd() { static std::mt19937 g(std::random_device{}()); return std::uniform_real_distribution<float>(0, 1)(g); }

// ------------------------------------------------------------------ helpers
float Traversal::floorAt(float x, float z, float y) const { return world_.groundHeight(x, z, y - 0.5f); }
// standable floor: thin pinnacles are not floors (the capsule push-out moves the body off them)
float Traversal::standAt(float x, float z, float y) const {
  float g = floorAt(x, z, y), r = 0.22f;
  float a = floorAt(x + r, z, y), b = floorAt(x - r, z, y), c = floorAt(x, z + r, y), d = floorAt(x, z - r, y);
  if (a < g - 0.25f && b < g - 0.25f && c < g - 0.25f && d < g - 0.25f) return std::max({a, b, c, d});
  return g;
}
Vec3 Traversal::inputDir(const InputState& I) const { return cam_.forwardFlat() * I.move.y + cam_.rightFlat() * I.move.x; }
// INVARIANT: the web is released ONLY by letting go of the swing button, a zip / dash, or teleport
void Traversal::setMode(const std::string& mode, const std::string& sub) {
  if (s.mode == "swing" && mode != "swing" && lastInput_ && lastInput_->swing && !leaveSwingOK_)
    std::fprintf(stderr, "[traversal] BUG: left 'swing' -> '%s/%s' while the swing button is held\n", mode.c_str(), sub.c_str());
  if (s.mode != mode) s.modeT = 0;
  s.mode = mode; setSub(sub);
}
float Traversal::releaseBoost() const { return RELEASE_BOOST * releaseBoostMul_; }
std::optional<Contact> Traversal::collide(float stepH, float rad) {
  Vec3 feet{s.pos.x, s.pos.y - H, s.pos.z};
  auto c = pushOutCapsule(world_, feet, rad, HEIGHT, stepH);
  s.pos.x = feet.x; s.pos.z = feet.z;
  return c;
}
std::optional<Vec3> Traversal::hdir(const Vec3& v) { Vec3 o{v.x, 0, v.z}; float l = o.length(); if (l > 1e-4f) return o / l; return std::nullopt; }
float Traversal::vmaxC() const { return VMAX + CHAIN_CAP * s.chain; }
void Traversal::capSpeed(float m) { if (m < 0) m = vmaxC(); float l = s.vel.length(); if (l > m) s.vel *= m / l; }

// ------------------------------------------------------------------ ground
void Traversal::enterGround(const std::string& sub) {
  setMode("ground", sub); s.grounded = true; s.vel.y = 0; s.dashCount = 0; s.dive = false; s.trick.clear(); s.quick.n = 0;
  float hs = std::hypot(s.vel.x, s.vel.z); s.speed = hs; if (hs > 3) s.facing = std::atan2(s.vel.x, s.vel.z);
}

void Traversal::stepGround(float h, InputState& I) {
  Vec3 inD = inputDir(I); float mag = std::min(1.f, inD.length()); if (mag > 1e-3f) inD /= mag;
  bool walkHeld = I.walk;
  s.walkK = s.speed < 0.3f ? (walkHeld ? 1.f : 0.f) : damp(s.walkK, walkHeld ? 1.f : 0.f, WALK_EASE, h);
  float wk = s.walkK;
  bool parkour = I.swing || (I.sprint && !walkHeld);
  auto& L = s.landing;
  bool landing = startsWith(s.sub, "land");
  if (landing) {
    L.lock -= h;
    if (s.sub == "landRoll") s.speed = std::max(s.speed - 6 * h, std::min(s.speed, 6.f));
    else if (L.lock > 0) s.speed = std::max(0.f, s.speed - 40 * h);
    bool cancel = L.lock <= 0 && (mag > 0.2f || I.jumpPressed || I.jump);
    float lim = s.sub == "landHard" ? 0.75f : s.sub == "landRoll" ? 0.6f : s.sub == "landMedium" ? 0.35f : 0.18f;
    if (s.subT > lim || cancel) setSub("idle");
  }
  bool locked = landing && L.lock > 0;
  if (stepSling(h, I, landing)) return;
  if (!locked) {
    if ((I.jumpPressed || (s.jumpBuf > 0 && I.jump)) && !s.charging) { s.charging = true; s.chargeT = 0; s.jumpBuf = 0; }
    if (s.charging) {
      s.chargeT += h; s.jumpCharge = clampf((s.chargeT - 0.1f) / 0.55f, 0, 1);
      if (!I.jump && s.chargeT >= (s.speed > RUN ? 0.06f : 0.1f)) { launchJump(parkour); return; }
    }
  }
  // RMB pressed on the ground: if there is anything to swing from, hop up and swing
  if (!locked && I.swingPressed && !s.charging && s.swingCooldown <= 0) {
    Vec3 fwd = travelDir(I), probe = s.pos; probe.y += 3;
    auto a = anchors.find(probe, fwd, nullptr, std::max(s.speed, 10.f), s.floorY);
    if (a && a->point.y > s.pos.y + 6) { s.jumpCharge = 0.35f; launchJump(true); s.groundSwing = true; s.swingCooldown = 0.1f; return; }
  }
  // locomotion: facing-driven, accel / decel curves
  float target = 0;
  if (!locked && mag > 0.08f) {
    target = mag < 0.55f ? WALK * mag / 0.55f + 0.4f : WALK + (RUN - WALK) * (mag - 0.55f) / 0.45f;
    if (wk > 1e-3f) target += (WALK_SLOW * clampf(mag / 0.6f, 0.45f, 1) - target) * wk;
    if (s.charging) target *= 1 - 0.75f * s.jumpCharge;
    float want = std::atan2(inD.x, inD.z), d = angWrap(want - s.facing);
    if (s.sub != "landRoll") {
      if (std::fabs(d) > 2.4f && s.speed > 6) { s.speed *= std::exp(-14 * h); s.facing += signf(d) * 9 * h; }
      else {
        float rate = s.speed < 2 ? 18.f : s.speed < 9 ? 11.f : 7.f;
        if (wk > 1e-3f && s.speed < 3) rate += ((s.speed < 0.3f ? 9 : WALK_TURN) - rate) * wk;
        s.facing += clampf(d, -rate * h, rate * h);
      }
    }
    target *= clampf(1 - std::max(0.f, std::fabs(d) - 0.9f) * 0.5f, 0.35f, 1);
  }
  if (s.sub != "landRoll" || s.speed < 6) {
    float acc = (parkour ? 24.f : 30.f) * (s.speed < 3 ? 1.6f : 1), dec = target < 0.1f ? 28.f : 22.f;
    if (wk > 1e-3f && s.speed < 2.2f) { if (target < 2.2f) acc += (WALK_ACC - acc) * wk; dec += (WALK_DEC - dec) * wk; }
    if (target > s.speed) s.speed = std::min(target, s.speed + acc * h);
    else s.speed = std::max(target, s.speed - dec * h);
    if (target < 0.1f && s.speed < 0.35f) s.speed = 0;
  }
  float fx = std::sin(s.facing), fz = std::cos(s.facing);
  s.carry *= std::exp(-7 * h);
  s.vel = {fx * s.speed + s.carry.x, 0, fz * s.speed + s.carry.z};
  s.pos.x += s.vel.x * h; s.pos.z += s.vel.z * h;
  auto c = collide();
  bool wide = c ? wideWall(c->normal, c->point) : false;
  if (c && !wide) { // narrow obstacle: parkour AROUND it
    float tx = -c->normal.z, tz = c->normal.x, sd = signf(fx * tx + fz * tz); if (sd == 0) sd = 1;
    float want = std::atan2(tx * sd + c->normal.x * 0.35f, tz * sd + c->normal.z * 0.35f);
    s.facing += clampf(angWrap(want - s.facing), -10 * h, 10 * h);
  } else if (c) {
    float into = -(fx * c->normal.x + fz * c->normal.z);
    if (into > 0.25f) s.speed *= std::max(0.f, 1 - into * 0.9f * std::min(1.f, h * 30));
    if (into > 0.8f && c->top - feetY() > 2.1f && !parkour) s.speed = 0;
    float obstacle = c->top - feetY();
    if (!locked && into > 0.55f && mag > 0.3f && walkHeld && !parkour) {
      if (obstacle >= 2.1f && s.wallCooldown <= 0) { enterWall(c->normal, c->point, true, std::max(12.f, s.speed)); return; }
      if (obstacle > STEP && obstacle <= 1.45f && startMantleOnto(*c)) return;
    } else if (!locked && into > 0.55f && mag > 0.3f) {
      if (obstacle > STEP && obstacle < 2.1f) {
        float behind = floorAt(s.pos.x - c->normal.x * (R + 1.2f), s.pos.z - c->normal.z * (R + 1.2f), c->top + 0.05f);
        if (parkour || (obstacle < 1.2f && behind > c->top - 1.5f)) { startVault(*c, parkour); return; }
        if (obstacle <= 1.45f && startMantleOnto(*c)) return;
      }
      if (parkour && obstacle >= 2.1f && s.wallCooldown <= 0) { enterWall(c->normal, c->point, true, std::max(12.f, s.speed)); return; }
      if (!parkour && obstacle > STEP && obstacle <= 1.45f && startMantleOnto(*c)) return;
    }
  }
  // ground snap
  float fy = feetY();
  float g = standAt(s.pos.x, s.pos.z, fy + STEP);
  if (g < fy - 0.65f) { setMode("air", "fall"); s.grounded = false; s.airT = 0; s.apexY = fy; s.coyote = 0.12f; s.vel.y = 0; return; }
  if (std::fabs(g - fy) > 1e-4f) { s.stepOff += fy - g; s.stepOff = clampf(s.stepOff, -0.6f, 0.6f); }
  if (g < WATER_Y && waterBounce()) return;
  s.pos.y = g + H; s.floorY = g;
  if (!startsWith(s.sub, "land") && s.sub != "vault") {
    if (s.charging) setSub("jumpCharge");
    else if (s.speed < 0.2f) setSub("idle");
    else if (s.speed < WALK + 0.8f) setSub("walk");
    else if (s.speed > RUN + 1.5f) setSub("sprint");
    else setSub("run");
  }
}

bool Traversal::startMantleOnto(const Contact& c) {
  Vec3 inward = -c.normal;
  float thick = 0; for (; thick < 0.9f; thick += 0.05f) { float f = floorAt(c.point.x + inward.x * (thick + 0.03f), c.point.z + inward.z * (thick + 0.03f), c.top + 0.05f); if (std::fabs(f - c.top) > 0.06f) break; }
  if (thick < 0.15f) return false;
  Vec3 land{c.point.x + inward.x * (thick / 2 + 0.02f), c.top + H, c.point.z + inward.z * (thick / 2 + 0.02f)};
  if (world_.inside({land.x, land.y + 0.3f, land.z})) return false;
  Vec3 p0 = s.pos, ctrl = vlerp(p0, land, 0.3f); ctrl.y = land.y + 0.35f;
  float sp = std::hypot(s.vel.x, s.vel.z);
  TravState::Kin k; k.type = "vault"; k.dur = clampf(1.2f / std::max(sp, 3.f), 0.22f, 0.42f); k.p0 = p0; k.p1 = ctrl; k.p2 = land; k.exitVel = inward * sp; k.floor = c.top;
  s.kin = k;
  s.facing = std::atan2(inward.x, inward.z);
  setMode("ground", "vault"); push("vault");
  return true;
}

void Traversal::launchJump(bool parkour) {
  float k = std::pow(s.jumpCharge, 0.85f);
  s.vel.y = JUMP + (JUMP_MAX - JUMP) * k + (parkour && s.speed > 10 ? 1.2f : 0);
  if (s.speed > 1) { float f = std::min(s.speed + 1.2f, VMAX); s.vel.x = std::sin(s.facing) * f; s.vel.z = std::cos(s.facing) * f; }
  s.charging = false; s.chargeT = 0; s.grounded = false;
  float charge = s.jumpCharge;
  setMode("air", "jumpLaunch"); s.airT = 0; s.apexY = feetY(); s.jumpCharge = charge; s.swingCooldown = 0.12f;
  TravEvent e; e.type = "jump"; e.charge = charge; events.push_back(e);
}

// ------------------------------------------------------------------ web slingshot (Ctrl held on the ground)
Vec3 Traversal::slingDir() {
  auto& S = s.sling; Vec3 out;
  for (auto& a : S.anchors) { float dx = a.p.x - S.origin.x, dz = a.p.z - S.origin.z, l = std::hypot(dx, dz); if (l > 1e-3f) { out.x += dx / l; out.z += dz / l; } }
  float l = out.length(); if (l > 1e-3f) out /= l;
  out += S.fwd0;
  if (out.lengthSq() < 1e-4f) out = S.fwd0;
  return out.normalized();
}
bool Traversal::slingAttach(int side) {
  auto& S = s.sling;
  int n = 0; for (auto& a : S.anchors) n += a.side == side;
  if (n >= SL_CAP) { push("slingFail"); return false; }
  Vec3 f = cam_.forwardFlat(), r = cam_.rightFlat();
  Vec3 o = s.pos; o.y += 0.6f;
  for (float th0 : {55.f, 42.f, 68.f, 30.f, 80.f}) for (float el0 : {22.f, 32.f, 14.f, 42.f}) {
    float th = (th0 - n * 7) * PI / 180 + (rnd() - 0.5f) * 0.07f, el = (el0 + n * 5) * PI / 180 + (rnd() - 0.5f) * 0.07f;
    Vec3 d = (f * std::cos(th) + r * (side * std::sin(th))).normalized();
    d *= std::cos(el); d.y = std::sin(el);
    Hit hit;
    if (world_.raycast(o, d, 40, hit) && hit.distance > 3 && std::fabs(hit.normal.y) < 0.7f) {
      S.anchors.push_back({hit.point, hit.normal, side, 0});
      TravEvent e; e.type = "slingAttach"; e.dist = hit.distance; events.push_back(e);
      return true;
    }
  }
  push("slingFail");
  return false;
}
void Traversal::slingEnd(bool launch) {
  auto& S = s.sling;
  TravEvent e; e.type = "slingRelease"; e.tension = S.tension; e.run = launch; events.push_back(e);
  S.active = false; S.anchors.clear(); S.tension = 0; S.pull = 0; S.moving = 0; S.rel = -1;
}
void Traversal::slingLaunch() {
  auto& S = s.sling; float k = S.tension;
  Vec3 D = slingDir();
  float sp = 24 + 28 * k;
  s.vel = D * (sp * std::cos(SL_ELEV)); s.vel.y = sp * std::sin(SL_ELEV);
  s.pos.y += 0.05f; s.speed = 0; s.charging = false;
  setMode("air", "pointLaunch"); s.airT = 0; s.apexY = feetY(); s.swingCooldown = 0.45f; s.wallCooldown = 0.5f; s.relT = 0;
  s.facing = std::atan2(D.x, D.z); s.trick.clear(); s.grounded = false; s.dive = false;
  push("pointLaunch"); { TravEvent e; e.type = "zipLaunch"; e.dist = 40 * k; events.push_back(e); }
  { TravEvent e; e.type = "slingLaunch"; e.tension = k; events.push_back(e); }
  slingEnd(true);
}
bool Traversal::stepSling(float h, InputState& I, bool landing) {
  auto& S = s.sling;
  if (!S.active) {
    if (!I.ctrl || landing || s.charging || s.sub == "vault" || s.kin) return false;
    S.active = true; S.anchors.clear(); S.tension = 0; S.pull = 0; S.moving = 0; S.t = 0; S.rel = -1;
    S.origin = s.pos; S.fwd0 = cam_.forwardFlat(); S.dir = S.fwd0;
    setSub("slingshot"); push("slingStart");
  }
  if (S.rel >= 0) {
    S.rel += h; for (auto& a : S.anchors) a.t += h;
    s.vel = {}; s.speed = 0;
    if (S.rel >= SL_REL) slingLaunch();
    return true;
  }
  if (!I.ctrl) {
    if (!S.anchors.empty() && S.tension > SL_MIN) { S.rel = 0; push("slingWindup"); return true; }
    slingEnd(false); setSub("idle"); return false;
  }
  S.t += h;
  if (I.slingL) { slingAttach(-1); I.slingL = false; }
  if (I.slingR) { slingAttach(1); I.slingR = false; }
  for (auto& a : S.anchors) a.t += h;
  Vec3 D = slingDir(); S.dir = D;
  Vec3 inD = inputDir(I); float back = -(inD.x * D.x + inD.z * D.z);
  float v = 0;
  if (!S.anchors.empty()) {
    if (back > 0.2f) v = SL_BACK * std::min(1.f, back) * std::max((1 - S.tension) * (1 - S.tension), SL_FLOOR);
    else if (back < -0.2f && S.pull > 0.01f) v = -1.4f * std::min(1.f, -back);
  }
  if (S.tension >= 0.999f && v > 0) v = 0;
  float px = s.pos.x, pz = s.pos.z;
  s.pos.x -= D.x * v * h; s.pos.z -= D.z * v * h;
  collide();
  float pull = -((s.pos.x - S.origin.x) * D.x + (s.pos.z - S.origin.z) * D.z);
  if (pull > SL_MAX) { s.pos.x += D.x * (pull - SL_MAX); s.pos.z += D.z * (pull - SL_MAX); pull = SL_MAX; }
  if (pull < 0) { s.pos.x += D.x * pull; s.pos.z += D.z * pull; pull = 0; }
  float fy = feetY(), g = standAt(s.pos.x, s.pos.z, fy + STEP);
  if (g < fy - 0.65f) { s.pos.x = px; s.pos.z = pz; pull = S.pull; }
  else { if (std::fabs(g - fy) > 1e-4f) s.stepOff = clampf(s.stepOff + fy - g, -0.6f, 0.6f); s.pos.y = g + H; s.floorY = g; }
  float moved = std::hypot(s.pos.x - px, s.pos.z - pz) / h;
  S.pull = std::max(0.f, pull); S.tension = clampf(S.pull / SL_MAX, 0, 1);
  S.moving = damp(S.moving, moved > 0.05f ? signf(v) : 0, 10, h);
  s.vel = {-D.x * v, 0, -D.z * v}; if (moved < 0.02f) s.vel = {};
  s.speed = 0; s.carry = {};
  s.facing += clampf(angWrap(std::atan2(D.x, D.z) - s.facing), -8 * h, 8 * h);
  setSub("slingshot");
  return true;
}

// ------------------------------------------------------------------ air
void Traversal::stepAir(float h, InputState& I) {
  s.airT += h; s.coyote -= h;
  if (s.coyote > 0 && I.jumpPressed) { s.jumpCharge = 0; launchJump(I.sprint || I.swing); return; }
  // double-tap Space in the air = an air flip (once per airtime)
  if (I.jumpPressed) {
    float dtap = s.airT - s.airTapT;
    if (dtap >= 0 && dtap < 0.4f && !s.airTrickUsed && heightAboveFloor() > 2.5f && s.sub != "trick") {
      startTrick(chooseTrick(&I)); s.airTrickUsed = true; s.airTapT = -9;
      TravEvent e; e.type = "airTrick"; e.trick = s.trick; events.push_back(e);
    } else s.airTapT = s.airT;
  }
  if (s.sub == "trick" && !s.trick.empty() && !s.trickBoosted && s.subT >= s.trickSnapT) trickBoost(I);
  bool wDive = I.move.y > 0.5f && s.airT > 0.3f && (s.vel.y < -7 || (s.dive && s.vel.y < 0)) && s.sub != "trick" && s.sub != "zipPull" && heightAboveFloor() > 6;
  s.dive = (I.drop || wDive) && s.airT > 0.08f && !(s.returnT > 0) && heightAboveFloor() > 3;
  float g = G;
  if (!s.dive && std::fabs(s.vel.y) < 3.5f && s.sub != "zipPull") g *= 0.55f; // apex hang time
  if (s.dive) g *= 1.55f;
  // open areas with the swing button held and nothing to attach to: web-assisted glide-dive
  s.returnT -= h;
  bool glide = I.swing && !s.dive && !(s.returnT > 0) && s.noAnchorT > 0.15f && s.vel.y < -4 && s.airT > 0.3f && heightAboveFloor() > 3.5f;
  s.gliding = glide;
  if (glide) {
    g *= 0.5f;
    float hs0 = std::hypot(s.vel.x, s.vel.z); Vec3 hv0 = hdir(s.vel).value_or(cam_.forwardFlat());
    float excess = std::max(0.f, -s.vel.y - 13), conv = std::min(excess, 18 * h);
    s.vel.y += conv; float add = std::min(conv * 0.9f + 3 * h, std::max(0.f, 34 - hs0));
    s.vel.x += hv0.x * add; s.vel.z += hv0.z * add;
  }
  s.vel.y = std::max(s.vel.y - g * h, s.dive ? -72.f : -56.f);
  if (glide && s.noAnchorT < 4 && heightAboveFloor() < 12) s.vel.y = damp(s.vel.y, -2.5f, 3, h);
  // air control (fades in after a web release: the release velocity owns the trajectory)
  Vec3 inD = inputDir(I);
  s.relT += h;
  float relK = s.returnT > 0 ? 0 : clampf((s.relT - 0.35f) / 0.55f, 0, 1);
  float rawMag = std::min(1.f, inD.length()), mag = rawMag * relK;
  float hs = std::hypot(s.vel.x, s.vel.z);
  if (mag > 0.05f) {
    inD /= std::max(inD.length(), 1e-3f);
    if (hs < 9) { s.vel.x += inD.x * 16 * mag * h; s.vel.z += inD.z * 16 * mag * h; float n = std::hypot(s.vel.x, s.vel.z), cap = std::max(9.f, hs); if (n > cap) { s.vel.x *= cap / n; s.vel.z *= cap / n; } }
    else {
      float cur = std::atan2(s.vel.x, s.vel.z), want = std::atan2(inD.x, inD.z), d = angWrap(want - cur);
      float turn = clampf(d, -1.9f * mag * h, 1.9f * mag * h), na = cur + turn;
      float sp = hs; if (std::fabs(d) > 2.2f) sp = std::max(6.f, hs - 8 * h);
      s.vel.x = std::sin(na) * sp; s.vel.z = std::cos(na) * sp;
    }
  }
  if (s.dive) { float n = std::hypot(s.vel.x, s.vel.z); if (n > 2) { float k = std::min(n + 3 * h, 30.f) / n; s.vel.x *= k; s.vel.z *= k; } }
  if (hs > 32 + 3 * s.chain) { s.vel.x *= 1 - 0.12f * h; s.vel.z *= 1 - 0.12f * h; }
  if (hs > 12 && relK >= 1 && (s.sub == "release" || s.sub == "trick" || I.swing)) corridor(h, inD);
  float prevFeet = feetY();
  s.pos += s.vel * h;
  auto c = collide(0.35f);
  if (c) {
    auto hv = hdir(s.vel); float into = hv ? -hv->dot(c->normal) : 0;
    bool pushIn = inD.dot(c->normal) < -0.4f;
    bool chaining = (I.swing || s.relT < 0.7f) && !pushIn && std::hypot(s.vel.x, s.vel.z) > 9;
    if ((into > 0.3f || pushIn) && s.wallCooldown <= 0 && c->top - feetY() > 1.2f && wideWall(c->normal, c->point) && !chaining) {
      if (c->top - feetY() < 1.9f && s.vel.y > -6) { startVault(*c, true); return; }
      float sp = s.vel.length(); enterWall(c->normal, c->point, ((I.swing || I.sprint) && sp > 7) || sp > 18, sp); return;
    }
    float vn = s.vel.dot(c->normal);
    if (vn < 0) {
      s.vel.addScaled(c->normal, -vn);
      if (chaining && -vn > 3) { s.vel.addScaled(c->normal, clampf(-vn * 0.25f, 1.5f, 4)); s.wallCooldown = 0.25f; push("swingWallKick", clampf(-vn / 30, 0.1f, 0.5f)); }
    }
  }
  // swing attach (RMB held): search throttled; after a release wait for the apex / trick to play out
  if (s.jumpRelHold && (s.vel.y <= 0 || s.mode != "air")) s.jumpRelHold = false;
  if (I.swing && s.swingCooldown <= 0 && (!s.jumpRelHold || I.swingPressed)) {
    bool trickBusy = s.sub == "trick" && s.subT < std::max(0.62f, s.trickDur - 0.35f);
    bool ready = I.swingPressed || (s.groundSwing && s.airT > 0.14f) || (s.airT > 0.1f && s.vel.y < 5.5f && !trickBusy) || s.vel.y < -6;
    s.searchT -= h;
    if (ready && s.searchT <= 0 && !trickBusy) { s.searchT = 0.06f; if (tryStartSwing(I)) return; }
  }
  // landing
  float f = standAt(s.pos.x, s.pos.z, prevFeet + 0.05f);
  if (feetY() <= f && s.vel.y <= 0) { land(f, I); return; }
  if (s.sub == "vault" && s.subT < 0.3f) s.facing = std::atan2(s.vel.x, s.vel.z);
  s.apexY = std::max(s.apexY, feetY());
  // sub-state
  static const std::map<std::string, float> timedBase = {{"jumpLaunch", 0.16f}, {"release", 0.4f}, {"pointLaunch", 0.45f}, {"wallJump", 0.3f}, {"zipPull", 0.28f}, {"vault", 0.3f}};
  float lim = -1;
  if (s.sub == "trick") lim = s.trickDur > 0 ? s.trickDur : 0.85f;
  else { auto it = timedBase.find(s.sub); if (it != timedBase.end()) lim = it->second; }
  if (lim < 0 || s.subT > lim) {
    if (s.sub == "trick") s.trick.clear();
    if (s.dive || s.gliding) setSub("dive");
    else if (s.vel.y > 3) setSub("rise");
    else if (s.vel.y > -4) setSub("apex");
    else setSub(s.vel.y < -24 && !I.swing ? "dive" : "fall");
  }
}

// Water: splash and web-yank back onto the nearest dry ground on a ballistic arc
bool Traversal::waterBounce() {
  bool found = false; Vec3 best; float bd = INF;
  for (float r = 4; r <= 120 && !found; r += 4) for (int k = 0; k < 24; k++) {
    float a = k / 24.f * 2 * PI, x = s.pos.x + std::sin(a) * r, z = s.pos.z + std::cos(a) * r;
    float gy = world_.groundHeight(x, z, 200);
    if (gy > WATER_Y + 0.3f && gy < s.pos.y + 25 && r < bd) { bd = r; best = {x + std::sin(a) * 5, 0, z + std::cos(a) * 5}; found = true; }
  }
  if (!found) return false;
  best.y = world_.groundHeight(best.x, best.z, 200);
  Vec3 from{s.pos.x, feetY(), s.pos.z};
  float hd = std::hypot(best.x - from.x, best.z - from.z), tf = clampf(0.55f + hd / 20, 0.9f, 2.4f);
  s.vel = {(best.x - from.x) / tf, (best.y + 0.4f - from.y) / tf + 0.5f * G * tf, (best.z - from.z) / tf};
  s.pos.y = WATER_Y + H - 0.3f;
  setMode("air", "pointLaunch"); s.airT = 0; s.apexY = feetY(); s.swingCooldown = tf + 0.2f; s.wallCooldown = 0.3f;
  s.returnT = tf + 0.3f;
  s.facing = std::atan2(s.vel.x, s.vel.z); s.trick.clear(); s.dive = false; s.gliding = false; s.noAnchorT = 0;
  Vec3 tgt = best; tgt.y += 0.2f;
  web_.attach(rig_.handWorld('R'), tgt, {0, 1, 0}); s.dashWebT = 0.3f;
  push("waterSplash", clampf(-s.vel.y / 40, 0.2f, 1)); push("pointLaunch");
  return true;
}

void Traversal::land(float f, InputState& I) {
  s.airTrickUsed = false; s.airTapT = -9;
  if (f < WATER_Y && waterBounce()) return;
  s.noAnchorT = 0; s.gliding = false; s.groundSwing = false;
  float impact = -s.vel.y, drop = s.apexY - f;
  s.pos.y = f + H; s.floorY = f;
  Vec3 inD = inputDir(I); auto hv = hdir(s.vel); float hs = std::hypot(s.vel.x, s.vel.z);
  bool holding = inD.lengthSq() > 0.09f && hv && inD.normalized().dot(*hv) > 0.3f;
  float sev = clampf((std::max(impact, std::sqrt(std::max(0.f, drop) * 2 * G) * 0.8f) - 9) / 28, 0, 1);
  enterGround("idle");
  s.landing.severity = sev;
  if (impact < 7) { s.landing.lock = 0; if (impact > 4) setSub("landLight"); s.speed = hs; }
  else if (holding && hs > 8.5f && (impact > 13 || drop > 4)) { setSub("landRoll"); s.landing.lock = 0.35f; s.speed = std::min(hs * 0.85f, 16.f); s.facing = std::atan2(hv->x, hv->z); }
  else if (impact < 13 && drop < 6) { setSub("landLight"); s.landing.lock = 0; s.speed = holding ? hs : hs * 0.8f; }
  else if (impact < 23 && drop < 16) { setSub("landMedium"); s.landing.lock = holding ? 0.06f : 0.14f; s.speed = hs * (holding ? 0.7f : 0.45f); }
  else { setSub("landHard"); s.landing.lock = 0.42f; s.speed = 0; }
  s.charging = false; s.jumpCharge = 0; s.carry = {};
  TravEvent e; e.type = "land"; e.kind = s.sub; e.severity = s.sub == "idle" ? 0 : sev; events.push_back(e);
}

// corridor keeping: swing / air chains stay in the street canyon
void Traversal::corridor(float h, const Vec3& inD) {
  corr_.t -= h;
  if (corr_.t <= 0) {
    corr_.t = 0.05f; corr_.push = {};
    auto hv = hdir(s.vel); if (!hv) return;
    Vec3 right{-hv->z, 0, hv->x};
    float sp = std::hypot(s.vel.x, s.vel.z);
    for (float sgn : {1.f, -1.f}) for (float fwdK : {0.f, 0.5f}) {
      Vec3 d = (right * sgn + *hv * fwdK).normalized();
      Hit hit; if (!world_.raycast(s.pos, d, 16, hit) || std::fabs(hit.normal.y) > 0.5f) continue;
      float k = clampf((16 - hit.distance) / 11, 0, 1);
      corr_.push.addScaled(right, -sgn * k * k * (fwdK ? 0.6f : 1) * std::min(1.f, sp / 15));
    }
  }
  if (corr_.push.lengthSq() < 1e-4f) return;
  float want = inD.lengthSq() > 0.1f ? -inD.dot(corr_.push) / std::max(corr_.push.length(), 1e-3f) / std::max(inD.length(), 1e-3f) : -1;
  if (want > 0.5f) return;
  s.vel.addScaled(corr_.push, 24 * h);
}

// ------------------------------------------------------------------ swing
float Traversal::facadeAvoid(float h) {
  avoid_.t -= h;
  if (avoid_.t <= 0) {
    avoid_.t = 0.06f; avoid_.rate = 0;
    auto hv = hdir(s.vel); float hs = std::hypot(s.vel.x, s.vel.z);
    if (hv && hs > 6) {
      float look = clampf(hs * 0.9f, 8, 34);
      Hit hit;
      if (world_.raycast(s.pos, *hv, look, hit) && std::fabs(hit.normal.y) < 0.5f) {
        const Vec3& n = hit.normal; float into = -(hv->x * n.x + hv->z * n.z);
        if (into > 0.25f) {
          float tx = -n.z, tz = n.x, sd = signf(hv->x * tx + hv->z * tz); if (sd == 0) sd = 1;
          float urgency = clampf(1 - hit.distance / look, 0, 1);
          float cur = std::atan2(hv->x, hv->z), tgt = std::atan2(tx * sd + n.x * 0.3f, tz * sd + n.z * 0.3f);
          avoid_.rate = clampf(angWrap(tgt - cur), -1, 1) * (0.6f + 2.2f * urgency) * into;
        }
      }
    }
  }
  return avoid_.rate * h;
}
Vec3 Traversal::travelDir(const InputState& I) {
  auto hv = hdir(s.vel); Vec3 inD = inputDir(I);
  if (hv && std::hypot(s.vel.x, s.vel.z) > 4) return *hv;
  if (inD.lengthSq() > 0.05f) return inD.normalized();
  if (hv) return *hv;
  return cam_.forwardFlat();
}
bool Traversal::tryStartSwing(InputState& I) {
  Vec3 fwd = travelDir(I);
  { Vec3 inD = inputDir(I); if (inD.lengthSq() >= 0.02f) { Vec3 want = Vec3{inD.x, 0, inD.z}.normalized(); if (want.dot(fwd) > -0.5f) fwd.lerp(want, 0.6f).normalize(); } }
  Vec3 inD = inputDir(I); Vec3 turnV; const Vec3* turn = nullptr;
  if (inD.lengthSq() > 0.1f) { inD.normalize(); if (inD.dot(fwd) < 0.85f) { turnV = inD; turn = &turnV; } }
  float hs = std::hypot(s.vel.x, s.vel.z);
  float fl = floorAt(s.pos.x, s.pos.z, feetY() + 0.1f);
  auto a = anchors.find(s.pos, fwd, turn, s.vel.length(), fl);
  if (!a) { push("noAnchor"); s.noAnchorT += 0.06f; return false; }
  s.noAnchorT = 0;
  startSwing(*a, fwd, turn, hs); s.groundSwing = false;
  return true;
}
void Traversal::startSwing(const Anchor& a, Vec3 fwd, const Vec3* turn, float hs) {
  auto& S = s.swing;
  s.chain = s.sinceSwing <= CHAIN_BUF ? std::min(CHAIN_MAX, s.chain + 1) : 0;
  push("swingChain");
  S.anchor = a.point; S.normal = a.normal; S.kind = a.kind;
  S.dir = turn ? vlerp(fwd, *turn, 0.6f).normalized() : fwd;
  float dx = a.point.x - s.pos.x, dz = a.point.z - s.pos.z, along = dx * S.dir.x + dz * S.dir.z;
  S.pivot = {s.pos.x + S.dir.x * along, a.point.y, s.pos.z + S.dir.z * along};
  float L = s.pos.distanceTo(S.pivot);
  float fmax = -INF;
  for (float k : {0.f, 0.5f, 1.f, 1.4f}) { float x = s.pos.x + (S.pivot.x - s.pos.x) * k, z = s.pos.z + (S.pivot.z - s.pos.z) * k; fmax = std::max(fmax, floorAt(x, z, S.pivot.y - 2)); }
  float hEntry = s.pos.y - H - fmax;
  float bottomFeet = a.kind == "low" ? 3.0f : std::max(fmax < 1 ? 4.2f : 2.6f, clampf(8 + hEntry * 0.3f, 11, 18));
  if (a.kind != "low") bottomFeet = std::max(bottomFeet, hEntry - SWING_DIP);
  S.ropeTarget = std::max(4.f, std::min(L, S.pivot.y - fmax - H - bottomFeet));
  { // raise the virtual pivot instead of letting the arc scrape below the clearance line
    float y0 = s.pos.y, B = fmax + H + bottomFeet, dyc = y0 - B;
    float hd = std::hypot(S.pivot.x - s.pos.x, S.pivot.z - s.pos.z);
    if (dyc > 0.5f && S.pivot.y - L < B) {
      float u = std::min(70.f, (hd * hd - dyc * dyc) / (2 * dyc));
      if (u > S.pivot.y - y0) S.pivot.y = y0 + u;
      S.ropeTarget = std::max(S.ropeTarget, s.pos.distanceTo(S.pivot) - 1);
    }
  }
  S.rope = s.pos.distanceTo(S.pivot); S.t = 0; S.tension = 0; S.tautT = 0; S.y0 = s.pos.y;
  S.slack = 0; S.slackT = 0; S.kick = 0; S.kickCd = 0; S.apexed = false; S.angMax = -9;
  // momentum conservation: velocity rotated onto the arc tangent, keeping speed
  Vec3 rd = (S.pivot - s.pos).normalized();
  float sp = s.vel.length(), vr = s.vel.dot(rd);
  { Vec3 tan = s.vel - rd * vr;
    if (tan.lengthSq() > 0.01f) s.vel = tan.normalized() * (sp * (vr < 0 ? 0.96f : 1));
    else if (sp > 0.5f) s.vel = S.dir * sp; }
  if (hs < 11) { Vec3 yd = S.dir - rd * S.dir.dot(rd); if (yd.lengthSq() > 1e-3f) s.vel.addScaled(yd.normalized(), (11 - hs) * 0.7f); }
  capSpeed();
  Vec3 right{-S.dir.z, 0, S.dir.x};
  float lat = dx * right.x + dz * right.z;
  S.hand = std::fabs(lat) > 2 ? (lat > 0 ? 'R' : 'L') : (S.hand == 'R' ? 'L' : 'R');
  web_.attach(rig_.handWorld(S.hand), S.anchor, S.normal);
  setMode("swing", "swingLow"); s.trick.clear(); s.dive = false; s.airTrickUsed = false; s.airTapT = -9;
  push("swingStart");
}
float Traversal::swingPhase() const {
  const auto& S = s.swing;
  float along = (s.pos.x - S.pivot.x) * S.dir.x + (s.pos.z - S.pivot.z) * S.dir.z, below = S.pivot.y - s.pos.y;
  return clampf(std::atan2(along, std::max(below, 0.01f)) / 1.25f, -1, 1);
}
float Traversal::swingAngle() const {
  const auto& S = s.swing;
  float along = (s.pos.x - S.pivot.x) * S.dir.x + (s.pos.z - S.pivot.z) * S.dir.z;
  return std::atan2(along, S.pivot.y - s.pos.y);
}
void Traversal::stepSwing(float h, InputState& I) {
  auto& S = s.swing; S.t += h;
  if (!I.swing) { releaseSwing("manual", I); return; }
  if (I.jumpPressed) { leaveSwingOK_ = true; releaseSwing("jump", I); leaveSwingOK_ = false; s.swingCooldown = 0.35f; push("swingJump"); return; }
  s.vel.y -= GS * h;
  Vec3 rd = S.pivot - s.pos; float dist = rd.length(); rd /= dist;
  Vec3 inD = inputDir(I);
  // steering: the whole swing precesses about the vertical through the pivot toward the wanted heading
  Vec3 sideA{-S.dir.z, 0, S.dir.x};
  {
    float dyaw = 0;
    if (inD.lengthSq() >= 0.02f) {
      Vec3 want = Vec3{inD.x, 0, inD.z}.normalized();
      float cur = std::atan2(S.dir.x, S.dir.z), tgt = std::atan2(want.x, want.z), d = angWrap(tgt - cur);
      if (std::fabs(d) < 2.6f) dyaw = clampf(d, -1.7f * h, 1.7f * h) * clampf(std::fabs(d) / 0.25f, 0, 1);
    }
    dyaw += facadeAvoid(h);
    if (std::fabs(dyaw) > 1e-6f) {
      Quat q = Quat::axisAngle(UP, dyaw);
      S.dir = (q * S.dir).normalized(); s.vel = q * s.vel;
      Vec3 rel = q * (S.pivot - s.pos); S.pivot = s.pos + rel;
      sideA = {-S.dir.z, 0, S.dir.x};
    }
  }
  // never grind along a facade: a wall within ~2.5 m at the side pushes the body (and the virtual pivot) out
  { S.sideT -= h;
    if (S.sideT <= 0) { S.sideT = 0.05f; S.hasSideN = false;
      for (float sg : {1.f, -1.f}) { Hit hh; if (world_.raycast(s.pos, sideA * sg, 2.5f, hh) && std::fabs(hh.normal.y) < 0.5f) { S.sideN = Vec3{hh.normal.x, 0, hh.normal.z}.normalized(); S.hasSideN = true; S.sideK = 1 - hh.distance / 2.5f; } } }
    if (S.hasSideN) { s.vel.addScaled(S.sideN, 10 * S.sideK * h); S.pivot.addScaled(S.sideN, 3 * S.sideK * h); } }
  { float vl = s.vel.dot(sideA); s.vel.addScaled(sideA, -vl * (1 - std::exp(-2.5f * h))); }
  corridor(h, inD);
  float spd = s.vel.length();
  // pump: stick along the swing while moving forward on the arc (never beyond ~PUMP_MAX_ANG of arc)
  Vec3 tan = s.vel - rd * s.vel.dot(rd);
  float pushK = inD.lengthSq() > 0.01f ? clampf(inD.dot(S.dir) / std::max(inD.length(), 1e-3f), 0, 1) : 0;
  if (pushK > 0 && tan.lengthSq() > 0.01f && tan.dot(S.dir) > 0 && S.tautT > 0) {
    float E = 0.5f * spd * spd + GS * (s.pos.y - S.pivot.y), Ecap = -GS * S.rope * std::cos(PUMP_MAX_ANG);
    float room = clampf((Ecap - E) / (GS * 1.5f), 0, 1), bottom = std::max(0.f, rd.y);
    s.vel.addScaled(tan.normalized(), 9 * bottom * bottom * pushK * room * h);
  }
  // climb assist: every swing exits SWING_GAIN above where it attached
  if (pushK > 0 && tan.lengthSq() > 0.01f && s.vel.y > 0 && tan.dot(S.dir) > 0 && S.tautT > 0.1f) {
    float shortBy = S.y0 + SWING_GAIN - s.pos.y;
    if (shortBy > 0) s.vel.addScaled(tan.normalized(), std::min(shortBy, 3.f) * 4.5f * pushK * h);
  }
  // first-arc carry: a web "motor" guarantees a minimum speed up the front half of the FIRST arc
  if (!S.apexed) {
    float ang = swingAngle();
    if (ang > 0.15f && ang < 1.0f && S.tautT > 0.05f && S.tension > 0.05f) {
      Vec3 tg = S.dir * std::cos(ang) + UP * std::sin(ang);
      float vt = s.vel.dot(tg);
      if (vt > -1.5f) { float u = clampf((ang - 0.15f) / 0.85f, 0, 1); float vmin = clampf(0.65f * std::sqrt(GS * S.rope), 10, 18) * u * u * (3 - 2 * u); if (vt < vmin) s.vel.addScaled(tg, std::min(vmin - vt, 30 * h)); }
    }
  }
  s.vel *= std::max(0.f, 1 - SWING_DRAG * (1 - 0.08f * s.chain) * spd * h);
  if (spd > 37 + 3 * s.chain) s.vel *= 1 - 0.3f * h;
  // reel toward the target length (lifts off the street), faster near the floor
  float fl = floorAt(s.pos.x, s.pos.z, feetY() + 0.2f), clearance = feetY() - fl;
  if (clearance < 2.2f && s.vel.y < 0) S.ropeTarget = std::min(S.ropeTarget, std::max(3.f, S.pivot.y - (fl + 2.4f + H)));
  { float want = damp(S.rope, S.ropeTarget, clearance < 1.5f ? 10.f : (S.kind == "low" || clearance < 4) ? 6.f : 3.2f, h);
    S.rope = std::max(want, S.rope - (clearance < 3 ? 22.f : 14.f) * h); }
  float Lnow = s.pos.distanceTo(S.pivot);
  if (Lnow < S.rope && S.t < 0.6f) S.rope = std::max(S.ropeTarget, Lnow);
  else if (Lnow < S.rope && s.pos.y < S.pivot.y - 0.5f) S.rope = std::max(S.ropeTarget, std::max(Lnow, S.rope - 45 * h));
  capSpeed();
  s.pos += s.vel * h;
  // rope constraint (inequality: slack allowed)
  Vec3 d = s.pos - S.pivot; float L = d.length();
  float tension = 0, vrIn = 0;
  if (L > S.rope) {
    d /= L; s.pos = S.pivot + d * S.rope;
    float vr = s.vel.dot(d); if (vr > 0) { s.vel.addScaled(d, -vr); vrIn = vr; }
    float vt2 = s.vel.lengthSq(); tension = clampf((vt2 / std::max(S.rope, 1.f) + GS * std::max(0.f, -d.y)) / (GS * 2.6f), 0, 1);
  }
  // slack: over the top without enough speed the body free-falls inside the circle
  Vec3 du = (s.pos - S.pivot) / std::max(L, 1e-3f);
  float vd = s.vel.dot(du), vtan2 = s.vel.lengthSq() - vd * vd, need = vtan2 / std::max(L, 1.f) - GS * du.y;
  float slackT = tension < 0.05f ? std::max(clampf(-need / (GS * 0.35f), 0, 1), clampf((S.rope - L) / 1.0f, 0, 1)) : 0;
  if (slackT > 0.2f) S.slackT += h;
  else if (tension > 0.05f) {
    if (S.slackT > 0.18f && vrIn > 2) { tension = 1; push("ropeSnap", clampf(vrIn / 14, 0.15f, 1)); }
    S.slackT = 0;
  }
  S.slack = damp(S.slack, slackT, slackT > S.slack ? 6.f : 14.f, h);
  S.tension = damp(S.tension, tension, 12, h);
  if (tension > 0.05f) S.tautT += h;
  // wall contact: a real impact becomes a wall-skip along the facade (the web stays attached)
  S.kickCd -= h; S.kick = std::max(0.f, S.kick - h / 0.4f);
  auto c = collide(0.3f, R + 0.22f);
  if (c) {
    { float dn = (s.pos.x - S.pivot.x) * c->normal.x + (s.pos.z - S.pivot.z) * c->normal.z; if (dn > 0) { S.pivot.x += c->normal.x * (dn + 0.6f); S.pivot.z += c->normal.z * (dn + 0.6f); } }
    float vn = s.vel.dot(c->normal);
    if (vn < 0) {
      float sp0 = s.vel.length();
      s.vel.addScaled(c->normal, -vn);
      if (-vn > 3.5f && S.kickCd <= 0 && sp0 > 6) {
        Vec3 slide = s.vel; if (slide.lengthSq() > 1e-4f) slide.normalize();
        Vec3 along = S.dir - c->normal * S.dir.dot(c->normal); along.y = 0;
        if (along.lengthSq() > 1e-4f) slide.addScaled(along.normalized(), 0.45f);
        slide.addScaled(UP, 0.35f); slide.addScaled(c->normal, -slide.dot(c->normal));
        if (slide.lengthSq() < 1e-4f) slide = UP;
        slide.normalize();
        s.vel = slide * (sp0 * 0.88f) + c->normal * clampf(-vn * 0.12f, 1.5f, 3);
        S.kickCd = 0.35f; S.kick = 1;
        push("swingWallKick", clampf(-vn / 30, 0.1f, 0.6f));
      }
    }
  }
  // floor contact: never scrape — lift and keep going
  float f2 = floorAt(s.pos.x, s.pos.z, feetY() + 0.4f);
  if (feetY() < f2 + 0.3f) { s.pos.y = f2 + 0.3f + H; if (s.vel.y < 0) s.vel.y = 0; }
  S.phase = swingPhase(); S.angle = swingAngle();
  S.angMax = std::max(S.angMax, S.angle);
  if (!S.apexed && ((S.angle < S.angMax - 0.06f && S.angMax > 0.2f) || S.t > 4)) S.apexed = true;
  if (S.kick > 0.3f) setSub("wallKick");
  else if (S.slack > 0.5f) setSub("swingSlack");
  else setSub(S.phase < -0.28f ? "swingLow" : S.phase < 0.28f ? "swingBottom" : "swingHigh");
  web_.setSlack(S.slack, S.tension);
  ropeWrap(h);
}
// Rope wrap: a building between the body and the anchor re-anchors ahead (or wraps on the building edge)
void Traversal::ropeWrap(float h) {
  auto& S = s.swing;
  S.wrapT -= h; if (S.wrapT > 0) return;
  S.wrapT = 0.05f;
  Vec3 d = S.anchor - s.pos; float L = d.length(); if (L < 4) return;
  d /= L;
  Hit hit; if (!world_.raycast(s.pos, d, L - 1.5f, hit) || hit.distance < 1.5f) return;
  {
    float fl = floorAt(s.pos.x, s.pos.z, feetY() + 0.1f);
    auto a = anchors.find(s.pos, Vec3{S.dir.x, 0, S.dir.z}.normalized(), nullptr, s.vel.length(), fl);
    if (a && a->point.y > s.pos.y + 3) {
      S.anchor = a->point; S.normal = a->normal; S.kind = a->kind;
      float dx = a->point.x - s.pos.x, dz = a->point.z - s.pos.z, al = dx * S.dir.x + dz * S.dir.z;
      S.pivot = {s.pos.x + S.dir.x * al, a->point.y, s.pos.z + S.dir.z * al};
      float Ln = s.pos.distanceTo(S.pivot); S.rope = Ln; S.ropeTarget = std::max(4.f, Ln - 6);
      WebSystem::AttachOpt o; o.shootDur = 0.06f;
      web_.attach(rig_.handWorld(S.hand), S.anchor, S.normal, o);
      push("ropeReanchor"); return;
    }
  }
  Vec3 p = hit.point + hit.normal * 0.06f;
  S.anchor = p; S.normal = hit.normal;
  { float dx = p.x - s.pos.x, dz = p.z - s.pos.z, al = dx * S.dir.x + dz * S.dir.z; S.pivot = {s.pos.x + S.dir.x * al, p.y, s.pos.z + S.dir.z * al}; }
  float Ln = s.pos.distanceTo(S.pivot); S.rope = Ln; S.ropeTarget = std::min(S.ropeTarget, Ln);
  web_.retarget(p, hit.normal);
  push("ropeWrap");
}

std::string Traversal::chooseTrick(const InputState* I) {
  float sp = s.vel.length(), hs = std::hypot(s.vel.x, s.vel.z), vy = s.vel.y, steep = sp > 1 ? vy / sp : 0;
  Vec3 hv = hdir(s.vel).value_or(Vec3{std::sin(s.facing), 0, std::cos(s.facing)});
  Vec3 inD = I ? inputDir(*I) : lastInput_ ? inputDir(*lastInput_) : Vec3{};
  float lat = inD.z * hv.x - inD.x * hv.z;
  std::map<std::string, float> W = {{"tuckFlip", 1}, {"corkscrew", 1}, {"layout", 1}, {"scissor", 0.7f}};
  if (steep < 0.3f) { W["tuckFlip"] += 2.2f; W["corkscrew"] += 1.4f; W["layout"] = 0.6f; }
  else if (steep > 0.55f) { W["layout"] += 2.4f; W["tuckFlip"] = 0.25f; W["scissor"] = 0.4f; }
  if (hs > 22) { W["tuckFlip"] += 1; W["corkscrew"] += 0.6f; }
  if (vy < -2) W["layout"] = 0.3f;
  if (vy < -5) { W["layout"] = 0.1f; W["tuckFlip"] += 1; }
  if (std::fabs(lat) > 0.35f) W["corkscrew"] += 2;
  if (!s.lastTrickName.empty()) W[s.lastTrickName] = 0;
  float tot = 0; for (const char* k : TRICKS) tot += W[k];
  float r = rnd() * tot; std::string name = TRICKS[0];
  for (const char* k : TRICKS) { r -= W[k]; if (r <= 0) { name = k; break; } }
  s.trickLat = lat; s.trickSteep = steep;
  return name;
}
void Traversal::startTrick(const std::string& name) {
  const TrickDef& D = TRICK_DEF.at(name); float lat = s.trickLat;
  s.trick = name; s.lastTrickName = name;
  s.trickSide = name == "layout" ? ((s.trickSteep > 0.45f ? -1 : 1) * (rnd() < 0.2f ? -1 : 1))
              : std::fabs(lat) > 0.35f ? (int)signf(lat) : (rnd() < 0.5f ? 1 : -1);
  s.trickDur = D.dur; s.trickSnapT = D.dur * D.snap; s.trickBoosted = false; s.trickNoUp = false;
  setSub("trick");
}
void Traversal::trickBoost(const InputState& I) {
  s.trickBoosted = true;
  auto it = TRICK_DEF.find(s.trick); if (it == TRICK_DEF.end()) return;
  const TrickDef& D = it->second;
  float k = releaseBoostMul_;
  float sp0 = s.vel.length();
  Vec3 hv = hdir(s.vel).value_or(Vec3{std::sin(s.facing), 0, std::cos(s.facing)});
  if (D.steer > 0) {
    Vec3 inD = inputDir(I);
    if (inD.lengthSq() > 0.09f) {
      float d = angWrap(std::atan2(inD.x, inD.z) - std::atan2(hv.x, hv.z)), a = clampf(d, -D.steer, D.steer);
      float hs = std::hypot(s.vel.x, s.vel.z), na = std::atan2(hv.x, hv.z) + a;
      s.vel.x = std::sin(na) * hs; s.vel.z = std::cos(na) * hs; hv = {std::sin(na), 0, std::cos(na)};
    }
  }
  s.vel.x += hv.x * D.boost * k; s.vel.z += hv.z * D.boost * k;
  if (!s.trickNoUp) s.vel.y += D.up * k;
  if (D.side > 0 && std::fabs(s.trickLat) > 0.35f) { float sd = (float)s.trickSide; s.vel.x += -hv.z * D.side * sd * k; s.vel.z += hv.x * D.side * sd * k; }
  float lim = std::max(vmaxC(), sp0), sp = s.vel.length(); if (sp > lim) s.vel *= lim / sp;
  TravEvent e; e.type = "trickBoost"; e.trick = s.trick; events.push_back(e);
}
void Traversal::releaseSwing(const std::string& kind, InputState& I) {
  web_.release();
  float sp = s.vel.length();
  if (sp > 0.5f) s.vel *= (sp + releaseBoost() + CHAIN_REL * s.chain) / sp;
  float k = releaseBoost() / RELEASE_BOOST;
  Vec3 hv = hdir(s.vel).value_or(Vec3{std::sin(s.facing), 0, std::cos(s.facing)});
  if (kind != "jump") s.vel.y = std::min(std::max(s.vel.y, REL_UP_VY), std::max(s.vel.y + REL_UP * k, REL_UP * 0.75f * k));
  if (kind == "jump") {
    s.vel.x += hv.x * SWING_JUMP * k; s.vel.z += hv.z * SWING_JUMP * k;
    s.vel.y = std::min(SWING_JUMP_VY, std::max(s.vel.y + SWING_JUMP_UP, SWING_JUMP_UP * 0.85f));
    s.jumpRelHold = true;
  }
  setMode("air", "release"); s.airT = 0; s.apexY = feetY();
  s.swingCooldown = 0.05f; s.relT = 0;
  float hf = heightAboveFloor(); bool room = hf > 5 && s.vel.length() > 9 && (s.vel.y > -5 || hf > 14);
  if (room && (!s.lastTrick || rnd() < 0.8f)) { startTrick(chooseTrick(&I)); s.lastTrick = true; }
  else { s.trick.clear(); s.lastTrick = false; s.vel.x += hv.x * REL_NOTRICK * k; s.vel.z += hv.z * REL_NOTRICK * k; }
  s.trickNoUp = false;
  float hs = std::hypot(s.vel.x, s.vel.z), hl = std::max(vmaxC(), sp); if (hs > hl) { s.vel.x *= hl / hs; s.vel.z *= hl / hs; }
  TravEvent e; e.type = "release"; e.kind = kind; e.trick = s.trick; events.push_back(e);
}

// ------------------------------------------------------------------ wall
// a real facade (>= ~1 m wide, both sides of the contact present and coplanar)? poles / trunks are not wall-runnable
bool Traversal::wideWall(const Vec3& n, const Vec3& point) {
  float tx = -n.z, tz = n.x; int ok = 0;
  for (float sd : {-1.f, 1.f}) {
    Vec3 o{point.x + n.x * 0.5f + tx * sd * 0.5f, s.pos.y, point.z + n.z * 0.5f + tz * sd * 0.5f};
    Hit hit;
    if (world_.raycast(o, {-n.x, 0, -n.z}, 1.1f, hit) && std::fabs(hit.normal.y) < 0.5f && hit.normal.x * n.x + hit.normal.z * n.z > 0.8f) ok++;
  }
  return ok == 2;
}
void Traversal::enterWall(const Vec3& n, const Vec3& point, bool run, float speed) {
  auto& W = s.wall;
  W.normal = Vec3{n.x, 0, n.z}.normalized();
  s.pos.x = point.x + W.normal.x * (R + 0.02f); s.pos.z = point.z + W.normal.z * (R + 0.02f);
  W.runV = run ? clampf(std::max(speed * 0.8f, s.vel.y), WALLRUN * 0.9f, WALLRUN * 1.15f) : std::max(0.f, std::min(8.f, s.vel.y));
  W.fast = run; s.vel = {}; s.dive = false; s.trick.clear();
  W.up = {0, 1, 0}; W.off = 0; W.runK = 0; W.dist = R + 0.02f; W.point = {point.x, s.pos.y, point.z};
  setMode("wall", run ? "wallRun" : "crawl"); s.grounded = false; s.dashCount = 0;
  TravEvent e; e.type = "wall"; e.run = run; events.push_back(e);
}
Vec3 Traversal::wallBasis(const Vec3& n) const {
  Vec3 right = cam_.rightFlat(); right.addScaled(n, -right.dot(n));
  if (right.lengthSq() < 0.09f) right = Vec3{-n.z, 0, n.x} * -1.f;
  return right.normalized();
}
void Traversal::stepWall(float h, InputState& I) {
  auto& W = s.wall; Vec3& n = W.normal;
  Vec3 right = wallBasis(n);
  bool fast = I.sprint || I.swing || std::hypot(I.move.x, I.move.y) > 0.2f;
  float mx = I.move.x, my = I.move.y;
  if (W.hasLock) {
    if (signf(mx) == W.lockMx && std::fabs(mx) > 0.2f) {
      if (right.dot(W.lockDir) * W.lockMx > 0.8f) W.hasLock = false;
      else right = W.lockDir * W.lockMx;
    } else W.hasLock = false;
  }
  if (fast && std::hypot(mx, my) < 0.2f) my = 1;
  float zv = 0;
  if (s.sub == "wallZip") {
    W.zipT += h; float u = clampf(W.zipT / WZIP.dur, 0, 1);
    zv = WZIP.v0 + (WZIP.v1 - WZIP.v0) * u * u * (3 - 2 * u); mx = 0; my = 1;
    if (W.zipWeb && u >= WZIP.snap) { W.zipWeb = false; web_.releaseSnap(0.3f); }
    if (u >= 1) { bool go = I.sprint && I.move.y > 0.2f; W.runV = go ? WALLRUN * 1.1f : 3; setSub(go ? "wallRun" : "crawl"); }
  }
  W.move = {mx, my};
  float len = std::hypot(mx, my); if (len > 1) { mx /= len; my /= len; }
  float vx = (fast ? WALLRUN : 4.2f) * mx, vyIn = (fast ? WALLRUN : 4.2f) * my;
  W.runV = damp(W.runV, 0, fast && my > 0.2f ? 0.4f : std::hypot(mx, my) < 0.2f ? 9.f : 3.f, h);
  if (s.sub == "wallZip") { W.runV = zv; fast = true; }
  float vy = zv ? zv : std::max(vyIn, my >= -0.1f ? W.runV : -INF);
  s.vel = right * vx + UP * vy;
  W.fast = fast && (std::fabs(vx) + std::fabs(vy) > 5);
  W.phase += s.vel.length() * h / (W.fast ? 2.6f : 1.2f);
  std::string sub = W.fast ? (std::fabs(vy) >= std::fabs(vx) ? "wallRun" : "wallRunSide") : "crawl";
  if (s.sub != "wallZip" && (s.sub != "cornerWrap" || s.subT > 0.3f)) setSub(sub);
  if (s.vel.lengthSq() > 0.04f) W.up.lerp(s.vel.normalized(), 1 - std::exp(-8 * h)).normalize(); else W.up.lerp(UP, 1 - std::exp(-4 * h)).normalize();
  if (W.up.y < -0.2f) W.up.lerp(UP, 0.5f).normalize();
  if (I.jumpPressed) { // wall jump
    s.vel = n * 8.5f + UP * (fast ? 11.f : 9.5f) + right * (mx * 4);
    setMode("air", "wallJump"); s.airT = 0; s.apexY = feetY(); s.wallCooldown = 0.35f; s.swingCooldown = 0.18f;
    s.facing = std::atan2(s.vel.x, s.vel.z); push("wallJump"); return;
  }
  if (I.swingPressed) { // RMB on a wall: kick off it and swing away
    Vec3 cf = cam_.forwardFlat(); cf.addScaled(n, -cf.dot(n));
    s.vel = n * 9.f + UP * 7.f + cf * 6.f;
    setMode("air", "wallJump"); s.airT = 0; s.apexY = feetY(); s.wallCooldown = 0.5f; s.swingCooldown = 0.1f; s.groundSwing = true;
    s.facing = std::atan2(s.vel.x, s.vel.z); push("wallJump"); return;
  }
  if (I.dropPressed) { s.vel = n * 3.f; setMode("air", "fall"); s.airT = 0; s.apexY = feetY(); s.wallCooldown = 0.5f; return; }
  Vec3 prev = s.pos;
  s.pos += s.vel * h;
  // inner corner: wall ahead in the sideways direction
  if (std::fabs(vx) > 0.5f) {
    Vec3 side = right * signf(vx); Hit hit;
    if (world_.raycast(s.pos, side, R + 0.25f, hit) && std::fabs(hit.normal.y) < 0.5f && hit.normal.dot(side) < -0.7f) {
      Vec3 n1 = Vec3{hit.normal.x, 0, hit.normal.z}.normalized();
      Vec3 p1 = hit.point + n1 * (R + 0.02f); p1.y = s.pos.y; Vec3 d1 = n;
      startCornerWrap(n1, p1, 0.22f, &d1, signf(vx)); return;
    }
  }
  // wall top ahead: pop up and hop onto the roof
  if (vyIn + W.runV > 0.5f || vy > 0.5f) {
    Vec3 o = s.pos - n * (R + 0.25f); o.y = feetY() + 2.4f;
    Hit top; if (world_.raycast(o, {0, -1, 0}, 2.4f, top) && top.normal.y > 0.5f && top.point.y - feetY() < 1.35f) { if (startWallHop(n, fast || W.fast)) return; }
  }
  // stay attached: probe the wall at chest and knee height
  auto probe = [&](float oy, Hit& hh) { Vec3 o = s.pos; o.y += oy; return world_.raycast(o, -n, R + 0.9f, hh); };
  Hit hit; bool got = probe(0.35f, hit);
  if (!got || std::fabs(hit.normal.y) > 0.5f) got = probe(-0.5f, hit);
  if (got && std::fabs(hit.normal.y) < 0.5f) {
    Vec3 nn = Vec3{hit.normal.x, 0, hit.normal.z}.normalized();
    if (nn.dot(n) < 0.98f) n = nn;
    float bx = hit.point.x, bz = hit.point.z;
    float prot = std::min(0.12f, wallProtrusion(n, bx, bz, right, false));
    W.off = prot > W.off ? prot : damp(W.off, prot, 10, h);
    float off = R + 0.02f + W.off;
    s.pos.x = bx + n.x * off; s.pos.z = bz + n.z * off;
    W.point = {bx + n.x * W.off, s.pos.y, bz + n.z * W.off}; W.dist = R + 0.02f;
  } else {
    // outer corner: wrap around it (moving sideways)
    if (std::fabs(vx) > 0.5f && std::fabs(vx) >= std::fabs(vy)) {
      Vec3 side = right * signf(vx);
      Vec3 o = prev + side * (R + 0.8f) - n * (R + 0.8f); Hit hit2;
      if (world_.raycast(o, -side, 2, hit2) && hit2.normal.dot(side) > 0.7f) {
        Vec3 n1 = Vec3{hit2.normal.x, 0, hit2.normal.z}.normalized();
        Vec3 p1 = hit2.point + n1 * (R + 0.02f); p1.y = prev.y; Vec3 d1 = -n;
        startCornerWrap(n1, p1, 0.3f, &d1, signf(vx)); return;
      }
    }
    // top of the wall: vault / mantle onto the roof
    if (vy > -0.5f) {
      Vec3 o = prev - n * (R + 0.7f); o.y += 2.4f; Hit top;
      if (world_.raycast(o, {0, -1, 0}, 5.5f, top) && top.normal.y > 0.5f && startWallHop(n, fast || W.fast)) return;
    }
    if (s.sub == "wallZip" && s.zip.target.y > s.pos.y + 0.5f) return; // a recess mid-pull never drops him off
    if (std::fabs(vx) > 0.5f) {
      Vec3 side = right * signf(vx);
      Vec3 o = prev + side * (R + 0.8f) - n * (R + 0.8f); Hit hit2;
      if (world_.raycast(o, -side, 2, hit2) && hit2.normal.dot(side) > 0.7f) {
        Vec3 p1 = hit2.point + side * (R + 0.02f); p1.y = prev.y; Vec3 d1 = -n;
        startCornerWrap(side, p1, 0.3f, &d1, signf(vx)); return;
      }
    }
    s.vel = n * 2.f + UP * (std::max(0.f, vy) * 0.5f); setMode("air", "fall"); s.airT = 0; s.apexY = feetY(); s.wallCooldown = 0.3f; return;
  }
  // bottom: step off onto the street
  float gf = floorAt(s.pos.x + n.x * 0.6f, s.pos.z + n.z * 0.6f, feetY() + 0.3f);
  if (feetY() <= gf + 0.02f && vy <= 0) {
    s.pos.y = gf + H; s.pos.x += n.x * 0.15f; s.pos.z += n.z * 0.15f; s.vel = {}; s.facing = std::atan2(n.x, n.z);
    enterGround("idle"); s.wallCooldown = 0.5f; return;
  }
}
// E while running up / clinging to a wall: a zip up the facade (two webs)
void Traversal::wallZip() {
  auto& W = s.wall; Vec3 n = W.normal;
  Vec3 tgt = s.pos - n * (R + 0.02f + W.off); tgt.y += WZIP.reach;
  float top = -1;
  for (float dy = 2; dy <= WZIP.reach; dy += 2) {
    Vec3 o = s.pos; o.y += dy; Hit hit;
    if (world_.raycast(o, -n, R + 2.5f, hit) && std::fabs(hit.normal.y) < 0.5f) { top = dy; tgt = hit.point; } else if (top > 0) break;
  }
  if (top > 0 && top < WZIP.reach) tgt.y -= 0.5f;
  s.zip.target = tgt; W.zipT = 0; W.zipWeb = true; W.hasLock = false;
  Vec3 side = Vec3{-n.z, 0, n.x} * 0.35f;
  web_.attach(rig_.handWorld('R'), tgt - side, n);
  web_.attachSecond(rig_.handWorld('L'), tgt + side, n);
  setSub("wallZip"); s.facing = std::atan2(-n.x, -n.z); s.zipCooldown = WZIP.cd;
  push("wallZip");
}
float Traversal::wallProtrusion(const Vec3& n, float bx, float bz, const Vec3& right, bool side) {
  float best = 0; Vec3 dn = -n;
  auto test = [&](float ox, float oy) {
    Vec3 o{bx + n.x * 0.75f + right.x * ox, s.pos.y + oy, bz + n.z * 0.75f + right.z * ox};
    Hit hit; if (!world_.raycast(o, dn, 1.6f, hit) || std::fabs(hit.normal.y) > 0.6f) return;
    float d = (hit.point.x - bx) * n.x + (hit.point.z - bz) * n.z;
    if (d > best && d < 0.7f) best = d;
  };
  for (float oy : {-0.88f, -0.45f, 0.05f, 0.5f, 0.85f}) test(0, oy);
  if (side) for (float ox : {-0.45f, 0.45f}) for (float oy : {-0.6f, 0.3f}) test(ox, oy);
  return best;
}
void Traversal::startCornerWrap(const Vec3& n1, const Vec3& p1, float dur, const Vec3* dir1, float mx) {
  auto& W = s.wall; bool run = W.fast && s.sub == "wallRunSide";
  Vec3 mid = vlerp(s.pos, p1, 0.5f) + W.normal * 0.35f + n1 * 0.35f;
  float sp = s.vel.length();
  if (run) dur = clampf((s.pos.distanceTo(mid) + mid.distanceTo(p1)) / std::max(sp, 6.f), 0.1f, 0.3f);
  TravState::Kin k; k.type = "cornerWrap"; k.dur = dur; k.p0 = s.pos; k.p1 = mid; k.p2 = p1; k.n0 = W.normal; k.n1 = n1; k.run = run; k.sp = sp;
  if (dir1) { k.dir1 = *dir1; k.hasDir1 = true; }
  s.kin = k;
  if (dir1 && mx != 0) { W.lockDir = *dir1; W.hasLock = true; W.lockMx = mx; }
  if (!run) setSub("cornerWrap");
  TravEvent e; e.type = "cornerWrap"; e.run = run; events.push_back(e);
}
void Traversal::stepLedge(float h) {
  auto& k = *s.kin; k.t += h;
  Vec3 prev = s.pos;
  if (k.t < k.grab) { float u = k.t / k.grab, e = u * u * (3 - 2 * u); s.pos = vlerp(k.p0, k.hang, e); }
  else {
    if (s.sub != "ledgeClimb") setSub("ledgeClimb");
    float u = std::min(1.f, (k.t - k.grab) / k.climb);
    float ey = 1 - std::pow(1 - u, 2.2f), eh = u * u * (3 - 2 * u);
    s.pos = {k.hang.x + (k.end.x - k.hang.x) * eh, k.hang.y + (k.end.y - k.hang.y) * ey, k.hang.z + (k.end.z - k.hang.z) * eh};
    if (u >= 1) {
      Vec3 ev = k.exitVel; float fl = k.floor;
      s.kin.reset(); s.floorY = floorAt(s.pos.x, s.pos.z, fl + 0.3f); s.pos.y = s.floorY + H;
      s.vel = ev; s.speed = ev.length(); enterGround(s.speed > 1 ? "run" : "idle"); s.speed = ev.length(); s.wallCooldown = 0.35f;
      return;
    }
  }
  s.vel = (s.pos - prev) / std::max(h, 1e-4f);
}
// Wall-top hop: pop straight up until the feet clear the lip, then a short forward jump onto the roof
bool Traversal::startWallHop(const Vec3& n, bool fast) {
  auto& W = s.wall; Vec3 inward = Vec3{-n.x, 0, -n.z}.normalized();
  bool run = s.mode == "wall" && ((s.sub == "wallRun" && W.fast) || s.sub == "wallZip");
  float vy0 = run ? std::max(0.f, s.vel.y) : 0;
  if (run) { // a vertical wall RUN reaching the top launches him into the air
    float vUp = clampf(std::max(vy0, 12.f), 12, 15);
    s.vel = inward * 2.6f + UP * vUp;
    s.facing = std::atan2(inward.x, inward.z);
    setMode("air", "jumpLaunch"); s.airT = 0; s.apexY = feetY(); s.grounded = false; s.jumpCharge = 1;
    s.wallCooldown = 0.9f; s.swingCooldown = 0.15f; s.kin.reset();
    push("wallLaunch"); TravEvent e; e.type = "jump"; e.charge = 1; events.push_back(e);
    return true;
  }
  float wallDist = R + 0.02f + W.off, f0 = feetY();
  const float D[] = {0.08f, 0.25f, 0.45f, 0.7f, 1.0f, 1.35f, 1.75f, 2.2f, 2.7f};
  float prof[9];
  for (int i = 0; i < 9; i++) {
    Vec3 o = s.pos + inward * (wallDist + D[i]); o.y = f0 + 6; Hit hit;
    prof[i] = world_.raycast(o, {0, -1, 0}, 14, hit) && hit.normal.y > 0.3f ? hit.point.y : -INF;
  }
  float floor = INF; bool any = false;
  for (int i = 3; i < 9; i++) if (prof[i] > -INF) { floor = std::min(floor, prof[i]); any = true; }
  if (!any) return false;
  if (floor < f0 - 3) return false;
  float obstD = -1, obstTop = floor;
  for (int i = 0; i < 9; i++) if (prof[i] > floor + 0.2f && D[i] < 1.6f) { obstD = D[i]; if (prof[i] > obstTop) obstTop = prof[i]; }
  float landD = std::max(1.25f, obstD + 0.95f);
  Vec3 landP = s.pos + inward * (wallDist + landD);
  float landTop = floorAt(landP.x, landP.z, std::max(obstTop, floor) + 0.3f);
  if (!(landTop > f0 - 3)) return false;
  float apex = std::max(std::max(obstTop, landTop) + 0.45f, f0 + 0.35f);
  float Dtot = wallDist + landD, tA = 0, tB = 0, v = 0;
  for (int k = 0; k < 4; k++) {
    tA = std::sqrt(2 * (apex - f0) / G);
    tB = std::max(0.28f, std::sqrt(2 * std::max(0.05f, apex - landTop) / G));
    v = Dtot / tB;
    if (obstD < 0) break;
    float tc = (wallDist + obstD + R) / v, drop = 0.5f * G * tc * tc;
    if (apex - drop >= obstTop + 0.1f) break;
    apex = obstTop + 0.1f + drop + 0.05f;
  }
  TravState::Kin k; k.type = "wallHop"; k.tA = tA; k.tB = tB; k.f0 = f0; k.apex = apex; k.landTop = landTop; k.p0 = s.pos; k.inward = inward; k.Dtot = Dtot; k.fast = fast;
  k.exitSpeed = fast ? 5.5f : 1.8f;
  s.kin = k;
  s.facing = std::atan2(inward.x, inward.z);
  setMode("air", "jumpLaunch"); s.airT = 0; s.apexY = apex; s.grounded = false; s.wallCooldown = 0.6f; s.swingCooldown = 0.25f;
  s.jumpCharge = fast ? 0.35f : 0.1f;
  push("wallHop");
  return true;
}
void Traversal::stepWallHop(float h) {
  auto& k = *s.kin; k.t += h;
  Vec3 prev = s.pos; float fy, d;
  if (k.t <= k.tA) {
    float vy0 = G * k.tA, t = k.t; fy = k.f0 + vy0 * t - 0.5f * G * t * t; d = 0;
    if (s.sub != "jumpLaunch" && k.t < 0.12f) setSub("jumpLaunch"); else if (k.t >= 0.12f) setSub("rise");
  } else {
    float t = std::min(k.t - k.tA, k.tB); fy = k.apex - 0.5f * G * t * t; d = k.Dtot * (t / k.tB);
    setSub(t < 0.12f ? "apex" : "fall");
    if (k.t - k.tA >= k.tB || fy <= k.landTop) {
      TravState::Kin kk = k; s.kin.reset();
      s.pos = kk.p0 + kk.inward * kk.Dtot; s.floorY = floorAt(s.pos.x, s.pos.z, kk.landTop + 0.3f); s.pos.y = s.floorY + H;
      s.vel = kk.inward * kk.exitSpeed; s.facing = std::atan2(kk.inward.x, kk.inward.z);
      enterGround("landLight"); s.speed = kk.exitSpeed; s.landing.severity = 0.15f; s.landing.lock = 0; s.wallCooldown = 0.4f;
      push("land", 0.15f);
      return;
    }
  }
  s.pos = {k.p0.x + k.inward.x * d, fy + H, k.p0.z + k.inward.z * d};
  s.vel = (s.pos - prev) / std::max(h, 1e-4f);
}
void Traversal::startVault(const Contact& c, bool fast) { // ground mantle over a low obstacle / parapet
  Vec3 n = c.normal; float top = c.top; Vec3 inward = -n;
  float onTop = floorAt(s.pos.x - n.x * 1.3f, s.pos.z - n.z * 1.3f, top + 0.05f);
  Vec3 land = s.pos + inward * (1.3f + R);
  float beyond = floorAt(land.x, land.z, top + 0.05f);
  if (std::fabs(onTop - top) > 0.1f && beyond < top - 2.2f) { // roof edge / parapet with a drop behind: leap over it
    float sp = std::max(std::hypot(s.vel.x, s.vel.z), 8.f);
    s.pos.y = std::max(s.pos.y, top + 0.15f + H);
    s.vel = inward * sp + UP * 6.5f;
    s.facing = std::atan2(inward.x, inward.z);
    setMode("air", "vault"); s.airT = 0; s.apexY = feetY(); s.swingCooldown = 0.1f; push("vault");
    return;
  }
  land.y = (std::fabs(onTop - top) < 0.1f ? top : beyond) + H;
  Vec3 p0 = s.pos, ctrl = vlerp(p0, land, 0.5f); ctrl.y = top + H + 0.5f;
  float sp = std::max(std::hypot(s.vel.x, s.vel.z), fast ? 10.f : 4.f);
  TravState::Kin k; k.type = "vault"; k.dur = clampf(1.6f / sp, 0.22f, 0.42f); k.p0 = p0; k.p1 = ctrl; k.p2 = land; k.exitVel = inward * (sp * 0.9f); k.floor = land.y - H;
  s.kin = k;
  s.facing = std::atan2(-n.x, -n.z);
  setMode("ground", "vault"); push("vault");
}
void Traversal::stepKin(float h) {
  auto& k = *s.kin; k.t += h / k.dur; float u = std::min(1.f, k.t);
  float e = k.type == "zip" ? zipEase(u) : u * u * (3 - 2 * u), a = 1 - e;
  s.pos = k.p0 * (a * a) + k.p1 * (2 * a * e) + k.p2 * (e * e);
  if (k.type == "cornerWrap") {
    s.wall.normal = vlerp(k.n0, k.n1, e).normalized();
    Vec3 pv = k.p1 * (2 * a) - k.p0 * (2 * a) + k.p2 * (2 * e) - k.p1 * (2 * e);
    if (pv.lengthSq() > 1e-6f) s.vel = pv.normalized() * (k.run ? k.sp : 2.f);
  }
  if (u >= 1) {
    TravState::Kin kk = k; s.kin.reset();
    if (kk.type == "cornerWrap") {
      s.wall.normal = kk.n1;
      if (kk.run && kk.hasDir1) { s.vel = kk.dir1 * kk.sp; setSub("wallRunSide"); } else setSub("crawl");
    } else if (kk.type == "vault") {
      s.floorY = floorAt(s.pos.x, s.pos.z, feetY() + 0.3f); s.pos.y = s.floorY + H;
      s.speed = kk.exitVel.length(); s.vel = kk.exitVel; if (s.speed > 0.1f) s.facing = std::atan2(kk.exitVel.x, kk.exitVel.z);
      enterGround(s.speed > RUN + 1.5f ? "sprint" : s.speed > 1 ? "run" : "idle"); s.speed = kk.exitVel.length(); s.wallCooldown = 0.35f;
    } else if (kk.type == "zip") arriveZip();
  }
}

// ------------------------------------------------------------------ zip / perch / point launch
// Web-zip: zipFire (both webs fire, ~0.05 s) -> zipFlight (burst ramp to a 40-72 m/s peak, webs snap off at ~25%)
// -> braking -> zipCatch -> perch.
void Traversal::startZip(const ZipTarget& t) {
  auto& Z = s.zip;
  Z.target = t.pos; Z.normal = t.normal; Z.kind = t.kind; Z.launch = false; Z.dash = false; Z.webs = true; Z.taut = 0; Z.t = 0; Z.u = 0;
  bool horiz = std::hypot(t.normal.x, t.normal.z) > 0.3f;
  Vec3 end = t.pos; if (horiz) end.addScaled(Vec3{t.normal.x, 0, t.normal.z}.normalized(), -0.12f); end.y += H;
  Z.p2 = end;
  float dist = s.pos.distanceTo(end);
  Vec3 los = (t.pos - s.pos).normalized();
  Vec3 zipOff = los.cross(UP); if (zipOff.lengthSq() < 1e-4f) zipOff = {1, 0, 0}; zipOff = zipOff.normalized() * 0.09f;
  Vec3 nrm = horiz ? t.normal : UP;
  Vec3 aR = t.pos + t.normal * 0.05f - zipOff, aL = t.pos + t.normal * 0.05f + zipOff;
  float shoot = clampf(dist / 900, 0.03f, 0.05f);
  WebSystem::AttachOpt o1; o1.shootDur = shoot; WebSystem::AttachOpt o2; o2.shootDur = shoot + 0.015f;
  web_.attach(rig_.handWorld('R'), aR, nrm, o1);
  web_.attachSecond(rig_.handWorld('L'), aL, nrm, o2);
  Z.fromGround = s.grounded;
  Z.fireDur = shoot + 0.015f;
  s.facing = std::atan2(end.x - s.pos.x, end.z - s.pos.z);
  setMode("zip", "zipFire"); s.charging = false; s.dive = false; s.trick.clear(); s.gliding = false;
  if (Z.fromGround) s.vel *= 0.35f;
  push("zip");
}
Vec3 Traversal::bez(float e) const { const auto& Z = s.zip; float a = 1 - e; return Z.p0 * (a * a) + Z.p1 * (2 * a * e) + Z.p2 * (e * e); }
void Traversal::zipCurve(const Vec3& vel) {
  auto& Z = s.zip;
  const Vec3 p0 = Z.p0, end = Z.p2; float dist = p0.distanceTo(end);
  Z.p1 = vlerp(p0, end, 0.62f);
  Z.p1.y = end.y + 0.4f + (p0.y > end.y ? 0 : dist * 0.03f);
  Vec3 hn{Z.normal.x, 0, Z.normal.z};
  if (hn.lengthSq() > 0.09f) Z.p1.addScaled(hn.normalized(), std::min(3.f, dist * 0.08f));
  float sp = vel.length();
  if (sp > 4) { Vec3 vd = vel / sp, toT = (end - p0).normalized(); if (vd.dot(toT) > -0.2f) Z.p1.lerp(p0 + vd * (dist * 0.5f), clampf(sp / 30, 0, 0.55f)); }
  for (int it = 0; it < 5 && !zipClear(); it++) {
    Z.p1.y += 2.5f + dist * 0.05f;
    Vec3 out{Z.normal.x, 0, Z.normal.z}; if (out.lengthSq() < 0.09f) { out = p0 - end; out.y = 0; } if (out.lengthSq() > 1e-4f) Z.p1.addScaled(out.normalized(), 1.5f);
  }
  // speed profile: arc-length LUT, burst ramp -> cruise -> constant braking to ZIP_VEND at the perch
  Vec3 q = p0; Z.lut[0] = 0;
  for (int k = 1; k <= 32; k++) { Vec3 b = bez(k / 32.f); Z.lut[k] = Z.lut[k - 1] + b.distanceTo(q); q = b; }
  float L = Z.lut[32]; Z.len = L;
  float v0 = clampf(vel.dot((Z.p1 - p0).normalized()), 0, 20);
  float vP = clampf(22 + L * 2.4f, 40, 72) * ZIP_SPEED;
  for (int it = 0; it < 12; it++) {
    float dR = (v0 + vP) / 2 * ZIP_RAMP, dB = (vP * vP - ZIP_VEND * ZIP_VEND) / (2 * ZIP_BRAKE);
    if (dR + dB <= L * 0.92f || vP <= 12) break; vP *= 0.88f;
  }
  float dR = (v0 + vP) / 2 * ZIP_RAMP, dB = std::max(0.f, (vP * vP - ZIP_VEND * ZIP_VEND) / (2 * ZIP_BRAKE));
  float dC = std::max(0.f, L - dR - dB);
  Z.v0 = v0; Z.vP = vP; Z.dR = dR; Z.dC = dC; Z.dB = L - dR - dC;
  Z.tC = ZIP_RAMP + dC / vP; Z.tBrake = Z.tC;
  float vE = std::sqrt(std::max(0.f, vP * vP - 2 * ZIP_BRAKE * Z.dB)), aB = Z.dB > 1e-3f ? (vP * vP - vE * vE) / (2 * Z.dB) : ZIP_BRAKE;
  Z.aB = aB; Z.dur = Z.tC + (vP - vE) / std::max(aB, 1e-3f);
}
float Traversal::zipDist(float tau) const {
  const auto& Z = s.zip;
  if (tau <= ZIP_RAMP) return Z.v0 * tau + (Z.vP - Z.v0) / ZIP_RAMP * tau * tau / 2;
  if (tau <= Z.tC) return Z.dR + Z.vP * (tau - ZIP_RAMP);
  float tb = std::min(tau, Z.dur) - Z.tC;
  return std::min(Z.len, Z.dR + Z.dC + Z.vP * tb - Z.aB * tb * tb / 2);
}
float Traversal::zipArcE(float d) const {
  const float* T = s.zip.lut; if (d >= T[32]) return 1; if (d <= 0) return 0;
  int k = 1; while (k < 32 && T[k] < d) k++;
  return (k - 1 + (d - T[k - 1]) / std::max(1e-6f, T[k] - T[k - 1])) / 32;
}
bool Traversal::zipClear() {
  const auto& Z = s.zip; Vec3 a = Z.p0;
  for (int k = 1; k <= 10; k++) {
    Vec3 b = bez(k / 10.f); if (k == 10) break;
    Vec3 d = b - a; float L = d.length();
    if (L > 1e-3f) { Hit hh; if (world_.raycast(a, d / L, L + 0.4f, hh) && hh.point.distanceTo(Z.p2) > 1.6f) return false; }
    a = b;
  }
  return true;
}
void Traversal::stepZip(float h, InputState& I) {
  auto& Z = s.zip;
  if (I.jumpPressed && (s.sub == "zipCatch" || (s.sub == "zipFlight" && (1 - Z.u) * Z.dur < 0.35f))) Z.launch = true;
  if (I.swingPressed && (s.sub == "zipFlight" || s.sub == "zipCatch")) { // RMB cancels the zip into a swing
    web_.release(); Z.webs = false; setMode("air", "fall"); s.airT = 0.2f; s.apexY = feetY(); s.swingCooldown = 0; s.relT = 0.4f;
    push("zipCancel");
    if (!tryStartSwing(I)) s.groundSwing = true;
    return;
  }
  if (s.sub == "zipFire" || s.sub == "zipYank") {
    if (Z.fromGround) { s.vel *= std::exp(-7 * h); s.vel.y = 0; }
    else { s.vel *= std::exp(-0.8f * h); s.vel.y -= 6 * h; }
    s.pos += s.vel * h;
    if (!Z.fromGround) { auto c = collide(0.3f); if (c) { float vn = s.vel.dot(c->normal); if (vn < 0) s.vel.addScaled(c->normal, -vn); } }
    if (s.subT >= Z.fireDur) {
      Z.taut = 1; web_.setTaut(1);
      Z.p0 = s.pos; zipCurve(s.vel); Z.u = 0; Z.tau = 0;
      setSub("zipFlight"); s.grounded = false;
      TravEvent e; e.type = "zipLaunch"; e.dist = Z.p0.distanceTo(Z.p2); events.push_back(e);
    }
    Z.t = 0;
    return;
  }
  Vec3 prev = s.pos;
  Z.tau += h; Z.u = std::min(1.f, Z.tau / Z.dur);
  s.pos = bez(zipArcE(zipDist(Z.tau)));
  s.vel = (s.pos - prev) / std::max(h, 1e-4f);
  Z.t = Z.u;
  { float sp = s.vel.length(); if (sp > 0.5f) { Z.pitch = std::asin(clampf(s.vel.y / sp, -1, 1)); Z.flightDir = s.vel / sp; Z.hasFlightDir = true; } }
  if (Z.webs && Z.u >= 0.25f) { Z.webs = false; web_.setTaut(0); web_.releaseSnap(0.3f); push("zipWebRelease"); }
  if (s.sub == "zipFlight" && Z.tau >= Z.tBrake) setSub("zipCatch");
  if (Z.u >= 1) arriveZip();
}
void Traversal::arriveZip() {
  auto& Z = s.zip; web_.release();
  Vec3 travelV = s.vel;
  if (Z.launch) { anchorLaunch(travelV); return; }
  auto& P = s.perch; P.pos = Z.target; P.normal = Z.normal; P.kind = Z.kind;
  { Vec3 hn2{Z.normal.x, 0, Z.normal.z};
    if (hn2.lengthSq() > 0.09f) P.edge = Vec3{-hn2.z, 0, hn2.x}.normalized(); else P.edge = {std::cos(s.facing), 0, -std::sin(s.facing)}; }
  P.radius = Z.kind == "lampTop" ? 0.25f : Z.kind == "waterTower" ? 1.2f : 0;
  Vec3 hn{Z.normal.x, 0, Z.normal.z};
  if (hn.lengthSq() > 0.09f) { hn.normalize(); s.facing = std::atan2(hn.x, hn.z); } else if (travelV.lengthSq() > 1) s.facing = std::atan2(travelV.x, travelV.z);
  P.roof = std::fabs(floorAt(Z.target.x - hn.x * 1.0f, Z.target.z - hn.z * 1.0f, Z.target.y + 0.2f) - Z.target.y) < 0.25f;
  P.impact = travelV; P.hasImpact = true;
  s.pos = Z.p2; s.vel = {}; s.floorY = Z.target.y;
  setMode("perch", "perchLand"); s.grounded = true; s.dashCount = 0;
  s.landing.severity = clampf(travelV.length() / 60, 0.1f, 0.45f);
  push("perch", s.landing.severity);
}
void Traversal::anchorLaunch(const Vec3& travelV) { // Space at a zip arrival: vault off the anchor keeping momentum
  Vec3 hv{travelV.x, 0, travelV.z}; float hs = hv.length();
  if (hs < 2) { hv = cam_.forwardFlat(); hs = 0; } else hv /= hs;
  if (lastInput_) { Vec3 inD = inputDir(*lastInput_); if (inD.lengthSq() > 0.09f) { inD.y = 0; hv.lerp(inD.normalized(), 0.35f).normalize(); } }
  float fwd = std::min(VMAX - 6, std::max(hs, 12.f) + 6);
  s.vel = hv * fwd + UP * (13 + std::max(0.f, travelV.y) * 0.3f);
  setMode("air", "pointLaunch"); s.airT = 0; s.apexY = feetY(); s.swingCooldown = 0.3f; s.wallCooldown = 0.3f; s.relT = 0;
  s.facing = std::atan2(hv.x, hv.z); s.trick.clear(); s.grounded = false;
  push("pointLaunch");
}
void Traversal::pointLaunch(const Vec3& normal, const Vec3& travelV) {
  Vec3 inD = lastInput_ ? inputDir(*lastInput_) : Vec3{};
  Vec3 dir = inD.lengthSq() > 0.09f ? inD.normalized() : cam_.forwardFlat();
  auto tv = hdir(travelV); if (tv && tv->dot(dir) > 0) dir.lerp(*tv, 0.25f).normalize();
  Vec3 out{normal.x, 0, normal.z}; if (out.lengthSq() > 0.09f && out.normalized().dot(dir) > -0.3f) { out.normalize(); dir.lerp(out, 0.2f).normalize(); }
  auto blocked = [&](const Vec3& d) { Vec3 o = s.pos; o.y += 1.5f; Hit hh; return world_.raycast(o, Vec3{d.x, 0.45f, d.z}.normalized(), 14, hh) && std::fabs(hh.normal.y) < 0.6f; };
  if (blocked(dir)) {
    bool found = false; Vec3 bestD; float bestA = 9, a0 = std::atan2(dir.x, dir.z);
    for (float da : {0.4f, -0.4f, 0.8f, -0.8f, 1.2f, -1.2f, 1.6f, -1.6f, 2.2f, -2.2f, PI}) {
      Vec3 d{std::sin(a0 + da), 0, std::cos(a0 + da)}; if (!blocked(d) && std::fabs(da) < bestA) { bestA = std::fabs(da); bestD = d; found = true; }
    }
    if (found) dir = bestD; else if (out.lengthSq() > 0.09f) dir = out.normalized();
  }
  float carry = std::min(12.f, std::hypot(travelV.x, travelV.z) * 0.25f);
  s.vel = dir * (15 + carry) + UP * 20.5f;
  setMode("air", "pointLaunch"); s.airT = 0; s.apexY = feetY(); s.swingCooldown = 0.35f; s.wallCooldown = 0.3f;
  s.facing = std::atan2(dir.x, dir.z); s.trick.clear();
  push("pointLaunch");
}
void Traversal::webDash() { // forward air dash when no zip point is targeted
  Vec3 f = cam_.forwardFlat();
  float hs = std::hypot(s.vel.x, s.vel.z), sp = std::min(std::max(hs + 8, 22.f), VMAX);
  s.vel.x = f.x * sp; s.vel.z = f.z * sp; s.vel.y = std::max(s.vel.y, 3.5f);
  Vec3 tgt = s.pos + f * 16; tgt.y += 3;
  s.zip.target = tgt; s.zip.t = 0; s.zip.dash = true;
  web_.attach(rig_.handWorld('R'), tgt, -f);
  s.dashWebT = 0.22f;
  setMode("air", "zipPull"); s.airT = 0.2f; s.zipCooldown = 0.45f; s.dashCount++; s.facing = std::atan2(f.x, f.z); s.trick.clear();
  push("webDash");
}

// ------------------------------------------------------------------ quick web boost (Q / L1, air only)
Traversal::QA Traversal::quickAnchor(Vec3& out, Vec3& nOut) {
  Vec3 L = cam_.forward();
  float yaw = std::atan2(L.x, L.z);
  if (lastInput_) { Vec3 inD = inputDir(*lastInput_); if (inD.lengthSq() > 0.09f) yaw = std::atan2(inD.x, inD.z); }
  float el0 = clampf(std::asin(clampf(L.y, -1, 1)) + 0.14f, 0.06f, 0.6f);
  Vec3 o = s.pos; o.y += 0.5f;
  bool haveBest = false, haveNear = false; float bestS = -INF; Vec3 bp, bn, np, nn; float bd = 0, nd = 0;
  const float offs[][2] = {{0, 0}, {0.16f, 0}, {-0.06f, 0}, {0, 0.2f}, {0, -0.2f}, {0.16f, 0.2f}, {0.16f, -0.2f}, {0.34f, 0}, {0.34f, 0.3f}, {0.34f, -0.3f}};
  for (auto& od : offs) {
    float el = el0 + od[0], a = yaw + od[1];
    Vec3 d{std::sin(a) * std::cos(el), std::sin(el), std::cos(a) * std::cos(el)};
    Hit hit; if (!world_.raycast(o, d, QUICK.maxD, hit) || (hit.normal.y > 0.7f && hit.point.y < s.pos.y - 1)) continue;
    if (hit.distance >= QUICK.minD) { float sc = -std::fabs(od[1]) * 3 - std::fabs(od[0]) * 1.5f; if (sc > bestS) { bestS = sc; bp = hit.point; bn = hit.normal; bd = hit.distance; haveBest = true; } }
    else if (hit.distance >= QUICK.nearD && (!haveNear || hit.distance > nd)) { np = hit.point; nn = hit.normal; nd = hit.distance; haveNear = true; }
  }
  if (haveBest) { out = bp; nOut = bn; return {bd, false}; }
  if (haveNear) { out = np; nOut = nn; return {nd, false}; }
  float el = el0 + 0.1f; Vec3 d{std::sin(yaw) * std::cos(el), std::sin(el), std::cos(yaw) * std::cos(el)};
  out = o + d * 60; nOut = -d;
  return {60, true};
}
void Traversal::quickBoostStart() {
  auto& Q = s.quick; bool fromSwing = s.mode == "swing";
  if (fromSwing) {
    leaveSwingOK_ = true; web_.release(); setMode("air", s.vel.y > 3 ? "rise" : s.vel.y > -4 ? "apex" : "fall"); leaveSwingOK_ = false;
    s.airT = 0; s.apexY = feetY(); s.relT = 0; s.trick.clear(); s.lastTrick = false;
  }
  QA r = quickAnchor(Q.anchor, Q.normal);
  char hand0 = Q.hand;
  if (s.clock - Q.last > 1.6f) Q.n = 0;
  if (fromSwing) Q.hand = s.swing.hand == 'L' ? 'R' : 'L';
  else if (Q.n > 0) Q.hand = hand0 == 'L' ? 'R' : 'L';
  else { Vec3 rel = Q.anchor - s.pos; float f = std::sin(s.facing) * rel.z - std::cos(s.facing) * rel.x; Q.hand = f > 0 ? 'R' : 'L'; }
  const float ks[] = {1, 0.8f, 0.65f, 0.55f};
  Q.k = ks[std::min(Q.n, 3)];
  Q.n++; Q.last = s.clock; Q.seq++;
  Q.dist = r.dist; Q.sky = r.sky; Q.t = 0; Q.applied = false; Q.active = true; Q.webOn = true;
  Q.hitT = clampf(r.dist / 600, 0.05f, 0.11f);
  s.dashWebT = 0;
  WebSystem::AttachOpt o; o.shootDur = Q.hitT; o.noSplat = r.sky;
  web_.attach(rig_.handWorld(Q.hand), Q.anchor, Q.normal, o);
  if (s.sub != "rise" && s.sub != "apex" && s.sub != "fall" && s.sub != "release") { s.trick.clear(); setSub(s.vel.y > 3 ? "rise" : s.vel.y > -4 ? "apex" : "fall"); }
  s.swingCooldown = std::max(s.swingCooldown, Q.hitT + 0.28f);
  s.zipCooldown = std::max(s.zipCooldown, 0.15f);
  TravEvent e; e.type = "quickZip"; e.dist = r.dist; e.k = Q.k; events.push_back(e);
}
void Traversal::quickImpulse() {
  auto& Q = s.quick;
  Vec3 d = Q.anchor - s.pos; float L = d.length(); if (L > 1e-3f) d /= L; else d = {std::sin(s.facing), 0, std::cos(s.facing)};
  Vec3 hd = hdir(d).value_or(Vec3{std::sin(s.facing), 0, std::cos(s.facing)});
  float hs0 = std::hypot(s.vel.x, s.vel.z);
  Vec3 cur = hs0 > 1 ? Vec3{s.vel.x / hs0, 0, s.vel.z / hs0} : hd;
  Vec3 hv = cur.lerp(hd, 0.75f).normalized();
  float hs = std::min(std::max(hs0, 8.f) + QUICK.dv * Q.k, std::max(hs0, QUICK.hCap));
  float vy = std::min((s.vel.y < 0 ? s.vel.y * 0.4f : s.vel.y) + (3 + 5 * std::max(0.f, d.y)) * Q.k, 9.f);
  s.vel = {hv.x * hs, std::max(s.vel.y, vy), hv.z * hs};
  capSpeed(VMAX);
  s.facing = std::atan2(hv.x, hv.z);
  s.relT = 0.15f; s.dive = false;
  Q.applied = true;
  web_.setTaut(0.8f);
  TravEvent e; e.type = "quickBoost"; e.k = Q.k; events.push_back(e);
}
void Traversal::stepQuickBoost(float dt) {
  auto& Q = s.quick; if (!Q.active) return;
  Q.t += dt;
  if (!Q.applied && Q.t >= Q.hitT) { if (s.mode == "air") quickImpulse(); else Q.applied = true; }
  if (Q.webOn && (Q.t > Q.hitT + QUICK.web || s.mode != "air")) {
    Q.webOn = false;
    if (s.mode != "swing" && s.mode != "zip" && !s.sling.active) web_.releaseSnap(0.3f);
  }
  if (Q.t > QUICK.dur) Q.active = false;
}

void Traversal::stepPerch(float h, InputState& I) {
  auto& P = s.perch;
  if (s.sub == "perchLand" && s.subT > 0.5f) setSub("perchIdle");
  Vec3 out{P.normal.x, 0, P.normal.z}; if (out.lengthSq() < 0.09f) out = {std::sin(s.facing), 0, std::cos(s.facing)}; out.normalize();
  if (I.jumpPressed) { if (s.sub == "perchLand" && s.subT < 0.25f && P.hasImpact) anchorLaunch(P.impact); else pointLaunch(P.normal, {}); return; }
  if (I.swingPressed) { s.vel = out * 7.f + UP * 5.5f; setMode("air", "jumpLaunch"); s.airT = 0; s.apexY = feetY(); s.swingCooldown = 0.12f; return; }
  if (I.dropPressed) { s.pos.addScaled(out, 0.55f); s.vel = out * 2.5f; setMode("air", "fall"); s.airT = 0; s.apexY = feetY(); return; }
  Vec3 inD = inputDir(I);
  if (inD.lengthSq() > 0.16f && s.modeT > 0.25f) {
    inD.normalize();
    bool alongEdge = std::fabs(inD.dot(out)) < 0.5f && P.kind != "lampTop" && P.kind != "signalMast" && P.kind != "pole" && P.kind != "antenna";
    if (!alongEdge && (inD.dot(out) > 0.3f || !P.roof)) { // hop down / off
      if (inD.dot(out) > 0.3f) s.pos.addScaled(out, 0.3f);
      s.vel = inD * 5.5f + UP * 4.5f; s.facing = std::atan2(inD.x, inD.z);
      setMode("air", "jumpLaunch"); s.airT = 0; s.apexY = feetY(); return;
    }
    s.facing = std::atan2(inD.x, inD.z); s.speed = 1.5f; if (!alongEdge) s.pos.addScaled(inD, 0.2f); enterGround("walk"); s.speed = 1.5f; return;
  }
  s.vel = {};
}

// ------------------------------------------------------------------ orientation (root transform)
Quat Traversal::orient(float dt) {
  float hv = std::hypot(s.vel.x, s.vel.z);
  float rate = 12;
  Vec3 up{0, 1, 0}, fwd{std::sin(s.facing), 0, std::cos(s.facing)};
  float velYaw = std::atan2(s.vel.x, s.vel.z), yr = hv > 2 ? angWrap(velYaw - lastVelYaw_) / std::max(dt, 1e-3f) : 0; lastVelYaw_ = velYaw;
  float bankT = clampf(yr * hv / 22, -1, 1);
  s.bank = damp(s.bank, bankT, 5, dt);
  s.pitch = 0; s.roll = 0;
  if (s.mode == "ground") {
    rate = s.sub == "landRoll" ? 20.f : 16.f;
    s.roll = -s.bank * (s.speed > RUN ? 0.28f : 0.15f);
    if (s.sub == "sprint" || s.sub == "run") s.pitch = 0.06f * std::min(1.f, s.speed / SPRINT);
  } else if (s.mode == "air") {
    if (hv > 1.5f && s.sub != "zipPull") s.facing = std::atan2(s.vel.x, s.vel.z);
    fwd = {std::sin(s.facing), 0, std::cos(s.facing)};
    s.roll = -s.bank * 0.45f;
    s.pitch = s.dive || s.gliding ? 0.25f : clampf(-s.vel.y * 0.008f, -0.2f, 0.25f);
    rate = 8;
  } else if (s.mode == "swing") {
    auto& S = s.swing;
    Vec3 ad = (S.anchor - s.pos).normalized();
    up = (ad * (1 - 0.85f * S.slack) + UP * (0.12f + 0.85f * S.slack)).normalized();
    if (hv > 1) s.facing = std::atan2(s.vel.x, s.vel.z);
    fwd = s.vel.lengthSq() > 1 ? s.vel : Vec3{std::sin(s.facing), 0, std::cos(s.facing)};
    S.bank = damp(S.bank, clampf(bankT + (s.vel.x * S.dir.z - s.vel.z * S.dir.x) * 0.02f, -1, 1), 6, dt);
    s.roll = -S.bank * 0.5f; rate = 14;
  } else if (s.mode == "zip") {
    auto& Z = s.zip;
    bool flying = s.sub == "zipFlight" || s.sub == "zipCatch";
    float k = flying ? sm01(Z.tau / 0.08f) * (1 - sm01((0.12f - (1 - Z.t) * Z.dur) / 0.12f)) : 0;
    Vec3 hf{std::sin(s.facing), 0, std::cos(s.facing)};
    if (k > 0.01f) {
      Vec3 toT = Z.p2 - s.pos; if (toT.lengthSq() < 0.25f && Z.hasFlightDir) toT = Z.flightDir; toT.normalize();
      up = vlerp(UP, toT, k).normalized();
      Vec3 xr = UP.cross(hf).normalized();
      fwd = xr.cross(up); if (fwd.lengthSq() < 1e-4f) fwd = hf;
    } else fwd = hf;
    rate = 14;
  } else if (s.mode == "perch") { fwd = {std::sin(s.facing), 0, std::cos(s.facing)}; rate = 10; }
  else if (s.mode == "wall") {
    auto& W = s.wall; Vec3 n = W.normal;
    bool running = s.sub == "wallRun" && W.fast;
    W.runK = damp(W.runK, running ? 1.f : 0.f, running ? 9.f : 7.f, dt);
    Vec3 along = W.up - n * W.up.dot(n); if (along.lengthSq() < 1e-4f) along = {0, 1, 0}; along.normalize();
    if (W.runK > 0.5f) { fwd = along; up = n; } else { fwd = -n; up = along; }
    rate = 14;
  }
  { Vec3 z = fwd - up * fwd.dot(up);
    if (z.lengthSq() > 1e-6f) { z.normalize(); Vec3 x = up.cross(z).normalized(), y = z.cross(x); s.bodyQ = Quat::slerp(s.bodyQ, Quat::fromBasis(x, y, z), 1 - std::exp(-rate * dt)); } }
  Quat q = s.bodyQ;
  if (s.roll) q = q * Quat::axisAngle({0, 0, 1}, s.roll);
  if (s.pitch) q = q * Quat::axisAngle({1, 0, 0}, s.pitch);
  s.stepOff = damp(s.stepOff, 0, 16, dt);
  Vec3 bodyUp = s.bodyQ * UP;
  if (s.mode == "wall") {
    auto& W = s.wall; float toPlane = W.dist;
    Vec3 crawl = s.pos + W.normal * (0.30f - toPlane) - bodyUp * (H * (1 - W.runK));
    Vec3 run = s.pos + W.normal * (-toPlane + 0.02f);
    rootPos = vlerp(crawl, run, W.runK);
  } else rootPos = s.pos - bodyUp * H;
  if (s.mode == "ground" || s.mode == "perch") rootPos.y = s.pos.y - H + s.stepOff;
  return q;
}

// ------------------------------------------------------------------ main update
Quat Traversal::update(float dt, InputState& I) {
  events.clear(); lastInput_ = &I;
  s.jumpBuf = I.jumpPressed ? 0.22f : std::max(0.f, s.jumpBuf - dt);
  s.subT += dt; s.modeT += dt;
  if (s.mode == "swing") s.sinceSwing = 0;
  else { s.sinceSwing += dt; if (s.sinceSwing > CHAIN_BUF && s.chain) { s.chain = 0; push("swingChain"); } }
  s.swingCooldown -= dt; s.wallCooldown -= dt; s.zipCooldown -= dt; s.clock += dt;
  if (s.mode != "ground") s.walkK = damp(s.walkK, I.walk && s.grounded ? 1.f : 0.f, WALK_EASE, dt);
  if (s.dashWebT > 0) { s.dashWebT -= dt; if (s.dashWebT <= 0 && s.mode == "air") web_.release(); }
  // zip targeting (reticle) — suppressed while swinging fast / zipping
  Vec3 eye = s.pos; eye.y += 0.5f;
  bool perched = s.mode == "perch";
  ZipTargeting::Opts to; to.enabled = s.mode != "zip" && !(s.mode == "swing" && s.vel.length() > 30);
  to.exclude = perched ? &s.perch.pos : nullptr; to.air = s.mode == "air" || s.mode == "swing";
  Vec3 perchOut;
  if (perched) { perchOut = {s.perch.normal.x, 0, s.perch.normal.z}; if (perchOut.lengthSq() > 0.09f) perchOut.normalize(); else perchOut = {std::sin(s.facing), 0, std::cos(s.facing)}; to.perchOut = &perchOut; }
  targeting.update(dt, camera_, eye, to);
  // E / MMB: zip to the highlighted point, or air web-dash
  if (I.zipPressed && s.zipCooldown <= 0 && !s.kin) {
    const ZipTarget* t = targeting.best();
    leaveSwingOK_ = true;
    if (s.mode == "wall" && (s.sub == "wallRun" || s.sub == "crawl")) wallZip();
    else if (t) { ZipTarget tt = *t; if (s.mode == "swing") web_.release(); startZip(tt); s.zipCooldown = 0.25f; }
    else if (s.mode == "perch") { pointLaunch(s.perch.normal, {}); s.zipCooldown = 0.25f; }
    else if (s.mode == "air" || s.mode == "swing") { if (s.mode == "swing") web_.release(); webDash(); }
    leaveSwingOK_ = false;
  }
  // Q / L1: quick web boost (air, or mid-swing)
  s.quickBuf = I.quickPressed ? 0.25f : std::max(0.f, s.quickBuf - dt);
  if (s.quickBuf > 0 && !s.kin && (s.mode == "air" || s.mode == "swing") && s.clock - s.quick.last >= QUICK.cd) { s.quickBuf = 0; quickBoostStart(); }
  stepQuickBoost(dt);
  int n = std::max(1, (int)std::ceil(dt / (1.f / 120))); float h = dt / n;
  for (int i = 0; i < n; i++) {
    std::string pre = s.mode;
    if (s.kin && s.kin->type == "ledge") stepLedge(h);
    else if (s.kin && s.kin->type == "wallHop") stepWallHop(h);
    else if (s.kin && s.mode != "zip") stepKin(h);
    else if (s.mode == "ground") stepGround(h, I);
    else if (s.mode == "air") stepAir(h, I);
    else if (s.mode == "swing") stepSwing(h, I);
    else if (s.mode == "wall") stepWall(h, I);
    else if (s.mode == "zip") stepZip(h, I);
    else if (s.mode == "perch") stepPerch(h, I);
    if (i == 0 || s.mode != pre) { I.jumpPressed = false; I.swingPressed = false; I.dropPressed = false; I.slingL = false; I.slingR = false; }
  }
  if (s.sling.active && s.mode != "ground") slingEnd(false);
  if (s.wall.zipWeb && s.sub != "wallZip") { s.wall.zipWeb = false; if (s.mode != "swing" && s.mode != "zip") web_.releaseSnap(0.3f); }
  // position-delta guard (debug)
  { float d = s.pos.distanceTo(guardP_), lim = std::max(4.f, s.vel.length() * dt * 2.5f + 1.5f);
    if (guardOk_ && d > lim) std::fprintf(stderr, "[traversal] position jump %.1f m in %.0f ms (%s/%s)\n", d, dt * 1000, s.mode.c_str(), s.sub.c_str());
    guardP_ = s.pos; guardOk_ = true; }
  // safety net: never below the terrain / inside a building
  float g = world_.groundHeight(s.pos.x, s.pos.z, s.pos.y - H + 0.3f);
  if (s.pos.y - H < g - 0.05f && s.mode != "wall") { s.pos.y = g + H; if (s.vel.y < 0) s.vel.y = 0; if (s.mode == "air") land(g, I); }
  if (s.mode != "zip" && s.mode != "wall" && world_.inside({s.pos.x, s.pos.y + 0.3f, s.pos.z})) {
    float fy0 = s.pos.y - H, top = world_.groundHeight(s.pos.x, s.pos.z, fy0 + 1.2f);
    bool okUp = top > fy0 - 0.3f && top - fy0 < 1.2f && !world_.inside({s.pos.x, top + 0.35f, s.pos.z});
    if (okUp) s.pos.y = top + H;
    else collide(STEP, R + 0.05f);
    if (okUp || !world_.inside({s.pos.x, s.pos.y + 0.3f, s.pos.z})) {
      if (s.mode == "swing") { if (s.vel.y < 0) s.vel.y = 0; }
      else if (okUp) { s.vel = {}; web_.release(); enterGround("idle"); }
    }
  }
  s.grounded = s.mode == "ground" || s.mode == "perch";
  if (s.mode == "ground") s.dashCount = 0;
  Quat q = orient(dt);
  s.lookDir = cam_.lookDir();
  return q;
}

void Traversal::teleport(const Vec3& p, float yaw) {
  guardOk_ = false;
  s.pos = p; float f = floorAt(p.x, p.z, p.y); if (s.pos.y < f + H) s.pos.y = f + H;
  leaveSwingOK_ = true;
  s.vel = {}; s.kin.reset(); web_.release(); if (s.sling.active) slingEnd(false); s.facing = yaw; s.speed = 0; s.stepOff = 0; s.trick.clear(); s.dive = false; s.charging = false;
  if (s.pos.y - H - f < 0.05f) enterGround("idle"); else { setMode("air", "fall"); s.airT = 0; s.apexY = feetY(); }
  leaveSwingOK_ = false;
  s.bodyQ = Quat::axisAngle(UP, yaw);
}
