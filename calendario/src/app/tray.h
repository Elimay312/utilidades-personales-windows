#pragma once

#include <windows.h>

#include <shellapi.h>  // NOTIFYICONDATAW

namespace agenda {

// The shell sends this back for the notification icon. With NOTIFYICON_VERSION_4 the event is
// in the low word of lParam and the screen position of the click is in wParam.
inline constexpr UINT kTrayMessage = WM_APP + 1;

enum TrayCommand : UINT {
  kTrayNone = 0,
  kTrayOpen = 1,
  kTrayExit = 2,
};

class Tray {
 public:
  bool Add(HINSTANCE instance, HWND owner);
  void Remove();
  void Warn(const wchar_t* title, const wchar_t* text);
  UINT ShowMenu(POINT at) const;

 private:
  NOTIFYICONDATAW Data() const;

  HWND owner_ = nullptr;
  bool added_ = false;
};

}  // namespace agenda
