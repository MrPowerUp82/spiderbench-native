// Spiderbench native — entry point (port of src/main.js): SDL2 window + OpenGL 3.3 core context, city build with a
// loading screen, player, chase camera, renderer, HUD and audio.
//
// Controls: WASD move · mouse camera (click to capture, Esc to release) · RMB hold web-swing · Space jump (hold = charge,
//           Space mid-swing = jump release) · Shift wall-run / parkour · E or MMB web-zip to the highlighted point
//           (no target in air = web-dash) · C / Ctrl drop / dive · Q quick web boost · Ctrl + LMB/RMB (ground) slingshot
//           H help · 1-5 teleport · R respawn · F11 fullscreen · F12 screenshot · M music on/off
//
// Test mode:  spiderbench --test [seconds] [--out dir]   scripted input at a fixed 60 Hz step; logs the traversal state
//             and saves screenshots (verification without a human at the controls)
#include <SDL.h>
#include "gfx/gl.h"
#include "gfx/renderer.h"
#include "gfx/texture.h"
#include "world/world.h"
#include "player/player.h"
#include "ui/hud.h"
#include "audio/audio.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

namespace {
struct App {
  SDL_Window* win = nullptr;
  SDL_GLContext gl = nullptr;
  int w = 1600, h = 900;
  Renderer renderer;
  World world;
  Camera camera;
  Player player;
  CharGpu charGpu;
  Input input;
  Hud hud;
  Audio audio;
  bool help = true, running = true, music = true;
  float fps = 60, time = 0, districtT = 0;
  std::string district;
  // test mode
  bool test = false; float testLen = 14; int testScene = 0; std::string outDir = ".";
};

bool saveScreenshot(App& a, const std::string& path) {
  std::vector<uint8_t> px((size_t)a.w * a.h * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, a.w, a.h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, a.w, a.h, 24, SDL_PIXELFORMAT_RGB24);
  if (!s) return false;
  for (int y = 0; y < a.h; y++) std::memcpy((uint8_t*)s->pixels + (size_t)y * s->pitch, px.data() + (size_t)(a.h - 1 - y) * a.w * 3, (size_t)a.w * 3);
  bool ok = SDL_SaveBMP(s, path.c_str()) == 0;
  SDL_FreeSurface(s);
  std::printf("[shot] %s %s\n", path.c_str(), ok ? "saved" : SDL_GetError());
  return ok;
}

void loadingFrame(App& a, const char* stage, float k) {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, a.w, a.h);
  glClearColor(0.05f, 0.06f, 0.08f, 1); glClear(GL_COLOR_BUFFER_BIT);
  a.hud.begin(a.w, a.h);
  a.hud.text(a.w * 0.5f, a.h * 0.42f, "SPIDERBENCH", 96, {0.92f, 0.15f, 0.18f, 1}, 1, 1);
  a.hud.text(a.w * 0.5f, a.h * 0.42f + 110, stage, 26, {0.8f, 0.82f, 0.86f, 1}, 0, 1);
  float bw = a.w * 0.3f;
  a.hud.rect(a.w * 0.5f - bw / 2, a.h * 0.42f + 160, bw, 4, {1, 1, 1, 0.15f});
  a.hud.rect(a.w * 0.5f - bw / 2, a.h * 0.42f + 160, bw * k, 4, {0.92f, 0.15f, 0.18f, 1});
  a.hud.end();
  SDL_GL_SwapWindow(a.win);
  SDL_Event e; while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) std::exit(0);
}

void teleportTo(App& a, int k) {
  struct Spot { Vec3 p; float yaw; } spots[] = {
    {a.world.spawn + Vec3{0, 0.95f, 0}, 0},                      // 1 Midtown, 5th Av crosswalk (the original spawn)
    {{0, 1.2f, -560}, PI},                                       // 2 Central Park South edge, facing the park
    {{120, 1.2f, 2760}, PI},                                     // 3 Financial District
    {{-330, 1.2f, 980}, PI},                                     // 4 Greenwich Village
    {{250, 0, 60}, PI},                                          // 5 rooftop above Midtown
  };
  Spot s = spots[std::clamp(k, 0, 4)];
  if (k == 4) s.p.y = a.world.groundHeight(262, 60) + 1.2f, s.p.x = 262;
  a.player.teleport(s.p, s.yaw);
}

