#include "ui/panel_window.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <chrono>

#include "core/config.h"
#include "core/hr.h"
#include "core/i18n.h"
#include "core/log.h"

using Microsoft::WRL::ComPtr;

namespace panel {
namespace {

constexpr wchar_t kClassName[] = L"PanelDeControl";
constexpr UINT_PTR kHideTimer = 1;
// Agenda's timings: one vocabulary of movement for the two popups in the same corner.
constexpr UINT kOpenMs = 160;
constexpr UINT kCloseMs = 120;

constexpr UINT kMenuOpenConfig = 1;
constexpr UINT kMenuQuit = 2;

// Ease-out cubic as a single DirectComposition segment. f(u) = 3u - 3u^2 + u^3 with
// u = t / seconds is a cubic, so AddCubic expresses it exactly and the compositor runs it on
// the GPU: no timer, no thread, no dropped frames.
void EaseOutCubic(IDCompositionAnimation* animation, double seconds, float from, float to) {
  const double delta = static_cast<double>(to) - static_cast<double>(from);
  animation->AddCubic(0.0, from, static_cast<float>(3.0 * delta / seconds),
                      static_cast<float>(-3.0 * delta / (seconds * seconds)),
                      static_cast<float>(delta / (seconds * seconds * seconds)));
  animation->End(seconds, to);
}

bool WorkArea(HMONITOR monitor, RECT& work) {
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) {
    LogError(L"panel: GetMonitorInfoW failed with error {}", GetLastError());
    return false;
  }
  work = info.rcWork;
  return true;
}

}  // namespace

bool PanelWindow::Create(HINSTANCE instance, HMONITOR monitor, std::wstring_view theme) {
  monitor_ = monitor;
  themeChoice_ = theme;
  theme_ = ResolveTheme(themeChoice_);
  // Phase 1: made-up data. Each later phase replaces one part of it with the real thing.
  state_ = SampleState();
  if (!fonts_.Create()) LogError(L"panel: DirectWrite is unavailable, opening without text");

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = WndProc;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = kClassName;
  if (RegisterClassExW(&windowClass) == 0) {
    LogError(L"panel: RegisterClassExW failed with error {}", GetLastError());
    return false;
  }

  RECT work{};
  if (!WorkArea(TargetMonitor(monitor_), work)) return false;
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
                          kClassName, L"Panel de control", WS_POPUP, work.right - 1,
                          work.bottom - 1, 1, 1, nullptr, nullptr, instance, this);
  if (hwnd_ == nullptr) {
    LogError(L"panel: CreateWindowExW failed with error {}", GetLastError());
    return false;
  }

  ApplyDwmAttributes();
  Place(work);
  if (!CreateDevices()) return false;
  Rest();
  Render();
  composition_->Commit();
  LogInfo(L"panel: ready, {}x{} px at {} dpi", size_.cx, size_.cy, dpi_);
  return true;
}

void PanelWindow::ApplyDwmAttributes() {
  // Dark first, because the acrylic takes its tint from it.
  const BOOL dark = theme_.light ? FALSE : TRUE;
  DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
  const DWORD corner = DWMWCP_ROUND;
  DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
  Backdrop(true);
  if (!backdrop_) LogInfo(L"panel: no system backdrop, falling back to a solid panel");
}

void PanelWindow::Backdrop(bool on) {
  // The material and the border are DWM's, drawn outside the visual that fades: left on while
  // closing, they stayed whole for the whole fade, a grey ghost with the content dying inside
  // it (Agenda, commit f16d4e1). So closing takes them off and paints the panel opaque.
  const DWORD type = on ? DWMSBT_TRANSIENTWINDOW : DWMSBT_NONE;
  const HRESULT set = DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
  if (on) backdrop_ = SUCCEEDED(set);
  const COLORREF border = on ? DWMWA_COLOR_DEFAULT : DWMWA_COLOR_NONE;
  DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
  acrylic_ = on && backdrop_;
}

