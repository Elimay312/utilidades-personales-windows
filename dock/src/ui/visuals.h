#pragma once

#include <windows.h>

#include <winrt/Windows.Foundation.Numerics.h>
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

  // El menú, dibujado a mano: TrackPopupMenu necesita el foco para cerrarse bien y el dock
  // no lo toma nunca. Centrado en anchorX pero sin salirse de [left, right] (la ventana): la
  // región recorta también el dibujo, así que quien lo abre tiene que meter MenuRect en ella.
  // closable: un ✕ por fila (la rueda).
  void OpenMenu(const std::vector<std::wstring>& items, float anchorX, float bottom, float left, float right,
                float scale, bool closable);
  void CloseMenu();
  bool MenuOpen() const { return static_cast<bool>(menu_); }
  float MenuTop() const { return menuOrigin_.y; }
  RECT MenuRect() const;
  int MenuHitTest(float x, float y) const;
  // La fila cuyo ✕ está bajo el punto, o -1. Va antes que MenuHitTest: el ✕ está dentro de
  // la fila.
  int MenuHitTestClose(float x, float y) const;
  void MenuSetHot(int index);
  // Cuántas filas caben en ese alto: la ventana no crece (su alto va dentro de las
  // expresiones), así que una lista larga se desplaza con la selección.
  static int RowsThatFit(float height, float scale);

  // Soltar: levanta el icono que recibiría el fichero (con muelle sobre Bounce) y baja el
  // anterior; -1 no levanta ninguno.
  void SetDropTarget(int index, float height);
  // El "+" pegado a la derecha de la barra, solo mientras se arrastra algo encima: se crea al
  // entrar y se tira al salir, el resto del tiempo el dock no tiene botones de más.
  void SetAddZone(bool visible);
  void SetAddZoneHot(bool hot);
  // Cuánto sobresale el "+" (resaltado) por la derecha del borde de la barra.
  float AddZoneReach() const;

  // El stack: la rejilla de una carpeta, en su propia ventana (la del dock no llega de alto).
  // Mismo material que la barra. cell, icon y pad en px físicos; filas de `columns`.
  void BuildStack(const std::vector<DockItem>& items, const IconSet& icons, int columns, float cell, float icon, float pad,
                  float scale);
  void StackSetHot(int index);

  // Esconder desliza el árbol hacia abajo con un muelle; la ventana no se mueve nunca.
  void Slide(bool hidden, bool instant);

  // Las superficies de icono se comparten entre docks por (clave, px) y sobreviven a las
  // reconstrucciones: abrir una app sin anclar, o recargar la config, no vuelve a extraer los
  // iconos que ya están. Después de cada tanda se poda lo que ya no usa nadie.
  static bool HasIcon(const std::wstring& key, int px);
  static void KeepOnlyIcons(const std::set<std::pair<std::wstring, int>>& used);
  // Devuelve al sistema lo que WARP guarda de las superficies ya soltadas (al cerrar un stack).
  static void Trim();

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
  float padding_ = 0, barTop_ = 0;
  int dropTarget_ = -1;
  winrt::Windows::UI::Composition::ContainerVisual addZone_{nullptr};
  bool addZoneHot_ = false;
  winrt::Windows::UI::Composition::SpriteVisual stackHot_{nullptr};
  int stackHotIndex_ = -1, stackColumns_ = 1;
  float stackCell_ = 0, stackPad_ = 0;
  winrt::Windows::UI::Composition::ContainerVisual menu_{nullptr};
  winrt::Windows::UI::Composition::SpriteVisual menuHot_{nullptr};
  winrt::Windows::Foundation::Numerics::float2 menuOrigin_{}, menuSize_{};
  size_t menuRows_ = 0;
  float menuScale_ = 1;
  bool menuClosable_ = false;
  int menuHotIndex_ = -1;
};

}  // namespace dock
