#pragma once

// The spring a card opens and closes on. Written the way Brújula's are (proyectos-github/
// CLAUDE.md, "Movimiento"): a period and a damping ratio, because the period IS the
// perceived duration and the only knob worth turning. SettleMs is not, and is not here.
//
// Stiffness (2*pi/period)^2 with mass one, damping 2*zeta*omega. Semi-implicit Euler in a few
// substeps per frame, as in calendario/src/ui/spring.h: stable with room to spare, and
// interruptible for free -- a new target starts from wherever x and v are right now.

#include <algorithm>
#include <cmath>
#include <numbers>

namespace panel {

struct Spring {
  // Tuned with the app in front, not with a number (CLAUDE.md, movement). The cards unfold on
  // these; a tile morphing into a card travels further and gets a longer one (panel_window.cpp).
  static constexpr float kPeriodSeconds = 0.25f;
  static constexpr float kDampingRatio = 0.85f;
  static constexpr float kMaxStep = 1.0f / 30.0f;  // a longer gap is a hitch, not time passing
  static constexpr int kSubsteps = 4;

  float x = 0.0f;
  float v = 0.0f;
  float period = kPeriodSeconds;
  float damping = kDampingRatio;

  // Advances `seconds` towards `target`. False once it has arrived and stopped; from then on x
  // is exactly the target, so the last frame lands on the pixel it was aimed at.
  bool Step(float seconds, float target) {
    const float omega = 2.0f * std::numbers::pi_v<float> / period;
    const float stiffness = omega * omega;
    const float friction = 2.0f * damping * omega;
    const float dt = std::clamp(seconds, 0.0f, kMaxStep) / static_cast<float>(kSubsteps);
    for (int i = 0; i < kSubsteps; ++i) {
      v += (-stiffness * (x - target) - friction * v) * dt;
      x += v * dt;
    }
    if (std::fabs(x - target) < 1e-3f && std::fabs(v) < 1e-2f) {
      Snap(target);
      return false;
    }
    return true;
  }

  void Snap(float target) {
    x = target;
    v = 0.0f;
  }

  bool resting(float target) const { return x == target && v == 0.0f; }
};

}  // namespace panel
