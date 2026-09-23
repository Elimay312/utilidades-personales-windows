#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <string>
#include <string_view>

#include "model/state.h"
#include "ui/layout.h"
#include "ui/paint.h"
#include "ui/panel_view.h"
#include "ui/theme.h"

namespace panel {

// The panel: a borderless tool window with the system acrylic behind it, drawn by Direct2D
// into a composition swap chain and moved by DirectComposition. Created once, hidden; the
// hotkey only shows and hides it, so opening costs a render and not a window. The skeleton is
// calendario/src/ui/popup_window.cpp without the calendar.
class PanelWindow {
 public:
  PanelWindow() = default;
  PanelWindow(const PanelWindow&) = delete;
  PanelWindow& operator=(const PanelWindow&) = delete;

  // `monitor` pins every opening to that monitor; nullptr follows the mouse. `theme` is
  // --theme or the config's, empty to follow Windows.
  bool Create(HINSTANCE instance, HMONITOR monitor, std::wstring_view theme);
  void Show();
  void Hide();
  void Toggle();
  // One line at the bottom of the panel saying what went wrong; empty takes it away.
  void SetNotice(std::wstring notice);
  bool visible() const { return visible_; }
  HWND hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);

  void ApplyDwmAttributes();
  void Backdrop(bool on);
  bool CreateDevices();
  void Place(const RECT& work);
  void Resize(SIZE size);
  void Render();
  void Rest();
  void Animate(bool opening);
  bool AnimationsEnabled() const;
  float SlidePx() const;
  void Shelve();
  void FollowTheme();
  void FollowMonitor();
  void ShowMenu(POINT screen);

  HWND hwnd_ = nullptr;
  HMONITOR monitor_ = nullptr;
  UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
  SIZE size_{};
  bool visible_ = false;
  bool placing_ = false;
  bool backdrop_ = false;  // DWM accepted the system backdrop at all
  bool acrylic_ = false;   // and it is on right now
  std::wstring themeChoice_;

  Theme theme_;
  Fonts fonts_;
  PanelState state_;
  ViewState view_;
  PanelLayout layout_;

  Microsoft::WRL::ComPtr<ID3D11Device> d3d_;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
  Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc_;
  Microsoft::WRL::ComPtr<IDCompositionDesktopDevice> composition_;
  Microsoft::WRL::ComPtr<IDCompositionTarget> target_;
  Microsoft::WRL::ComPtr<IDCompositionVisual2> visual_;
  Microsoft::WRL::ComPtr<IDCompositionVisual3> visual3_;
};

}  // namespace panel
