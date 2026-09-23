#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <chrono>
#include <string>
#include <string_view>

#include "model/state.h"
#include "system/audio.h"
#include "system/brightness.h"
#include "system/nightlight.h"
#include "system/radios.h"
#include "system/wifi.h"
#include "system/worker.h"
#include "ui/controls.h"
#include "ui/layout.h"
#include "ui/paint.h"
#include "ui/panel_view.h"
#include "ui/spring.h"
#include "ui/theme.h"
#include "ui/vsync.h"

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
  // Lets go of everything that talks to the system. Before COM goes, so from main.
  void Shutdown();
  bool visible() const { return visible_; }
  HWND hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);

  void ApplyDwmAttributes();
  void Backdrop(bool on);
  bool CreateDevices();
  void Place(const RECT& work);
  void Relayout();
  void Buffer(SIZE want);
  void Render();
  void Rest();
  void Animate(bool opening);
  bool AnimationsEnabled() const;
  float SlidePx() const;
  void Shelve();
  void FollowTheme();
  void FollowMonitor();
  void ShowMenu(POINT screen);

  // Input. Coordinates are DIP from the panel's top left.
  D2D1_POINT_2F ToDip(LPARAM lparam) const;
  void OnMouseMove(float x, float y);
  void OnLeftDown(float x, float y);
  void OnLeftUp(float x, float y);
  void OnWheel(int delta);
  bool OnKey(WPARAM key);
  void Activate(Target target);
  void ToggleCard(bool& goal);
  void OpenModule(int tile);
  void CloseModule();
  int OpenModuleTile() const;
  Expanded Goals() const;
  float SliderLevel(Target target) const;
  void SetSliderLevel(Target target, float level);
  void Nudge(Target target, float by);
  void KeepFocusValid();
  void UpdateHot();
  void ReadAudio();
  void TakeBrightness();
  void TakeRadios();
  void TakeWifi();
  void TakeNight();
  void OpenWindowsNetworks();
  void OpenWindowsAddDevice();
  bool OpenSettingsFor(Target target);
  void BrightnessFromWindows(float level);

  // Animation: everything that moves inside the panel steps on one clock, once per composed
  // frame, and the clock stops when nothing is moving.
  void StartAnimating();
  void OnFrame();
  bool StepInks(float seconds);

  HWND hwnd_ = nullptr;
  HMONITOR monitor_ = nullptr;
  UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
  RECT work_{};
  SIZE size_{};    // the window
  SIZE buffer_{};  // the swap chain: the window's size, or the size the window is growing to
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
  Audio audio_;
  Worker worker_;
  Brightness brightness_;
  Radios radios_;
  Wifi wifi_;
  NightLight night_;
  HPOWERNOTIFY brightnessNotify_ = nullptr;
  // When the panel last wrote the brightness. Windows' notice of each write arrives a moment
  // later, and during a drag an old one would pull the slider back; for a short while after a
  // write, the slider is what counts.
  std::chrono::steady_clock::time_point brightnessWritten_{};

  Target hover_;     // under the mouse
  Target pressed_;   // the button went down on it and has not come up
  Target dragging_;  // the slider that has the mouse captured
  bool tracking_ = false;  // TrackMouseEvent is watching for the mouse to leave
  bool mouseIn_ = false;
  D2D1_POINT_2F mouse_{};  // the last place the mouse was seen, to hit test again after a relayout

  bool brightnessGoal_ = false;
  bool audioGoal_ = false;
  Spring brightnessSpring_;
  Spring audioSpring_;
  // Phase 5b: Wi-Fi and Bluetooth morph into cards; at most one goal is ever true.
  bool wifiGoal_ = false;
  bool bluetoothGoal_ = false;
  Spring wifiSpring_;
  Spring bluetoothSpring_;
  FrameClock clock_;
  bool animating_ = false;
  std::chrono::steady_clock::time_point lastFrame_{};
  // What the last card movement cost, logged when it comes to rest: the acceptance criterion
  // "moves at the monitor's rate" is checked against this line, not by eye alone.
  int cardFrames_ = 0;
  double cardRenderMs_ = 0.0;
  std::chrono::steady_clock::time_point cardStart_{};

  Microsoft::WRL::ComPtr<ID3D11Device> d3d_;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
  Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc_;
  Microsoft::WRL::ComPtr<IDCompositionDesktopDevice> composition_;
  Microsoft::WRL::ComPtr<IDCompositionTarget> target_;
  Microsoft::WRL::ComPtr<IDCompositionVisual2> visual_;
  Microsoft::WRL::ComPtr<IDCompositionVisual3> visual3_;
};

}  // namespace panel
