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
// City:       the original city exported by tools/ref (build/city-bake, or build/city-bake-low with --quality low) when
//             present; --procedural forces the simplified
//             native city. --validate-baked-traversal compares the gameplay queries with the original JS answers.
#include <SDL.h>
#include "gfx/gl.h"
#include "gfx/renderer.h"
#include "gfx/baked_programs.h"
#include "gfx/baked_city.h"
#include "gfx/texture.h"
#include "world/world.h"
#include "world/baked_traversal.h"
#include "player/player.h"
#include "ui/hud.h"
#include "audio/audio.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <filesystem>

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
  BakedCity* city = nullptr; // original city renderer (null: procedural city meshes)
  bool help = true, running = true, music = true; // help overlay: off in --test (screenshots)
  float fps = 60, time = 0, districtT = 0;
  std::string district;
  // test mode
  bool test = false; float testLen = 14; int testScene = 0; std::string outDir = "."; float cam[6] = {0}, camFov = 0; bool hasCam = false; // --cam x,y,z,tx,ty,tz (web: ?cam=)
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
  if (k == 4 && a.city) s.p = {278, a.world.groundHeight(278, 138) + 1.2f, 138}; // original city: the 24 m roof east of 5th Av
  else if (k == 4) s.p.y = a.world.groundHeight(262, 60) + 1.2f, s.p.x = 262;
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
  // original city: the facade on the west side of 5th Av (x = 234, z 169-231) is 10 m to the right of this spot
  const bool baked = a.city != nullptr;
  if ((a.testScene == 1 || a.testScene == 4) && baked && t < 0.05f) a.player.teleport({244, 1.2f, 190}, 0);
  if (a.testScene == 4) { // wall cling: run into the facade, then let go of every key on the wall
    key(SDL_SCANCODE_D, at(0.3f, 6.5f)); key(SDL_SCANCODE_LSHIFT, at(0.3f, 6.5f));
    if (at(0.3f, 0.6f) && !baked) I.look(0, -40);
    return;
  }
  if (a.testScene == 6) { // walk into the open Central Park lawn and stand in the sun (character shadows / self-shadowing)
    if (t < 0.05f) teleportTo(a, 1);
    key(SDL_SCANCODE_W, at(0.3f, 2.3f));
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
    if (at(0.3f, 0.6f) && !baked) I.look(0, -40);
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

int viewBake(App& a, const std::string& directory, const std::string& shaders) {
  BakedCity city;
  if (!city.open(directory, shaders)) { std::fprintf(stderr, "[baked-city] could not open bake\n"); return 1; }
  a.camera.position = {250, 32, 175}; a.camera.lookAt({250, 8, -250}); a.camera.aspect = float(a.w) / a.h;
  if (a.testScene == 2) { a.camera.position = {0, 70, -600}; a.camera.lookAt({0, 30, -1400}); }
  if (a.testScene == 3) { a.camera.position = {120, 100, 2760}; a.camera.lookAt({80, 50, 2200}); }
  if (a.hasCam) { a.camera.position = {a.cam[0], a.cam[1], a.cam[2]}; a.camera.lookAt({a.cam[3], a.cam[4], a.cam[5]}); }
  if (a.camFov > 0) a.camera.fov = a.camFov; // player/camera.js sets the web camera FOV (58 deg base)
  uint64_t last = SDL_GetPerformanceCounter(); int frames = 0;
  if (a.test) std::filesystem::create_directories(a.outDir);
  while (a.running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) a.running = false;
      if (event.type == SDL_MOUSEBUTTONDOWN) SDL_SetRelativeMouseMode(SDL_TRUE);
      if (event.type == SDL_MOUSEMOTION && SDL_GetRelativeMouseMode() && !a.test) {
        a.camera.quaternion = Quat::axisAngle(UP, -event.motion.xrel * .002f) * a.camera.quaternion;
        a.camera.rotateX(-event.motion.yrel * .002f);
      }
      if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        SDL_GL_GetDrawableSize(a.win, &a.w, &a.h); a.renderer.resize(a.w, a.h); a.camera.aspect = float(a.w) / a.h;
      }
    }
    uint64_t now = SDL_GetPerformanceCounter(); float dt = std::min(.1f, float(double(now - last) / SDL_GetPerformanceFrequency())); last = now;
    if (!a.test) {
      const auto* keys = SDL_GetKeyboardState(nullptr); Vec3 move;
      if (keys[SDL_SCANCODE_W]) move += a.camera.direction(); if (keys[SDL_SCANCODE_S]) move -= a.camera.direction();
      Vec3 right = a.camera.quaternion * Vec3{1, 0, 0};
      if (keys[SDL_SCANCODE_D]) move += right; if (keys[SDL_SCANCODE_A]) move -= right;
      if (keys[SDL_SCANCODE_E]) move.y++; if (keys[SDL_SCANCODE_Q]) move.y--;
      a.camera.position += move * (dt * (keys[SDL_SCANCODE_LSHIFT] ? 150.f : 30.f)); a.time += dt;
    }
    FrameInput frame; frame.cam = &a.camera; frame.world = &a.world; frame.bakedCity = &city; frame.characterVisible = false; frame.time = a.time; frame.dt = a.test ? 1.f / 60 : dt;
    a.renderer.render(frame);
    if (!city.healthy) { city.clear(); return 1; }
    if (frames == 0) std::printf("[baked-city] %zu draws, %zu shadow draws, %llu triangles, %.1f MiB vertex/index buffers\n", city.drawn, city.shadowDrawn, (unsigned long long)city.triangles, city.residentBytes / 1048576.0);
    // shots.js renders 90 frames before the capture (TAA history and auto exposure settle); match it for --cam
    if (a.test && frames == (a.hasCam ? 89 : 1)) { if (!saveScreenshot(a, a.outDir + "/baked_city.bmp")) { city.clear(); return 1; } a.running = false; }
    SDL_GL_SwapWindow(a.win); frames++;
  }
  city.clear(); return 0;
}
}  // namespace

