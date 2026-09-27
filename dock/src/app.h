#pragma once

#include <windows.h>

#include <filesystem>
#include <memory>
#include <vector>

#include "model/config.h"
#include "ui/dock_window.h"

namespace dock {

// Lo que es del proceso y no de una pantalla: la ventana anfitriona (shell hook, avisos de
// explorer, cambios de pantalla, salida), la config vigente y un dock por monitor.
//
// En el dock de C# esto vivía repartido entre los docks, con una "ventana dueña" para el
// shell hook y un flag para que el último WM_DESTROY no cerrara el proceso a mitad de un
// rebuild. Con una anfitriona que nunca se destruye en un rebuild, ninguna de las dos cosas
// hace falta.
class App {
 public:
  explicit App(std::filesystem::path configPath);
  int Run();
  void Quit();

 private:
  static LRESULT CALLBACK HostProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT HandleHost(UINT message, WPARAM wparam, LPARAM lparam);
  void Rebuild();

  std::filesystem::path configPath_;
  DockConfig config_;
  HWND host_ = nullptr;
  UINT shellHookMessage_ = 0;
  UINT taskbarCreatedMessage_ = 0;
  std::vector<std::unique_ptr<DockWindow>> docks_;
};

}  // namespace dock
