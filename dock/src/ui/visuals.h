#pragma once

#include <windows.h>

#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>

namespace dock {

// El árbol de Composition de un dock. El Compositor es uno para todo el proceso, con un
// DesktopWindowTarget por ventana: el dock de C# creaba uno por pantalla y bajar a uno
// ahorró de 128 a 117 MB.
class Visuals {
 public:
  explicit Visuals(HWND hwnd);

  // Barra de acrílico centrada y apoyada en el borde inferior. Medidas en px físicos.
  void LayoutBar(float windowWidth, float windowHeight, float barWidth, float barHeight);

  // Esconder desliza la barra hacia abajo con un muelle; la ventana no se mueve nunca.
  // instant: sin animación (al crear el dock ya escondido).
  void Slide(bool hidden, bool instant);

 private:
  winrt::Windows::UI::Composition::Compositor compositor_{nullptr};
  winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target_{nullptr};
  winrt::Windows::UI::Composition::ContainerVisual root_{nullptr};
  winrt::Windows::UI::Composition::ContainerVisual bar_{nullptr};
  winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry corners_{nullptr};
  float barHeight_ = 0;
};

}  // namespace dock
