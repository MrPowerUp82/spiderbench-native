#include "player/traversal/helpers.h"
#include "world/baked_collision.h"
#include <algorithm>

// ================================================================= collide.js (exact collider over the baked solids)
namespace {
struct Shape { uint8_t type; float x0, y0, z0, x1, y1, z1, cx, cz, rad; };
Shape shapeOf(const BakedCollision& g, uint32_t i) {
  const float* b = g.bb.data() + size_t(i) * 6; const float* p = g.par.data() + size_t(i) * 6;
  Shape s{g.type[i], b[0], b[1], b[2], b[3], b[4], b[5], 0, 0, 0};
  if (s.type == 1) { s.cx = p[0]; s.cz = p[1]; s.rad = std::max(p[2], p[3]); }
  return s;
}

std::optional<Contact> pushOutExact(const BakedCollision& g, Vec3& feet, float r, float h, float stepH) {
  std::optional<Contact> best;
  const float lo = feet.y + stepH, hi = feet.y + h;
  for (int iter = 0; iter < 3; iter++) {
    bool moved = false;
    g.query(feet.x - (r + 0.05f), feet.z - (r + 0.05f), feet.x + r + 0.05f, feet.z + r + 0.05f, [&](uint32_t i) {
      const Shape b = shapeOf(g, i);
      if (b.y0 >= hi || b.y1 <= lo) return;
      float nx, nz, depth, cx, cz;
      if (b.type == 1) { // vertical cylinder / cone: radial
        const float dx = feet.x - b.cx, dz = feet.z - b.cz, d = std::hypot(dx, dz);
        if (d >= r + b.rad) return;
        if (d > 1e-6f) { nx = dx / d; nz = dz / d; } else { nx = 1; nz = 0; }
        depth = r + b.rad - d; cx = b.cx + nx * b.rad; cz = b.cz + nz * b.rad;
      } else {
        cx = std::min(std::max(feet.x, b.x0), b.x1); cz = std::min(std::max(feet.z, b.z0), b.z1);
        const float dx = feet.x - cx, dz = feet.z - cz, d2 = dx * dx + dz * dz;
        if (d2 >= r * r) return;
        if (d2 > 1e-10f) { const float d = std::sqrt(d2); nx = dx / d; nz = dz / d; depth = r - d; }
        else { // centre inside the footprint: push out along the shallowest face
          const float e[4] = {feet.x - b.x0, b.x1 - feet.x, feet.z - b.z0, b.z1 - feet.z};
          int k = 0; for (int j = 1; j < 4; j++) if (e[j] < e[k]) k = j;
          nx = k == 0 ? -1.f : k == 1 ? 1.f : 0.f; nz = k == 2 ? -1.f : k == 3 ? 1.f : 0.f; depth = e[k] + r;
        }
      }
      const float top = b.type == 2 ? g.top(i, cx, cz) : b.y1;
      if (top <= lo) return; // ramp surface below the step band
      feet.x += nx * (depth + 1e-4f); feet.z += nz * (depth + 1e-4f); moved = true;
      if (!best || depth > best->depth) {
        Contact c; c.normal = {nx, 0, nz}; c.point = {feet.x - nx * r, feet.y + h * 0.5f, feet.z - nz * r}; c.box = int(i); c.depth = depth; c.top = top;
        if (std::fabs(nx) > 0.999f) c.normal = {signf(nx), 0, 0}; else if (std::fabs(nz) > 0.999f) c.normal = {0, 0, signf(nz)};
        best = c;
      }
    });
    if (!moved) break;
  }
  if (best) { // report the top of the whole obstacle stack in front (wall + parapet + coping), for vault / wall-run decisions
    float top = best->top; const float px = best->point.x - best->normal.x * 0.05f, pz = best->point.z - best->normal.z * 0.05f;
    for (int k = 0; k < 6; k++) {
      float next = top;
      g.query(px - 0.02f, pz - 0.02f, px + 0.02f, pz + 0.02f, [&](uint32_t i) {
        const Shape b = shapeOf(g, i);
        if (b.y0 <= top + 0.05f && b.y1 > next && px >= b.x0 - 0.02f && px <= b.x1 + 0.02f && pz >= b.z0 - 0.02f && pz <= b.z1 + 0.02f) next = b.type == 2 ? g.top(i, px, pz) : b.y1;
      });
      if (next <= top + 1e-3f) break; top = next;
    }
    best->top = top;
  }
  return best;
}
}  // namespace

