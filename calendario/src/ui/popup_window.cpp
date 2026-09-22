#include "ui/popup_window.h"

#include <dwmapi.h>

#include <chrono>

#include "core/hr.h"
#include "core/log.h"
#include "ui/layout.h"
#include "ui/popup_view.h"

namespace agenda {
namespace {

constexpr wchar_t kClassName[] = L"AgendaPopup";
constexpr UINT_PTR kHideTimer = 1;

// Ease-out cubic as a single DirectComposition segment. f(u) = 3u - 3u^2 + u^3 with
// u = t / seconds is a cubic polynomial, so AddCubic expresses it exactly and the compositor
// interpolates it on the GPU: no timer, no thread, no dropped frames.
void EaseOutCubic(IDCompositionAnimation* animation, double seconds, float from, float to) {
  const double delta = static_cast<double>(to) - static_cast<double>(from);
  animation->AddCubic(0.0, from, static_cast<float>(3.0 * delta / seconds),
                      static_cast<float>(-3.0 * delta / (seconds * seconds)),
                      static_cast<float>(delta / (seconds * seconds * seconds)));
  animation->End(seconds, to);
}

}  // namespace

bool PopupWindow::Create(HINSTANCE instance, HMONITOR monitor, Timing timing) {
  monitor_ = monitor;
  timing_ = timing;

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = WndProc;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = kClassName;
  if (RegisterClassExW(&windowClass) == 0) {
    LogError(L"popup: RegisterClassExW failed with error {}", GetLastError());
    return false;
  }

  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor_, &info)) {
    LogError(L"popup: GetMonitorInfoW failed with error {}", GetLastError());
    return false;
  }

  const RECT rect = PopupRect(info.rcWork, dpi_);
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
                          kClassName, L"Agenda", WS_POPUP, rect.left, rect.top,
                          rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                          instance, this);
  if (hwnd_ == nullptr) {
    LogError(L"popup: CreateWindowExW failed with error {}", GetLastError());
    return false;
  }

  ApplyDwmAttributes();
  Place(info.rcWork);
  if (!CreateDevices()) return false;

  Rest();
  Render();
  composition_->Commit();
  LogInfo(L"popup: ready, {}x{} at {} dpi", size_.cx, size_.cy, dpi_);
  return true;
}

void PopupWindow::ApplyDwmAttributes() {
  // Dark first, because the acrylic takes its tint from it.
  const BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

  const DWORD corner = DWMWCP_ROUND;
  DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

  const DWORD backdrop = DWMSBT_TRANSIENTWINDOW;
  acrylic_ = SUCCEEDED(
      DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop)));
  if (!acrylic_) {
    // Windows 10 has no system backdrop, so the panel is painted opaque instead.
    LogInfo(L"popup: no system backdrop available, falling back to a solid panel");
  }
}

