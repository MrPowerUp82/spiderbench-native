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
class Rig;

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
  // The player model with its captured Three.js programs (<bake>/character, capture_shaders.mjs --character):
  // GLB suit material + suitfabric.js patch, skinning, the city's IBL / CSM lighting, shadows into cascades 0-2 and
  // the CSM_char cascade of csm.js. setCharacter() before drawShadows(); drawCharacter() after draw().
  bool openCharacter(const Rig& rig);
  bool hasCharacter() const { return !charParts_.empty(); }
  // focus: the player object's position (render/lighting.js csm.setFocus(player.object world position))
  void setCharacter(const Rig* rig, bool visible, const Vec3& focus) { rig_ = rig; charVisible_ = visible; charFocus_ = focus; }
  bool drawCharacter(const Camera& camera, float time);
  void clear();
  size_t drawn = 0;
  size_t shadowDrawn = 0;
  uint64_t triangles = 0, residentBytes = 0;
  bool healthy = true;
 private:
  BakedGeometryArchive geometry_;
  BakedPools pools_;
  BakedVisibility visibility_;
  BakedEnvironment environment_;
  struct Buffer { GLuint id = 0; uint64_t bytes = 0; uint64_t frame = 0; };
  struct Texture { GLuint id = 0; GLenum target = GL_TEXTURE_2D; };
  struct Mesh { GLuint vao = 0; std::vector<uint32_t> blobs; };
  std::unordered_map<uint32_t, Buffer> buffers_;
  std::unordered_map<GLenum, Texture> placeholders_;
  std::unordered_map<uint64_t, Mesh> meshes_;
  std::vector<std::vector<int>> bindings_;
  std::vector<std::vector<int>> depthBindings_;
  BakedCSM csm_;
  GLuint shadowFbo_ = 0;
  std::vector<Texture> shadowMaps_;
  uint64_t frame_ = 0;
  // Per-mesh metadata and per-program-usage upload plans, extracted once from the JSON manifests
  // (the draw loop runs ~1-2k times per frame across the main pass and the shadow cascades).
  struct MeshInfo {
    struct Group { uint32_t start, count, slot; };
    bool skip = false, instanced = false, frustumCulled = true, receiveShadow = false, transparent = false, hasIndex = false;
    uint32_t layers = 0, shadowLayers = 0, first = 0, count = 0;
    int minCascade = 0, maxCascade = 0, renderOrder = 0, instanceCount = 0, geometryInstances = 0;
    GLenum indexType = GL_UNSIGNED_INT;
    Mat4 model; Vec3 lo, hi;
    BakedPool* pool = nullptr;
    std::vector<Group> groups;
    std::string name;
  };
  struct TextureSlot { std::string name; GLenum target = 0; int size = 1, textureId = -1; bool shadowSampler = false, env = false, shadowMaps = false; };
  struct UsagePlan {
    bool ready = false;
    Shader* shader = nullptr;
    const Json::Value* state = nullptr;
    CapturedUniforms uniforms;
    std::vector<TextureSlot> textures;
    GLint modelMatrix = -1, modelViewMatrix = -1, projectionMatrix = -1, viewMatrix = -1, normalMatrix = -1, cameraPosition = -1,
          isOrthographic = -1, receiveShadow = -1, shadowMatrix = -1, csmParams = -1, lodFrame = -1, lightDirection = -1,
          uTime = -1, time = -1, uSun = -1;
    static constexpr int MAX_CASCADES = 5;
    GLint shadowBias[MAX_CASCADES], shadowNormalBias[MAX_CASCADES], shadowRadius[MAX_CASCADES],
          shadowIntensity[MAX_CASCADES], shadowMapSize[MAX_CASCADES];
  };
  // captured programs + texture manifest of one bake: the city, or the player model (<bake>/character)
  struct Library {
    BakedPrograms programs;
    Json::Value textures;
    std::string directory;
    std::unordered_map<int, Texture> gpu;
    std::vector<UsagePlan> plans;
  };
  Library city_, char_;
  std::vector<MeshInfo> info_;
  Vec3 sunDirection_;
  bool charCascade_ = false; // the preset has a character shadow light after the N sun cascades
  // uniforms shared by every draw of one pass (main view or one shadow cascade)
  struct PassState {
    bool shadow = false; int cascade = -1, cascades = 0, lights = 0;
    Mat4 view, proj; Vec3 eye, lightDirection; float time = 0, lodFrame = 0;
    Mat4 shadowMatrices[UsagePlan::MAX_CASCADES + 1];
  };
  PassState pass_;
  // player model
  struct CharPart { GLuint vao = 0, vaoDepth = 0, vbo = 0, ibo = 0; GLsizei count = 0; int main = -1, depth = -1; };
  std::vector<CharPart> charParts_;
  GLuint boneTex_ = 0; std::vector<Mat4> bones_;
  const Rig* rig_ = nullptr; bool charVisible_ = false; Vec3 charFocus_;
  std::string shaderDir_;
  BakedCascade charLight_; GLuint charMap_ = 0; bool charNear_ = false;
  void prepare();
  UsagePlan* plan(Library& lib, int usageIndex);
  void beginPass(const Camera& camera, const Mat4& view, const Mat4& proj, float time, int cascade);
  bool setupDraw(Library& lib, UsagePlan& p, const Mat4& model, bool receiveShadow);
  bool drawCharacterPass(bool shadow);
  void updateCharacterCascade(const Camera& camera);
  void clearCharacter();
  void traceCharacterMap();
  GLuint buffer(uint32_t id);
  Texture* texture(Library& lib, int id);
  Texture& placeholder(GLenum target, bool shadow = false);
  bool bindMesh(size_t mesh, Shader& shader);
  void setUniforms(Shader& shader, const Json::Value& values);
  void evict();
  bool renderPass(const Camera& camera, const Mat4& view, const Mat4& projection, const Frustum& frustum, float time, int cascade);
};
