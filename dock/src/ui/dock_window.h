#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "model/config.h"

namespace dock {

class App;

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
  DockWindow(App& app, const Monitor& monitor, const DockConfig& config);
  ~DockWindow();
  DockWindow(const DockWindow&) = delete;
  DockWindow& operator=(const DockWindow&) = delete;

  bool Create();
  const std::wstring& Device() const { return monitor_.device; }

  // Lo que App reparte a todos los docks.
  void OnWindowActivated();
  void OnTaskbarCreated();

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);

  int Px(float logical) const;
  void RefreshMonitor();
  void Reposition();
  RECT BarRect(bool tall) const;
  void ApplyRegion();
  void Reveal();
  void Hide();
  void CheckFullscreen();
  void ReassertTopmost();

  App& app_;
  Monitor monitor_;
  DockConfig config_;
  HWND hwnd_ = nullptr;
  UINT dpi_ = 96;
  int width_ = 0;
  int height_ = 0;
  bool registered_ = false;
  bool revealed_ = false;
  bool hovering_ = false;
  bool fullscreen_ = false;
  std::vector<RECT> region_;
};

}  // namespace dock
