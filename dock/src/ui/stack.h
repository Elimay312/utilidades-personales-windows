#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "system/icons.h"
#include "ui/visuals.h"

namespace dock {

class App;

// Lo que el stack avisa al dock que lo abrió: que se ha cerrado (y el dock ya puede
// esconderse). Por mensaje y no llamando: el stack se cierra desde su propio WndProc y el dock
// lo destruye, así que la destrucción tiene que esperar a que ese WndProc vuelva.
constexpr UINT kStackClosed = WM_APP + 4;

struct StackItem {
  std::wstring name;
  std::wstring path;
  bool folder = false;
  bool back = false;  // la entrada "Atrás", que vuelve a la carpeta de arriba
};

// Lo que hay en una carpeta, ya con iconos. folder vacío si no se pudo leer.
struct StackContents {
  std::wstring folder;
  std::vector<StackItem> items;
  IconSet icons;
};

// Lee la carpeta con el shell (así funciona igual con la papelera y da los nombres como el
// Explorador) y extrae los iconos en el proceso hijo. Bloquea: solo desde el worker.
// back: la carpeta a la que vuelve la primera entrada, o vacío en la raíz.
StackContents ReadStack(const std::wstring& folder, const std::wstring& back);

// La rejilla de una carpeta del dock, el "stack" de macOS. Ventana propia porque la del dock
// no llega de alto para veinte elementos; recibe clics pero, como el dock, nunca el foco.
class StackWindow {
 public:
  // dock: la ventana del dock, que es su dueña y a la que avisa con kStackClosed.
  StackWindow(App& app, HWND dock);
  ~StackWindow();
  StackWindow(const StackWindow&) = delete;
  StackWindow& operator=(const StackWindow&) = delete;

  // anchorX: el centro del icono; above: el techo de lo que ocupa el dock. En px de pantalla.
  bool Open(StackContents contents, int anchorX, int above, const RECT& monitor, UINT dpi);
  // Salir del dock sin entrar aquí también cierra, con el mismo margen que salir de aquí.
  void ScheduleClose();

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);
  void Show(StackContents contents);
  int HitTest(int x, int y) const;
  void OnClick(int index);
  void Close();

  App& app_;
  HWND dock_;
  HWND hwnd_ = nullptr;
  std::unique_ptr<Visuals> visuals_;
  std::vector<StackItem> items_;
  std::vector<std::wstring> history_;  // por dónde se ha bajado, para volver
  int anchorX_ = 0, above_ = 0;
  RECT monitor_{};
  float scale_ = 1;
  int columns_ = 1;
  bool tracking_ = false;
};

}  // namespace dock