// ================================================================= collide.js (box path)
std::optional<Contact> pushOutCapsule(const World& w, Vec3& feet, float r, float h, float stepH) {
  if (const BakedCollision* g = w.collision()) return pushOutExact(*g, feet, r, h, stepH);
  std::optional<Contact> best;
  float lo = feet.y + stepH, hi = feet.y + h;
  std::vector<int> ids;
  for (int iter = 0; iter < 3; iter++) {
    bool moved = false;
    w.nearBoxes(feet.x, feet.z, r + 0.05f, ids);
    for (int i : ids) {
      const Box& b = w.boxes[i];
      if (b.mn.y >= hi || b.mx.y <= lo) continue;
      float nx, nz, depth;
      float cx = std::clamp(feet.x, b.mn.x, b.mx.x), cz = std::clamp(feet.z, b.mn.z, b.mx.z);
      float dx = feet.x - cx, dz = feet.z - cz, d2 = dx * dx + dz * dz;
      if (d2 >= r * r) continue;
      if (d2 > 1e-10f) { float d = std::sqrt(d2); nx = dx / d; nz = dz / d; depth = r - d; }
      else { // centre inside the footprint: push out along the shallowest face
        float e[4] = {feet.x - b.mn.x, b.mx.x - feet.x, feet.z - b.mn.z, b.mx.z - feet.z};
        int k = 0; for (int j = 1; j < 4; j++) if (e[j] < e[k]) k = j;
        nx = k == 0 ? -1.f : k == 1 ? 1.f : 0.f; nz = k == 2 ? -1.f : k == 3 ? 1.f : 0.f; depth = e[k] + r;
      }
      float top = b.mx.y;
      if (top <= lo) continue;
      feet.x += nx * (depth + 1e-4f); feet.z += nz * (depth + 1e-4f); moved = true;
      if (!best || depth > best->depth) {
        Contact c; c.normal = {nx, 0, nz}; c.point = {feet.x - nx * r, feet.y + h * 0.5f, feet.z - nz * r}; c.box = i; c.depth = depth; c.top = top;
        if (std::fabs(nx) > 0.999f) c.normal = {signf(nx), 0, 0}; else if (std::fabs(nz) > 0.999f) c.normal = {0, 0, signf(nz)};
        best = c;
      }
    }
    if (!moved) break;
  }
  if (best) { // report the top of the whole obstacle stack in front (wall + parapet + coping) for vault / wall-run decisions
    float top = best->top; float px = best->point.x - best->normal.x * 0.05f, pz = best->point.z - best->normal.z * 0.05f;
    for (int k = 0; k < 6; k++) {
      float next = top;
      w.nearBoxes(px, pz, 0.02f, ids);
      for (int i : ids) { const Box& b = w.boxes[i]; if (b.mn.y <= top + 0.05f && b.mx.y > next && px >= b.mn.x - 0.02f && px <= b.mx.x + 0.02f && pz >= b.mn.z - 0.02f && pz <= b.mx.z + 0.02f) next = b.mx.y; }
      if (next <= top + 1e-3f) break; top = next;
    }
    best->top = top;
  }
  return best;
}

// ================================================================= anchors.js
void AnchorFinder::faceCandidates(const Vec3& pos, const Vec3& D, const Vec3& fwd, const Vec3& right, const Pass& o) {
  cands_.clear();
  world_.nearBoxes(pos.x, pos.z, o.radius, near_);
  for (int i : near_) {
    const Box& b = world_.boxes[i]; float top = b.mx.y;
    if (b.kind == K_POLE || b.kind == K_TRUNK) continue;
    if (top < pos.y + o.minAbove) continue;
    if (b.mx.x - b.mn.x < 2.5f && b.mx.z - b.mn.z < 2.5f) continue;
    float yLo = std::max(b.mn.y + 0.8f, pos.y + o.minAbove), yHi = top - 0.45f;
    if (yHi < yLo) continue;
    float ay = std::min(std::max(D.y, yLo), yHi);
    for (int f = 0; f < 4; f++) {
      int ax = f < 2 ? 0 : 2; float sgn = f % 2 == 0 ? -1.f : 1.f, plane = sgn < 0 ? b.mn[ax] : b.mx[ax];
      float pc = ax == 0 ? pos.x : pos.z;
      if ((pc - plane) * sgn < 4) continue; // player must be well in front of the face
      int tax = ax == 0 ? 2 : 0; float t0 = b.mn[tax] + 0.35f, t1 = b.mx[tax] - 0.35f; if (t1 < t0) continue;
      float dt = tax == 0 ? D.x : D.z; float tv = std::min(std::max(dt, t0), t1);
      Vec3 A = ax == 0 ? Vec3{plane, ay, tv} : Vec3{tv, ay, plane};
      Vec3 rel = A - pos;
      float L = rel.length(); if (L < o.minL || L > o.maxL) continue;
      float ahead = rel.x * fwd.x + rel.z * fwd.z; if (ahead < o.minAhead) continue;
      float lat = rel.x * right.x + rel.z * right.z, hl = std::hypot(rel.x, rel.z);
      float elev = std::atan2(rel.y, hl);
      float score = A.distanceTo(D) / 10 + std::max(0.f, A.y - D.y) * 0.08f;
      score += std::max(0.f, std::fabs(lat) - o.maxLat) * 0.25f;
      score += std::max(0.f, o.elevLo - elev) * 4 + std::max(0.f, elev - o.elevHi) * 3;
      score -= std::min(ahead, 40.f) * 0.02f;
      if (o.turn) { float nd = ax == 0 ? sgn * o.turn->x : sgn * o.turn->z; if (nd < -0.5f) score += 4; else if (std::fabs(nd) < 0.5f) score -= 0.5f; }
      cands_.push_back({A, {ax == 0 ? sgn : 0.f, 0, ax == 2 ? sgn : 0.f}, L, score, lat});
    }
  }
  std::sort(cands_.begin(), cands_.end(), [](const Cand& a, const Cand& b) { return a.score < b.score; });
}

