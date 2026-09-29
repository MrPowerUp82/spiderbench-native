#include "player/web.h"

static constexpr int SEG = 24;

void WebSystem::fire(Strand& s, const Vec3& hand, const Vec3& anchor, const Vec3& normal, const AttachOpt& o) {
  s.on = true; s.hand = hand; s.anchor = anchor; s.normal = normal.normalized(); s.noSplat = o.noSplat;
  s.shootDur = o.shootDur > 0 ? o.shootDur : clampf(hand.distanceTo(anchor) / 380.f, 0.05f, 0.16f);
  s.t = o.instant ? s.shootDur : 0; s.slack = 0; s.tension = 0; s.taut = 0;
  shotsThisFrame++;
}

void WebSystem::kill(Strand& s, float life) {
  Dying d; d.life = life;
  strandPoints(s, d.pts);
  // the strand whips loose from the hand end and falls away from the anchor
  for (size_t i = 0; i < d.pts.size(); i++) { float u = (float)i / (d.pts.size() - 1); d.vel.push_back(Vec3{0, -2.f - 4.f * (1 - u), 0}); }
  dying_.push_back(std::move(d));
  s.on = false;
}

void WebSystem::strandPoints(const Strand& s, std::vector<Vec3>& out) const {
  out.clear();
  float k = s.shootDur > 0 ? clampf(s.t / s.shootDur, 0, 1) : 1;
  Vec3 tip = vlerp(s.hand, s.anchor, k);
  float L = s.hand.distanceTo(tip);
  // sag: slack webs hang (parabola under gravity), taut webs are straight; a flying strand wobbles a little
  float sag = L * (0.02f + 0.16f * s.slack) * (1 - 0.8f * std::max(s.tension, s.taut));
  if (k < 1) sag = L * 0.015f;
  for (int i = 0; i <= SEG; i++) {
    float u = (float)i / SEG;
    Vec3 p = vlerp(s.hand, tip, u);
    p.y -= sag * 4 * u * (1 - u);
    out.push_back(p);
  }
}

void WebSystem::update(float dt, const Vec3& hand, const Vec3* hand2) {
  lines_.clear();
  if (a_.on) { a_.hand = hand; a_.t += dt; }
  if (b_.on) { b_.hand = hand2 ? *hand2 : hand; b_.t += dt; }
  for (Strand* s : {&a_, &b_}) if (s->on) { WebLine l; strandPoints(*s, l.pts); lines_.push_back(std::move(l)); }
  for (auto& d : dying_) {
    d.t += dt;
    for (size_t i = 0; i < d.pts.size(); i++) { d.vel[i].y -= 18 * dt; d.pts[i] += d.vel[i] * dt; }
    WebLine l; l.pts = d.pts; l.alpha = clampf(1 - d.t / d.life, 0, 1); l.width = 0.018f;
    lines_.push_back(std::move(l));
  }
  dying_.erase(std::remove_if(dying_.begin(), dying_.end(), [](const Dying& d) { return d.t >= d.life; }), dying_.end());
}
