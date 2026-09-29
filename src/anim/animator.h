// Procedural animation layer driven by the C1 contract (port of player/anim/animator.js).
//  state machine : select(anim) -> node key; a transition pushes a new layer that cross-fades in over a per-transition
//                  blend time (TRANS) while the outgoing layers keep evaluating. Nodes can be sticky (hold()).
//  blend spaces  : locomotion walk/jog/run/sprint by speed (phase-aligned, cadence + stride warped so feet never slide),
//                  air rise/apex/fall/dive by vertical velocity, swing low/bottom/high by phase + corner bank, crawl.
//  procedural    : visual-root frame per mode (lean into turns / acceleration, trick spins), hand IK onto the web with the
//                  two-handed grip, swing legs, quick-boost yank, secondary motion, look-at, breathing, foot IK against
//                  world.raycast, air life, perch squat, wall cling / climb, wall-run, procedural release tricks.
// Not ported: web tightrope node (rope), combat one-shot overlays, the rig.js POSES fallback (all clips exist in the GLB).
#pragma once
#include "anim/anim_state.h"
#include "anim/pose.h"
#include <memory>
#include <string>
#include <vector>

class World;
struct TravState;

struct AnimIO {
  const AnimState* anim = nullptr;
  Vec3 center;               // capsule centre (world)
  const World* world = nullptr;
  float H = 0.95f;
  const TravState* trav = nullptr;
};

enum class NodeK { Frozen, Ground, RunStart, RunStop, Turn180, JumpCharge, JumpLaunch, Air, Trick, Land, Perch, PerchToStand,
                   Zip, PointLaunch, Slingshot, Swing, Crawl, WallRun, WallJump, Vault, Ledge, Corner };

struct ZipPh { float f = 0, y = 0, g = 0, c = 0; };

struct Layer;
struct LayerData {
  std::string clip, relClip, tr, climb;
  float vp = NAN, vrPrev = NAN, cw = 0, wlPrev = 0, bal = 0, c = 0, t0 = 0, dive = 0, minHold = 0.2f, seat = NAN;
  float travelT = NAN, cMax = 0, gw = NAN, yankT0 = NAN, catchT0 = NAN;
  float k = 0, ph = 0, in = 0, mv = 0, rel = 0, grip = 0, angPrev = NAN, fwd = 1, roll = 0, delta = 0, tl = 0, grab = 0, dur = 0, phi0 = 0, phiEnd = 0;
  bool stopOn = false, stopDone = false; float stopRem = 0;
  bool hop = false, wings = false, proc = false, seam = false, noHandover = false, reverse = false, mirror = false, done = false;
  bool landed = false, hasPin = false, fg = false, dash = false, nat = false, side = false, useClip = false;
  int trSide = 1; Quat resid; bool sprInit = false; Spring3 spr[4]; float lenArm = 0, lenLeg = 0;
  char hand = 'R', cling = 0;
  Vec3 pin[2], O, f;
  ZipPh zph; bool hasZph = false;
  float g[2] = {0, 0}, gv[2] = {0, 0}; Vec3 aim[2], hold[2];
  std::unique_ptr<Layer> air; // trick: the air blend it rides on
};

struct Layer {
  std::string key; NodeK name = NodeK::Ground; std::string nameStr;
  float w = 1, dur = 1e-3f, t = 0;
  Pose pose;
  LayerData data;
};

struct FrameInfo {
  std::string kind; Vec3 heading; bool hasHeading = false;
  float tilt = 0.55f, back = 0, lay = 0, catchK = 0, pivot = 1, w = 0; bool useVel = false, aim = false;
  Vec3 O, fwd;
};

class Animator {
 public:
  bool enabled = true;
  Vec3 visP; Quat visQ; Mat4 charToWorld;
  std::string debugNode, debugClip, debugLayers;

  bool build(Rig& rig);
  void update(float dt, const AnimIO& io);
  float twoMax() const { return two_.max; }

