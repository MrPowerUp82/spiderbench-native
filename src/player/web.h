// Web strands (port of the player/web.js behaviour; the look is a lit ribbon instead of the forked / venom shader):
// shoot from the hand to the anchor over shootDur, sag with slack, straighten under tension, snap off and fall on release.
#pragma once
#include "core/math.h"
#include <vector>

struct WebLine { std::vector<Vec3> pts; float width = 0.022f, alpha = 1; };

struct WebAttachOpt { bool instant = false; float shootDur = -1; bool noSplat = false; };

class WebSystem {
 public:
  using AttachOpt = WebAttachOpt;
  bool active() const { return a_.on; }
  bool active2() const { return b_.on; }
  const Vec3& anchor() const { return a_.anchor; }
  void attach(const Vec3& hand, const Vec3& anchor, const Vec3& normal, AttachOpt o = AttachOpt()) { if (a_.on || b_.on) release(); fire(a_, hand, anchor, normal, o); }
  void attachSecond(const Vec3& hand, const Vec3& anchor, const Vec3& normal, AttachOpt o = AttachOpt()) { if (b_.on) kill(b_, 0.25f); fire(b_, hand, anchor, normal, o); }
  void release() { if (a_.on) kill(a_, 0.35f); if (b_.on) kill(b_, 0.35f); }
  void releaseSnap(float dur) { if (a_.on) kill(a_, dur); if (b_.on) kill(b_, dur); }
  void retarget(const Vec3& p, const Vec3& n) { a_.anchor = p; a_.normal = n; }
  void setSlack(float slack, float tension) { a_.slack = slack; a_.tension = tension; }
  void setTaut(float k) { a_.taut = k; b_.taut = k; }
  void update(float dt, const Vec3& hand, const Vec3* hand2);
  const std::vector<WebLine>& lines() const { return lines_; }
  int shotsThisFrame = 0; // new strands fired (audio)

 private:
  struct Strand { bool on = false; float t = 0, shootDur = 0.1f, slack = 0, tension = 0, taut = 0; Vec3 hand, anchor, normal; bool noSplat = false; };
  struct Dying { float t = 0, life = 0.35f; std::vector<Vec3> pts, vel; };
  Strand a_, b_;
  std::vector<Dying> dying_;
  std::vector<WebLine> lines_;
  void fire(Strand& s, const Vec3& hand, const Vec3& anchor, const Vec3& normal, const AttachOpt& o);
  void kill(Strand& s, float life);
  void strandPoints(const Strand& s, std::vector<Vec3>& out) const;
};
