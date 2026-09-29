// Keyboard + mouse (relative mode) + game controller -> per-frame action state (port of player/input.js).
//   WASD move · mouse camera · RMB (hold) web-swing · Shift wall-run / ground parkour · Space jump (hold = charge)
//   E / MMB web-zip · C / Ctrl drop / dive · Q quick web boost · Ctrl + LMB / RMB (ground) web slingshot
//   Pad: LS move, RS camera, R2 swing / parkour, A jump, L2+R2 or Y zip, B drop, L1 quick boost
#pragma once
#include "core/math.h"
#include <SDL.h>
#include <set>

struct InputState {
  Vec2 move; float lookDx = 0, lookDy = 0;
  bool usingPad = false;
  bool swing = false, jump = false, zip = false, sprint = false, walk = false, drop = false, quick = false, rope = false, power = false, ctrl = false;
  bool slingL = false, slingR = false;
  bool swingPressed = false, jumpPressed = false, zipPressed = false, dropPressed = false, sprintPressed = false, walkPressed = false, quickPressed = false, ropePressed = false, powerPressed = false;
  bool swingReleased = false, jumpReleased = false, zipReleased = false, dropReleased = false, sprintReleased = false, walkReleased = false, quickReleased = false, ropeReleased = false, powerReleased = false;
  float jumpHeld = 0, aimT = 99;
  bool combat = false;
};

class Input {
 public:
  bool slingGate = false; // set by the player each frame: Ctrl + click = slingshot anchors only on the ground
  bool mouseCaptured = false;
  void handle(const SDL_Event& e);
  InputState& poll(float dt);
  bool keyDown(SDL_Scancode s) const { return keys_.count(s) > 0; }
  bool tapped(SDL_Scancode s) const { return tappedKeys_.count(s) > 0; }
  void setCapture(bool on);
  void releaseAll() { keys_.clear(); tappedKeys_.clear(); buttons_ = 0; synthKeys_.clear(); synthBtn_ = 0; }
  // automation (test mode): synthetic keys / mouse buttons (bit 1 left, 2 middle, 4 right) and look deltas
  void press(SDL_Scancode s) { synthKeys_.insert(s); }
  void release(SDL_Scancode s) { synthKeys_.erase(s); }
  void pressMouse(int bit) { synthBtn_ |= bit; }
  void releaseMouse(int bit) { synthBtn_ &= ~bit; }
  void look(float dx, float dy) { mdx_ += dx; mdy_ += dy; }
 private:
  std::set<SDL_Scancode> synthKeys_;
  int synthBtn_ = 0;
  std::set<SDL_Scancode> keys_, tappedKeys_, edgeKeys_;
  int buttons_ = 0, tappedBtn_ = 0; // bit 1 left, 2 middle, 4 right
  float mdx_ = 0, mdy_ = 0;
  int slingTap_ = 0;
  SDL_GameController* pad_ = nullptr;
  InputState st_;
  bool prev_[9] = {};
};
