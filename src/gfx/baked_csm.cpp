#include "gfx/baked_csm.h"
#include <filesystem>
#include <fstream>
#include <cstdio>

bool BakedCSM::configure(const Json::Value& config, const Vec3& sunDirection) {
  frame = 0; splits_.clear(); cascades.clear();
  if (!config["splits"].isArray() || config["splits"].size() != 4 || config["mapSize"].asInt() <= 0) return false;
  for (const auto& split : config["splits"]) splits_.push_back(split.asDouble());
  size_ = config["mapSize"].asInt(); sun_ = sunDirection.normalized();
  cascades.resize(3);
  for (int i = 0; i < 3; i++) {
    cascades[i].size = i == 2 ? size_ / 2 : size_;
    cascades[i].radius = i == 0 ? 1.6f : i == 1 ? 1.25f : 0.66f;
  }
  return true;
}

void BakedCSM::update(const Camera& camera) {
  ++frame;
  const Vec3 up = std::abs(sun_.y) > 0.99f ? Vec3{0, 0, 1} : UP;
  const Vec3 right = up.cross(sun_).normalized(), lightUp = sun_.cross(right).normalized();
  const double tanV = std::tan(std::max(double(camera.fov), 80.0) * 3.14159265358979323846 / 360);
  const double tanH = tanV * std::max(double(camera.aspect), 16.0 / 9);
  const double k2 = tanV * tanV + tanH * tanH;
  const int periods[] = {1, 2, 4}; const double padding[] = {1, 1.06, 1.12};
  for (int i = 0; i < 3; i++) {
    auto& c = cascades[i]; c.due = frame == 1 || (frame + i) % periods[i] == 0;
    if (!c.due) continue;
    const double n = std::max(splits_[i], double(camera.zNear)), f = std::min(splits_[i + 1], double(camera.zFar));
    double zc = (f + n) * 0.5 * (1 + k2), r;
    if (zc >= f) { zc = f; r = f * std::sqrt(k2); }
    else r = std::sqrt((zc - n) * (zc - n) + n * n * k2);
    r = std::ceil(r * padding[i]);
    Vec3 center = camera.position + camera.direction() * float(zc);
    const double texel = 2 * r / c.size;
    // JavaScript Math.round rounds half toward +infinity, including negatives.
    const auto snap = [](double v, double step) { return float(std::floor(v / step + 0.5) * step); };
    center = right * snap(center.dot(right), texel) + lightUp * snap(center.dot(lightUp), texel) + sun_ * snap(center.dot(sun_), 4);
    const float D = float(r + 900), depthFar = float(D + r + 50);
    c.position = center + sun_ * D;
    c.view = Mat4::lookAt(c.position, center, up);
    c.cullProjection = Mat4::ortho(float(-r), float(r), float(-r), float(r), 1, depthFar);
    c.projection = c.cullProjection;
    c.projection.m[10] = 1 / (depthFar - 1); c.projection.m[14] = depthFar / (depthFar - 1);
    Mat4 bias; bias.m[0] = bias.m[5] = 0.5f; bias.m[12] = bias.m[13] = 0.5f;
    c.matrix = bias * c.projection * c.view;
    c.normalBias = float(texel * 1.4); c.bias = float(texel * 0.6 / (depthFar - 1));
  }
}

bool validateBakedCSM(const std::string& directory) {
  std::ifstream file(std::filesystem::path(directory) / "csm-queries.json");
  Json::Value root; Json::CharReaderBuilder builder; std::string error;
  if (!file || !Json::parseFromStream(builder, file, &root, &error) || root["format"].asString() != "SBCSMCHECK1") return false;
  BakedCSM csm; const auto& sun = root["sunDirection"];
  if (!csm.configure(root["config"], {sun[0].asFloat(), sun[1].asFloat(), sun[2].asFloat()})) return false;
  size_t checked = 0; double maxError = 0;
  for (const auto& frame : root["frames"]) {
    Camera camera; const auto& p = frame["camera"]["position"], q = frame["camera"]["quaternion"];
    camera.position = {p[0].asFloat(), p[1].asFloat(), p[2].asFloat()};
    camera.quaternion = {q[0].asFloat(), q[1].asFloat(), q[2].asFloat(), q[3].asFloat()};
    camera.fov = frame["camera"]["fov"].asFloat(); camera.aspect = frame["camera"]["aspect"].asFloat();
    camera.zNear = frame["camera"]["zNear"].asFloat(); camera.zFar = frame["camera"]["zFar"].asFloat();
    csm.update(camera);
    for (int i = 0; i < 3; i++) {
      const auto& expected = frame["cascades"][i]; const auto& actual = csm.cascades[i];
      bool ok = actual.due == expected["due"].asBool() && actual.size == expected["size"].asInt();
      for (int j = 0; j < 16; j++) {
        double diff = std::abs(actual.matrix.m[j] - expected["matrix"][j].asDouble()); maxError = std::max(maxError, diff); ok &= diff < 0.00005;
      }
      ok &= std::abs(actual.bias - expected["bias"].asDouble()) < 1e-7;
      ok &= std::abs(actual.normalBias - expected["normalBias"].asDouble()) < 1e-5;
      if (!ok) { std::fprintf(stderr, "[baked-csm] mismatch at frame %llu cascade %d\n", (unsigned long long)csm.frame, i); return false; }
      checked++;
    }
  }
  std::printf("[baked-csm] %zu cascade fits match JS (matrix max error %.8g)\n", checked, maxError);
  return true;
}
