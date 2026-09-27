#pragma once

#include <windows.h>
#include <dwmapi.h>

#include <memory>
#include <string>
#include <vector>

#include "model/config.h"
#include "model/magnify.h"
#include "system/jumplist.h"
#include "ui/visuals.h"

namespace dock {

class App;
struct Dropped;
class StackWindow;

// Un monitor tal como lo ve el dock. Se identifica por el nombre de dispositivo
// (\\.\DISPLAY2), nunca por el HMONITOR, que cambia entre arranques.
struct Monitor {
  HMONITOR handle = nullptr;
  std::wstring device;
  RECT bounds{};  // rcMonitor
  RECT work{};    // rcWork
};

// La ventana del dock en un monitor. Ocupa todo el ancho del área de trabajo y la altura del
// icono magnificado más aire para la etiqueta y el menú; lo que es del dock lo decide la
// región, no el rectángulo.
class DockWindow {
 public:
  DockWindow(App& app, const Monitor& monitor, const DockConfig& config, ScreenApps screen);
  ~DockWindow();
  DockWindow(const DockWindow&) = delete;
  DockWindow& operator=(const DockWindow&) = delete;

  bool Create();
  const std::wstring& Device() const { return monitor_.device; }
  // Lo que se dibuja: lo anclado más las apps abiertas sin anclar.
  const std::vector<DockApp>& Apps() const { return drawn_; }
  // Tamaño de las superficies de icono de este dock (icono × magnificación, en px).
  int IconPx() const;
  // Estado de las ventanas tras un barrido. true si cambió el juego de apps abiertas sin
  // anclar y hay iconos nuevos que pedir.
  bool UpdateRunning(struct Snapshot& snapshot, bool showRunning);
  // Los píxeles de los iconos, recién extraídos por el worker.
  void ShowIcons(const IconSet& icons);
  // Config nueva sin destruir la ventana: geometría, appbar y visuals. Los iconos los vuelve
  // a pedir App.
  void Apply(const DockConfig& config, ScreenApps screen);

  // Lo que App reparte a todos los docks.
  void OnWindowActivated();
  void OnTaskbarCreated();

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);

  int Px(float logical) const;
  void RefreshMonitor();
  Curve CurveFor() const;
  void BuildVisuals(const IconSet& icons);
  // true si el tamaño cambió y ya reconstruyó los visuals (y pidió los iconos otra vez).
  bool Reposition();
  RECT BarRect(bool tall) const;
  void ApplyRegion();
  void Compose();
  void OnClick(bool middle);
  // El menú del clic derecho: recientes de la app (llegan del worker), Quitar/Anclar, Salir.
  void OpenContextMenu();
  void ShowMenu();
  // Encima del icono index, dentro de la ventana; la región crece para cubrirlo.
  void PlaceMenu(const std::vector<std::wstring>& items, int index, bool closable);
  void CloseMenu();
  void OnMenuChoice(int choice);
  // La rueda sobre una app con varias ventanas: la lista de sus ventanas, con miniatura en
  // vivo de la elegida y un ✕ por fila. La rueda elige; el clic (en la fila o en el icono)
  // es el que va, porque solo el clic da permiso para traer una ventana al frente.
  void OnWheel(int delta, POINT screen);
  void ShowWheelList();
  void OnWheelPick();
  void OnCloseWindow(int row);
  void ShowPreview();
  void HidePreview();
  // Lo anclado en el orden de head, con la papelera detrás si iba tras las abiertas.
  std::vector<DockApp> WithTrash(std::vector<DockApp> head) const;
  // Arrastrar: solo lo anclado (lo que va antes del separador de las abiertas).
  int DraggableEnd() const;
  void OnDragMove(int x, int y);
  bool PulledOff(int y) const;
  void ApplyDragShifts();
  void FinishDrag(int y);
  void CancelDrag();
  // Soltar desde fuera: sobre el icono de una app se abre con ella, sobre la papelera va a la
  // papelera, y en el "+" se añade. En el resto (separador, márgenes, carpetas) nada: false.
  bool DropOver(POINT screen);
  void DropLeave();
  void OnDropped(std::vector<Dropped> items);
  RECT BarOnScreen() const;
  void UpdateSmartHide();
  void Reveal();
  // force: esconderse aunque no haya nada debajo (el autoocultar inteligente ya lo decidió).
  void Hide(bool force = false);
  void CheckFullscreen();
  void ReassertTopmost();

  App& app_;
  Monitor monitor_;
  DockConfig config_;
  std::vector<DockApp> base_;    // lo que dice dock.json, para deducir la superposición
  std::vector<DockApp> apps_;    // ancladas, ya resueltas para esta pantalla
  std::vector<DockApp> extras_;  // abiertas sin anclar
  std::vector<DockApp> drawn_;   // lo que se ve: apps_ + separador + extras_ + papelera
  std::vector<bool> running_;    // un puntito por entrada de drawn_
  std::vector<std::vector<HWND>> windows_;  // las ventanas de cada entrada de drawn_
  float lastRest_ = -1;          // el cursor en reposo, del último movimiento
  int pressedIndex_ = -1;        // el icono pulsado, hasta soltar
  POINT press_{};
  bool dragging_ = false;        // pasado el umbral
  std::vector<int> dragOrder_;   // el orden que va quedando, en índices de drawn_
  bool dropping_ = false;        // algo arrastrado desde fuera está encima
  int dropSlot_ = -1;            // la app que lo abriría, o la papelera
  bool dropAdd_ = false;         // sobre el "+"
  int menuIndex_ = -1;           // el icono del menú abierto, o -1 (solo "Salir")
  std::vector<struct JumpItem> menuJumps_;
  int wheelIndex_ = -1;               // el icono de la lista de la rueda, o -1
  std::vector<HWND> wheelWindows_;    // copia: la lista no cambia mientras se mira
  int wheelAt_ = 0;                   // la elegida
  int wheelFirst_ = -1;               // la primera fila visible
  HWND preview_ = nullptr;            // la ventana de la miniatura: DWM no pinta en un visual
  HTHUMBNAIL thumb_ = nullptr;
  HWND thumbSource_ = nullptr;
  std::unique_ptr<StackWindow> stack_;  // la rejilla de una carpeta, si hay una abierta
  std::wstring stackFolder_;
  // La app que se está abriendo: su icono bota hasta que aparece su ventana, o 20 s.
  std::wstring launchingTarget_;
  ULONGLONG launchingUntil_ = 0;
  std::wstring lastRunningTrace_;
  HWND hwnd_ = nullptr;
  std::unique_ptr<Visuals> visuals_;
  Curve curve_;  // en px físicos
  bool hoverShown_ = false;
  UINT dpi_ = 96;
  int width_ = 0;
  int height_ = 0;
  bool registered_ = false;
  bool revealed_ = false;
  bool sliding_ = false;  // bajando: la región espera a que termine
  bool hovering_ = false;
  bool fullscreen_ = false;
  std::vector<RECT> region_;
};

}  // namespace dock