void drawHud(App& a) {
  Hud& H = a.hud; const auto& s = a.player.trav->s;
  H.begin(a.w, a.h);
  const Rgba white{1, 1, 1, 0.92f}, dim{1, 1, 1, 0.55f}, red{0.92f, 0.16f, 0.2f, 1};
  // zip reticle: candidates (dim) + the highlighted target
  if (a.player.aiming() || s.mode == "air" || s.mode == "swing") {
    for (const auto& c : a.player.trav->targeting.candidates()) {
      Vec3 ndc = a.camera.project(c.t.pos);
      if (ndc.z > 1) continue;
      float x = (ndc.x * 0.5f + 0.5f) * a.w, y = (1 - (ndc.y * 0.5f + 0.5f)) * a.h;
      if (c.best) { H.ring(x, y, 13, 3, white); H.ring(x, y, 5, 5, red, 16); }
      else H.ring(x, y, 8, 2, dim, 20);
    }
  }
  // district banner (fades after a change)
  std::string d = a.world.districtAt(s.pos.x, s.pos.z);
  if (d != a.district) { a.district = d; a.districtT = 0; }
  float da = clampf(3.5f - a.districtT, 0, 1);
  if (da > 0) { H.text(48, 40, a.district, 54, {1, 1, 1, da}, 1); H.rect(48, 104, 80, 4, {red.r, red.g, red.b, da}); }
  // speed / mode
  char buf[160];
  std::snprintf(buf, sizeof buf, "%3.0f km/h", s.vel.length() * 3.6f);
  H.text(a.w - 40.f, a.h - 90.f, buf, 44, white, 1, 2);
  std::snprintf(buf, sizeof buf, "%s · %s   %.0f fps", s.mode.c_str(), s.sub.c_str(), a.fps);
  H.text(a.w - 40.f, a.h - 40.f, buf, 20, dim, 0, 2);
  if (s.chain > 1) { std::snprintf(buf, sizeof buf, "SWING CHAIN x%d", s.chain); H.text(a.w - 40.f, a.h - 140.f, buf, 26, red, 1, 2); }
  if (!a.input.mouseCaptured && !a.test) H.text(a.w * 0.5f, a.h * 0.5f + 60, "Clique para capturar o mouse", 26, white, 0, 1);
  if (a.help) {
    const char* lines[] = {"CONTROLES", "WASD  mover   ·   Mouse  câmera", "Botão direito (segurar)  balançar na teia", "Espaço  pular (segure = carga) · no swing = soltar com impulso",
                           "Shift  correr na parede / parkour", "E / botão do meio  web-zip até o ponto marcado", "C / Ctrl  mergulhar / soltar   ·   Q  impulso rápido",
                           "Ctrl + clique (no chão)  estilingue de teia", "1-5  teleporte   ·   R  renascer   ·   M  música", "F11 tela cheia · F12 screenshot · H esconder ajuda"};
    float y = a.h - 40.f - 30 * 10;
    H.rect(32, y - 18, 560, 30 * 10 + 20, {0, 0, 0, 0.35f});
    for (int i = 0; i < 10; i++) H.text(48, y + i * 30.f, lines[i], i == 0 ? 24.f : 20.f, i == 0 ? red : white, i == 0 ? 1 : 0);
  }
  H.end();
}

