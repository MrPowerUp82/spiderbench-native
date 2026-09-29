// Audio (subset of game/systems/audio.js): SDL audio device + software mixer.
// Sound sprites and their variants / gains come from the original manifest (assets/audio_manifest.json); music loops;
// a procedural wind bed follows the player's speed.
#pragma once
#include <SDL.h>
#include <string>
#include <unordered_map>
#include <vector>

class Audio {
 public:
  bool init();
  void shutdown();
  void play(const std::string& name, float gain = 1.f, float pan = 0.f, float rate = 1.f);
  void setWind(float speed01) { windTarget_ = speed01; }
  void setMusicGain(float g) { musicGain_ = g; }
  bool ok() const { return dev_ != 0; }

 private:
  struct Buffer { std::vector<float> data; int ch = 2; };         // float, device rate
  struct Variant { double start, dur; };
  struct Sound { std::string sprite; std::vector<Variant> v; float gain = 1; int max = 4; float jit = 0; int next = 0; };
  struct Voice { const Buffer* buf; double pos, end, rate; float gain, pan; bool loop; };
  SDL_AudioDeviceID dev_ = 0;
  int rate_ = 44100;
  std::unordered_map<std::string, Buffer> sprites_;
  std::unordered_map<std::string, Sound> sounds_;
  Buffer music_;
  std::vector<Voice> voices_;
  float musicGain_ = 0.22f, windTarget_ = 0, wind_ = 0;
  float windLp_ = 0, windLp2_ = 0; uint32_t noiseSeed_ = 1234567;
  double musicPos_ = 0;
  bool loadWav(const std::string& path, Buffer& out);
  static void callback(void* self, Uint8* stream, int len);
  void mix(float* out, int frames);
};
