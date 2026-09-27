#pragma once

#include <windows.h>
#include <oleidl.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <functional>
#include <string>
#include <vector>

#include "model/config.h"

namespace dock {

// Algo soltado sobre el dock, ya resuelto. app es lo que se guardaría al anclarlo; path, su
// fichero de disco (vacío en un objeto virtual, como una app de la Store arrastrada desde
// Inicio), que es lo único que se le puede pasar a una app para abrirlo.
struct Dropped {
  DockApp app;
  std::wstring path;
};

// SHCreateShellItemArrayFromDataObject y no CF_HDROP a mano: entiende también
// CFSTR_SHELLIDLIST (PIDL), que es lo que llevan los objetos sin ruta de disco.
std::vector<Dropped> ItemsOf(IDataObject* data);

// IDropTarget y no WM_DROPFILES: hace falta la posición DURANTE el arrastre para levantar el
// icono de debajo, y WM_DROPFILES solo avisa al soltar.
//
// Lo llama OLE en el hilo de UI, desde el bucle modal de quien arrastra: si aquí se bloquea,
// la app que arrastra se cuelga con nosotros. Por eso nada de lo que se hace aquí espera.
class DropTarget final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IDropTarget> {
 public:
  DropTarget(HWND hwnd, std::function<bool(POINT)> over, std::function<void()> leave,
             std::function<void(std::vector<Dropped>)> drop);

  STDMETHODIMP DragEnter(IDataObject* data, DWORD keys, POINTL at, DWORD* effect) override;
  STDMETHODIMP DragOver(DWORD keys, POINTL at, DWORD* effect) override;
  STDMETHODIMP DragLeave() override;
  STDMETHODIMP Drop(IDataObject* data, DWORD keys, POINTL at, DWORD* effect) override;

 private:
  HWND hwnd_;
  std::function<bool(POINT)> over_;
  std::function<void()> leave_;
  std::function<void(std::vector<Dropped>)> drop_;
  // La miniatura que sigue al cursor es cosa del DESTINO: sin él, sobre el dock se ve un
  // cursor pelado y sobre cualquier otra ventana el fichero. Solo es visual: si falta, se sigue.
  Microsoft::WRL::ComPtr<IDropTargetHelper> helper_;
};

}  // namespace dock
