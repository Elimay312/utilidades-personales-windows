#pragma once

#include <windows.h>

#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>

#include <map>
#include <set>
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
  // El puntito de "abierta" de cada ranura, en su orden.
  void SetRunning(const std::vector<bool>& running);
  // Sube y baja un par de veces, cada vez menos. forever: hasta StopBounce (una app que
  // arranca: macOS bota hasta que aparece la ventana).
  void Bounce(int index, float height, bool forever);
  void StopBounce(int index);

  // Arrastrar: el icono cogido sube por encima de los vecinos y va donde va el dedo, sin
  // muelle; los vecinos se apartan con muelle. Shift se suma en la expresión de Offset.
  void SetLifted(int index, bool lifted);
  void SetShift(int index, float x);
  void SpringShift(int index, float x);
  // Sacado del dock: se desvanece (no encoge: Scale es de la expresión de la lupa).
  void Puff(int index);

  // Esconder desliza el árbol hacia abajo con un muelle; la ventana no se mueve nunca.
  void Slide(bool hidden, bool instant);

  // Las superficies de icono se comparten entre docks por (clave, px) y sobreviven a las
  // reconstrucciones: abrir una app sin anclar, o recargar la config, no vuelve a extraer los
  // iconos que ya están. Después de cada tanda se poda lo que ya no usa nadie.
  static bool HasIcon(const std::wstring& key, int px);
  static void KeepOnlyIcons(const std::set<std::pair<std::wstring, int>>& used);

 private:
  winrt::Windows::UI::Composition::Compositor compositor_{nullptr};
  winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target_{nullptr};
  winrt::Windows::UI::Composition::ContainerVisual root_{nullptr};
  winrt::Windows::UI::Composition::CompositionPropertySet props_{nullptr};
  std::vector<winrt::Windows::UI::Composition::SpriteVisual> labels_;  // nullptr en separadores
  std::vector<winrt::Windows::UI::Composition::SpriteVisual> dots_;    // nullptr en separadores
  std::vector<winrt::Windows::UI::Composition::SpriteVisual> items_;   // icono o separador
  std::vector<bool> running_;
  int labelShown_ = -1;
  float hiddenOffset_ = 0;
};

}  // namespace dock
