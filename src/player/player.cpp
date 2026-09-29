#include "player/player.h"
#include "audio/audio.h"
#include "gfx/texture.h"
#include "world/world.h"
#include <cstdio>

bool Player::init(const World& world, Camera& camera) {
  camera_ = &camera; world_ = &world;
  if (!rig.load(assetPath("spiderman.glb"))) return false;
  useAnimator = animator.build(rig);
  if (!useAnimator) std::fprintf(stderr, "[player] animator unavailable, using clip playback\n");
  cam = std::make_unique<ChaseCamera>(camera, world);
  trav = std::make_unique<Traversal>(world, *cam, web, rig, camera);
  teleport(world.spawn + Vec3{0, TRAV_H, 0}, 0);
  return true;
}

void Player::teleport(const Vec3& p, float yaw) {
  trav->teleport(p, yaw);
  cam->reset(trav->s.pos, yaw);
}

bool Player::aiming() const {
  const auto& s = trav->s;
  return aimT < 1.6f || zipHeld || s.mode == "perch" || (s.mode == "ground" && s.speed < 0.3f && s.modeT > 0.6f);
}

// clip-driven animation from the traversal state (player.js fallbackAnimate, using the richer GLB clip set)
void Player::animate(float dt) {
  const auto& s = trav->s;
  const std::string &m = s.mode, &sub = s.sub; float t = s.subT;
  bool entered = sub != lastSub_ || m != lastMode_;
  lastSub_ = sub; lastMode_ = m;
  Rig& R = rig;
  auto once = [&](const char* name, float fade) { if (entered) R.play(name, fade, 1, false); else if (R.currentName() != name) R.play(name, fade, 1, false); };
  if (m == "ground") {
    if (sub == "idle") R.play("idle", 0.25f);
    else if (sub == "walk") R.play("walk", 0.22f, clampf(s.speed / 1.6f, 0.6f, 1.6f));
    else if (sub == "run") R.play("run", 0.22f, clampf(s.speed / 7.8f, 0.7f, 1.6f));
    else if (sub == "sprint") R.play("sprint", 0.22f, clampf(s.speed / 11.5f, 0.8f, 1.6f));
    else if (sub == "jumpCharge") R.drive("jump", 0.05f + 0.2f * s.jumpCharge, 0.12f);
    else if (sub == "vault") R.drive("jump", 0.45f + 0.45f * std::min(1.f, t / 0.35f), 0.1f);
    else if (sub == "slingshot") R.play("idle", 0.25f);
    else if (sub == "landLight") once("landLight", 0.06f);
    else if (sub == "landMedium") once("landMedium", 0.06f);
    else if (sub == "landHard") once("landHard", 0.05f);
    else if (sub == "landRoll") once("landRoll", 0.06f);
    else R.play("idle", 0.25f);
  } else if (m == "air") {
    if (sub == "trick" && !s.trick.empty()) {
      const char* clip = s.trick == "tuckFlip" ? "releaseTuck" : s.trick == "layout" ? "releaseFlip" : s.trick == "corkscrew" ? "releaseCorkscrew" : "releaseSpread";
      R.drive(clip, std::min(0.99f, t / std::max(s.trickDur, 0.1f)), 0.08f);
    } else if (sub == "jumpLaunch" || sub == "wallJump") R.drive("jump", std::min(0.9f, 0.3f + t * 2.2f), 0.08f);
    else if (sub == "pointLaunch") once("pointLaunch", 0.08f);
    else if (sub == "zipPull") once("webZipPull", 0.1f);
    else if (sub == "vault") R.drive("jump", std::min(0.9f, 0.45f + t * 2), 0.1f);
    else if (sub == "release" || sub == "rise") R.play("airRise", 0.25f);
    else if (sub == "apex") R.play("airApex", 0.3f);
    else if (sub == "dive") R.play("fallFast", 0.35f);
    else R.play(s.vel.y < -16 ? "fallFast" : "fall", 0.35f);
  } else if (m == "swing") {
    // clip: 0 back of arc, 18 bottom, 32 front (of 56 frames)
    float ph = s.swing.phase, f = ph < 0 ? 18 * (ph + 1) : 18 + 14 * ph;
    R.drive(s.swing.hand == 'L' ? "swingL" : "swing", f / 56, entered && prevMode_ != "swing" ? 0.12f : 0.2f);
  } else if (m == "zip") {
    if (sub == "zipFire") once("webZipFire", 0.06f);
    else if (sub == "zipFlight") R.play("zipFlight", 0.1f);
    else once("zipCatch", 0.12f);
  } else if (m == "perch") {
    if (sub == "perchLand") once("perchLand", 0.08f);
    else R.play("perchIdle", 0.4f);
  } else if (m == "wall") {
    if (sub == "wallZip") R.play("zipFlight", 0.15f);
    else if (sub == "cornerWrap") once("cornerWrap", 0.08f);
    else if (sub == "wallRun" && s.wall.runK > 0.5f) R.play("run", 0.2f, 1.25f);   // run cycle rotated onto the wall
    else if (sub == "wallRunSide") R.play("wallRunHorizontal", 0.2f, 1.1f);
    else if (s.vel.lengthSq() > 0.25f) R.play("wallCrawl", 0.2f, clampf(s.vel.length() / 3.f, 0.6f, 1.8f));
    else R.play("wallIdle", 0.3f);
  }
  prevMode_ = m;
  R.update(dt);
  // arm IK: the web arm points at the anchor
  if (m == "swing" && web.active()) {
    char hnd = s.swing.hand;
    auto aim = [&](char S, float w) {
      std::string sd = S == 'L' ? ".L" : ".R";
      Vec3 d = (web.anchor() - R.boneWorld("upperArm" + sd)).normalized();
      R.aimBone("upperArm" + sd, d, w); R.aimBone("forearm" + sd, d, w);
    };
    aim(hnd, 1); if (hnd == 'L') aim('R', 0.55f);
  }
}