// scripted input for --test: scene 0 run + swing chain + zip; 1 wall run; 2 Central Park swing; 3 Financial District
void testScript(App& a, float t) {
  Input& I = a.input;
  auto at = [&](float t0, float t1) { return t >= t0 && t < t1; };
  auto key = [&](SDL_Scancode k, bool on) { if (on) I.press(k); else I.release(k); };
  if (a.testScene == 4) { // wall cling: run into the facade, then let go of every key on the wall
    key(SDL_SCANCODE_D, at(0.3f, 6.5f)); key(SDL_SCANCODE_LSHIFT, at(0.3f, 6.5f));
    if (at(0.3f, 0.6f)) I.look(0, -40);
    return;
  }
  if (a.testScene == 5) { // perch: swing, zip to the highlighted point, stay crouched on it
    key(SDL_SCANCODE_W, at(0.5f, 8.7f));
    if (at(1.2f, 3.6f) || at(3.9f, 6.2f) || at(6.5f, 8.8f)) I.pressMouse(4); else I.releaseMouse(4);
    key(SDL_SCANCODE_E, at(8.9f, 9.0f));
    return;
  }
  if (a.testScene == 1) { // Shift + D into the facade west of 5th Av -> wall run up -> roof
    key(SDL_SCANCODE_D, at(0.3f, 60)); key(SDL_SCANCODE_LSHIFT, at(0.3f, 60));
    if (at(0.3f, 0.6f)) I.look(0, -40);
    return;
  }
  if (a.testScene == 2 || a.testScene == 3) {
    if (t < 0.05f) teleportTo(a, a.testScene == 2 ? 1 : 2);
    key(SDL_SCANCODE_W, at(0.5f, 60));
    bool rmb = at(1.2f, 3.6f) || at(3.9f, 6.2f) || at(6.5f, 8.8f) || at(9.1f, 11.5f);
    if (rmb) I.pressMouse(4); else I.releaseMouse(4);
    return;
  }
  if (at(0.5f, 60)) I.press(SDL_SCANCODE_W); else I.release(SDL_SCANCODE_W);
  bool rmb = at(1.2f, 3.6f) || at(3.9f, 6.2f) || at(6.5f, 8.8f);
  if (rmb) I.pressMouse(4); else I.releaseMouse(4);
  if (at(8.9f, 9.0f) || at(12.0f, 12.1f)) I.press(SDL_SCANCODE_E); else I.release(SDL_SCANCODE_E);
  if (at(10.0f, 10.8f)) I.press(SDL_SCANCODE_A); else I.release(SDL_SCANCODE_A);
}
}  // namespace

