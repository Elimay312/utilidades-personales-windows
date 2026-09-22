#include "app/tray.h"

#include <shellapi.h>

#include <cwchar>

#include "core/i18n.h"
#include "core/log.h"

namespace agenda {
namespace {

// Icon 1 of the executable, declared in assets/agenda.rc. Explorer uses the same one.
constexpr WORD kIconResource = 1;
constexpr UINT kIconId = 1;

}  // namespace

NOTIFYICONDATAW Tray::Data() const {
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = owner_;
  data.uID = kIconId;
  return data;
}

bool Tray::Add(HINSTANCE instance, HWND owner) {
  owner_ = owner;

  NOTIFYICONDATAW data = Data();
  data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
  data.uCallbackMessage = kTrayMessage;
  // Asking for the small icon metrics keeps it crisp instead of letting the shell downscale.
  data.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kIconResource),
                                             IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                             GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  wcsncpy_s(data.szTip, L"Agenda", _TRUNCATE);

  added_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
  if (!added_) {
    LogError(L"tray: Shell_NotifyIconW(NIM_ADD) failed with error {}", GetLastError());
    return false;
  }

  NOTIFYICONDATAW version = Data();
  version.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &version);
  return true;
}

void Tray::Remove() {
  if (!added_) return;
  NOTIFYICONDATAW data = Data();
  Shell_NotifyIconW(NIM_DELETE, &data);
  added_ = false;
}

void Tray::Warn(const wchar_t* title, const wchar_t* text) {
  if (!added_) return;
  NOTIFYICONDATAW data = Data();
  data.uFlags = NIF_INFO;
  data.dwInfoFlags = NIIF_WARNING;
  wcsncpy_s(data.szInfoTitle, title, _TRUNCATE);
  wcsncpy_s(data.szInfo, text, _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &data);
}

void Tray::Notify(const wchar_t* title, const wchar_t* text) {
  if (!added_) return;
  NOTIFYICONDATAW data = Data();
  data.uFlags = NIF_INFO;
  data.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
  wcsncpy_s(data.szInfoTitle, title, _TRUNCATE);
  wcsncpy_s(data.szInfo, text, _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &data);
}

UINT Tray::ShowMenu(POINT at, const TrayState& state) const {
  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) return kTrayNone;

  AppendMenuW(menu, MF_STRING, kTrayOpen, T(L"Abrir", L"Open").data());

  // Nothing about Google appears at all without credentials. An entry that would only ever say
  // "you have not set this up" is worse than no entry: it makes a setup step look like a
  // feature that is broken.
  HMENU calendars = nullptr;
  if (state.configured) {
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    if (state.connected) {
      AppendMenuW(menu, MF_STRING, kTrayDisconnect,
                  T(L"Desconectar de Google", L"Disconnect from Google").data());
    } else {
      AppendMenuW(menu, MF_STRING, kTrayConnect,
                  T(L"Conectar con Google…", L"Connect to Google…").data());
    }

    if (!state.calendars.empty()) {
      calendars = CreatePopupMenu();
      if (calendars != nullptr) {
        for (size_t index = 0; index < state.calendars.size(); ++index) {
          const CalendarInfo& calendar = state.calendars[index];
          const UINT flags = MF_STRING | (calendar.isDefault ? MF_CHECKED : MF_UNCHECKED);
          AppendMenuW(calendars, flags, kTrayCalendarFirst + static_cast<UINT>(index),
                      calendar.title.c_str());
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(calendars),
                    T(L"Calendario por defecto", L"Default calendar").data());
      }
    }
  }

  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kTraySettings, T(L"Configuración…", L"Settings…").data());
  AppendMenuW(menu, MF_STRING, kTrayExit, T(L"Salir", L"Exit").data());
  SetMenuDefaultItem(menu, kTrayOpen, FALSE);

  // The documented dance for tray menus: without the foreground window the menu never closes
  // when you click elsewhere, and without the trailing message it lingers after a pick.
  SetForegroundWindow(owner_);
  const UINT command = static_cast<UINT>(TrackPopupMenu(
      menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, at.x, at.y, 0, owner_, nullptr));
  PostMessageW(owner_, WM_NULL, 0, 0);

  DestroyMenu(menu);
  return command;
}

}  // namespace agenda