std::optional<Anchor> AnchorFinder::confirm(const Vec3& pos, const Cand& c, bool strict) {
  Vec3 d = c.point - pos; float len = d.length(); d /= len;
  Hit h; if (!world_.raycast(pos, d, len + 1.5f, h)) return std::nullopt;
  if (h.point.distanceTo(c.point) > 1.2f) {
    if (strict) return std::nullopt;
    if (std::fabs(h.normal.y) > 0.5f || h.distance < len * 0.6f || h.point.y < pos.y + 4) return std::nullopt;
    return Anchor{h.point, h.normal, h.distance, c.lat, "wall"};
  }
  return Anchor{h.point, h.normal, h.distance, c.lat, "wall"};
}

bool AnchorFinder::arcClear(const Vec3& pos, float pivotY, const Vec3& pivot, float rope) {
  Vec3 bottom{pivot.x, pivotY - rope, pivot.z};
  Vec3 d = bottom - pos; float len = d.length(); if (len < 1) return true;
  Hit h; bool hit = world_.raycast(pos, d / len, len, h);
  return !hit || h.normal.y > 0.7f;
}

std::optional<Anchor> AnchorFinder::coneRays(const Vec3& pos, const Vec3& fwd) {
  std::optional<Anchor> best; float bestScore = INF;
  for (float e : {0.95f, 1.15f, 0.75f, 1.3f}) for (float y : {0.f, 0.35f, -0.35f, 0.7f, -0.7f, 1.05f, -1.05f}) {
    float cy = std::cos(y), sy = std::sin(y);
    float fx = fwd.x * cy + fwd.z * sy, fz = -fwd.x * sy + fwd.z * cy;
    Vec3 dir = Vec3{fx * std::cos(e), std::sin(e), fz * std::cos(e)}.normalized();
    Hit h; if (!world_.raycast(pos, dir, 90, h) || h.point.y < pos.y + 5 || h.distance < 9 || h.normal.y > 0.7f) continue;
    float score = std::fabs(e - 0.95f) * 2 + std::fabs(y) * 1.2f + std::fabs(h.distance - 30) / 20;
    if (score < bestScore) { bestScore = score; best = Anchor{h.point, h.normal, h.distance, 0, "wall"}; }
  }
  return best;
}

