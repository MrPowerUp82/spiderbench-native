#include "audio/audio.h"
#include "gfx/texture.h" // assetPath
#include <json/json.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>

bool Audio::loadWav(const std::string& path, Buffer& out) {
  SDL_AudioSpec spec; Uint8* buf = nullptr; Uint32 len = 0;
  if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len)) { std::fprintf(stderr, "[audio] %s: %s\n", path.c_str(), SDL_GetError()); return false; }
  SDL_AudioCVT cvt;
  SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_F32SYS, spec.channels, rate_);
  cvt.len = (int)len;
  std::vector<Uint8> tmp((size_t)len * std::max(1, cvt.len_mult));
  std::memcpy(tmp.data(), buf, len);
  cvt.buf = tmp.data();
  if (cvt.needed) SDL_ConvertAudio(&cvt); else cvt.len_cvt = (int)len;
  SDL_FreeWAV(buf);
  out.ch = spec.channels;
  out.data.assign((float*)tmp.data(), (float*)(tmp.data() + cvt.len_cvt));
  return true;
}

bool Audio::init() {
  SDL_AudioSpec want{}, have{};
  want.freq = 44100; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 1024;
  want.callback = &Audio::callback; want.userdata = this;
  dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (!dev_) { std::fprintf(stderr, "[audio] no device: %s\n", SDL_GetError()); return false; }
  rate_ = have.freq;
  loadWav(assetPath("sfx_trav.wav"), sprites_["trav"]);
  loadWav(assetPath("sfx_world.wav"), sprites_["world"]);
  loadWav(assetPath("music_day.wav"), music_);
  std::ifstream f(assetPath("audio_manifest.json"));
  Json::Value j; Json::CharReaderBuilder b; std::string err;
  if (f && Json::parseFromStream(b, f, &j, &err)) {
    for (const auto& name : j["sounds"].getMemberNames()) {
      const auto& s = j["sounds"][name];
      Sound S; S.sprite = s["sprite"].asString(); S.gain = s.get("gain", 1.0).asFloat(); S.max = s.get("max", 4).asInt(); S.jit = s.get("jit", 0.0).asFloat();
      for (const auto& v : s["v"]) S.v.push_back({v[0].asDouble(), v[1].asDouble()});
      sounds_[name] = S;
    }
  }
  SDL_PauseAudioDevice(dev_, 0);
  std::printf("[audio] %d Hz, %zu sounds\n", rate_, sounds_.size());
  return true;
}

void Audio::shutdown() { if (dev_) SDL_CloseAudioDevice(dev_); dev_ = 0; }

void Audio::play(const std::string& name, float gain, float pan, float rate) {
  if (!dev_) return;
  auto it = sounds_.find(name); if (it == sounds_.end() || it->second.v.empty()) return;
  Sound& S = it->second;
  auto sp = sprites_.find(S.sprite); if (sp == sprites_.end() || sp->second.data.empty()) return;
  const Variant& v = S.v[S.next++ % S.v.size()];
  static std::mt19937 rng(42);
  float jit = S.jit > 0 ? 1 + std::uniform_real_distribution<float>(-S.jit, S.jit)(rng) : 1;
  Voice V{&sp->second, v.start * rate_, (v.start + v.dur) * rate_, rate * jit, S.gain * gain, pan, false};
  SDL_LockAudioDevice(dev_);
  if (voices_.size() < 32) voices_.push_back(V);
  SDL_UnlockAudioDevice(dev_);
}

void Audio::callback(void* self, Uint8* stream, int len) { static_cast<Audio*>(self)->mix((float*)stream, len / 8); }

void Audio::mix(float* out, int frames) {
  for (int i = 0; i < frames * 2; i++) out[i] = 0;
  // music (loop)
  if (!music_.data.empty()) {
    size_t n = music_.data.size() / music_.ch;
    for (int i = 0; i < frames; i++) {
      size_t p = (size_t)musicPos_ % n;
      float l = music_.data[p * music_.ch], r = music_.ch > 1 ? music_.data[p * music_.ch + 1] : l;
      out[i * 2] += l * musicGain_; out[i * 2 + 1] += r * musicGain_;
      musicPos_ += 1;
    }
  }
  // voices
  for (auto& v : voices_) {
    size_t n = v.buf->data.size() / v.buf->ch;
    float gl = v.gain * std::min(1.f, 1 - v.pan), gr = v.gain * std::min(1.f, 1 + v.pan);
    for (int i = 0; i < frames && v.pos < v.end; i++) {
      size_t p = (size_t)v.pos; if (p >= n) { v.pos = v.end; break; }
      float l = v.buf->data[p * v.buf->ch], r = v.buf->ch > 1 ? v.buf->data[p * v.buf->ch + 1] : l;
      out[i * 2] += l * gl; out[i * 2 + 1] += r * gr;
      v.pos += v.rate;
    }
  }
  voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.pos >= v.end; }), voices_.end());
  // wind: two-pole low-passed noise, level and brightness follow speed
  for (int i = 0; i < frames; i++) {
    wind_ += (windTarget_ - wind_) * 0.00008f;
    noiseSeed_ = noiseSeed_ * 1664525u + 1013904223u;
    float n = ((noiseSeed_ >> 8) / 8388608.f) - 1.f;
    float k = 0.01f + 0.08f * wind_;
    windLp_ += (n - windLp_) * k; windLp2_ += (windLp_ - windLp2_) * k;
    float w = windLp2_ * wind_ * wind_ * 1.6f;
    out[i * 2] += w; out[i * 2 + 1] += w * 0.9f;
  }
  for (int i = 0; i < frames * 2; i++) out[i] = std::tanh(out[i]); // soft limiter
}