bool PanelWindow::CreateDevices() {
  const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                 D3D11_SDK_VERSION, &d3d_, nullptr, nullptr);
  if (FAILED(hr)) {
    LogInfo(L"panel: no hardware D3D11 device (hr {:#010x}), trying WARP",
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
  if (Failed(adapter->GetParent(IID_PPV_ARGS(&factory)), L"IDXGIAdapter::GetParent")) return false;

  DXGI_SWAP_CHAIN_DESC1 description{};
  description.Width = static_cast<UINT>(size_.cx);
  description.Height = static_cast<UINT>(size_.cy);
  description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  description.SampleDesc.Count = 1;
  description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  description.BufferCount = 2;
  description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  if (Failed(factory->CreateSwapChainForComposition(d3d_.Get(), &description, nullptr, &swapChain_),
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
  if (Failed(d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice), L"ID2D1Factory1::CreateDevice") ||
      Failed(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_),
             L"ID2D1Device::CreateDeviceContext")) {
    return false;
  }

  // Device2, not the original one: visuals made by the v1 device do not implement
  // IDCompositionVisual3, which is where SetOpacity lives.
  if (Failed(DCompositionCreateDevice2(dxgiDevice.Get(), IID_PPV_ARGS(&composition_)),
             L"DCompositionCreateDevice2") ||
      Failed(composition_->CreateTargetForHwnd(hwnd_, TRUE, &target_), L"CreateTargetForHwnd") ||
      Failed(composition_->CreateVisual(&visual_), L"IDCompositionDesktopDevice::CreateVisual") ||
      Failed(visual_->SetContent(swapChain_.Get()), L"IDCompositionVisual2::SetContent") ||
      Failed(target_->SetRoot(visual_.Get()), L"IDCompositionTarget::SetRoot")) {
    return false;
  }
  if (FAILED(visual_.As(&visual3_))) LogInfo(L"panel: no IDCompositionVisual3, opening without the fade");
  return true;
}

void PanelWindow::Place(const RECT& work) {
  placing_ = true;
  layout_ = MakeLayout(view_.open, state_);

  RECT rect = PlaceRect(work, layout_.size(), dpi_);
  SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
               rect.bottom - rect.top, SWP_NOACTIVATE);
  // Now that the window sits on the target monitor its real DPI is known. Fixing it while the
  // window is still hidden means the corrected size never shows as a resize.
  const UINT dpi = MonitorDpi(hwnd_);
  if (dpi != 0 && dpi != dpi_) {
    dpi_ = dpi;
    rect = PlaceRect(work, layout_.size(), dpi_);
    SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
                 rect.bottom - rect.top, SWP_NOACTIVATE);
  }
  Resize(SIZE{rect.right - rect.left, rect.bottom - rect.top});
  placing_ = false;
}

void PanelWindow::Resize(SIZE size) {
  if (size.cx == size_.cx && size.cy == size_.cy) return;
  size_ = size;
  if (!swapChain_) return;  // still creating the devices, which will pick up the new size
  dc_->SetTarget(nullptr);
  Failed(swapChain_->ResizeBuffers(0, static_cast<UINT>(size_.cx), static_cast<UINT>(size_.cy),
                                   DXGI_FORMAT_UNKNOWN, 0),
         L"IDXGISwapChain1::ResizeBuffers");
}

void PanelWindow::Render() {
  ComPtr<IDXGISurface> surface;
  if (Failed(swapChain_->GetBuffer(0, IID_PPV_ARGS(&surface)), L"IDXGISwapChain1::GetBuffer")) {
    return;
  }
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
  // The device context keeps its own DPI and starts at 96 whatever the bitmap says. Without
  // this a scaled monitor gets the panel one DIP to one pixel, filling only a corner (Agenda).
  dc_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
  dc_->BeginDraw();
  dc_->Clear(D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.0f});
  DrawPanel(dc_.Get(), fonts_, theme_, layout_, state_, view_, acrylic_);
  const HRESULT drawn = dc_->EndDraw();
  dc_->SetTarget(nullptr);
  if (Failed(drawn, L"ID2D1DeviceContext::EndDraw")) return;
  Failed(swapChain_->Present(0, 0), L"IDXGISwapChain1::Present");
}

float PanelWindow::SlidePx() const { return static_cast<float>(ScaleDip(kSlideDip, dpi_)); }

bool PanelWindow::AnimationsEnabled() const {
  BOOL enabled = TRUE;
  SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
  return enabled != FALSE;
}

void PanelWindow::Rest() {
  if (visual3_) visual3_->SetOpacity(0.0f);
  visual_->SetOffsetY(SlidePx());
}