bool PopupWindow::CreateDevices() {
  const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                 D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
  if (FAILED(hr)) {
    LogInfo(L"popup: no hardware D3D11 device (hr {:#010x}), trying WARP",
            static_cast<unsigned long>(hr));
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                           D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
  }
  if (Failed(hr, L"D3D11CreateDevice")) return false;

  ComPtr<IDXGIDevice> dxgiDevice;
  if (Failed(d3d_.As(&dxgiDevice), L"ID3D11Device as IDXGIDevice")) return false;

  ComPtr<IDXGIAdapter> adapter;
  if (Failed(dxgiDevice->GetAdapter(&adapter), L"IDXGIDevice::GetAdapter")) return false;

  ComPtr<IDXGIFactory2> factory;
  if (Failed(adapter->GetParent(IID_PPV_ARGS(&factory)), L"IDXGIAdapter::GetParent")) {
    return false;
  }

  DXGI_SWAP_CHAIN_DESC1 description{};
  description.Width = static_cast<UINT>(size_.cx);
  description.Height = static_cast<UINT>(size_.cy);
  description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  description.SampleDesc.Count = 1;
  description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  description.BufferCount = 2;
  description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  if (Failed(
          factory->CreateSwapChainForComposition(d3d_.Get(), &description, nullptr, &swapChain_),
          L"CreateSwapChainForComposition")) {
    return false;
  }

  ComPtr<ID2D1Factory1> d2dFactory;
  const D2D1_FACTORY_OPTIONS factoryOptions{};
  if (Failed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factoryOptions,
                               d2dFactory.GetAddressOf()),
             L"D2D1CreateFactory")) {
    return false;
  }

  ComPtr<ID2D1Device> d2dDevice;
  if (Failed(d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice),
             L"ID2D1Factory1::CreateDevice")) {
    return false;
  }
  if (Failed(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_),
             L"ID2D1Device::CreateDeviceContext")) {
    return false;
  }

  // Device2, not the original one: visuals made by the v1 device do not implement
  // IDCompositionVisual3, which is where SetOpacity lives. IDCompositionDesktopDevice is the
  // v2 device that still knows how to target an HWND.
  if (Failed(DCompositionCreateDevice2(dxgiDevice.Get(), IID_PPV_ARGS(&composition_)),
             L"DCompositionCreateDevice2")) {
    return false;
  }
  if (Failed(composition_->CreateTargetForHwnd(hwnd_, TRUE, &target_), L"CreateTargetForHwnd")) {
    return false;
  }
  if (Failed(composition_->CreateVisual(&visual_), L"IDCompositionDesktopDevice::CreateVisual")) {
    return false;
  }
  if (Failed(visual_->SetContent(swapChain_.Get()), L"IDCompositionVisual2::SetContent")) {
    return false;
  }
  if (Failed(target_->SetRoot(visual_.Get()), L"IDCompositionTarget::SetRoot")) return false;

  // SetOpacity lives on IDCompositionVisual3, not on the base visual. Every Windows build this
  // app supports has it; if one day it is missing, the popup still slides, it just does not
  // fade.
  if (FAILED(visual_.As(&visual3_))) {
    LogInfo(L"popup: no IDCompositionVisual3, opening without the fade");
  }
  return true;
}

void PopupWindow::Place(const RECT& work) {
  RECT rect = PopupRect(work, dpi_);
  SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
               rect.bottom - rect.top, SWP_NOACTIVATE);

  // Now that the window sits on the target monitor, its real DPI is known. Doing this while it
  // is still hidden means the corrected size never shows up as a resize on screen.
  const UINT dpi = GetDpiForWindow(hwnd_);
  if (dpi != 0 && dpi != dpi_) {
    dpi_ = dpi;
    rect = PopupRect(work, dpi_);
    SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
                 rect.bottom - rect.top, SWP_NOACTIVATE);
  }
  Resize(SIZE{rect.right - rect.left, rect.bottom - rect.top});
}

void PopupWindow::Resize(SIZE size) {
  if (size.cx == size_.cx && size.cy == size_.cy) return;
  size_ = size;
  if (!swapChain_) return;  // still creating the devices, which will pick up the new size

  dc_->SetTarget(nullptr);
  if (Failed(swapChain_->ResizeBuffers(0, static_cast<UINT>(size.cx), static_cast<UINT>(size.cy),
                                       DXGI_FORMAT_UNKNOWN, 0),
             L"IDXGISwapChain1::ResizeBuffers")) {
    return;
  }
  Render();
}

void PopupWindow::Render() {
  ComPtr<IDXGISurface> surface;
  if (Failed(swapChain_->GetBuffer(0, IID_PPV_ARGS(&surface)), L"IDXGISwapChain1::GetBuffer")) {
    return;
  }

  // Giving the target bitmap the window DPI lets everything below be written in DIPs.
  const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
      static_cast<float>(dpi_), static_cast<float>(dpi_));

  ComPtr<ID2D1Bitmap1> bitmap;
  if (Failed(dc_->CreateBitmapFromDxgiSurface(surface.Get(), properties, &bitmap),
             L"CreateBitmapFromDxgiSurface")) {
    return;
  }

  dc_->SetTarget(bitmap.Get());
  dc_->BeginDraw();
  dc_->Clear(D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.0f});
  DrawPopup(dc_.Get(),
            D2D1_SIZE_F{static_cast<float>(kPopupWidthDip), static_cast<float>(kPopupHeightDip)},
            acrylic_);
  if (Failed(dc_->EndDraw(), L"ID2D1DeviceContext::EndDraw")) return;
  dc_->SetTarget(nullptr);

  Failed(swapChain_->Present(0, 0), L"IDXGISwapChain1::Present");
}

