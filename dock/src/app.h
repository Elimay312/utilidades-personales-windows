#pragma once

#include <windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "model/config.h"
#include "system/worker.h"
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
  ~App();
  int Run();
  void Quit();
  // Extrae otra vez los iconos de lo que enseñan los docks y se los da a todos.
  void RequestIcons();
  // La ventana con el foco a efectos del clic: la de primer plano, salvo que sea del propio
  // dock, y entonces la última ajena que lo tuvo.
  HWND ForeignForeground() const;
  // Algo que puede bloquear (el shell), al worker: responde con PostMessage a quien lo pidió.
  void PostJob(std::function<void()> job) { worker_.Post(std::move(job)); }
  // Guarda la superposición de una pantalla y recarga todo tras delayMs (para dejar acabar el
  // desvanecido de un icono quitado). Recargar reconstruye también las otras pantallas.
  void SaveAndReload(const std::wstring& device, const std::vector<DockApp>& base,
                     const std::vector<DockApp>& current, UINT delayMs);

 private:
  static LRESULT CALLBACK HostProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT HandleHost(UINT message, WPARAM wparam, LPARAM lparam);
  void LoadFiles();
  // Un dock nuevo por monitor: al arrancar y cuando cambian las pantallas.
  void Rebuild();
  // La config releída sobre los docks que ya hay, sin destruir sus ventanas.
  void Apply();
  void CheckFilesChanged();
  // Un barrido de ventanas para todos los docks: puntitos, apps sin anclar, autoocultar.
  // true si apareció o se fue alguna app sin anclar (hay iconos que pedir).
  bool RefreshRunning();
  void OnIcons(struct IconResult* result);
  // El atajo de perfil de la config vigente, registrado en la anfitriona. Solo toca el
  // registro si cambió: registrar y soltar en cada recarga lo dejaría libre un instante.
  void SyncHotkey();

  std::filesystem::path configPath_;
  std::filesystem::path localPath_;
  DockConfig config_;
  LocalOverlay local_;
  FILETIME configStamp_{}, localStamp_{};
  HANDLE watch_ = INVALID_HANDLE_VALUE;
  Worker worker_;
  // Cada petición lleva su número: la respuesta de una config que ya no está se tira.
  unsigned iconRound_ = 0;
  HWND host_ = nullptr;
  HWND lastForeign_ = nullptr;
  std::wstring hotkey_;  // el texto registrado ahora, o vacío
  UINT shellHookMessage_ = 0;
  UINT taskbarCreatedMessage_ = 0;
  std::vector<std::unique_ptr<DockWindow>> docks_;
};

}  // namespace dock
