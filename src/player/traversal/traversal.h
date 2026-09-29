// Insomniac-style traversal state machine (port of player/traversal/traversal.js).
//   modes: ground | air | swing | zip | perch | wall   (the web tightrope 'rope' mode, Superman flight and Flash burst are
//   not ported yet). Physics conventions: s.pos = body centre (feet + H); 120 Hz substeps; gravity 24 m/s^2; cap 45 m/s.
#pragma once
#include "player/traversal/helpers.h"
#include "player/input.h"
#include <optional>
#include <string>
#include <vector>

class ChaseCamera; class WebSystem; class Rig; struct Camera;

constexpr float TRAV_H = 0.95f;      // body centre above the feet
constexpr float TRAV_R = 0.36f;      // capsule radius
constexpr float TRAV_HEIGHT = 1.8f;  // capsule height

struct TravEvent {
  std::string type, kind, trick;
  float severity = 0, charge = 0, dist = 0, k = 0, tension = 0;
  bool run = false;
};

struct TravState {
  struct Swing {
    Vec3 anchor, normal, pivot; float rope = 20, ropeTarget = 20, t = 0;
    Vec3 dir{0, 0, 1}; char hand = 'R'; float phase = 0, bank = 0, tension = 0; std::string kind = "wall";
    float slack = 0, kick = 0, kickCd = 0; bool apexed = false; float slackT = 0, tautT = 0, y0 = 0, angMax = -9, angle = 0;
    float sideT = 0, sideK = 0; bool hasSideN = false; Vec3 sideN; float wrapT = 0;
  } swing;
  struct Zip {
    Vec3 target, normal; std::string kind; Vec3 p0, p1, p2; float t = 0, dur = 0.5f; bool launch = false, dash = false, webs = false;
    float taut = 0, u = 0, tau = 0; bool fromGround = false; float fireDur = 0;
    float lut[33] = {}; float len = 0, v0 = 0, vP = 0, dR = 0, dC = 0, dB = 0, tC = 0, tBrake = 0, aB = 0;
    Vec3 flightDir; bool hasFlightDir = false; float pitch = 0;
  } zip;
  struct Perch { Vec3 pos, normal{0, 0, 1}; std::string kind = "roofEdge"; bool roof = true; Vec3 edge{1, 0, 0}; float radius = 0; Vec3 impact; bool hasImpact = false; } perch;
  struct Wall {
    Vec3 normal{0, 0, 1}; Vec2 move; bool fast = false; float runV = 0; Vec3 up{0, 1, 0}; float phase = 0, off = 0, dist = TRAV_R + 0.02f; Vec3 point; float runK = 0;
    bool hasLock = false; Vec3 lockDir; float lockMx = 0; float zipT = 0; bool zipWeb = false;
  } wall;
  struct Kin {
    std::string type; float t = 0, dur = 1; Vec3 p0, p1, p2, n0, n1; bool run = false; float sp = 0; Vec3 dir1; bool hasDir1 = false;
    Vec3 exitVel; float floor = 0;
    std::string variant; float grab = 0, climb = 0; Vec3 hang, end, lip, inward;
    float tA = 0, tB = 0, f0 = 0, apex = 0, landTop = 0, Dtot = 0, exitSpeed = 0; bool fast = false;
  };
  struct SlingAnchor { Vec3 p, n; int side; float t; };
  struct Sling { bool active = false; float rel = -1; std::vector<SlingAnchor> anchors; float tension = 0, pull = 0; Vec3 origin, dir{0, 0, 1}, fwd0{0, 0, 1}; float moving = 0, t = 0; } sling;
  struct Quick { bool active = false; float t = 0; char hand = 'R'; Vec3 anchor, normal{0, 0, 1}; float hitT = 0.08f; bool applied = false, webOn = false; float dist = 0; bool sky = false; float k = 1; int n = 0; float last = -9; int seq = 0; } quick;

  std::string mode = "air", sub = "fall"; float subT = 0, modeT = 0;
  Vec3 pos, vel; float facing = 0, speed = 0; bool grounded = false; float floorY = 0;
  float stepOff = 0; Vec3 carry;
  bool charging = false; float chargeT = 0, jumpCharge = 0, coyote = 0;
  float airT = 0, apexY = 0; bool dive = false; float relT = 99;
  bool lastTrick = false; std::string trick; float searchT = 0, swingCooldown = 0, wallCooldown = 0, zipCooldown = 0; int dashCount = 0;
  std::optional<Kin> kin;
  struct { float severity = 0, lock = 0; } landing;
  Quat bodyQ; float roll = 0, pitch = 0, bank = 0;
  Vec3 lookDir{0, 0, 1};
  float clock = 0, walkK = 0;
  int chain = 0; float sinceSwing = 99, jumpBuf = 0, noAnchorT = 0, returnT = 0, dashWebT = 0, quickBuf = 0, airTapT = -9;
  bool airTrickUsed = false, gliding = false, groundSwing = false, jumpRelHold = false;
  float trickDur = 0, trickSnapT = 9; bool trickBoosted = false, trickNoUp = false; int trickSide = 1; std::string lastTrickName; float trickLat = 0, trickSteep = 0;
};

