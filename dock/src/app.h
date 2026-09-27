#pragma once

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
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
  // Apunta dónde está ahora una ventana (sus bordes visibles), para el genio: cuando llega el
  // aviso de minimizar ya es un icono en -32000.
  void Remember(HWND window);
  std::optional<RECT> Remembered(HWND window) const {
    const auto found = rects_.find(window);
    return found != rects_.end() ? std::optional(found->second) : std::nullopt;
  }

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
  // Una ventana empieza a minimizarse (EVENT_SYSTEM_MINIMIZESTART): el genio hacia su icono.
  void OnMinimizeStart(HWND window);
  static void CALLBACK OnWinEvent(HWINEVENTHOOK hook, DWORD event, HWND window, LONG object, LONG child, DWORD thread,
                                  DWORD time);
  // Sin la animación propia de Windows en las ventanas que enseña algún dock: el genio la
  // sustituye, y las dos a la vez se pisarían.
  void QuietTransitions();

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
  HWINEVENTHOOK minimizeHook_ = nullptr, moveHook_ = nullptr;
  std::set<HWND> quiet_;  // ventanas con las transiciones de DWM apagadas por el dock
  std::map<HWND, RECT> rects_;  // dónde estaba cada una la última vez que se miró
  std::chrono::steady_clock::time_point lastMinimize_{};
  UINT shellHookMessage_ = 0;
  UINT taskbarCreatedMessage_ = 0;
  std::vector<std::unique_ptr<DockWindow>> docks_;
};

}  // namespace dock
