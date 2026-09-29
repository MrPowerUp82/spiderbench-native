#include "anim/anim_state.h"
#include "player/traversal/traversal.h"

void writeAnim(AnimState& a, const TravState& s, const Quat& q) {
  std::string mode = s.mode;
  if (mode == "ground" && s.sub.rfind("land", 0) == 0) mode = "land";
  if (mode != a.mode) a.fromMode = a.mode;
  a.mode = mode; a.sub = s.sub; a.t = s.subT; a.modeT = s.modeT;
  a.velocity = s.vel; a.speed = mode == "ground" || mode == "land" ? s.speed : s.vel.length();
  a.grounded = s.grounded; a.jumpCharge = s.charging || s.sub == "jumpLaunch" ? s.jumpCharge : 0;
  const auto& S = s.swing; bool sw = s.mode == "swing";
  a.swing.phase = S.phase; a.swing.bank = S.bank; a.swing.tension = sw ? S.tension : 0; a.swing.anchor = S.anchor; a.swing.hand = S.hand;
  a.swing.ropeLength = S.rope; a.swing.angle = sw ? S.angle : 0; a.swing.slack = sw ? S.slack : 0; a.swing.kick = sw ? S.kick : 0;
  const auto& Z = s.zip; bool zm = s.mode == "zip";
  a.zip.target = Z.target; a.zip.t = zm ? Z.t : 0; a.zip.dash = Z.dash && s.sub == "zipPull" && s.mode == "air";
  a.zip.phase = !zm ? "" : s.sub == "zipFire" ? "fire" : s.sub == "zipYank" ? "yank" : s.sub == "zipFlight" ? "flight" : s.sub == "zipCatch" ? "catch" : "";
  a.zip.webs = zm && Z.webs; a.zip.taut = zm ? Z.taut : 0; a.zip.pitch = zm ? Z.pitch : 0;
  if (Z.hasFlightDir) a.zip.dir = Z.flightDir;
  a.perch.normal = s.perch.normal; a.perch.kind = s.perch.kind; a.perch.point = s.perch.pos; a.perch.up = UP;
  a.perch.edge = s.perch.edge; a.perch.radius = s.perch.radius; if (s.perch.hasImpact) a.perch.impact = s.perch.impact;
  a.wall.normal = s.wall.normal; a.wall.move = s.wall.move; a.wall.fast = s.wall.fast; a.wall.phase = s.wall.phase;
  a.wall.dist = s.wall.dist; a.wall.point = s.wall.point; a.wall.runK = mode == "wall" ? s.wall.runK : 0;
  a.ledge.active = s.kin && s.kin->type == "ledge";
  if (a.ledge.active) { a.ledge.point = s.kin->lip; a.ledge.inward = s.kin->inward; a.ledge.variant = s.kin->variant; a.ledge.t = s.kin->t; }
  a.landing.severity = s.landing.severity; a.trick = s.trick; a.trickSide = s.trickSide; a.lookDir = s.lookDir;
  a.sling.active = s.sling.active; a.sling.tension = s.sling.active ? s.sling.tension : 0; a.sling.dir = s.sling.dir;
  a.sling.moving = s.sling.active ? s.sling.moving : 0; a.sling.release = s.sling.active && s.sling.rel >= 0 ? std::min(1.f, s.sling.rel / 0.12f) : -1;
  a.sling.anchors.clear(); for (const auto& x : s.sling.anchors) a.sling.anchors.push_back({x.p, x.side, x.t});
  const auto& Q = s.quick;
  a.quick.active = Q.active; a.quick.t = Q.t; a.quick.hand = Q.hand; a.quick.anchor = Q.anchor; a.quick.hitT = Q.hitT; a.quick.web = Q.webOn; a.quick.seq = Q.seq; a.quick.k = Q.k;
  a.walkK = mode == "ground" ? s.walkK : 0;
  a.balance = false; a.facing = s.facing; a.dive = s.dive || s.gliding; a.glide = s.gliding; a.bodyQ = q; a.stepOffset = s.stepOff;
}
