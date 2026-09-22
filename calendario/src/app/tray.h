#pragma once

#include <windows.h>

#include <shellapi.h>  // NOTIFYICONDATAW

#include <vector>

#include "data/model.h"

namespace agenda {

// The shell sends this back for the notification icon. With NOTIFYICON_VERSION_4 the event is
// in the low word of lParam and the screen position of the click is in wParam.
inline constexpr UINT kTrayMessage = WM_APP + 1;

enum TrayCommand : UINT {
  kTrayNone = 0,
  kTrayOpen = 1,
  kTrayExit = 2,
  kTrayConnect = 3,
  kTrayDisconnect = 4,
  // And one per calendar, from here up: the command that comes back is this plus the position
  // in the list that was handed in. A range instead of a name, because the entries are whatever
  // Google last said there was.
  kTrayCalendarFirst = 100,
};

// What the menu has to know to draw itself. It is asked for at the moment of the right click
// and not kept, because the list of calendars changes underneath it on every pass.
struct TrayState {
  bool configured = false;  // there are credentials in config.local.json
  bool connected = false;   // and an account behind them
  std::vector<CalendarInfo> calendars;
};

class Tray {
 public:
  bool Add(HINSTANCE instance, HWND owner);
  void Remove();
  void Warn(const wchar_t* title, const wchar_t* text);
  UINT ShowMenu(POINT at, const TrayState& state) const;

 private:
  NOTIFYICONDATAW Data() const;

  HWND owner_ = nullptr;
  bool added_ = false;
};

}  // namespace agenda
