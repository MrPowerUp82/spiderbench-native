#pragma once
#include "gfx/baked_programs.h"
#include "gfx/camera.h"
#include "world/baked_geometry.h"
#include <unordered_map>
#include <vector>

class BakedEnvironment {
 public:
  bool open(const std::string& directory);
  bool drawSky(const Camera& camera, int width, int height, uint64_t frame);
  bool drawPost(const Camera& camera, int width, int height, GLuint color, GLuint depth, float dt);
  Vec2 jitter(int width, int height) const;
  bool validate(const std::string& reportPath = "");
  void clear();
  GLuint environment() const;
  float exposure() const { return programs_.manifest["exposure"].asFloat(); }
  const Json::Value& manifest() const { return programs_.manifest; }
 private:
  struct Texture { GLuint id = 0; GLenum target = 0; int width = 0, height = 0; };
  BakedPrograms programs_;
  BakedGeometryArchive archive_;
  std::vector<Texture> textures_;
  std::unordered_map<uint32_t, GLuint> buffers_;
  std::unordered_map<uint64_t, GLuint> vaos_;
  GLuint fbo_ = 0;
  uint64_t postFrame_ = 0;
  Vec3 previousPosition_;
  Quat previousQuaternion_;
  Mat4 previousViewProjection_;
  int previousWidth_ = 0, previousHeight_ = 0;
  bool attach(const Json::Value& target);
  bool draw(const Json::Value& job, const Camera* camera = nullptr, int width = 0, int height = 0, uint64_t frame = 0,
            const std::unordered_map<uint32_t, GLuint>* overrides = nullptr);
  GLuint buffer(uint32_t id);
  bool allocateTextures();
};
