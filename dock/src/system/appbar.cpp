#include "system/appbar.h"

#include <shellapi.h>

namespace dock {
namespace {

APPBARDATA Data(HWND hwnd) {
  APPBARDATA data{};
  data.cbSize = sizeof(data);
  data.hWnd = hwnd;
  data.uCallbackMessage = kAppBarCallback;
  data.uEdge = ABE_BOTTOM;
  return data;
}

}  // namespace

bool AppBarRegister(HWND hwnd) {
  APPBARDATA data = Data(hwnd);
  return SHAppBarMessage(ABM_NEW, &data) != 0;
}

void AppBarRemove(HWND hwnd) {
  // Sin esto, la franja reservada sobrevive al proceso.
  APPBARDATA data = Data(hwnd);
  SHAppBarMessage(ABM_REMOVE, &data);
}

std::optional<RECT> AppBarReserve(HWND hwnd, const RECT& monitor, int height) {
  APPBARDATA data = Data(hwnd);
  data.rc = monitor;
  data.rc.top = monitor.bottom - height;
  SHAppBarMessage(ABM_QUERYPOS, &data);
  // QUERYPOS puede haber movido el borde de abajo (otra appbar); la altura es la nuestra.
  data.rc.top = data.rc.bottom - height;
  if (SHAppBarMessage(ABM_SETPOS, &data) == 0) return std::nullopt;
  return data.rc;
}

void AppBarActivate(HWND hwnd) {
  APPBARDATA data = Data(hwnd);
  SHAppBarMessage(ABM_ACTIVATE, &data);
}

void AppBarWindowPosChanged(HWND hwnd) {
  APPBARDATA data = Data(hwnd);
  SHAppBarMessage(ABM_WINDOWPOSCHANGED, &data);
}

}  // namespace dock
