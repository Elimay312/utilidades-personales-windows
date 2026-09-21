#include "compositor/MotionSpec.h"

#include <numbers>

namespace Motion {

Resolved Resolve(Kind kind, bool systemAnimationsEnabled) {
    Resolved resolved;
    resolved.spring = SpringFor(kind);

    if (systemAnimationsEnabled) {
        resolved.animate = true;
        // El fundido acompaña al muelle y termina antes, para que el contenido ya se lea
        // mientras la forma todavía se está asentando. Es lo que hace quicklook, y es la
        // diferencia entre "aparece" y "llega".
        resolved.fadeMs = SettleMs(resolved.spring) * 0.6f;
        return resolved;
    }

    resolved.animate = false;
    resolved.fadeMs = kReducedFadeMs;
    return resolved;
}

float SettleMs(Spring spring) {
    if (spring.dampingRatio <= 0.0f || spring.periodMs <= 0.0f) return 0.0f;
    constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
    return 4.0f * spring.periodMs / (kTwoPi * spring.dampingRatio);
}

}  // namespace Motion