int main(int argc, char** argv) {
  App a;
  if (const char* size = std::getenv("SB_SIZE")) std::sscanf(size, "%dx%d", &a.w, &a.h); // window size, e.g. SB_SIZE=800x450
  bool validateBake = false;
  bool validatePools = false;
  bool validateCSM = false;
  bool validateTiles = false;
  bool validateEnvironment = false;
  bool previewBake = false;
  bool procedural = false;
  bool validateTraversal = false;
  std::string shaderDirectory = SB_SOURCE_SHADERS;
  std::string bakeDirectory = SB_SOURCE_BAKE;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--test")) { a.test = true; a.help = std::getenv("SB_HELP") != nullptr; if (i + 1 < argc && argv[i + 1][0] != '-') a.testLen = (float)std::atof(argv[++i]); }
    else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) a.outDir = argv[++i];
    else if (!std::strcmp(argv[i], "--scene") && i + 1 < argc) a.testScene = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--fov") && i + 1 < argc) a.camFov = float(std::atof(argv[++i]));
    else if (!std::strcmp(argv[i], "--cam") && i + 1 < argc)
      a.hasCam = std::sscanf(argv[++i], "%f,%f,%f,%f,%f,%f", &a.cam[0], &a.cam[1], &a.cam[2], &a.cam[3], &a.cam[4], &a.cam[5]) == 6;
    else if (!std::strcmp(argv[i], "--validate-baked-shaders")) validateBake = true;
    else if (!std::strcmp(argv[i], "--validate-baked-pools")) validatePools = true;
    else if (!std::strcmp(argv[i], "--validate-baked-csm")) validateCSM = true;
    else if (!std::strcmp(argv[i], "--validate-baked-tiles")) validateTiles = true;
    else if (!std::strcmp(argv[i], "--validate-baked-environment")) validateEnvironment = true;
    else if (!std::strcmp(argv[i], "--shader-dir") && i + 1 < argc) shaderDirectory = argv[++i];
    else if (!std::strcmp(argv[i], "--view-bake")) previewBake = true;
    else if (!std::strcmp(argv[i], "--procedural")) procedural = true;
    else if (!std::strcmp(argv[i], "--quality") && i + 1 < argc) { // the remake's preset of the bake (bake_city_low -> low)
      const std::string q = argv[++i], suffix = q == "med" ? "" : "-" + q;
      bakeDirectory = std::string(SB_SOURCE_BAKE) + suffix; shaderDirectory = std::string(SB_SOURCE_SHADERS) + suffix;
    }
    else if (!std::strcmp(argv[i], "--validate-baked-traversal")) validateTraversal = true;
    else if (!std::strcmp(argv[i], "--bake-dir") && i + 1 < argc) bakeDirectory = argv[++i];
  }
  if (validateTraversal) { // CPU only: no window needed
    World world;
    return world.loadBaked(bakeDirectory) && validateBakedTraversal(world, (std::filesystem::path(bakeDirectory) / "traversal_queries.json").string()) ? 0 : 1;
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

  if (validateBake || validatePools || validateCSM || validateTiles || validateEnvironment) {
    BakedPrograms programs;
    BakedEnvironment environment;
    bool ok = validateEnvironment ? environment.open((std::filesystem::path(bakeDirectory) / "environment").string()) && environment.validate((std::filesystem::path(bakeDirectory) / "environment/native-check.json").string()) : validateTiles ? validateBakedVisibility(bakeDirectory, shaderDirectory) : validateCSM ? validateBakedCSM(bakeDirectory) : validatePools ? validateBakedPools(bakeDirectory) : programs.open(shaderDirectory) && programs.validateAll();
    if (ok && validateBake) { // the player model's programs (capture_shaders.mjs --character), when captured
      const std::string characterShaders = (std::filesystem::path(shaderDirectory) / "character").string();
      BakedPrograms character;
      if (std::filesystem::exists(std::filesystem::path(characterShaders) / "manifest.json"))
        ok = character.open(characterShaders) && character.validateAll();
      character.clear();
    }
    environment.clear();
    programs.clear();
    SDL_GL_DeleteContext(a.gl); SDL_DestroyWindow(a.win); SDL_Quit();
    return ok ? 0 : 1;
  }

  if (!a.hud.init()) return 1;
  loadingFrame(a, "Carregando shaders", 0.05f);
  if (!a.renderer.init(a.w, a.h)) return 1;
  if (previewBake) {
    int result = viewBake(a, bakeDirectory, shaderDirectory);
    SDL_GL_DeleteContext(a.gl); SDL_DestroyWindow(a.win); SDL_Quit(); return result;
  }
  uint32_t t0 = SDL_GetTicks();
  BakedCity bakedCity;
  const bool haveBake = !procedural && std::filesystem::exists(std::filesystem::path(bakeDirectory) / "traversal.sbtrv");
  if (haveBake) {
    loadingFrame(a, "Carregando a cidade", 0.2f);
    if (!a.world.loadBaked(bakeDirectory) || !bakedCity.open(bakeDirectory, shaderDirectory)) {
      std::fprintf(stderr, "[world] baked city unavailable (run the bake_city target or pass --procedural)\n"); return 1;
    }
    a.city = &bakedCity;
  } else {
    loadingFrame(a, "Gerando a cidade", 0.2f);
    a.world.build(1234);
    a.world.upload();
  }
  std::printf("[world] %s city ready in %u ms\n", a.city ? "baked" : "procedural", SDL_GetTicks() - t0);
  loadingFrame(a, "Carregando o personagem", 0.7f);
  a.camera.aspect = (float)a.w / a.h;
  if (!a.player.init(a.world, a.camera)) { std::fprintf(stderr, "player init failed\n"); return 1; }
  a.charGpu.build(a.player.rig);
  if (a.city && !bakedCity.openCharacter(a.player.rig)) std::fprintf(stderr, "[baked-char] using the native character shader\n");
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
    static const bool profile = std::getenv("SB_PROFILE") != nullptr;
    const uint64_t p0 = SDL_GetPerformanceCounter();
    a.player.update(dt, a.input, a.test ? nullptr : &a.audio);
    const uint64_t p1 = SDL_GetPerformanceCounter();
    FrameInput f;
    f.cam = &a.camera; f.world = &a.world; f.rig = &a.player.rig; f.character = &a.charGpu;
    f.characterVisible = a.player.meshVisible; f.webs = &a.player.web.lines(); f.time = a.time;
    f.motionBlur = a.player.cam->motionBlur; f.bakedCity = a.city; f.dt = dt; f.focus = a.player.trav->rootPos;
    a.renderer.render(f);
    if (a.city && !a.city->healthy) { std::fprintf(stderr, "[baked-city] render failed\n"); a.running = false; }
    drawHud(a);
    if (profile) { // SB_PROFILE=1: CPU time of the simulation and of the frame submission, averaged per second
      static double simMs = 0, drawMs = 0; static int frames = 0;
      const double f2ms = 1000.0 / SDL_GetPerformanceFrequency(); const uint64_t p2 = SDL_GetPerformanceCounter();
      simMs += (p1 - p0) * f2ms; drawMs += (p2 - p1) * f2ms;
      if (++frames == 60) { std::printf("[profile] update %.2f ms, render %.2f ms | %zu draws, %zu shadow draws, %.2f M triangles\n", simMs / frames, drawMs / frames,
                                          a.city ? a.city->drawn : size_t(0), a.city ? a.city->shadowDrawn : size_t(0), a.city ? a.city->triangles / 1e6 : 0.0); simMs = drawMs = 0; frames = 0; }
    }
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
  bakedCity.clear();
  SDL_GL_DeleteContext(a.gl);
  SDL_DestroyWindow(a.win);
  SDL_Quit();
  return 0;
}
