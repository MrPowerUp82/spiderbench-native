// Character rig: GLB skeleton + clip mixer (crossfades like three.js AnimationMixer.fadeIn/fadeOut), skinning palette,
// procedural bone aim (web arm -> anchor) and hand positions. Port of player/rig.js (clip path).
#pragma once
#include "asset/gltf.h"
#include <string>
#include <vector>

class Rig {
 public:
  GltfModel model;
  float heightOffset = 0; // added to the model so the feet rest at y = 0 (rig.js: root.position.y -= box.min.y)
  float modelScale = 1;

  bool load(const std::string& glbPath);
  bool hasClip(const std::string& name) const { return model.findClip(name) >= 0; }
  float clipDuration(const std::string& name) const { int c = model.findClip(name); return c >= 0 ? model.clips[c].duration : 0; }
  // looped / one-shot clip at its own clock; returns false if the clip does not exist
  bool play(const std::string& name, float fade = 0.2f, float timeScale = 1.f, bool loop = true);
  // clip held at normalized time t01 (driven by gameplay state, e.g. swing phase)
  bool drive(const std::string& name, float t01, float fade = 0.2f);
  const std::string& currentName() const { return curName_; }
  float currentTime01() const;
  void setTimeScale(float ts);

  void update(float dt);              // advance + blend -> local pose -> world matrices
  void setObjectMatrix(const Mat4& m) { object_ = m * Mat4::translate({0, heightOffset, 0}) * Mat4::scale({modelScale, modelScale, modelScale}); }
  const Mat4& objectMatrix() const { return object_; }
  // procedural layers (call after update, before skinMatrices)
  void aimBone(const std::string& bone, const Vec3& worldDir, float w = 1.f);
  Vec3 boneWorld(const std::string& bone) const;
  Vec3 handWorld(char side) const;
  // skinning palette in WORLD space (object matrix folded in), per skin joint
  void skinMatrices(std::vector<Mat4>& out) const;
  // direct pose access for the animation layer (anim/animator.cpp)
  void setLocal(int node, const Vec3& t, const Quat& r) { poseT_[node] = t; poseR_[node] = r; }
  void updateWorld() { computeWorld(); }
  Vec3 nodeWorldPos(int node) const { return (object_ * world_[node]).position(); }

 private:
  struct Action { int clip; float time; float timeScale; float weight; float target; float fadeRate; bool loop; bool driven; };
  std::vector<Action> actions_;
  std::string curName_;
  std::vector<Vec3> restT_, poseT_; std::vector<Quat> restR_, poseR_; std::vector<Vec3> restS_, poseS_;
  std::vector<Mat4> local_, world_; // world_ = model space (before object_)
  std::vector<int> order_;          // parents before children
  Mat4 object_;
  Action* startAction(int clip, float fade, bool loop, bool driven);
  void sample(const GltfClip& c, float t, std::vector<Vec3>& T, std::vector<Quat>& R, std::vector<Vec3>& S) const;
  void computeWorld();
  int node(const std::string& n) const { return model.findNode(n); }
};