std::optional<Anchor> AnchorFinder::find(const Vec3& pos, const Vec3& fwd, const Vec3* turn, float speed, float floorY) {
  Vec3 want = turn ? (fwd * 0.55f + *turn * 0.9f).normalized() : fwd;
  Vec3 right{-want.z, 0, want.x};
  float hAbove = pos.y - floorY;
  // altitude band: the desired anchor sits ~30-40 m over the street (chains stay in the canyon)
  float streetY = std::min(floorY, world_.groundHeight(pos.x, pos.z, 0.6f));
  float band = streetY + clampf(30 + speed * 0.25f, 30, 40);
  bool high = pos.y > band - 4;
  Pass passes[2] = {
    {clampf(12 + speed * 0.75f, 18, 40) * clampf(hAbove / 20, 0.55f, 1), clampf(15 + speed * 0.25f, 15, 26) * clampf(1.25f - (hAbove - 18) / 40, 0.4f, 1), 70, 5, 9, 72, 2, 22, 0.5f, 1.3f},
    {clampf(16 + speed * 0.7f, 22, 46), 26, 95, 4, 8, 95, 4, 40, 0.3f, 1.4f},
  };
  if (high) for (auto& P : passes) { P.minAbove = 1.5f; P.elevLo = 0.12f; }
  for (auto& P : passes) {
    Vec3 D{pos.x + want.x * P.ahead, std::max(pos.y + P.minAbove + 1, std::min(pos.y + P.up, band)), pos.z + want.z * P.ahead};
    P.turn = turn;
    faceCandidates(pos, D, want, right, P);
    int tries = 0;
    for (const auto& c : cands_) {
      if (++tries > 7) break;
      auto a = confirm(pos, c, turn != nullptr); if (!a) continue;
      float pivotY = a->point.y, rope = std::max(5.f, std::min(a->L, pivotY - floorY - 3.2f));
      if (!arcClear(pos, pivotY, a->point, rope) && tries < 6) continue;
      return a;
    }
  }
  // low swings off street furniture / trees / water towers (parks, waterfront, wide avenues)
  std::vector<ZipPoint> pts; world_.getZipPoints(pos, 40, pts);
  std::vector<const TreePt*> tps; world_.treesNear(pos, 60, tps);
  for (auto* t : tps) pts.push_back({t->pos, {0, 1, 0}, "tree", -1});
  const ZipPoint* best = nullptr; float bs = INF;
  for (const auto& p : pts) {
    Vec3 rel = p.pos - pos; bool tree = p.kind == "tree";
    float up = rel.y; if (up < (tree ? 4.f : 1.f) || p.pos.y - floorY < (tree ? 9.f : 6.f)) continue;
    if (tree && up < std::hypot(rel.x, rel.z) * 0.45f) continue;
    float ahead = rel.x * want.x + rel.z * want.z; if (ahead < 1) continue;
    float L = rel.length(); if (L < 5 || L > 40) continue;
    float s = std::fabs(ahead - 12) / 8 + std::fabs(rel.x * right.x + rel.z * right.z) / 10 - std::min(up, 20.f) / 20;
    if (s < bs) { if (world_.raycast(pos, rel / L, L - (tree ? 3.f : 0.5f))) continue; bs = s; best = &p; }
  }
  if (best) return Anchor{best->pos + Vec3{0, 0.1f, 0}, best->normal, best->pos.distanceTo(pos), 0, "low"};
  if (hAbove > 3) return coneRays(pos, want);
  return std::nullopt;
}

// ================================================================= zippoints.js targeting
int64_t ZipTargeting::key(const Vec3& p) {
  int64_t x = (int64_t)std::lround(p.x * 10) & 0x1FFFFF, y = (int64_t)std::lround(p.y * 10) & 0x1FFFFF, z = (int64_t)std::lround(p.z * 10) & 0x1FFFFF;
  return (x << 42) | (y << 21) | z;
}

ZipTargeting::Meta ZipTargeting::pointMeta(const ZipPoint& p) {
  int64_t k = key(p.pos); auto it = meta_.find(k); if (it != meta_.end()) return it->second;
  float below = world_.groundHeight(p.pos.x + p.normal.x * 0.02f, p.pos.z + p.normal.z * 0.02f, p.pos.y - 0.6f);
  float h = p.pos.y - std::min(below, world_.groundHeight(p.pos.x + p.normal.x * 1.2f, p.pos.z + p.normal.z * 1.2f, p.pos.y - 0.6f));
  bool hit = world_.raycast({p.pos.x, p.pos.y + 0.6f, p.pos.z}, {0, -1, 0}, 1.6f);
  float gh = world_.groundHeight(p.pos.x, p.pos.z, p.pos.y + 0.3f);
  bool solid = hit || std::fabs(gh - p.pos.y) < 0.3f;
  float minH = p.kind == "ledge" ? 8.f : (p.kind == "roofEdge" || p.kind == "roofCorner" || p.kind == "waterTower") ? 6.f : 4.5f;
  Meta m{h, solid && h >= minH};
  if (meta_.size() > 4000) meta_.clear();
  meta_[k] = m; return m;
}

bool ZipTargeting::visible(const ZipPoint& p, const Vec3& eye) {
  int64_t k = key(p.pos); auto it = vis_.find(k);
  if (it != vis_.end() && time_ - it->second.t < 0.25f) return it->second.ok;
  Vec3 tgt = p.pos + p.normal * 0.35f; tgt.y += 0.35f;
  Vec3 dir = tgt - eye; float len = dir.length(); dir /= len;
  Hit h; bool ok = !world_.raycast(eye, dir, len, h) || h.distance > len - 0.7f;
  if (vis_.size() > 600) vis_.clear();
  vis_[k] = {time_, ok}; return ok;
}

