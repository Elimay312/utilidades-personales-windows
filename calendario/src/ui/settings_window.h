#pragma once

// The settings: one ordinary window with a real title bar, drawn with the popup's own tokens,
// fonts and shapes -- cards on the panel colour, the segmented capsule of the app's view tabs,
// the detail panel's chooser. A normal caption because this is the one place in Agenda that is
// a window and not a surface: it is found in Alt+Tab, it moves between monitors and it closes
// with its own ×.
//
// It is built when opened and destroyed when closed, so it costs nothing while Agenda sits in
// the tray.

#include <windows.h>

#include <d2d1.h>
#include <wrl/client.h>

#include <functional>
#include <string>
#include <vector>

#include "core/config.h"
#include "data/model.h"
#include "ui/accessibility.h"
#include "ui/paint.h"
#include "ui/theme.h"

namespace agenda {

class Store;
namespace sync {
class GoogleSync;
}

class SettingsWindow final : public A11ySource {
 public:
  // What changing a setting does outside this window. Main owns the hotkey and the popup, so
  // it is main that answers.
  struct Hooks {
    // Registers `shortcut` in place of the current one. False means Windows refused it and the
    // old one is back, which the window says in red.
    std::function<bool(const std::string& shortcut)> setHotkey;
    // Lets go of the global hotkey while the field listens, so pressing the current one reaches
    // the field instead of opening the popup.
    std::function<void(bool paused)> pauseHotkey;
    // Theme, language or duration moved: the popup has to hear about it.
    std::function<void()> changed;
    // Asks before opening the browser, as CLAUDE.md requires, and then connects.
    std::function<void(HWND owner)> connectGoogle;
  };

  SettingsWindow() = default;
  ~SettingsWindow();
  SettingsWindow(const SettingsWindow&) = delete;
  SettingsWindow& operator=(const SettingsWindow&) = delete;

  void Init(HINSTANCE instance, Preferences* prefs, Store* store, sync::GoogleSync* sync,
            Hooks hooks);
  // Opens it centred on `monitor`, or brings it forward when it is already open.
  void Show(HMONITOR monitor);
  HWND hwnd() const { return hwnd_; }

  // The offscreen snapshot: the window's client area as it opens, with kSnapshotAccounts
  // accounts connected and `calendars` in the chooser, painted into `target` at the size
  // SizeDip() gives.
  bool PaintForSnapshot(ID2D1RenderTarget* target, const Theme& theme,
                        std::vector<CalendarInfo> calendars);
  static constexpr int kSnapshotAccounts = 2;
  static D2D1_SIZE_F SizeDip();

  // --- A11ySource ---
  std::vector<A11yNode> A11yNodes() override;
  void A11yInvoke(int id) override;
  void A11ySelect(int id) override;
  void A11ySetValue(int, const std::wstring&) override {}
  void A11yFocus(int id) override;

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);

  void Render();
  void Paint(ID2D1RenderTarget* target);
  void Refresh();  // rereads what can change behind the window's back
  void ApplyTheme();
  void Retitle();
  void Resize();
  bool Tick(float ms);
  void StartTicking();

  void OnKeyDown(WPARAM key);
  bool OnCapture(WPARAM key);
  void OnLeftDown(float x, float y);
  void OnMouseMove(float x, float y);
  void Activate(int control);
  void Step(int control, int direction);
  void Choose(int control, int option);
  void PickCalendar(int index);
  void RemoveAccount(int index);
  // The window's height follows the number of accounts, one card each.
  void FitHeight();
  int Controls() const;
  void StartCapture();
  void StopCapture();

  int Selected(int control) const;
  D2D1_POINT_2F ToDip(LPARAM lparam) const;

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  Preferences* prefs_ = nullptr;
  Store* store_ = nullptr;
  sync::GoogleSync* sync_ = nullptr;
  Hooks hooks_;
  Accessibility a11y_;

  Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
  Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
  Fonts fonts_;
  Theme theme_ = DarkTheme();
  UINT dpi_ = USER_DEFAULT_SCREEN_DPI;

  int focus_ = 0;
  bool focusVisible_ = false;
  bool capturing_ = false;
  bool listOpen_ = false;
  int listIndex_ = 0;
  int hover_ = -1;
  int hoverOption_ = -1;
  bool tracking_ = false;
  bool ticking_ = false;
  ULONGLONG lastTick_ = 0;
  float hoverT_[16] = {};  // the fixed controls, then one per account
  float toggleT_ = 0.0f;

  std::wstring hotkeyError_;
  std::vector<CalendarInfo> calendars_;
  bool startup_ = false;
  bool configured_ = false;
  bool connected_ = false;
  // The Google accounts (phase 12), a card each under "Cuentas de Google".
  struct AccountRow {
    int id = 0;
    std::wstring email;
    bool connected = false;
    bool operator==(const AccountRow&) const = default;
  };
  std::vector<AccountRow> accounts_;
};

}  // namespace agenda
