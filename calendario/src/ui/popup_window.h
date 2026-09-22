#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

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

  bool Create(HINSTANCE instance, HMONITOR monitor, Timing timing);
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

  HWND hwnd_ = nullptr;
  HMONITOR monitor_ = nullptr;
  Timing timing_{};
  UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
  SIZE size_{};
  bool visible_ = false;
  bool acrylic_ = false;

  ComPtr<ID3D11Device> d3d_;
  ComPtr<IDXGISwapChain1> swapChain_;
  ComPtr<ID2D1DeviceContext> dc_;
  ComPtr<IDCompositionDesktopDevice> composition_;
  ComPtr<IDCompositionTarget> target_;
  ComPtr<IDCompositionVisual2> visual_;
  ComPtr<IDCompositionVisual3> visual3_;
};

}  // namespace agenda
