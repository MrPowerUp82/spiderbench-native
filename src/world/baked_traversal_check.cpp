// --validate-baked-traversal: World queries over the baked city against the original JS answers exported by
// tools/ref/bake_traversal.mjs (makeQueries groundHeight / raycast, zippoints.js query).
// Collision solids are exact, so every answer that does not touch the terrain must match. The terrain is a 0.25 m
// raster of an analytic function: answers decided by the terrain may differ within one sample of a curb or shoreline.
#include "world/baked_traversal.h"
#include "world/world.h"
#include <json/json.h>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace {
Vec3 vec(const Json::Value& v) { return {v[0].asFloat(), v[1].asFloat(), v[2].asFloat()}; }
// The queries arrive as JS doubles and the game works in float: a world coordinate near 3 km carries ~2.4e-4 m of
// rounding, which slopes (ramps, cones, height fields) and ray origins turn into answers a few 1e-4 m apart.
constexpr float POS_TOL = 2e-3f;
}  // namespace

bool validateBakedTraversal(const World& world, const std::string& path) {
  std::ifstream file(path);
  Json::Value root; Json::CharReaderBuilder builder; std::string error;
  if (!file || !Json::parseFromStream(builder, file, &root, &error) || root["format"].asString() != "SBTRV1") {
    std::fprintf(stderr, "[validate] cannot read %s %s\n", path.c_str(), error.c_str()); return false;
  }
  // ground heights
  int terrainDiff = 0, groundBad = 0, groundTerrain = 0;
  for (const auto& q : root["ground"]) {
    const float x = q["x"].asFloat(), z = q["z"].asFloat(), y = q["y"].asFloat();
    const bool terrainOk = world.terrainAt(x, z) == q["terrain"].asFloat();
    if (!terrainOk) terrainDiff++;
    const bool ok = std::fabs(world.groundHeight(x, z) - q["free"].asFloat()) <= POS_TOL && std::fabs(world.groundHeight(x, z, y) - q["step"].asFloat()) <= POS_TOL;
    if (!ok) {
      (terrainOk ? groundBad : groundTerrain)++;
      if (terrainOk && groundBad <= 5)
        std::fprintf(stderr, "[validate] ground (%.3f %.3f y %.3f): %.4f/%.4f, JS %.4f/%.4f\n", x, z, y,
                     world.groundHeight(x, z), world.groundHeight(x, z, y), q["free"].asFloat(), q["step"].asFloat());
    }
  }
  // rays
  int rayBad = 0, rayTerrain = 0, hits = 0;
  for (const auto& q : root["rays"]) {
    const Vec3 o = vec(q["o"]), d = vec(q["d"]);
    Hit h; const bool hit = world.raycast(o, d, q["max"].asFloat(), h);
    const Json::Value& e = q["hit"];
    bool ok = hit == !e.isNull();
    if (ok && hit) {
      hits++;
      ok = std::fabs(h.distance - e["t"].asFloat()) <= POS_TOL && h.normal.dot(vec(e["n"])) > 0.999f && h.point.distanceTo(vec(e["p"])) < 0.01f;
    }
    if (!ok) {
      // a terrain answer on either side: the raster may move the ground hit by one sample
      const bool terrain = (!e.isNull() && e["ground"].asBool()) || (hit && h.normal.y > 0.99f && h.point.y < 0.4f && h.point.y > -2.1f);
      (terrain ? rayTerrain : rayBad)++;
      if (!terrain && rayBad <= 5)
        std::fprintf(stderr, "[validate] ray o(%.2f %.2f %.2f) d(%.3f %.3f %.3f): %s t=%.4f, JS %s t=%.4f\n", o.x, o.y, o.z, d.x, d.y, d.z,
                     hit ? "hit" : "miss", hit ? h.distance : 0.f, e.isNull() ? "miss" : "hit", e.isNull() ? 0.f : e["t"].asFloat());
    }
  }
  // zip point queries
  int zipBad = 0; std::vector<ZipPoint> pts;
  for (const auto& q : root["zips"]) {
    world.getZipPoints(vec(q["c"]), q["r"].asFloat(), pts);
    bool ok = int(pts.size()) == q["count"].asInt();
    const Json::Value& first = q["first"];
    for (Json::ArrayIndex i = 0; ok && i < first.size(); i++) {
      const Vec3 p{first[i][0].asFloat(), first[i][1].asFloat(), first[i][2].asFloat()};
      // equal distances may be ordered differently: accept the same point anywhere among the first entries
      bool found = false;
      for (size_t k = 0; k < pts.size() && k < first.size() + 4 && !found; k++)
        found = pts[k].pos.distanceTo(p) < 1e-4f && pts[k].kind == first[i][6].asString();
      ok = found;
    }
    if (!ok) zipBad++;
  }
  const int nG = int(root["ground"].size()), nR = int(root["rays"].size()), nZ = int(root["zips"].size());
  std::printf("[validate] terrain: %d/%d samples differ (raster edge)\n", terrainDiff, nG);
  std::printf("[validate] groundHeight: %d/%d mismatches (+%d at terrain edges)\n", groundBad, nG, groundTerrain);
  std::printf("[validate] raycast: %d/%d mismatches (+%d on terrain), %d hits compared\n", rayBad, nR, rayTerrain, hits);
  std::printf("[validate] getZipPoints: %d/%d mismatches\n", zipBad, nZ);
  const bool ok = groundBad == 0 && rayBad == 0 && zipBad == 0 && groundTerrain * 50 <= nG && rayTerrain * 50 <= nR;
  std::printf("[validate] traversal queries %s\n", ok ? "match the original" : "FAILED");
  return ok;
}
