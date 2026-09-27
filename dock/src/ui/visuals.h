#pragma once

#include <windows.h>

#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>

#include <map>
#include <string>
#include <vector>

#include "model/magnify.h"
#include "system/icons.h"

namespace dock {

// Lo que se pinta en una ranura: un icono (con su nombre) o un separador.
struct DockItem {
  std::wstring name;
  std::wstring iconKey;
  bool separator = false;
};

// El árbol de Composition de un dock. El Compositor es uno para todo el proceso, con un
// DesktopWindowTarget por ventana: el dock de C# creaba uno por pantalla y bajar a uno
// ahorró de 128 a 117 MB.
//
// A partir de Build el hilo de UI no toca la geometría: la posición y la escala de cada
// icono son expresiones en forma cerrada sobre el cursor, y se evalúan en DWM. Por cada
// movimiento del ratón solo se escribe un escalar.
class Visuals {
 public:
  explicit Visuals(HWND hwnd);

  // Todo en px físicos. scale es dpi/96.
  void Build(const Curve& curve, const std::vector<DockItem>& items, const IconSet& icons, float windowWidth,
             float windowHeight, float padding, float iconSize, float scale);

  // Cursor en coordenadas de REPOSO. Lo único que el hilo de UI escribe al mover el ratón.
  void SetCursor(float rest);
  // Entrada y salida del hover con un muelle sobre Amount (nunca sobre C, que tiene que
  // seguir al puntero sin retraso).
  void SetHover(bool hovering);
  void SetLabel(int index);

  // Esconder desliza el árbol hacia abajo con un muelle; la ventana no se mueve nunca.
  void Slide(bool hidden, bool instant);

  // Las superficies de icono se comparten entre docks por (clave, px). Se vacía al empezar
  // cada tanda de iconos, para no arrastrar las de apps que ya no están.
  static void ClearIconCache();

 private:
  winrt::Windows::UI::Composition::Compositor compositor_{nullptr};
  winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target_{nullptr};
  winrt::Windows::UI::Composition::ContainerVisual root_{nullptr};
  winrt::Windows::UI::Composition::CompositionPropertySet props_{nullptr};
  std::vector<winrt::Windows::UI::Composition::SpriteVisual> labels_;  // nullptr en separadores
  int labelShown_ = -1;
  float hiddenOffset_ = 0;
};

}  // namespace dock
