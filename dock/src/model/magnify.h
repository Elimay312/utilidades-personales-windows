#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace dock {

// Una ranura del dock: cuánto sitio ocupa y cuánto de ese sitio se pinta. Un icono ocupa
// su tamaño más la separación; un separador, mucho menos, y solo pinta una raya.
struct Slot {
  float width = 0;
  float content = 0;
};

// La curva de magnificación. La regla que lo gobierna todo: la escala se define y la
// posición se integra. Con c = cursor en coordenadas de reposo, R = radio y M = escala
// máxima:
//
//   s(u) = 1 + (M-1)·f(t),  t = (u-c)/R,  f(t) = (1+cos(π·t))/2 si |t|≤1, si no 0
//   T(u) = ∫₀ᵘ s = u + (M-1)·R·[ G((u-c)/R) − G(−c/R) ]
//   G(t) = clamp(t,−1,1)/2 + sin(π·clamp(t,−1,1))/(2π)
//
// Y se mapean los BORDES de cada elemento, no su centro: así nunca se solapan ni dejan
// huecos sin corregir nada a mano. Coseno elevado y no gaussiana porque su derivada es 0 en
// el borde del radio (sin costura) y porque las expresiones de Composition tienen Sin y
// Clamp pero no Exp.
class Curve {
 public:
  Curve() = default;
  Curve(std::vector<Slot> slots, float radius, float maxScale)
      : slots_(std::move(slots)), radius_(radius), maxScale_(maxScale) {
    edges_.reserve(slots_.size() + 1);
    float running = 0;
    for (const Slot& slot : slots_) {
      edges_.push_back(running);
      running += slot.width;
    }
    edges_.push_back(running);
  }

  int Count() const { return static_cast<int>(slots_.size()); }
  float Radius() const { return radius_; }
  float MaxScale() const { return maxScale_; }
  const Slot& At(int i) const { return slots_[i]; }
  float RestWidth() const { return edges_.empty() ? 0 : edges_.back(); }
  // El ancho total es exactamente RestWidth + (M−1)·R mientras el bulto no toque los
  // extremos: el dock no "respira" al mover el ratón por el centro.
  float MaxGrowth() const { return (maxScale_ - 1) * radius_; }
  float SlotStart(int i) const { return edges_[i]; }
  float RestLeft(int i) const { return edges_[i] + (slots_[i].width - slots_[i].content) * 0.5f; }
  float RestRight(int i) const { return RestLeft(i) + slots_[i].content; }

  static float G(float t) {
    t = std::clamp(t, -1.0f, 1.0f);
    return t * 0.5f + std::sin(std::numbers::pi_v<float> * t) / (2 * std::numbers::pi_v<float>);
  }

  float Transfer(float u, float cursor, float amount = 1) const {
    const float k = (maxScale_ - 1) * amount * radius_;
    return u + k * (G((u - cursor) / radius_) - G(-cursor / radius_));
  }

  float Origin(float available, float cursor, float amount = 1) const {
    return (available - Transfer(RestWidth(), cursor, amount)) * 0.5f;
  }

  float Project(float u, float available, float cursor, float amount = 1) const {
    return Origin(available, cursor, amount) + Transfer(u, cursor, amount);
  }

  // Del x de pantalla al punto de REPOSO que cae ahí. El cursor llega distorsionado y la
  // curva está en reposo: usar el x de pantalla como c es justo el "resbalón" de los clones
  // malos del Dock. Project es monótona, así que basta bisección; 40 pasos dejan el error
  // por debajo de una millonésima del ancho.
  float Invert(float screenX, float available, float amount = 1) const {
    float low = 0, high = RestWidth();
    for (int i = 0; i < 40; i++) {
      const float mid = (low + high) * 0.5f;
      if (Project(mid, available, mid, amount) < screenX) low = mid;
      else high = mid;
    }
    return (low + high) * 0.5f;
  }

  // Ranura que contiene esa coordenada de reposo (intervalos semiabiertos), o -1.
  int SlotAt(float rest) const {
    for (int i = 0; i < Count(); i++)
      if (rest >= edges_[i] && rest < edges_[i + 1]) return i;
    return -1;
  }

 private:
  std::vector<Slot> slots_;
  std::vector<float> edges_;
  float radius_ = 1;
  float maxScale_ = 1;
};

}  // namespace dock
