#pragma once
#include "gfx/baked_programs.h"
#include "gfx/camera.h"
#include "gfx/baked_csm.h"
#include "gfx/baked_environment.h"
#include "world/baked_geometry.h"
#include "world/baked_pools.h"
#include "world/baked_visibility.h"
#include <unordered_map>
#include <vector>
struct Frustum;

// Native inspection path for the original city. Loads only the buffers and
// textures used by visible meshes, preserving their original attributes.
class BakedCity {
 public:
  bool open(const std::string& directory, const std::string& shaderDirectory);
  bool draw(const Camera& camera, bool reversedDepth, float time);
  bool drawShadows(const Camera& camera, float time);
  bool drawSky(const Camera& camera, int width, int height) { return environment_.drawSky(camera, width, height, frame_); }
  Vec2 jitter(int width, int height) const { return environment_.jitter(width, height); }
  bool drawPost(const Camera& camera, int width, int height, GLuint color, GLuint depth, float dt) {
    return environment_.drawPost(camera, width, height, color, depth, dt);
  }
  void clear();
  size_t drawn = 0;
  size_t shadowDrawn = 0;
  uint64_t triangles = 0, residentBytes = 0;
  bool healthy = true;
 private:
  BakedGeometryArchive geometry_;
  BakedPools pools_;
  BakedVisibility visibility_;
  BakedPrograms programs_;
  BakedEnvironment environment_;
  Json::Value textures_;
  std::string directory_;
  struct Buffer { GLuint id = 0; uint64_t bytes = 0; uint64_t frame = 0; };
  struct Texture { GLuint id = 0; GLenum target = GL_TEXTURE_2D; };
  struct Mesh { GLuint vao = 0; std::vector<uint32_t> blobs; };
  std::unordered_map<uint32_t, Buffer> buffers_;
  std::unordered_map<int, Texture> textureGpu_;
  std::unordered_map<GLenum, Texture> placeholders_;
  std::unordered_map<uint64_t, Mesh> meshes_;
  std::vector<std::vector<int>> bindings_;
  std::vector<std::vector<int>> depthBindings_;
  BakedCSM csm_;
  GLuint shadowFbo_ = 0;
  std::vector<Texture> shadowMaps_;
  uint64_t frame_ = 0;
  GLuint buffer(uint32_t id);
  Texture* texture(int id);
  Texture& placeholder(GLenum target, bool shadow = false);
  bool bindMesh(size_t mesh, Shader& shader);
  void setUniforms(Shader& shader, const Json::Value& values);
  void evict();
  bool renderPass(const Camera& camera, const Mat4& view, const Mat4& projection, const Frustum& frustum, float time, int cascade);
};