 private:
  Rig* rig_ = nullptr;
  Skel skel_; RigData rd_; PoseBuilder b_; ClipLib clips_;
  struct { Pose a, b, c, d, e, idle, loco, tmp, mir, out, prev, fi; } P;
  std::vector<std::unique_ptr<Layer>> layers_;
  std::vector<Pose> pool_;
  const AnimState* A_ = nullptr; AnimIO io_; const World* world_ = nullptr;
  float time_ = 0, dt_ = 1.f / 60, idleT_ = 0, locoPhase_ = 0, airT_ = 0, wallPhase_ = 0;
  float yaw_ = 0; bool yawInit_ = false; float speedH_ = 0, prevSpeed_ = 0, stillT_ = 0, runSpeedMem_ = 0, decel_ = 0, moveIntent_ = 0, wantYaw_ = 0, turnErr_ = 0, yawRate_ = 0;
  Vec3 prevVel_, accel_, velS_;
  Quat frameQ_; bool frameInit_ = false; float pivotW_ = 0, wallK_ = 0; bool wallKInit_ = false;
  std::string frameKind_; float kindT_ = 0;
  Spring lean_{0, 1.6f, 0.8f}, pitchLean_{0, 1.5f, 0.8f};
  float pelvisOff_ = 0, footOff_[2] = {0, 0}; Vec3 footN_[2] = {{0, 1, 0}, {0, 1, 0}};
  Spring lookYaw_{0, 1.4f, 0.9f}, lookPitch_{0, 1.4f, 0.9f};
  Spring3 legSpring_{1.3f, 0.35f}, armSpring_{1.8f, 0.3f};
  Spring impact_{0, 2.2f, 0.45f}; bool impactKicked_ = false;
  float webW_ = 0, webWH_[2] = {0, 0};
  struct Two { float L = 0, R = 0; bool on = false, dropped = false; float t = 0; Vec3 anc; char hand = 0; float max = 0; float hdg = NAN, hRate = 0;
               float spP[2] = {0, 0}, spX[2] = {0, 0}; float sw[2] = {0, 0}; float get(char S) const { return S == 'L' ? L : R; } } two_;
  float swingPh_ = 0; char swingPhHand_ = 'R'; float twoPh_ = 0, swLegW_ = 0, tuckW_ = 0;
  float wrW_ = 0; Vec3 wrOff_; bool wrOffOk_ = false; Vec3 wrHead_{0, 1, 0};
  Vec3 wallUp_{0, 1, 0}, lastWallN_{0, 0, 1}, zipDir_{0, 0, 1};
  std::string prevMode_, prevSub_, lastTrick_, lastTrickDone_; int trickSeq_ = 0; float mirrorBank_ = -1;
  std::map<std::string, float> lastOneShot_;
  struct { std::string key; Quat q, from; float t = 9; } spinS_;
  float wallCorr_ = 0; Vec3 wallCorrN_{0, 0, 1};
  float airLifeW_ = 0;
  bool webGripHas_[2] = {false, false}; Quat webGripQ_[2][3]; std::vector<Quat> webGripF_[2];
  bool twoGripHas_[2] = {false, false}; Quat twoGripQ_[2][3]; std::vector<Quat> twoGripF_[2];
  float twoOffAdd_ = 0; Vec3 dLine_{0, 1, 0};
  struct QY { Spring w{0, 4.6f, 0.72f}, p{1, 5.2f, 0.5f}; Vec3 a; bool has = false; } qy_[2];
  std::vector<float> armMask_;
  float jumpLead_ = -1; bool jumpLeadInit_ = false;
  Spring armSpr_{0, 3, 0.42f}; bool armSprInit_ = false; float armT_ = -1, armAmp_ = 0.3f;
  float climbW_ = 0, climbPh_ = 0;
  float idleFX_ = NAN; int lhf_ = -1; std::map<std::string, float> kneeSpread_;
  ClipLib::LocoMeta walkMeta_; bool walkMetaOk_ = false;
  struct { float rate = 1, k = 1, vN = 1, w = 0; std::string clipA, clipB; } locoInfo_;
  int helpBone_[4][2]; int nHelp_ = -1; struct TwistH { int ti, hi; Vec3 ax; }; std::vector<TwistH> twH_; bool twInit_ = false;
  int handKids_[2][3]; float handLen_[2]; bool handKidsInit_[2] = {false, false}, handKidsOk_[2] = {false, false};
  std::vector<std::vector<std::pair<int, std::pair<Vec3, float>>>> fist_;
  bool nanWarned_ = false;
  struct { bool foot = false; float footW = 0, look = 0, web = 0; } tw_;