void PanelWindow::Animate(bool opening) {
  const float slide = SlidePx();
  if (!AnimationsEnabled()) {
    // "Show animations in Windows" is off: no movement, only the state it lands in.
    if (visual3_) visual3_->SetOpacity(opening ? 1.0f : 0.0f);
    visual_->SetOffsetY(0.0f);
    composition_->Commit();
    return;
  }
  const double seconds = (opening ? kOpenMs : kCloseMs) / 1000.0;
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

void PanelWindow::Show() {
  if (hwnd_ == nullptr || visible_) return;
  const auto started = std::chrono::steady_clock::now();
  KillTimer(hwnd_, kHideTimer);

  RECT work{};
  if (!WorkArea(TargetMonitor(monitor_), work)) return;
  // It always opens with the cards closed: what it shows first is the same every time.
  view_ = ViewState{};
  const Theme theme = ResolveTheme(themeChoice_);
  const bool flipped = theme.light != theme_.light;
  theme_ = theme;
  if (flipped) ApplyDwmAttributes();
  Place(work);
  // The material back on before the first frame: it came off when this last closed.
  Backdrop(true);
  Render();

  visible_ = true;
  ShowWindow(hwnd_, SW_SHOW);
  SetForegroundWindow(hwnd_);
  Animate(/*opening=*/true);

  const std::chrono::duration<double, std::milli> elapsed =
      std::chrono::steady_clock::now() - started;
  LogInfo(L"panel: on screen in {:.1f} ms", elapsed.count());
}

void PanelWindow::Hide() {
  if (hwnd_ == nullptr || !visible_) return;
  visible_ = false;
  Backdrop(false);
  Render();
  Animate(/*opening=*/false);
  if (!AnimationsEnabled()) {
    ShowWindow(hwnd_, SW_HIDE);
    Shelve();
    return;
  }
  // DirectComposition has no completion callback, so a timer takes the window off screen once
  // the fade is over. Until then it is there, fully transparent.
  SetTimer(hwnd_, kHideTimer, kCloseMs, nullptr);
}

void PanelWindow::SetNotice(std::wstring notice) {
  state_.notice = std::move(notice);
  if (visible_) FollowMonitor();
}

void PanelWindow::Toggle() {
  if (visible_) {
    Hide();
  } else {
    Show();
  }
}

void PanelWindow::Shelve() {
  // Back to rest for the next opening, and the pages back to Windows until they are wanted:
  // most are the graphics driver's, shared with every other program drawing with Direct3D, and
  // they come back from the standby list in the few milliseconds the next opening can spare.
  Rest();
  composition_->Commit();
  SetProcessWorkingSetSizeEx(GetCurrentProcess(), static_cast<SIZE_T>(-1),
                             static_cast<SIZE_T>(-1), 0);
}

void PanelWindow::FollowTheme() {
  const Theme theme = ResolveTheme(themeChoice_);
  const bool flipped = theme.light != theme_.light;
  theme_ = theme;
  if (flipped) ApplyDwmAttributes();
  if (visible_) Render();
}

void PanelWindow::FollowMonitor() {
  // A scale change on the panel's own monitor arrives as a setting or display change and not
  // as WM_DPICHANGED (Agenda, phase 7), so open or not, it is laid out again where it is.
  if (!visible_ || placing_) return;
  RECT work{};
  if (!WorkArea(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), work)) return;
  dpi_ = MonitorDpi(hwnd_);
  Place(work);
  Render();
}

void PanelWindow::ShowMenu(POINT screen) {
  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) return;
  AppendMenuW(menu, MF_STRING, kMenuOpenConfig, T(L"Abrir panel.json", L"Open panel.json").data());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuQuit, T(L"Salir", L"Quit").data());
  const UINT chosen = static_cast<UINT>(TrackPopupMenu(
      menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, screen.x, screen.y, 0, hwnd_, nullptr));
  DestroyMenu(menu);

  if (chosen == kMenuOpenConfig) {
    Hide();
    EnsureConfigFile();
    // SEGURIDAD.md 1.6: ShellExecute only ever opens this file or an ms-settings: page.
    const HINSTANCE opened = ShellExecuteW(nullptr, L"open", ConfigPath().c_str(), nullptr,
                                           nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(opened) <= 32) {
      LogError(L"panel: could not open panel.json (error {})", reinterpret_cast<INT_PTR>(opened));
    }
  } else if (chosen == kMenuQuit) {
    PostQuitMessage(0);
  }
}

LRESULT CALLBACK PanelWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    auto* created = static_cast<PanelWindow*>(create->lpCreateParams);
    created->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
  }
  auto* self = reinterpret_cast<PanelWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self == nullptr) return DefWindowProcW(hwnd, message, wparam, lparam);
  return self->Handle(message, wparam, lparam);
}

LRESULT PanelWindow::Handle(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_ACTIVATE:
      // It goes when something else is clicked, like Windows' own quick settings.
      if (LOWORD(wparam) == WA_INACTIVE) {
        Hide();
        return 0;
      }
      break;

    case WM_KEYDOWN:
      if (wparam == VK_ESCAPE) {
        Hide();
        return 0;
      }
      break;

    case WM_CLOSE:
      // Alt+F4 hides, like the hotkey. Destroying the window would leave the hotkey with
      // nothing to show until Panel is restarted (Agenda, commit f913d9b).
      Hide();
      return 0;

    case WM_CONTEXTMENU: {
      POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      if (screen.x == -1 && screen.y == -1) {
        // From the keyboard (Shift+F10, the menu key): under the panel's top left corner.
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        screen = POINT{rect.left + ScaleDip(kPadDip, dpi_), rect.top + ScaleDip(kPadDip, dpi_)};
      }
      ShowMenu(screen);
      return 0;
    }

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
      FollowMonitor();
      FollowTheme();
      break;

    case WM_DISPLAYCHANGE:
      FollowMonitor();
      break;

    case WM_DPICHANGED:
      if (!placing_ && visible_) {
        dpi_ = HIWORD(wparam);
        FollowMonitor();
      }
      return 0;

    case WM_TIMER:
      if (wparam == kHideTimer) {
        KillTimer(hwnd_, kHideTimer);
        ShowWindow(hwnd_, SW_HIDE);
        Shelve();
        return 0;
      }
      break;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

}  // namespace panel
