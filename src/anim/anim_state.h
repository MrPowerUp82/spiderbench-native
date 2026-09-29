// C1 contract (port of player/traversal/anim.js): written by traversal every frame, read by the animation layer.
// See the original file for the full field documentation; fields for unported features (web tightrope) stay inactive.
#pragma once
#include "core/math.h"
#include <string>
#include <vector>

struct TravState;

struct AnimState {
  std::string mode = "ground", sub = "idle", fromMode = "ground";
  float t = 0, modeT = 0, speed = 0, jumpCharge = 0;
  Vec3 velocity; bool grounded = true;
  struct { float phase = 0, bank = 0, tension = 0, ropeLength = 0, angle = 0, slack = 0, kick = 0; Vec3 anchor; char hand = 'R'; } swing;
  struct { Vec3 target, dir{0, 0, 1}; float t = 0, taut = 0, pitch = 0; bool dash = false, webs = false; std::string phase; } zip;
  struct { Vec3 normal{0, 0, 1}, point, up{0, 1, 0}, edge{1, 0, 0}, impact; std::string kind = "roofEdge"; float radius = 0; } perch;
  struct { Vec3 normal{0, 0, 1}, point; Vec2 move; bool fast = false; float phase = 0, dist = 0.38f, runK = 0; } wall;
  struct { bool active = false; Vec3 point, inward{0, 0, 1}; std::string variant = "flip"; float t = 0; } ledge;
  struct { float severity = 0; } landing;
  std::string trick; int trickSide = 1;
  Vec3 lookDir{0, 0, 1};
  float facing = 0; bool dive = false, glide = false, balance = false; float stepOffset = 0, walkK = 0;
  Quat bodyQ; Vec3 rootPos;
  struct SlingAnchor { Vec3 p; int side; float t; };
  struct { bool active = false; float tension = 0, moving = 0, release = -1; std::vector<SlingAnchor> anchors; Vec3 dir{0, 0, 1}; } sling;
  struct { bool active = false, web = false; float t = 0, hitT = 0.08f, k = 1; char hand = 'R'; Vec3 anchor; int seq = 0; } quick;
};

void writeAnim(AnimState& a, const TravState& s, const Quat& q);