void Player::update(float dt, Input& input, Audio* audio) {
  auto& s = trav->s;
  input.slingGate = s.mode == "ground";
  InputState I = input.poll(dt);
  aimT = I.aimT; zipHeld = I.zip;
  cam->applyLook(I);
  web.shotsThisFrame = 0;
  Quat q = trav->update(dt, I);
  // camera reactions + sounds
  for (const auto& e : trav->events) {
    const std::string& t = e.type;
    if (t == "land") {
      if (e.severity > 0.02f) cam->impact(e.severity);
      if (audio) { float k = e.severity; audio->play(k < 0.3f ? "land_soft" : k < 0.7f ? "land_med" : "land_heavy", 0.7f + 0.4f * k); }
    }
    else if (t == "perch") { cam->impact(e.severity * 0.6f); if (audio) audio->play("perch"); }
    else if (t == "pointLaunch") { cam->impact(0.18f); cam->kick(0.7f); if (audio) audio->play("launch", 0.6f); }
    else if (t == "zipLaunch") { cam->kick(std::min(1.f, 0.45f + e.dist / 60)); if (audio) audio->play("zip"); }
    else if (t == "quickBoost") { cam->kick(0.22f + 0.18f * e.k); cam->shake(0.04f); }
    else if (t == "waterSplash") cam->shake(0.25f);
    else if (t == "wall") { if (e.run) cam->shake(0.08f); if (audio) audio->play("wall"); }
    else if (t == "ropeSnap") cam->shake(0.12f + 0.25f * e.severity);
    else if (t == "swingWallKick") cam->shake(0.1f + 0.3f * e.severity);
    else if (t == "slingFail") cam->shake(0.05f);
    else if (t == "slingAttach") cam->shake(0.03f);
    else if (t == "slingLaunch") { cam->shake(0.1f + 0.2f * e.tension); if (audio) audio->play("launch", 0.6f + 0.4f * e.tension); }
    else if (t == "swingStart") { if (audio) audio->play("attach"); }
    else if (t == "release") { if (audio) audio->play("release"); }
    else if (t == "jump") { if (audio) audio->play("jump", 0.7f + 0.5f * e.charge); }
  }
  if (audio && web.shotsThisFrame) audio->play("thwip");
  CamParams cp;
  cp.pos = s.pos; cp.vel = s.vel; cp.mode = &s.mode; cp.sub = &s.sub; cp.modeT = s.modeT;
  cp.anchor = s.mode == "swing" ? &s.swing.anchor : nullptr; cp.swingDir = s.mode == "swing" ? &s.swing.dir : nullptr;
  cp.wallNormal = s.wall.normal; cp.facing = s.facing; cp.dive = s.dive || s.gliding;
  cp.tension = s.swing.tension; cp.bank = s.swing.bank; cp.sling = s.sling.active ? 0.25f + 0.75f * s.sling.tension : 0; cp.walkK = s.mode == "ground" ? s.walkK : 0;
  cam->update(dt, cp);
  meshVisible = camera_->position.distanceTo(s.pos) > 0.85f; // never render the camera inside the character
  if (useAnimator && animator.enabled) {
    writeAnim(anim, s, q); anim.rootPos = trav->rootPos;
    AnimIO io; io.anim = &anim; io.center = s.pos; io.world = world_; io.H = TRAV_H; io.trav = &s;
    animator.update(dt, io);
  } else {
    rig.setObjectMatrix(Mat4::compose(trav->rootPos, q, {1, 1, 1}));
    animate(dt);
  }
  char hand = s.mode == "swing" ? s.swing.hand : s.quick.webOn ? s.quick.hand : 'R';
  Vec3 h2 = rig.handWorld('L');
  web.update(dt, rig.handWorld(hand), web.active2() ? &h2 : nullptr);
  if (audio) audio->setWind(clampf((s.vel.length() - 6) / 34, 0, 1));
}