class Traversal {
 public:
  TravState s;
  std::vector<TravEvent> events;
  Vec3 rootPos;
  ZipTargeting targeting;
  AnchorFinder anchors;

  Traversal(const World& w, ChaseCamera& cam, WebSystem& web, Rig& rig, const Camera& camera);
  Quat update(float dt, InputState& I);
  void teleport(const Vec3& p, float yaw = 0);
  float floorAt(float x, float z, float y) const;
  void setStats(float releaseBoostMul) { releaseBoostMul_ = releaseBoostMul; }

 private:
  const World& world_;
  ChaseCamera& cam_;
  WebSystem& web_;
  Rig& rig_;
  const Camera& camera_;
  InputState* lastInput_ = nullptr;
  bool leaveSwingOK_ = false;
  float releaseBoostMul_ = 1;
  float lastVelYaw_ = 0;
  struct { float t = 0; Vec3 push; } corr_;
  struct { float t = 0, rate = 0; } avoid_;
  Vec3 guardP_; bool guardOk_ = false;

  float rnd();
  float feetY() const { return s.pos.y - TRAV_H; }
  float standAt(float x, float z, float y) const;
  Vec3 inputDir(const InputState& I) const;
  void setMode(const std::string& mode, const std::string& sub);
  void setSub(const std::string& sub) { if (s.sub != sub) { s.sub = sub; s.subT = 0; } }
  std::optional<Contact> collide(float stepH = 0.55f, float rad = TRAV_R);
  static std::optional<Vec3> hdir(const Vec3& v);
  float vmaxC() const;
  void capSpeed(float m = -1);
  float heightAboveFloor() const { return feetY() - floorAt(s.pos.x, s.pos.z, feetY() + 0.1f); }
  float releaseBoost() const;
  void push(const std::string& type, float severity = 0) { TravEvent e; e.type = type; e.severity = severity; events.push_back(e); }

  void enterGround(const std::string& sub = "idle");
  void stepGround(float h, InputState& I);
  bool startMantleOnto(const Contact& c);
  void launchJump(bool parkour);
  Vec3 slingDir();
  bool slingAttach(int side);
  void slingEnd(bool launch);
  void slingLaunch();
  bool stepSling(float h, InputState& I, bool landing);
  void stepAir(float h, InputState& I);
  bool waterBounce();
  void land(float f, InputState& I);
  void corridor(float h, const Vec3& inD);
  float facadeAvoid(float h);
  Vec3 travelDir(const InputState& I);
  bool tryStartSwing(InputState& I);
  void startSwing(const Anchor& a, Vec3 fwd, const Vec3* turn, float hs);
  float swingPhase() const;
  float swingAngle() const;
  void stepSwing(float h, InputState& I);
  void ropeWrap(float h);
  std::string chooseTrick(const InputState* I);
  void startTrick(const std::string& name);
  void trickBoost(const InputState& I);
  void releaseSwing(const std::string& kind, InputState& I);
  bool wideWall(const Vec3& n, const Vec3& point);
  void enterWall(const Vec3& n, const Vec3& point, bool run, float speed = 0);
  Vec3 wallBasis(const Vec3& n) const;
  void stepWall(float h, InputState& I);
  void wallZip();
  float wallProtrusion(const Vec3& n, float bx, float bz, const Vec3& right, bool side);
  void startCornerWrap(const Vec3& n1, const Vec3& p1, float dur, const Vec3* dir1, float mx);
  void stepLedge(float h);
  bool startWallHop(const Vec3& n, bool fast);
  void stepWallHop(float h);
  void startVault(const Contact& c, bool fast);
  void stepKin(float h);
  void startZip(const ZipTarget& t);
  void zipCurve(const Vec3& vel);
  float zipDist(float tau) const;
  float zipArcE(float d) const;
  bool zipClear();
  Vec3 bez(float e) const;
  void stepZip(float h, InputState& I);
  void arriveZip();
  void anchorLaunch(const Vec3& travelV);
  void pointLaunch(const Vec3& normal, const Vec3& travelV);
  void webDash();
  struct QA { float dist; bool sky; };
  QA quickAnchor(Vec3& out, Vec3& nOut);
  void quickBoostStart();
  void quickImpulse();
  void stepQuickBoost(float dt);
  void stepPerch(float h, InputState& I);
  Quat orient(float dt);
};
