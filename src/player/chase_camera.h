// Insomniac-feel chase camera (port of player/camera.js).
// - Orbit with mouse / right stick; smooth auto-recenter behind the direction of travel when the player isn't orbiting.
// - Spring-follow with speed lag + velocity lead, distance / FOV grow with speed, pulls back and pitches down in dives,
//   frames the swing arc (lifts, leans toward the anchor, rolls with the bank), looks up the wall while wall-running.
// - Collision: multi-ray sweep from the pivot, fast pull-in / slow ease-out, never below the ground.
// - Impacts: impact(severity 0..1) -> trauma shake + FOV punch + dip (landing / perch / point-launch).
#pragma once
#include "core/math.h"
#include "gfx/camera.h"
#include <cmath>
#include <string>

class World;
struct InputState;

// critically-damped spring state (Game Programming Gems 4 SmoothDamp)
struct CamSpring { float v = NAN, vel = 0; };
struct CamSpringV { Vec3 v, vel; bool init = false; };

struct CamParams {
  Vec3 pos, vel;
  const std::string* mode = nullptr; const std::string* sub = nullptr;
  float modeT = 0;
  const Vec3* anchor = nullptr; const Vec3* swingDir = nullptr;
  Vec3 wallNormal; float facing = 0; bool dive = false;
  float tension = 0, bank = 0, sling = 0, walkK = 0;
  bool noAuto = false;
};

class ChaseCamera {
 public:
  float yaw = 0, pitch = 0.14f, sens = 0.0023f;
  float motionBlur = 0; // 0..~1.8 for the post pipeline
  explicit ChaseCamera(Camera& cam, const World& world) : camera_(cam), world_(world) {}
  void reset(const Vec3& pos, float yaw_);
  void shake(float amt) { trauma_ = std::min(1.f, trauma_ + amt); }
  void kick(float amt) { kickV_ += 9 * amt; trauma_ = std::min(1.f, trauma_ + 0.08f * amt); }
  void impact(float sev) { trauma_ = std::min(1.f, trauma_ + 0.12f + 0.55f * sev); punchV_ -= 40 * sev; dipV_ -= 5 * sev; }
  Vec3 forward() const { return {std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)}; }
  Vec3 forwardFlat() const { return {std::sin(yaw), 0, std::cos(yaw)}; }
  Vec3 rightFlat() const { return {-std::cos(yaw), 0, std::sin(yaw)}; }
  Vec3 lookDir() const { return camera_.direction(); }
  void applyLook(const InputState& I);
  void update(float dt, const CamParams& p);

 private:
  Camera& camera_;
  const World& world_;
  float dist_ = 4.2f, fov_ = 58, roll_ = 0, lastLook_ = 10, trauma_ = 0, time_ = 0, heightOff_ = 0, collDist_ = 4.2f, sideOff_ = 0.32f;
  float punch_ = 0, punchV_ = 0, dip_ = 0, dipV_ = 0, kickV_ = 0, kickK_ = 0;
  CamSpring autoYaw_, autoPitch_, autoRate_, autoPRate_, lagK_, lagMax_, distS_, heightS_, sideS_, fovS_, yawRate_, bankS_, anchorLean_, collS_, mbK_;
  CamSpringV lagOff_, jumpOff_, lead_, leanOff_;
  bool haveLastGoal_ = false; Vec3 lastGoal_, lastVel_;
  float lastVelYaw_ = NAN, occHold_ = 0, occT_ = 0, pivCap_ = NAN;
  bool occGoal_ = false; float occYaw_ = 0, occPitch_ = 0;
  Vec3 target_;
};
