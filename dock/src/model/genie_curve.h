#pragma once

// La deformación del genio: una ventana que se derrite hacia su icono del dock. Portada del
// dock de C# (GenieCurve.cs), que la sacó del plugin Magic Lamp de compiz.
//
// Misma disciplina que la lupa: se mapean los BORDES de cada franja y la escala sale de la
// diferencia, así que las franjas encajan sin huecos ni solapes por construcción.
//
// En vertical, dos fases: primero la ventana se estira desde su borde de arriba hasta el
// icono; después todo el conjunto se absorbe dentro de él. En horizontal, el cuello del
// embudo: el ancho de cada franja no depende de su posición en la ventana sino de hasta dónde
// ha bajado, con una sigmoide, y de ahí sale la silueta que hace que se reconozca como genio y
// no como un encogimiento.

#include <algorithm>
#include <cmath>
#include <utility>

namespace dock {

struct Box {
  float left = 0, top = 0, right = 0, bottom = 0;
  float Width() const { return right - left; }
  float Height() const { return bottom - top; }
  float CenterX() const { return (left + right) / 2; }
  float HalfWidth() const { return Width() / 2; }
};

class GenieCurve {
 public:
  static constexpr float kStretchEnd = 0.45f;  // donde acaba el estirado y empieza la absorción
  static constexpr float kSlope = 10;          // pendiente de la sigmoide: la de compiz

  GenieCurve(Box window, Box target, int slices) : window_(window), target_(target), slices_(std::max(1, slices)) {}

  const Box& Window() const { return window_; }
  const Box& Target() const { return target_; }
  int Slices() const { return slices_; }

  static float RawSigmoid(float t) { return 1 / (1 + std::exp(-kSlope * (t - 0.5f))); }
  // Normalizada a 0 en 0 y 1 en 1: sin eso la ventana en reposo ya saldría encogida.
  static float Sigmoid(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float low = RawSigmoid(0);
    return (RawSigmoid(t) - low) / (RawSigmoid(1) - low);
  }
  static float SmoothStep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) return value >= edge1 ? 1.0f : 0.0f;
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
  }
  float Stretch(float p) const { return SmoothStep(0, kStretchEnd, p); }
  float Absorb(float p) const { return SmoothStep(kStretchEnd, 1, p); }

  // Y de pantalla del punto a la altura relativa v de la ventana (0 arriba, 1 abajo) en p.
  float VerticalAt(float v, float p) const {
    const float rest = window_.top + v * window_.Height();
    const float stretched = window_.top + v * (target_.bottom - window_.top);
    const float absorbed = target_.top + v * target_.Height();
    const float formed = std::lerp(rest, stretched, Stretch(p));
    return std::lerp(formed, absorbed, Absorb(p));
  }

  // Cuánto se ha estrechado lo que ha llegado a la altura y: 1 arriba (ancho de ventana), 0 en
  // el icono.
  float NeckAt(float y) const {
    const float span = target_.bottom - window_.top;
    if (std::abs(span) < 0.001f) return 1;
    return Sigmoid((target_.bottom - y) / span);
  }

  std::pair<float, float> HorizontalAt(int index, float p) const {
    const float neck = NeckAt(VerticalAt((index + 0.5f) / slices_, p));
    float half = std::lerp(window_.HalfWidth(), target_.HalfWidth(), 1 - neck);
    float center = std::lerp(window_.CenterX(), target_.CenterX(), 1 - neck);
    // El embudo entra con el estirado: en reposo la malla tiene que ser la ventana, píxel a
    // píxel.
    half = std::lerp(window_.HalfWidth(), half, Stretch(p));
    center = std::lerp(window_.CenterX(), center, Stretch(p));
    // Y la absorción la lleva al icono EXACTAMENTE: la sigmoide se acerca a 0 pero no llega, y
    // sin esto al final la franja seguía unos píxeles más ancha que el icono.
    half = std::lerp(half, target_.HalfWidth(), Absorb(p));
    center = std::lerp(center, target_.CenterX(), Absorb(p));
    return {center - half, center + half};
  }

  float TopOf(int index, float p) const { return VerticalAt(static_cast<float>(index) / slices_, p); }
  float BottomOf(int index, float p) const { return VerticalAt((index + 1.0f) / slices_, p); }

 private:
  Box window_, target_;
  int slices_;
};

// El ritmo: cubic-bezier(0.45, 0, 0.2, 1), el del de C#. Lineal se siente muerto: el genio
// arranca despacio, coge velocidad y se mete de golpe en el icono.
inline float GenieEase(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  const auto bezier = [](float s, float a, float b) {
    const float r = 1 - s;
    return 3 * r * r * s * a + 3 * r * s * s * b + s * s * s;
  };
  // x(s) es monótona: se busca la s de ese tiempo por bisección.
  float low = 0, high = 1;
  for (int i = 0; i < 30; i++) {
    const float mid = (low + high) / 2;
    (bezier(mid, 0.45f, 0.2f) < t ? low : high) = mid;
  }
  return bezier((low + high) / 2, 0, 1);
}

}  // namespace dock
