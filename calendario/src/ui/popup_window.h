#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <optional>
#include <string>
#include <string_view>

#include <chrono>

#include "data/store.h"
#include "ui/app_layout.h"
#include "ui/app_view.h"
#include "ui/paint.h"
#include "ui/popup_view.h"
#include "ui/spring.h"
#include "ui/theme.h"
#include "ui/vsync.h"

namespace agenda {

namespace sync {
class GoogleSync;
}

// One frame of the expansion, posted by the FrameClock once per composed frame.
inline constexpr UINT kFrameMessage = WM_APP + 4;

// The popup window: WS_POPUP with no redirection bitmap, its content composed by
// DirectComposition over a premultiplied swap chain so the acrylic DWM paints behind it shows
// through. It is created hidden when the app starts and reused for the life of the process,
// so reacting to the hotkey is a SetWindowPos plus an animation, never device creation.
//
// It is also the expanded app. A click on the month grows this same window into it on a spring
// -- phase 6 -- and the code for that half lives in popup_window_app.cpp, as more members of
// this class: two windows would be a cut between them, which is the one thing the expansion
// must not look like.
class PopupWindow {
 public:
  struct Timing {
    UINT openMs = 160;
    UINT closeMs = 120;
  };

  // `themeOverride` is "dark" or "light" from --theme; empty follows the system.
  bool Create(HINSTANCE instance, HMONITOR monitor, Timing timing,
              std::wstring_view themeOverride);

  // Forces the panel size in DIP instead of working it out from the monitor. Only --panel
  // sets this, so a size can be judged on a screen that would not produce it.
  void SetPanelOverride(D2D1_SIZE_F panel) { panelOverride_ = panel; }

  // Where the popup reads its day from and sends its writes. Owned by the app, not by the
  // window, because phase 5 hangs the Google sync off the same store.
  void SetStore(Store* store) { store_ = store; }
  // Declared and not included: the popup asks it two questions and never looks inside.
  void SetSync(sync::GoogleSync* sync) { sync_ = sync; }

  void Toggle();
  void Show();
  void Hide();

  // The expansion, and back. Both can be called halfway through the other: the spring simply
  // turns round from wherever it is.
  void Expand(Date day);
  void Contract();

  HWND hwnd() const { return hwnd_; }

 private:
  template <class T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);

  void ApplyDwmAttributes();
  bool CreateDevices();
  void Place(const RECT& work);
  void Resize(SIZE size);
  void Render();
  void Animate(bool opening);
  void Rest();  // hidden state: fully transparent and slid down, ready to open
  float SlidePx() const;
  bool AnimationsEnabled() const;

  // There is no WM_PAINT on a window with no redirection bitmap, so anything that changes the
  // model redraws by hand.
  void Invalidate();
  // Rereads the day and the month dots. Called when the day or the month changes and when the
  // worker says it finished something; never in the middle of drawing.
  void Reload();
  // Enter: turns whatever the preview understood into a row. Optimistic -- the card is on
  // screen before the worker has written anything.
  bool CreateFromInput();
  void UndoCreate();
  void ShowToast(std::wstring text);
  void HideToast();
  bool ToggleCardAt(float x, float y);
  bool Tick(float ms);  // advances hover, focus and the month slide; true while still moving
  void StartTicking();
  void RestartCaret();

  // --- The expanded app (popup_window_app.cpp) -------------------------------------------
  enum class Mode { Popup, Morphing, App };
  bool InApp() const { return mode_ != Mode::Popup; }
  // The popup's layout with the capsule wherever the expansion has it now: what the input's
  // drawing and hit testing read, in both modes.
  PanelLayout ActiveLayout() const;
  D2D1_SIZE_F SizeDip() const;
  void StepMorph();
  void ApplyMorph();
  void FinishMorph();
  void ReloadApp();
  void Relayout();
  void SetView(AppView view);
  void MovePeriod(int direction);
  void AddToApp(const DayItem& item, std::optional<Date> day);
  void ScheduleNowTick();
  bool OnAppKeyDown(WPARAM key);
  bool OnAppLeftDown(float x, float y);
  bool OnAppMouseMove(float x, float y);
  void OnWheel(int delta);
  bool TickApp(float step);

  D2D1_POINT_2F ToDip(LPARAM lparam) const;
  int HitDay(float x, float y) const;
  void OnMouseMove(float x, float y);
  void OnLeftDown(float x, float y);
  bool OnKeyDown(WPARAM key);
  void OnImeComposition(LPARAM flags);
  void PlaceCandidateWindow();
  void FocusInput(bool focused);
  void SelectDay(Date date);
  void ChangeMonth(int direction);
  void CopySelection(bool cut);
  void Paste();

  // Moves the red line on the turn of every minute while the app is open.
  static constexpr UINT_PTR kNowTimer = 5;

  HWND hwnd_ = nullptr;
  HMONITOR monitor_ = nullptr;
  Timing timing_{};
  std::wstring themeOverride_;
  D2D1_SIZE_F panelOverride_{};
  UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
  SIZE size_{};    // the window, which is what everything is laid out in
  SIZE buffer_{};  // the swap chain: big enough for the app, so growing never resizes it
  bool visible_ = false;
  bool acrylic_ = false;

  Fonts fonts_;
  Theme theme_ = DarkTheme();
  PanelLayout layout_ = BaseLayout();
  PopupModel model_;

  // What the pointer is over and where the keyboard is going. The model carries the fades;
  // these carry the targets they are walking to.
  int hoverDay_ = -1;
  bool hoverPrev_ = false;
  bool hoverNext_ = false;
  bool tracking_ = false;
  bool ticking_ = false;
  bool inputFocused_ = true;
  bool caretVisible_ = true;
  bool toastOn_ = false;
  bool strikeOn_ = false;

  // What Ctrl+Z would take back, and the line it would put back in the capsule. Empty once the
  // notice is gone: undo is offered while it is on screen and not a second longer.
  struct Undone {
    std::wstring uid;
    bool isTask = false;
    std::wstring typed;
  };
  std::optional<Undone> undo_;
  Store* store_ = nullptr;
  sync::GoogleSync* sync_ = nullptr;
  ULONGLONG lastTick_ = 0;
  std::wstring parsed_;  // the text the preview in the model was built from

  // The expansion. `spring_.x` is how far it has got, 0 the popup and 1 the app; `goal_` is
  // where it is going. The two rectangles are in physical pixels on this monitor.
  Mode mode_ = Mode::Popup;
  Spring spring_;
  float goal_ = 0.0f;
  RECT popupRect_{};
  RECT appRect_{};
  LARGE_INTEGER lastFrame_{};
  FrameClock clock_;
  AppModel app_;
  AppLayout appLayout_;
  int hoverTab_ = -1;
  int hoverCalendar_ = -1;
  bool hoverCollapse_ = false;
  bool hoverPeriodPrev_ = false;
  bool hoverPeriodNext_ = false;

  ComPtr<ID3D11Device> d3d_;
  ComPtr<IDXGISwapChain1> swapChain_;
  ComPtr<ID2D1DeviceContext> dc_;
  ComPtr<IDCompositionDesktopDevice> composition_;
  ComPtr<IDCompositionTarget> target_;
  ComPtr<IDCompositionVisual2> visual_;
  ComPtr<IDCompositionVisual3> visual3_;
};

}  // namespace agenda
