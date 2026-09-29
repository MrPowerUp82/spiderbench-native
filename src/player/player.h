// Player (port of player/player.js): wires input -> camera -> traversal -> animation -> webs. Animation = the procedural
// animation layer (anim/animator.cpp, driven by the C1 contract); the clip-only path (player.js fallbackAnimate) remains as
// a fallback. Traversal events feed camera reactions and sounds.
#pragma once
#include "anim/rig.h"
#include "anim/animator.h"
#include "anim/anim_state.h"
#include "player/chase_camera.h"
#include "player/traversal/traversal.h"
#include "player/web.h"
#include <memory>

class Audio;

class Player {
 public:
  Rig rig;
  Animator animator;          // procedural animation layer (anim/animator.cpp); clip fallback if it fails to build
  AnimState anim;             // C1 contract, written from the traversal state every frame
  bool useAnimator = true;
  WebSystem web;
  std::unique_ptr<ChaseCamera> cam;
  std::unique_ptr<Traversal> trav;
  bool meshVisible = true;
  float aimT = 99; bool zipHeld = false;

  bool init(const World& world, Camera& camera);
  void update(float dt, Input& input, Audio* audio);
  void teleport(const Vec3& p, float yaw);
  bool aiming() const;

 private:
  Camera* camera_ = nullptr;
  const World* world_ = nullptr;
  std::string lastSub_, lastMode_, prevMode_;
  float runPhase_ = 0;
  void animate(float dt);
};
