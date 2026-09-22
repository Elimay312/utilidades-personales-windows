#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <string>
#include <string_view>

#include "ui/paint.h"
#include "ui/popup_view.h"
#include "ui/theme.h"

namespace agenda {

// The popup window: WS_POPUP with no redirection bitmap, its content composed by
// DirectComposition over a premultiplied swap chain so the acrylic DWM paints behind it shows
// through. It is created hidden when the app starts and reused for the life of the process,
// so reacting to the hotkey is a SetWindowPos plus an animation, never device creation.
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
  void Toggle();
  void Show();
  void Hide();

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
  bool Tick(float ms);  // advances hover, focus and the month slide; true while still moving
  void StartTicking();
  void RestartCaret();

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

  HWND hwnd_ = nullptr;
  HMONITOR monitor_ = nullptr;
  Timing timing_{};
  std::wstring themeOverride_;
  D2D1_SIZE_F panelOverride_{};
  UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
  SIZE size_{};
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
  ULONGLONG lastTick_ = 0;
  std::wstring parsed_;  // the text the preview in the model was built from

  ComPtr<ID3D11Device> d3d_;
  ComPtr<IDXGISwapChain1> swapChain_;
  ComPtr<ID2D1DeviceContext> dc_;
  ComPtr<IDCompositionDesktopDevice> composition_;
  ComPtr<IDCompositionTarget> target_;
  ComPtr<IDCompositionVisual2> visual_;
  ComPtr<IDCompositionVisual3> visual3_;
};

}  // namespace agenda
