#include "player/input.h"

void Input::setCapture(bool on) {
  mouseCaptured = on;
  SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
}

void Input::handle(const SDL_Event& e) {
  switch (e.type) {
    case SDL_KEYDOWN:
      if (!e.key.repeat) { tappedKeys_.insert(e.key.keysym.scancode); edgeKeys_.insert(e.key.keysym.scancode); }
      keys_.insert(e.key.keysym.scancode); break;
    case SDL_KEYUP: keys_.erase(e.key.keysym.scancode); break;
    case SDL_MOUSEMOTION:
      if (mouseCaptured || (buttons_ & 1)) { mdx_ += (float)e.motion.xrel; mdy_ += (float)e.motion.yrel; }
      break;
    case SDL_MOUSEBUTTONDOWN: {
      if (!mouseCaptured) { setCapture(true); break; } // first click captures the mouse (pointer lock)
      int b = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_MIDDLE ? 2 : e.button.button == SDL_BUTTON_RIGHT ? 4 : 0;
      bool ctrl = keys_.count(SDL_SCANCODE_LCTRL) || keys_.count(SDL_SCANCODE_RCTRL);
      if ((b == 1 || b == 4) && slingGate && ctrl) { slingTap_ |= b; break; }
      buttons_ |= b; tappedBtn_ |= b; break;
    }
    case SDL_MOUSEBUTTONUP: {
      int b = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_MIDDLE ? 2 : e.button.button == SDL_BUTTON_RIGHT ? 4 : 0;
      buttons_ &= ~b; break;
    }
    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) { keys_.clear(); buttons_ = 0; }
      break;
    case SDL_CONTROLLERDEVICEADDED: if (!pad_) pad_ = SDL_GameControllerOpen(e.cdevice.which); break;
    case SDL_CONTROLLERDEVICEREMOVED: if (pad_ && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_))) { SDL_GameControllerClose(pad_); pad_ = nullptr; } break;
  }
}

InputState& Input::poll(float dt) {
  auto has = [&](SDL_Scancode s) { return keys_.count(s) || tappedKeys_.count(s) || synthKeys_.count(s); };
  float mx = 0, my = 0;
  if (has(SDL_SCANCODE_W) || has(SDL_SCANCODE_UP)) my += 1;
  if (has(SDL_SCANCODE_S) || has(SDL_SCANCODE_DOWN)) my -= 1;
  if (has(SDL_SCANCODE_D) || has(SDL_SCANCODE_RIGHT)) mx += 1;
  if (has(SDL_SCANCODE_A) || has(SDL_SCANCODE_LEFT)) mx -= 1;
  float lx = mdx_, ly = mdy_; mdx_ = mdy_ = 0;
  int btn = buttons_ | tappedBtn_ | synthBtn_; tappedBtn_ = 0;
  bool swing = btn & 4;
  bool sprint = has(SDL_SCANCODE_LSHIFT) || has(SDL_SCANCODE_RSHIFT);
  bool walk = false; // (input.js: Shift walk disabled — Shift = ground parkour / wall-run)
  bool jump = has(SDL_SCANCODE_SPACE);
  bool zip = has(SDL_SCANCODE_E) || (btn & 2);
  bool quick = has(SDL_SCANCODE_Q);
  bool power = has(SDL_SCANCODE_V);
  bool rope = has(SDL_SCANCODE_T);
  bool ctrl = keys_.count(SDL_SCANCODE_LCTRL) || keys_.count(SDL_SCANCODE_RCTRL) || synthKeys_.count(SDL_SCANCODE_LCTRL);
  bool drop = has(SDL_SCANCODE_C) || ctrl;
  bool slingL = slingTap_ & 1, slingR = slingTap_ & 4; slingTap_ = 0;
  bool usingPad = false;
  if (pad_) {
    auto ax = [&](SDL_GameControllerAxis a) { float v = SDL_GameControllerGetAxis(pad_, a) / 32767.f; return std::fabs(v) < 0.15f ? 0.f : (v - signf(v) * 0.15f) / 0.85f; };
    auto b = [&](SDL_GameControllerButton k) { return SDL_GameControllerGetButton(pad_, k) != 0; };
    float lsx = ax(SDL_CONTROLLER_AXIS_LEFTX), lsy = ax(SDL_CONTROLLER_AXIS_LEFTY), rsx = ax(SDL_CONTROLLER_AXIS_RIGHTX), rsy = ax(SDL_CONTROLLER_AXIS_RIGHTY);
    bool rt = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.3f * 32767, lt = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.3f * 32767;
    if (lsx || lsy || rsx || rsy || rt || lt || b(SDL_CONTROLLER_BUTTON_A) || b(SDL_CONTROLLER_BUTTON_B) || b(SDL_CONTROLLER_BUTTON_Y)) usingPad = true;
    mx += lsx; my -= lsy; lx += rsx * 900 * dt; ly += rsy * 600 * dt;
    bool zipCombo = lt && rt;
    quick = quick || (b(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) && !b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
    power = power || b(SDL_CONTROLLER_BUTTON_LEFTSTICK);
    swing = swing || (rt && !zipCombo); sprint = sprint || (rt && !zipCombo); jump = jump || b(SDL_CONTROLLER_BUTTON_A);
    zip = zip || zipCombo || b(SDL_CONTROLLER_BUTTON_Y); drop = drop || b(SDL_CONTROLLER_BUTTON_B);
  }
  tappedKeys_.clear();
  float len = std::hypot(mx, my); if (len > 1) { mx /= len; my /= len; }
  InputState& s = st_;
  s.move = {mx, my}; s.lookDx = lx; s.lookDy = ly;
  s.swing = swing; s.jump = jump; s.zip = zip; s.drop = drop; s.sprint = sprint; s.walk = walk; s.quick = quick; s.rope = rope; s.power = power;
  s.usingPad = usingPad; s.ctrl = ctrl; s.slingL = slingL; s.slingR = slingR;
  bool* cur[9] = {&s.swing, &s.jump, &s.zip, &s.drop, &s.sprint, &s.walk, &s.quick, &s.rope, &s.power};
  bool* pr[9] = {&s.swingPressed, &s.jumpPressed, &s.zipPressed, &s.dropPressed, &s.sprintPressed, &s.walkPressed, &s.quickPressed, &s.ropePressed, &s.powerPressed};
  bool* rl[9] = {&s.swingReleased, &s.jumpReleased, &s.zipReleased, &s.dropReleased, &s.sprintReleased, &s.walkReleased, &s.quickReleased, &s.ropeReleased, &s.powerReleased};
  for (int k = 0; k < 9; k++) { *pr[k] = *cur[k] && !prev_[k]; *rl[k] = !*cur[k] && prev_[k]; prev_[k] = *cur[k]; }
  s.jumpHeld = jump ? s.jumpHeld + dt : 0;
  s.aimT = std::fabs(lx) + std::fabs(ly) > 1.5f ? 0 : s.aimT + dt;
  edgeKeys_.clear();
  return s;
}