  // selection / layers
  std::string select(const AnimState& A);
  std::string trickKey(const std::string& trick);
  std::string groundVariant(Layer* top, const AnimState& A);
  void kickImpact(const AnimState& A);
  Layer* setTarget(const std::string& key);
  // nodes
  void enter(Layer& L, const AnimState& A, Layer* prev);
  bool hold(Layer& L, const AnimState& A);
  void eval(Layer& L, const AnimState& A, Pose& out);
  bool frame(Layer& L, const AnimState& A, FrameInfo& f);
  bool spin(Layer& L, const AnimState& A, Quat& out);
  void evalGround(Layer& L, const AnimState& A, Pose& out);
  void evalAir(Layer& L, const AnimState& A, Pose& out);
  void evalPerch(Layer& L, const AnimState& A, Pose& out);
  void evalSlingshot(Layer& L, const AnimState& A, Pose& out);
  void evalSwing(Layer& L, const AnimState& A, Pose& out);
  void evalCrawl(Layer& L, const AnimState& A, Pose& out);
  void evalWallRun(Layer& L, const AnimState& A, Pose& out);
  bool oneShot(const std::string& name, float t, Pose& out, int lock = 1);
  // frame / post layers
  void updateFrame(float dt, const AnimState& A, Layer& top);
  Vec3 worldToChar(const Vec3& p) const { return charToWorldInv_.transformPoint(p); }
  Vec3 dirToChar(const Vec3& d) const { return visQ.conj() * d; }
  Mat4 charToWorldInv_;
  void wallContact(float dt, const AnimState& A, Layer& top);
  void postWeb(float dt, const AnimState& A, float w);
  void postQuickYank(float dt, const AnimState& A);
  void postSecondary(float dt, const AnimState& A);
  void postLook(float dt, const AnimState& A, float w);
  void postBreath(float dt, Layer& top);
  void postFeet(float dt, const AnimState& A, float w);
  void postAirLife(float dt, const AnimState& A, Layer& top);
  void helpers(Pose& pose);
  // pose helpers
  void widenStance(Pose& pose, float w);
  void freeArmJump(char S, float w);
  void twoHandState(float dt, const AnimState& A, const Vec3* anc, char hand);
  void twoHandBody(float w, char hand);
  void swingLegs(float w, float tuck, char hand);
  bool handKids(char S, int& idx, int& mid, int& pin, float& len);
  struct Grip { Vec3 f, n, wrist; };
  Grip gripFrame(char S, const Vec3& g, const Vec3& d, const Vec3& sh, const Vec3& fwd, float beta);
  Grip overlapFrame(char S, const Vec3& g, const Grip& fw);
  struct Plan { char F, W; bool joining; float w, raw, sw; Vec3 d, g, tW, tF, fW, fF, pnW, pnF, poleW, poleF, shF; };
  bool twoHandPlan(const Vec3& a, Plan& pl);
  void fist(char S, float amount, float w);
  void gripHand(char S, const Vec3& f0, const Vec3& pN, float w, float curl);
  void twoHandIK(const Plan& pl);
  Vec3 headCentre();
  float twoProbe(char F, char W);
  void fallback(Pose& out) { out.copy(skel_.rest); b_.dirty = true; }
  void mirror(Pose& pose) { skel_.mirrorPose(pose, P.mir); pose.copy(P.mir); b_.dirty = true; }
  const std::vector<float>& maskArms();
  void locoPose(Pose& out, float v, const float* ovrK = nullptr, const float* ovrRate = nullptr);
  ClipLib::LocoMeta locoMeta(const std::string& n);
  void walkForm(Pose& pose, float w);
  void zipPhases(Layer& L, const AnimState& A);
  void zipPose(Pose& out, Layer& L, const AnimState& A);
  float idleFootX();
  void runTrack(Pose& pose, float v);
  void armPump(Pose& pose, float v, float w);
  float kneeSpread(const std::string& name);
  void perchSquat(Pose& pose, Layer& L, float land);
  void groundContact(Pose& pose, float w = 1);
  void groundReach(Pose& pose, const AnimState& A);
  void perchSeat(Pose& pose, Layer& L, const AnimState& A);
  bool landHardNeedsFix();
  void threePoint(Pose& pose);
  void solesAbove(Pose& pose, float y0);
  void strideWarp(Pose& pose, float k);
  void jumpArms(Pose& pose, float w, float rise, float fall);
  void jumpTuck(Pose& pose, float w);
  void clingPose(Pose& pose, float w, char side);
  char pickClingSide(const AnimState& A, char prev);
  void palmToWall(char S, const Vec3& dir, float w);
  void soleToWall(char S, const Vec3& dir, float w);
  void climbPose(Pose& pose, float dt, float v, bool moving);
  void matchLocoPhase(Pose& pose, float v);
  void trickPose(Pose& out, LayerData& D, float u, float w);
  bool trickSpin(LayerData& D, float u, float t, Quat& out);
};