static float kindBonus(const std::string& k) {
  if (k == "roofEdge") return -0.45f; if (k == "roofCorner") return -0.5f; if (k == "waterTower") return -0.35f; if (k == "ledge") return 0.15f;
  if (k == "lampTop" || k == "signalMast") return 0.1f; if (k == "antenna") return 0.05f; if (k == "pole") return 0.2f; return 0;
}

const ZipTarget* ZipTargeting::update(float dt, const Camera& camera, const Vec3& eye, const Opts& o) {
  time_ += dt; poolT_ += dt;
  shown_.clear();
  if (!o.enabled) { hasBest_ = false; bestKey_ = 0; return nullptr; }
  if (poolT_ > 0.15f) { poolT_ = 0; world_.getZipPoints(eye, RANGE, pool_); }
  Vec3 cf = camera.direction();
  struct Sc { const ZipPoint* p; float dist, ang, score, sx, sy; };
  std::vector<Sc> scored;
  for (const auto& p : pool_) {
    float dist = p.pos.distanceTo(eye);
    if (dist < 3 || dist > RANGE) continue;
    if (o.exclude && p.pos.distanceToSquared(*o.exclude) < 4) continue;
    Vec3 ndc = camera.project(p.pos);
    if (ndc.z > 1 || std::fabs(ndc.x) > 0.92f || std::fabs(ndc.y) > 0.9f) continue;
    Vec3 dir = (p.pos - camera.position).normalized();
    float ang = std::acos(clampf(dir.dot(cf), -1, 1));
    Meta m = pointMeta(p); if (!m.ok) continue;
    float score = ang / 0.3f + dist / RANGE * 0.9f + kindBonus(p.kind) - std::min(m.h, 40.f) / 40 * 0.35f;
    if (o.air) score += std::max(0.f, eye.y - p.pos.y - 3) * 0.07f;
    if (p.pos.y < eye.y - 12) score += 0.5f;
    if (p.pos.y > eye.y + 30) score += 0.35f;
    scored.push_back({&p, dist, ang, score, ndc.x, ndc.y});
  }
  std::sort(scored.begin(), scored.end(), [](const Sc& a, const Sc& b) { return a.score < b.score; });
  const Sc* nb = nullptr; const Sc* prevEntry = nullptr; Sc fallback{};
  std::vector<const Sc*> show;
  for (size_t i = 0; i < scored.size() && show.size() < 12; i++) {
    const Sc& e = scored[i];
    if (i >= 16) break;
    if (!visible(*e.p, eye)) continue;
    if (!nb && e.ang < 0.55f) nb = &e;
    if (e.ang < 0.42f && show.size() < 3) show.push_back(&e);
    if (key(e.p->pos) == bestKey_ && bestKey_) prevEntry = &e;
  }
  if (!nb && o.perchOut) { // perched with nothing in view: best point out in front of the perch
    float bs = INF;
    for (const auto& p : pool_) {
      Vec3 d = p.pos - eye; float dist = d.length(); if (dist < 3 || dist > RANGE) continue;
      if (o.exclude && p.pos.distanceToSquared(*o.exclude) < 4) continue;
      d /= dist; float out = d.x * o.perchOut->x + d.z * o.perchOut->z; if (out < 0.2f) continue;
      Meta m = pointMeta(p); if (!m.ok || !visible(p, eye)) continue;
      float sc = dist / RANGE - out + kindBonus(p.kind) - (p.pos.y > eye.y - 2 ? 0.3f : 0);
      if (sc < bs) { bs = sc; fallback = {&p, dist, 0, sc, 0, 0}; nb = &fallback; }
    }
    if (nb) show.push_back(nb);
  }
  if (prevEntry && prevEntry->ang < 0.6f && (!nb || prevEntry->score < nb->score + 0.25f)) nb = prevEntry;
  if (nb && std::find(show.begin(), show.end(), nb) == show.end()) { show.insert(show.begin(), nb); if (show.size() > 3) show.resize(3); }
  hasBest_ = nb != nullptr;
  if (nb) { best_ = {nb->p->pos, nb->p->normal, nb->p->kind, nb->dist, nb->sx, nb->sy}; bestKey_ = key(nb->p->pos); } else bestKey_ = 0;
  for (auto* e : show) shown_.push_back({{e->p->pos, e->p->normal, e->p->kind, e->dist, e->sx, e->sy}, e->ang, e->score, e == nb});
  return hasBest_ ? &best_ : nullptr;
}