float PopupWindow::SlidePx() const {
  return static_cast<float>(ScaleDip(static_cast<int>(kPopupSlideDip), dpi_));
}

bool PopupWindow::AnimationsEnabled() const {
  BOOL enabled = TRUE;
  SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
  return enabled != FALSE;
}

void PopupWindow::Rest() {
  if (visual3_) visual3_->SetOpacity(0.0f);
  visual_->SetOffsetY(SlidePx());
}

void PopupWindow::Animate(bool opening) {
  const UINT ms = opening ? timing_.openMs : timing_.closeMs;
  const float slide = SlidePx();

  if (ms == 0 || !AnimationsEnabled()) {
    if (visual3_) visual3_->SetOpacity(opening ? 1.0f : 0.0f);
    visual_->SetOffsetY(opening ? 0.0f : slide);
    composition_->Commit();
    return;
  }

  const double seconds = ms / 1000.0;

  ComPtr<IDCompositionAnimation> fade;
  if (visual3_ && SUCCEEDED(composition_->CreateAnimation(&fade))) {
    EaseOutCubic(fade.Get(), seconds, opening ? 0.0f : 1.0f, opening ? 1.0f : 0.0f);
    visual3_->SetOpacity(fade.Get());
  }

  ComPtr<IDCompositionAnimation> rise;
  if (SUCCEEDED(composition_->CreateAnimation(&rise))) {
    EaseOutCubic(rise.Get(), seconds, opening ? slide : 0.0f, opening ? 0.0f : slide);
    visual_->SetOffsetY(rise.Get());
  }

  composition_->Commit();
}

void PopupWindow::Show() {
  if (hwnd_ == nullptr || visible_) return;
  const auto started = std::chrono::steady_clock::now();
  KillTimer(hwnd_, kHideTimer);

  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor_, &info)) {
    LogError(L"popup: GetMonitorInfoW failed with error {}", GetLastError());
    return;
  }
  Place(info.rcWork);

  visible_ = true;
  ShowWindow(hwnd_, SW_SHOW);
  SetForegroundWindow(hwnd_);
  Animate(/*opening=*/true);

  const std::chrono::duration<double, std::milli> elapsed =
      std::chrono::steady_clock::now() - started;
  LogInfo(L"popup: on screen in {:.1f} ms", elapsed.count());
}

void PopupWindow::Hide() {
  if (hwnd_ == nullptr || !visible_) return;
  visible_ = false;
  Animate(/*opening=*/false);

  if (timing_.closeMs == 0 || !AnimationsEnabled()) {
    ShowWindow(hwnd_, SW_HIDE);
    return;
  }
  // DirectComposition has no completion callback, so a timer takes the window off screen once
  // the fade is over. Until then it is still there, just fully transparent.
  SetTimer(hwnd_, kHideTimer, timing_.closeMs, nullptr);
}

void PopupWindow::Toggle() {
  if (visible_) {
    Hide();
  } else {
    Show();
  }
}

LRESULT CALLBACK PopupWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    auto* created = static_cast<PopupWindow*>(create->lpCreateParams);
    created->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
  }

  auto* self = reinterpret_cast<PopupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self == nullptr) return DefWindowProcW(hwnd, message, wparam, lparam);
  return self->Handle(message, wparam, lparam);
}

LRESULT PopupWindow::Handle(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_KEYDOWN:
      if (wparam == VK_ESCAPE) {
        Hide();
        return 0;
      }
      break;

    case WM_ACTIVATE:
      if (LOWORD(wparam) == WA_INACTIVE) {
        Hide();
        return 0;
      }
      break;

    case WM_TIMER:
      if (wparam == kHideTimer) {
        KillTimer(hwnd_, kHideTimer);
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
      }
      break;

    default:
      break;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

}  // namespace agenda
