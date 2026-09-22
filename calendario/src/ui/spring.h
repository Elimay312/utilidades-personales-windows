#pragma once

// The one spring the app has: the popup growing into the app and shrinking back.
//
// Everything else keeps the 160 ms ease-out of CLAUDE.md. This is the exception the design
// system names -- rigidity ~300, damping ~30 -- because a window that changes size by a factor
// of four reads as heavy when it moves on a fixed curve, and as physical when it settles.
//
// With mass one that is a damping ratio of 30 / (2 * sqrt(300)) = 0.87: just under critical, so
// it overshoots by a hair (well under one percent) and comes to rest instead of wobbling.

#include <algorithm>
#include <cmath>

namespace agenda {

struct Spring {
  static constexpr float kStiffness = 300.0f;
  static constexpr float kDamping = 30.0f;
  // Anything longer than this between two frames is a hitch -- a debugger, a sleeping laptop --
  // and simulating it whole would teleport the window to the end.
  static constexpr float kMaxStep = 1.0f / 30.0f;
  static constexpr int kSubsteps = 4;

  float x = 0.0f;  // where it is, 0 the popup and 1 the app
  float v = 0.0f;  // how fast it is going there, per second

  // Advances `seconds` towards `target`. False once it has arrived and stopped, and from then
  // on x is exactly the target, so the last frame lands on the pixel it was aimed at.
  bool Step(float seconds, float target) {
    const float dt = std::clamp(seconds, 0.0f, kMaxStep) / static_cast<float>(kSubsteps);
    // Semi-implicit Euler, a few small steps per frame: stable at this stiffness with room to
    // spare, and no library for twelve lines.
    for (int i = 0; i < kSubsteps; ++i) {
      const float accel = -kStiffness * (x - target) - kDamping * v;
      v += accel * dt;
      x += v * dt;
    }
    if (std::fabs(x - target) < 1e-3f && std::fabs(v) < 1e-2f) {
      x = target;
      v = 0.0f;
      return false;
    }
    return true;
  }

  void Snap(float target) {
    x = target;
    v = 0.0f;
  }
};

}  // namespace agenda
