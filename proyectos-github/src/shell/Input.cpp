#include "shell/Input.h"

#include <cmath>

namespace Input {

void Clicks::Configure(unsigned intervalMs, float slopDip) {
    if (intervalMs > 0) m_intervalMs = intervalMs;
    if (slopDip > 0.0f) m_slop = slopDip;
}

int Clicks::Count(std::uint64_t timeMs, float x, float y) {
    // La distancia cuenta tanto como el tiempo. Dos clics rápidos en sitios distintos son
    // dos clics, no un doble: sin el umbral de distancia, teclear deprisa en un campo y
    // luego pinchar en otro sitio seleccionaría una palabra que nadie pidió.
    const bool aTiempo = m_count > 0 && timeMs >= m_last && (timeMs - m_last) <= m_intervalMs;
    const bool enSitio = std::fabs(x - m_lastX) <= m_slop && std::fabs(y - m_lastY) <= m_slop;

    if (aTiempo && enSitio && m_count < 3) {
        ++m_count;
    } else {
        m_count = 1;
    }

    m_last = timeMs;
    m_lastX = x;
    m_lastY = y;
    return m_count;
}

void Clicks::Reset() {
    m_count = 0;
    m_last = 0;
}

}  // namespace Input