int main(int argc, char** argv) {
  App a;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--test")) { a.test = true; if (i + 1 < argc && argv[i + 1][0] != '-') a.testLen = (float)std::atof(argv[++i]); }
    else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) a.outDir = argv[++i];
    else if (!std::strcmp(argv[i], "--scene") && i + 1 < argc) a.testScene = std::atoi(argv[++i]);
  }
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 0);
  a.win = SDL_CreateWindow("Spiderbench", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, a.w, a.h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!a.win) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
  // newest core context first (4.5+ gives glClipControl -> reversed-Z depth), OpenGL 3.3 is the minimum
  for (auto [maj, mnr] : {std::pair{4, 6}, std::pair{4, 5}, std::pair{3, 3}}) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, maj);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, mnr);
    if ((a.gl = SDL_GL_CreateContext(a.win))) break;
  }
  if (!a.gl) { std::fprintf(stderr, "GL context: %s\n", SDL_GetError()); return 1; }
  SDL_GL_MakeCurrent(a.win, a.gl);
  SDL_GL_SetSwapInterval(a.test ? 0 : 1);
  if (!glLoad()) { std::fprintf(stderr, "OpenGL 3.3 functions missing\n"); return 1; }
  SDL_GL_GetDrawableSize(a.win, &a.w, &a.h);
  std::printf("[gl] %s | %s\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));

  if (!a.hud.init()) return 1;
  loadingFrame(a, "Carregando shaders", 0.05f);
  if (!a.renderer.init(a.w, a.h)) return 1;
  loadingFrame(a, "Gerando a cidade", 0.2f);
  uint32_t t0 = SDL_GetTicks();
  a.world.build(1234);
  a.world.upload();
  std::printf("[world] built in %u ms\n", SDL_GetTicks() - t0);
  loadingFrame(a, "Carregando o personagem", 0.7f);
  a.camera.aspect = (float)a.w / a.h;
  if (!a.player.init(a.world, a.camera)) { std::fprintf(stderr, "player init failed\n"); return 1; }
  a.charGpu.build(a.player.rig);
  loadingFrame(a, "Áudio", 0.9f);
  if (!a.test) a.audio.init();

  uint64_t last = SDL_GetPerformanceCounter();
  float logT = 0; int shot = 0;
  while (a.running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) a.running = false;
      else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        SDL_GL_GetDrawableSize(a.win, &a.w, &a.h); a.renderer.resize(a.w, a.h);
      } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
        switch (e.key.keysym.scancode) {
          case SDL_SCANCODE_ESCAPE: if (a.input.mouseCaptured) a.input.setCapture(false); else a.running = false; break;
          case SDL_SCANCODE_H: a.help = !a.help; break;
          case SDL_SCANCODE_R: teleportTo(a, 0); break;
          case SDL_SCANCODE_M: a.music = !a.music; a.audio.setMusicGain(a.music ? 0.22f : 0); break;
          case SDL_SCANCODE_F11: { bool fs = SDL_GetWindowFlags(a.win) & SDL_WINDOW_FULLSCREEN_DESKTOP; SDL_SetWindowFullscreen(a.win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP); break; }
          case SDL_SCANCODE_F12: saveScreenshot(a, "screenshot_" + std::to_string(SDL_GetTicks()) + ".bmp"); break;
          default:
            if (e.key.keysym.scancode >= SDL_SCANCODE_1 && e.key.keysym.scancode <= SDL_SCANCODE_5) teleportTo(a, e.key.keysym.scancode - SDL_SCANCODE_1);
        }
      }
      a.input.handle(e);
    }
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (float)((now - last) / (double)SDL_GetPerformanceFrequency()); last = now;
    if (a.test) dt = 1.f / 60;
    dt = std::min(dt, 1.f / 20);
    a.fps = a.fps * 0.95f + (1.f / std::max(dt, 1e-4f)) * 0.05f;
    a.time += dt; a.districtT += dt;
    if (a.test) testScript(a, a.time);

    a.camera.aspect = (float)a.w / std::max(a.h, 1);
    a.player.update(dt, a.input, a.test ? nullptr : &a.audio);
    FrameInput f;
    f.cam = &a.camera; f.world = &a.world; f.rig = &a.player.rig; f.character = &a.charGpu;
    f.characterVisible = a.player.meshVisible; f.webs = &a.player.web.lines(); f.time = a.time;
    f.motionBlur = a.player.cam->motionBlur;
    a.renderer.render(f);
    drawHud(a);
    if (a.test) {
      const auto& s = a.player.trav->s;
      logT += dt;
      static bool logAll = std::getenv("SB_LOGALL") != nullptr;
      if (logT >= 0.25f || logAll) {
        logT = 0;
        std::printf("[test] t=%5.2f %-6s %-12s pos=(%7.1f %6.1f %7.1f) v=%5.1f chain=%d anim=%s | %s\n", a.time, s.mode.c_str(), s.sub.c_str(), s.pos.x, s.pos.y, s.pos.z, s.vel.length(), s.chain,
                    a.player.animator.debugNode.c_str(), a.player.animator.debugClip.c_str());
      }
      if (a.time >= 1.0f + shot * 1.5f) { saveScreenshot(a, a.outDir + "/test_" + std::to_string(shot) + ".bmp"); shot++; }
      if (a.time >= a.testLen) a.running = false;
    }
    SDL_GL_SwapWindow(a.win);
  }
  a.audio.shutdown();
  SDL_GL_DeleteContext(a.gl);
  SDL_DestroyWindow(a.win);
  SDL_Quit();
  return 0;
}
