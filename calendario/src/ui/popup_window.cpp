#include "ui/popup_window.h"

#include "sync/google.h"

#include <dwmapi.h>
#include <imm.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>
#include <vector>

#include "core/dates.h"
#include "core/hr.h"
#include "core/log.h"
#include "ui/components.h"
#include "ui/layout.h"
#include "ui/popup_view.h"

namespace agenda {
namespace {

constexpr wchar_t kClassName[] = L"AgendaPopup";
constexpr UINT_PTR kHideTimer = 1;
constexpr UINT_PTR kAnimTimer = 2;
constexpr UINT_PTR kCaretTimer = 3;
constexpr UINT_PTR kToastTimer = 4;
constexpr UINT kTickMs = 16;  // one frame at 60 Hz, which is all the content fades need

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

bool PopupWindow::Create(HINSTANCE instance, HMONITOR monitor, Timing timing,
                         std::wstring_view themeOverride) {
  monitor_ = monitor;
  timing_ = timing;
  themeOverride_ = themeOverride;
  theme_ = ResolveTheme(ThemeChoice());
  model_ = MakeModel(TodayLocal());
  model_.focus = 1.0f;

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
  a11y_.Attach(hwnd_, this);

  Rest();
  Render();
  composition_->Commit();
  LogInfo(L"popup: ready, {}x{} px at {} dpi, panel {}x{} dip with {} cards", size_.cx,
          size_.cy, dpi_, static_cast<int>(layout_.width), static_cast<int>(layout_.height),
          layout_.visibleCards);
  return true;
}

void PopupWindow::ApplyDwmAttributes() {
  // Dark first, because the acrylic takes its tint from it.
  const BOOL dark = theme_.light ? FALSE : TRUE;
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
  description.Width = static_cast<UINT>(buffer_.cx);
  description.Height = static_cast<UINT>(buffer_.cy);
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
  placing_ = true;
  const auto panelFor = [&](UINT dpi) {
    return panelOverride_.width > 0.0f ? panelOverride_ : PanelSize(work, dpi);
  };

  D2D1_SIZE_F panel = panelFor(dpi_);
  RECT rect = PlaceRect(work, panel, dpi_);
  appRect_ = ExpandedRect(work);
  SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
               rect.bottom - rect.top, SWP_NOACTIVATE);

  // Now that the window sits on the target monitor, its real DPI is known. Doing this while it
  // is still hidden means the corrected size never shows up as a resize on screen.
  const UINT dpi = MonitorDpi(hwnd_);
  if (dpi != 0 && dpi != dpi_) {
    dpi_ = dpi;
    panel = panelFor(dpi_);
    rect = PlaceRect(work, panel, dpi_);
    SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
                 rect.bottom - rect.top, SWP_NOACTIVATE);
  }

  // The panel takes a share of this monitor, so its measurements and the text sizes that go
  // with them are worked out here, once, and everything below just reads them.
  layout_ = MakeLayout(panel);
  if (fonts_.scale != layout_.type && !fonts_.Create(layout_)) {
    LogError(L"popup: DirectWrite is unavailable, the panel will open without text");
  }

  popupRect_ = rect;
  Resize(SIZE{rect.right - rect.left, rect.bottom - rect.top});
  placing_ = false;
}

bool PopupWindow::IsCaption(float x, float y) const {
  if (mode_ != Mode::App || x < appLayout_.sidebarRight || y >= appLayout_.main.top) return false;
  if (Inside(appLayout_.prev, x, y) || Inside(appLayout_.next, x, y) ||
      Inside(appLayout_.input, x, y) || Inside(appLayout_.collapse, x, y)) {
    return false;
  }
  for (const D2D1_RECT_F& tab : appLayout_.tabs) {
    if (Inside(tab, x, y)) return false;
  }
  return true;
}

void PopupWindow::OnDpiChanged(UINT dpi, const RECT& suggested) {
  if (placing_ || dpi == 0) return;
  if (mode_ == Mode::Morphing) {
    // Halfway through growing is no moment to change scale: it lands where it was going first.
    spring_.Snap(goal_);
    ApplyMorph();
    FinishMorph();
  }
  dpi_ = dpi;
  if (mode_ == Mode::App) {
    appRect_ = suggested;
    SetWindowPos(hwnd_, nullptr, suggested.left, suggested.top, suggested.right - suggested.left,
                 suggested.bottom - suggested.top, SWP_NOZORDER | SWP_NOACTIVATE);
    Resize(SIZE{suggested.right - suggested.left, suggested.bottom - suggested.top});
    Relayout();
    // And the corner it folds back into, measured at the new DPI: without this the popup would
    // come back at the old size in pixels with the new size drawn inside it, cut off.
    AdoptPosition();
  } else if (visible_) {
    // The popup's own monitor changed scale under it: laid out again, where it already is.
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info)) {
      Place(info.rcWork);
    }
  }
  LogInfo(L"popup: now at {} dpi, {}x{} px", dpi_, size_.cx, size_.cy);
  Invalidate();
}

void PopupWindow::FollowMonitorDpi() {
  if (!visible_ || placing_ || hwnd_ == nullptr) return;
  const UINT dpi = MonitorDpi(hwnd_);
  if (dpi == dpi_) return;
  // What Windows would have suggested: the same size in DIP, around the same centre, kept
  // inside the work area.
  RECT now{};
  GetWindowRect(hwnd_, &now);
  const float ratio = static_cast<float>(dpi) / static_cast<float>(dpi_);
  const LONG width = std::lround(static_cast<float>(now.right - now.left) * ratio);
  const LONG height = std::lround(static_cast<float>(now.bottom - now.top) * ratio);
  const LONG cx = (now.left + now.right) / 2;
  const LONG cy = (now.top + now.bottom) / 2;
  RECT suggested{cx - width / 2, cy - height / 2, cx - width / 2 + width, cy - height / 2 + height};
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info)) {
    const RECT& work = info.rcWork;
    const LONG dx = (std::max)(work.left - suggested.left, (std::min)(0L, work.right - suggested.right));
    const LONG dy = (std::max)(work.top - suggested.top, (std::min)(0L, work.bottom - suggested.bottom));
    OffsetRect(&suggested, dx, dy);
  }
  LogInfo(L"popup: the monitor went from {} to {} dpi under the window", dpi_, dpi);
  OnDpiChanged(dpi, suggested);
}

void PopupWindow::AdoptPosition() {
  if (mode_ != Mode::App) return;
  GetWindowRect(hwnd_, &appRect_);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info)) return;
  popupRect_ = PlaceRect(info.rcWork, layout_.size(), dpi_);
}

void PopupWindow::Resize(SIZE size) {
  size_ = size;
  // As the popup the buffer is the popup's size: that is what sits in memory all day in the
  // tray. As soon as the expansion starts it becomes the size of the app, and a little over --
  // the spring overshoots by well under one percent -- so growing the window every frame is a
  // SetWindowPos and never a ResizeBuffers, which is what would make it flicker. The one
  // ResizeBuffers happens before the first frame of the spring, while the content is still the
  // popup's, so it cannot be seen.
  const auto grown = [](LONG side) { return side + side / 50 + 1; };
  const bool app = mode_ != Mode::Popup;
  const SIZE want{(std::max)(size.cx, app ? grown(appRect_.right - appRect_.left) : 0),
                  (std::max)(size.cy, app ? grown(appRect_.bottom - appRect_.top) : 0)};
  if (want.cx == buffer_.cx && want.cy == buffer_.cy) return;
  buffer_ = want;
  if (!swapChain_) return;  // still creating the devices, which will pick up the new size

  dc_->SetTarget(nullptr);
  if (Failed(swapChain_->ResizeBuffers(0, static_cast<UINT>(buffer_.cx),
                                       static_cast<UINT>(buffer_.cy), DXGI_FORMAT_UNKNOWN, 0),
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
  // The device context keeps its own DPI and starts at 96, whatever the target bitmap says. On
  // a scaled monitor that drew the panel one DIP to one pixel, so it covered only a corner of
  // the window and the rest showed through as bare backdrop.
  dc_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
  dc_->BeginDraw();
  dc_->Clear(D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.0f});
  if (InApp()) {
    DrawApp(dc_.Get(), fonts_, theme_, layout_, appLayout_, model_, app_, spring_.x, acrylic_);
  } else {
    DrawPopup(dc_.Get(), fonts_, theme_, layout_, model_, acrylic_);
  }
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
  if (hwnd_ == nullptr) return;
  if (visible_) {
    // The app is a window like any other and can end up behind one; the tray brings it back.
    if (InApp()) SetForegroundWindow(hwnd_);
    return;
  }
  const auto started = std::chrono::steady_clock::now();
  KillTimer(hwnd_, kHideTimer);

  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor_, &info)) {
    LogError(L"popup: GetMonitorInfoW failed with error {}", GetLastError());
    return;
  }
  // Whatever it was when it was hidden, it opens as the popup.
  mode_ = Mode::Popup;
  clock_.Pause();
  spring_.Snap(0.0f);
  goal_ = 0.0f;
  KillTimer(hwnd_, kNowTimer);
  Place(info.rcWork);

  // The popup always opens on today with an empty line waiting, and picks up a theme the user
  // may have flipped while it was hidden.
  const Theme theme = ResolveTheme(ThemeChoice());
  const bool flipped = theme.light != theme_.light;
  theme_ = theme;
  if (flipped) ApplyDwmAttributes();

  model_ = MakeModel(TodayLocal());
  model_.focus = 1.0f;
  // Opening closes the undo window: the notice lives inside the panel, so once the panel is
  // gone there is nothing left offering to take anything back.
  undo_.reset();
  toastOn_ = false;
  strikeOn_ = false;
  KillTimer(hwnd_, kToastTimer);
  Reload();
  // Opening the popup is the best moment to ask Google: somebody is about to read the day. A
  // minute of grace, because opening and closing it three times in a row is not three reasons
  // to sync. It does not block -- the panel is on screen either way in under 100 ms.
  if (sync_ != nullptr) sync_->Nudge(std::chrono::seconds(60));
  hoverDay_ = -1;
  hoverPrev_ = false;
  hoverNext_ = false;
  inputFocused_ = true;
  zone_ = Zone::Grid;
  listFocus_ = 0;
  focusVisible_ = false;
  RestartCaret();
  Render();

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
  clock_.Pause();
  CancelDrag();
  FlushDeletes();
  KillTimer(hwnd_, kAnimTimer);
  KillTimer(hwnd_, kCaretTimer);
  KillTimer(hwnd_, kNowTimer);
  ticking_ = false;
  Animate(/*opening=*/false);

  if (timing_.closeMs == 0 || !AnimationsEnabled()) {
    ShowWindow(hwnd_, SW_HIDE);
    Shelve();
    return;
  }
  // DirectComposition has no completion callback, so a timer takes the window off screen once
  // the fade is over. Until then it is still there, just fully transparent.
  SetTimer(hwnd_, kHideTimer, timing_.closeMs, nullptr);
}

void PopupWindow::ShowDay(Date day) {
  Show();
  if (hwnd_ != nullptr && visible_) SelectDay(day);
}

void PopupWindow::PreferencesChanged() {
  const Theme theme = ResolveTheme(ThemeChoice());
  const bool flipped = theme.light != theme_.light;
  theme_ = theme;
  if (flipped) ApplyDwmAttributes();
  // The preview is worded in the language of the moment and lasts as long as the setting says.
  parsed_.assign(1, wchar_t{1});
  if (InApp()) ReloadApp();
  Invalidate();
}

void PopupWindow::Shelve() {
  // Whatever it was, it opens next time as the popup (Show says so), so the buffer can go back
  // to the popup's size now instead of holding an app's worth of pixels while nobody looks.
  mode_ = Mode::Popup;
  Resize(SIZE{popupRect_.right - popupRect_.left, popupRect_.bottom - popupRect_.top});
  // And the pages go back to Windows until they are wanted: most of them are the graphics
  // driver's code, shared with every other program drawing with Direct3D, and they come back
  // from the standby list in the few milliseconds the next opening can spare.
  SetProcessWorkingSetSizeEx(GetCurrentProcess(), static_cast<SIZE_T>(-1),
                             static_cast<SIZE_T>(-1), 0);
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
    case WM_MOUSEMOVE: {
      const D2D1_POINT_2F point = ToDip(lparam);
      if (drag_.kind != DragKind::None) {
        UpdateDrag(point.x, point.y);
        return 0;
      }
      OnMouseMove(point.x, point.y);
      return 0;
    }

    case WM_LBUTTONUP:
      if (drag_.kind != DragKind::None) {
        const D2D1_POINT_2F point = ToDip(lparam);
        UpdateDrag(point.x, point.y);
        EndDrag();
        return 0;
      }
      break;

    case WM_CAPTURECHANGED:
      // Somebody else took the mouse -- an alt-tab, a menu -- and the drag is over, unapplied.
      if (drag_.kind != DragKind::None && reinterpret_cast<HWND>(lparam) != hwnd_) CancelDrag();
      break;

    case WM_MOUSELEAVE:
      tracking_ = false;
      hoverDay_ = -1;
      hoverPrev_ = false;
      hoverNext_ = false;
      hoverTab_ = -1;
      hoverCalendar_ = -1;
      hoverCollapse_ = false;
      hoverPeriodPrev_ = false;
      hoverPeriodNext_ = false;
      StartTicking();
      return 0;

    case WM_LBUTTONDOWN: {
      // The mouse is in charge again: the keyboard's ring goes until a key brings it back.
      focusVisible_ = false;
      const D2D1_POINT_2F point = ToDip(lparam);
      OnLeftDown(point.x, point.y);
      return 0;
    }

    case WM_GETOBJECT: {
      LRESULT result = 0;
      if (a11y_.OnGetObject(wparam, lparam, result)) return result;
      break;
    }

    case WM_SYSKEYDOWN:
      // Alt with an arrow moves the selected event in the app. Windows sends those as system
      // keys; everything else with Alt is left to it.
      if (InApp() && (wparam == VK_UP || wparam == VK_DOWN || wparam == VK_LEFT ||
                      wparam == VK_RIGHT)) {
        if (OnKeyDown(wparam)) return 0;
      }
      break;

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
      // High contrast switched on or off, or the app theme changed, while this was open. A
      // change of scale arrives here too, and not as a WM_DPICHANGED.
      FollowMonitorDpi();
      PreferencesChanged();
      break;

    case WM_DISPLAYCHANGE:
      FollowMonitorDpi();
      break;

    case WM_DESTROY:
      a11y_.Detach();
      break;

    case WM_SETCURSOR: {
      POINT cursor{};
      if (GetCursorPos(&cursor) && ScreenToClient(hwnd_, &cursor)) {
        const float scale =
            static_cast<float>(USER_DEFAULT_SCREEN_DPI) / static_cast<float>(dpi_);
        const float x = static_cast<float>(cursor.x) * scale;
        const float y = static_cast<float>(cursor.y) * scale;
        LPCWSTR shape = Inside(ActiveLayout().input(), x, y) ? IDC_IBEAM : IDC_ARROW;
        if (InApp()) DetailCursor(x, y, shape);
        SetCursor(LoadCursorW(nullptr, shape));
        return TRUE;
      }
      break;
    }

    case WM_KEYDOWN:
      if (OnKeyDown(wparam)) return 0;
      break;

    case WM_CHAR: {
      // Enter, Tab and Backspace arrive here too. The model would drop them anyway, but there
      // is no reason to let them look like something was typed.
      const wchar_t typed = static_cast<wchar_t>(wparam);
      if (typed < 0x20 || typed == 0x7F) return 0;
      // The Space that ticked a task or opened a day already did its job.
      if (typed == L' ' && eatSpace_) {
        eatSpace_ = false;
        return 0;
      }
      // In the app a letter is a shortcut until Ctrl+K or a click puts the capsule -- or a field
      // of the detail panel -- in charge.
      if (InApp() && !inputFocused_) {
        if (app_.detail.focus < 0) return 0;
        FocusedText().Insert(std::wstring_view(&typed, 1));
        RestartCaret();
        Invalidate();
        return 0;
      }
      FocusInput(true);
      model_.input.Insert(std::wstring_view(&typed, 1));
      RestartCaret();
      Invalidate();
      return 0;
    }

    case WM_IME_STARTCOMPOSITION:
      // Returning zero keeps the IME from drawing its own composition window over the panel:
      // the in-flight text is drawn underlined inside the capsule instead.
      model_.composition.clear();
      if (app_.detail.focus < 0) FocusInput(true);
      PlaceCandidateWindow();
      return 0;

    case WM_IME_COMPOSITION:
      OnImeComposition(lparam);
      PlaceCandidateWindow();
      return 0;

    case WM_IME_ENDCOMPOSITION:
      model_.composition.clear();
      Invalidate();
      break;

    case kStoreChangedMessage: {
      // The worker finished something. It sends no payload -- a pointer allocated on that
      // thread would have to be freed on this one -- so the answer is always to reread.
      if (store_ != nullptr) {
        for (const Store::Failure& failure : store_->TakeFailures()) {
          // Optimism undone: whatever was put on screen before the write was not written.
          LogError(L"popup: {}", failure.message);
          if (undo_ && undo_->uid == failure.uid) undo_.reset();
          ShowToast(failure.message);
        }
        Reload();
        Invalidate();
      }
      return 0;
    }

    case WM_NCHITTEST: {
      if (mode_ != Mode::App) break;
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(hwnd_, &point);
      const float scale = static_cast<float>(USER_DEFAULT_SCREEN_DPI) / static_cast<float>(dpi_);
      if (IsCaption(static_cast<float>(point.x) * scale, static_cast<float>(point.y) * scale)) {
        return HTCAPTION;
      }
      break;
    }

    case WM_NCLBUTTONDBLCLK:
      // A title bar maximises on a double click; this one is a fixed share of the work area.
      if (wparam == HTCAPTION) return 0;
      break;

    case WM_EXITSIZEMOVE:
      AdoptPosition();
      break;

    case WM_DPICHANGED:
      LogInfo(L"popup: WM_DPICHANGED {} dpi (mode {}, {})", HIWORD(wparam),
              static_cast<int>(mode_), placing_ ? L"placing" : (visible_ ? L"visible" : L"hidden"));
      OnDpiChanged(HIWORD(wparam), *reinterpret_cast<const RECT*>(lparam));
      return 0;

    case WM_ACTIVATE:
      // The popup goes when something else is clicked; the app stays, like any window does.
      if (LOWORD(wparam) == WA_INACTIVE && !InApp()) {
        Hide();
        return 0;
      }
      break;

    case WM_MOUSEWHEEL:
      if (InApp()) {
        OnWheel(GET_WHEEL_DELTA_WPARAM(wparam));
        return 0;
      }
      break;

    case kFrameMessage:
      StepMorph();
      return 0;

    case WM_TIMER:
      if (wparam == kHideTimer) {
        KillTimer(hwnd_, kHideTimer);
        ShowWindow(hwnd_, SW_HIDE);
        Shelve();
        return 0;
      }
      if (wparam == kAnimTimer) {
        const ULONGLONG now = GetTickCount64();
        const float elapsed = static_cast<float>(now - lastTick_);
        lastTick_ = now;
        const bool moving = Tick(elapsed);
        Invalidate();
        if (!moving) {
          KillTimer(hwnd_, kAnimTimer);
          ticking_ = false;
        }
        return 0;
      }
      if (wparam == kToastTimer) {
        KillTimer(hwnd_, kToastTimer);
        HideToast();
        return 0;
      }
      if (wparam == kNowTimer) {
        ScheduleNowTick();
        Invalidate();
        return 0;
      }
      if (wparam == kCaretTimer) {
        caretVisible_ = !caretVisible_;
        model_.caretOn = inputFocused_ && caretVisible_;
        app_.detail.caretOn = app_.detail.focus >= 0 && caretVisible_;
        Invalidate();
        return 0;
      }
      break;

    default:
      break;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void PopupWindow::Invalidate() {
  // Every edit funnels through here, and nothing else does: typing, deleting, pasting and the
  // IME all end in an Invalidate, while the sixteen millisecond animation tick does not change
  // the text. So this one guard is the whole "reparse when it changed" rule.
  if (model_.input.text() != parsed_) {
    parsed_ = model_.input.text();
    model_.preview = nlp::ParseInput(parsed_, nlp::Now{TodayLocal(), NowMinuteLocal()},
                                     DefaultMinutes());
  }
  UpdateRing();
  if (swapChain_) Render();
  a11y_.Changed();
}

void PopupWindow::Reload() {
  // The dot is read here and not watched for, because Reload is what both doors lead to: the
  // popup opening, and the worker saying something changed.
  model_.offline = sync_ != nullptr && sync_->Offline();
  if (store_ != nullptr && store_->IsOpen()) {
    // Today's list is also where the tasks with no date at all land, so nothing that was
    // created is left with nowhere to be seen.
    model_.day = store_->ItemsForDay(model_.selected, model_.selected == model_.today);
    DropPending(model_.day);

    // Mid-slide two months are on screen, so the dots have to cover both grids.
    Month from = model_.month;
    Month to = model_.month;
    if (model_.slideDir != 0) {
      from = (std::min)(from, model_.slideFrom);
      to = (std::max)(to, model_.slideFrom);
    }
    model_.dots = store_->DotsForRange(GridStart(from), AddDays(GridStart(to), kGridCells - 1));
  }
  if (InApp()) ReloadApp();
}

bool PopupWindow::CreateFromInput() {
  if (store_ == nullptr || !store_->IsOpen()) return false;
  const nlp::ParsedInput& understood = model_.preview;
  // A line with no title is a date and nothing else. There is nothing to create and nothing to
  // say about it: Enter simply does nothing, the way it did before this phase.
  if (understood.title.empty()) return false;

  Draft draft;
  draft.isTask = understood.kind == nlp::Kind::Task;
  // The same capital the preview card put on it: what gets created has to be what the preview
  // promised, and "Dentista" in the card above turning into "dentista" in the list is exactly
  // the sort of small lie that makes a preview stop being worth reading.
  draft.title = nlp::Capitalised(understood.title);
  if (understood.recurrence) draft.recurrence = *understood.recurrence;
  if (understood.start) {
    draft.day = understood.start->date;
    if (understood.start->minuteOfDay != nlp::kNoTime) draft.startMin = understood.start->minuteOfDay;
  }
  if (understood.end) {
    draft.endDay = understood.end->date;
    if (understood.end->minuteOfDay != nlp::kNoTime) draft.endMin = understood.end->minuteOfDay;
  }

  const std::wstring typed = model_.input.text();
  const DayItem created = store_->Create(draft);
  // Straight up, and only the queue -- not a whole pass. Somebody who just wrote something down
  // wants it on their phone, not a download of the year.
  if (sync_ != nullptr) sync_->Push();

  // Jump to the day it landed on first: SelectDay rereads the list, and doing it afterwards
  // would wipe the card that has not been written yet. A task with no date lands on today,
  // so that is where the popup goes -- otherwise the notice says "Creado" over a day where
  // nothing appeared, which is the one thing this was all meant to avoid.
  const Date landed = draft.day ? *draft.day : model_.today;
  if (landed != model_.selected) SelectDay(landed);

  {
    const auto where = std::lower_bound(model_.day.begin(), model_.day.end(), created,
                                        EarlierThan);
    model_.day.insert(where, created);
  }
  if (draft.day) {
    bool already = false;
    for (const DayDot& dot : model_.dots) already = already || dot.date == *draft.day;
    if (!already) model_.dots.push_back(DayDot{*draft.day, created.color});
  }

  if (InApp()) AddToApp(created, draft.day);

  model_.enterUid = created.uid;
  model_.enterT = 0.0f;
  FlushDeletes();
  undo_ = Undone{created.uid, created.isTask, typed};
  ShowToast(std::wstring(T(L"Creado · Deshacer", L"Created · Undo")));

  model_.input.Clear();
  RestartCaret();
  StartTicking();
  Invalidate();
  return true;
}

void PopupWindow::Undo() {
  if (!undo_ || store_ == nullptr) return;
  const Undone taken = *undo_;
  if (taken.kind != UndoKind::Created) {
    // A deletion was never done, only hidden: showing it again is the whole undo. A task made
    // into an event gets its task back, and the event it became goes.
    std::erase_if(pendingDelete_, [&taken](const Pending& pending) {
      return pending.uid == taken.uid || pending.uid == taken.other;
    });
    if (taken.kind == UndoKind::Converted) {
      store_->Remove(taken.uid, /*isTask=*/false);
      if (sync_ != nullptr) sync_->Push();
    }
    KillTimer(hwnd_, kToastTimer);
    HideToast();
    Reload();
    Invalidate();
    return;
  }
  store_->Remove(taken.uid, taken.isTask);
  if (sync_ != nullptr) sync_->Push();

  std::erase_if(model_.day, [&taken](const DayItem& item) { return item.uid == taken.uid; });
  if (model_.enterUid == taken.uid) {
    model_.enterUid.clear();
    model_.enterT = 1.0f;
  }
  // The dot is left for the worker to correct: its message arrives in under a millisecond, and
  // guessing here would mean a second idea of which days have something on them.

  KillTimer(hwnd_, kToastTimer);
  HideToast();

  // The line goes back in the capsule. Undoing a typo is only worth anything if the typo comes
  // back to be fixed.
  FocusInput(true);
  model_.input.Clear();
  model_.input.Insert(taken.typed);
  RestartCaret();
  StartTicking();
  Invalidate();
}

void PopupWindow::ShowToast(std::wstring text) {
  a11y_.Announce(text);
  model_.toast = std::move(text);
  toastOn_ = true;
  SetTimer(hwnd_, kToastTimer, kToastMs, nullptr);
  StartTicking();
}

void PopupWindow::HideToast() {
  toastOn_ = false;
  // The offer ends with the notice: undo is available while it is on screen and not a second
  // longer, which is the only way the two can never disagree. What was only hidden until then
  // is deleted now.
  undo_.reset();
  FlushDeletes();
  StartTicking();
}

bool PopupWindow::ToggleCardAt(float x, float y) {
  if (store_ == nullptr || model_.day.empty()) return false;
  const D2D1_RECT_F list = DayListRect(layout_, model_);
  if (!Inside(list, x, y)) return false;

  const CardSlots slots =
      PlaceCards(layout_, list, static_cast<int>(model_.day.size()), KeptCard(model_));

  for (int position = 0; position < slots.shown; ++position) {
    const D2D1_RECT_F card = CardRect(layout_, list, slots, position);
    if (!Inside(card, x, y)) continue;
    const size_t index = static_cast<size_t>(slots.first + position);
    zone_ = Zone::List;
    listFocus_ = static_cast<int>(index);
    // A click anywhere else on a card is still a click on the card, not on the day behind it.
    if (!model_.day[index].isTask || !Inside(CheckboxRect(layout_, card), x, y)) return true;
    ToggleDone(index);
    return true;
  }
  return false;
}

void PopupWindow::ToggleDone(size_t index) {
  if (store_ == nullptr || index >= model_.day.size()) return;
  DayItem& item = model_.day[index];
  item.done = !item.done;
  store_->SetDone(item.uid, item.done);
  if (sync_ != nullptr) sync_->Push();
  model_.strikeUid = item.uid;
  // Start from where the line is drawn right now, which is the state it had a moment ago.
  model_.strikeT = item.done ? 0.0f : 1.0f;
  strikeOn_ = item.done;
  StartTicking();
  Invalidate();
}

void PopupWindow::StartTicking() {
  if (ticking_ || hwnd_ == nullptr) return;
  ticking_ = true;
  lastTick_ = GetTickCount64();
  SetTimer(hwnd_, kAnimTimer, kTickMs, nullptr);
}

bool PopupWindow::Tick(float ms) {
  const bool animate = AnimationsEnabled();
  const float step = animate ? ms / kStateMs : 1.0f;

  bool moving = false;
  for (int cell = 0; cell < kGridCells; ++cell) {
    if (Settle(model_.dayHover[cell], cell == hoverDay_, step)) moving = true;
  }
  if (Settle(model_.prevHover, hoverPrev_, step)) moving = true;
  if (Settle(model_.nextHover, hoverNext_, step)) moving = true;
  if (Settle(model_.focus, inputFocused_, step)) moving = true;

  if (model_.slideDir != 0) {
    model_.slideT = animate ? (std::min)(1.0f, model_.slideT + ms / kMonthSlideMs) : 1.0f;
    if (model_.slideT >= 1.0f) {
      model_.slideDir = 0;
    } else {
      moving = true;
    }
  }

  // The card that just arrived rises once and is then forgotten: dropping the uid is what
  // stops it animating again the next time the list is reread.
  if (!model_.enterUid.empty()) {
    model_.enterT = animate ? (std::min)(1.0f, model_.enterT + ms / kCardEnterMs) : 1.0f;
    if (model_.enterT >= 1.0f) {
      model_.enterUid.clear();
    } else {
      moving = true;
    }
  }

  // The line across a finished task. It walks both ways, because unticking has to take it back
  // off rather than blink it away.
  if (!model_.strikeUid.empty()) {
    const float strikeStep = animate ? ms / kStrikeMs : 1.0f;
    if (Settle(model_.strikeT, strikeOn_, strikeStep)) {
      moving = true;
    } else {
      model_.strikeUid.clear();
    }
  }

  if (Settle(model_.toastT, toastOn_, animate ? ms / kMonthSlideMs : 1.0f)) {
    moving = true;
  } else if (!toastOn_) {
    model_.toast.clear();
  }
  if (InApp() && TickApp(step)) moving = true;
  return moving;
}

void PopupWindow::RestartCaret() {
  KillTimer(hwnd_, kCaretTimer);
  caretVisible_ = true;
  model_.caretOn = inputFocused_;
  app_.detail.caretOn = app_.detail.focus >= 0;

  // Windows answers INFINITE when the user has turned the blink off in accessibility.
  const UINT blink = GetCaretBlinkTime();
  const bool typing = inputFocused_ || app_.detail.focus >= 0;
  if (typing && blink != 0 && blink != INFINITE) {
    SetTimer(hwnd_, kCaretTimer, blink, nullptr);
  }
}

D2D1_POINT_2F PopupWindow::ToDip(LPARAM lparam) const {
  const float scale = static_cast<float>(USER_DEFAULT_SCREEN_DPI) / static_cast<float>(dpi_);
  return D2D1_POINT_2F{static_cast<float>(GET_X_LPARAM(lparam)) * scale,
                       static_cast<float>(GET_Y_LPARAM(lparam)) * scale};
}

int PopupWindow::HitDay(float x, float y) const {
  if (!Inside(layout_.grid(), x, y)) return -1;
  for (int cell = 0; cell < kGridCells; ++cell) {
    if (Inside(layout_.cell(cell), x, y)) return cell;
  }
  return -1;
}

void PopupWindow::OnMouseMove(float x, float y) {
  if (!tracking_) {
    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
    TrackMouseEvent(&track);
    tracking_ = true;
  }
  if (InApp() && OnAppMouseMove(x, y)) StartTicking();

  // Mid-slide the cell under the pointer belongs to a month that is still moving, so nothing
  // lights up until it lands.
  const int day = model_.slideDir == 0 ? HitDay(x, y) : -1;
  const bool prev = Inside(layout_.prevArrow(), x, y);
  const bool next = Inside(layout_.nextArrow(), x, y);
  if (day == hoverDay_ && prev == hoverPrev_ && next == hoverNext_) return;

  hoverDay_ = day;
  hoverPrev_ = prev;
  hoverNext_ = next;
  StartTicking();
}

void PopupWindow::OnLeftDown(float x, float y) {
  if (InApp() && OnAppLeftDown(x, y)) return;
  if (Inside(layout_.prevArrow(), x, y)) {
    ChangeMonth(-1);
    return;
  }
  if (Inside(layout_.nextArrow(), x, y)) {
    ChangeMonth(1);
    return;
  }
  if (Inside(ActiveLayout().input(), x, y)) {
    FocusInput(true);
    model_.input.MoveTo(InputIndexAt(fonts_, ActiveLayout(), model_, x),
                        GetKeyState(VK_SHIFT) < 0);
    RestartCaret();
    Invalidate();
    return;
  }

  if (!InApp() && ToggleCardAt(x, y)) return;

  const int cell = HitDay(x, y);
  if (cell >= 0) {
    FocusInput(false);
    // In the popup a day is the door to the app; in the app it is the day the main view shows.
    if (InApp()) {
      SelectDay(CellDate(model_.month, cell));
    } else {
      Expand(CellDate(model_.month, cell));
    }
    return;
  }
  // A click on nothing in particular gives the keyboard back to the shortcuts.
  if (InApp()) FocusInput(false);
}

bool PopupWindow::OnKeyDown(WPARAM key) {
  const bool shift = GetKeyState(VK_SHIFT) < 0;
  // AltGr reports itself as Ctrl plus Alt, and a Spanish keyboard types @ # { } with it, so
  // the shortcuts only fire when Alt is up.
  const bool control = GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) >= 0;
  TextInput& input = model_.input;

  switch (key) {
    case VK_TAB:
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_PRIOR:
    case VK_NEXT:
      focusVisible_ = true;
      break;
    default:
      break;
  }
  if (control && key == VK_OEM_COMMA) {  // Ctrl+, : the settings, as in every Windows app
    if (openSettings_) openSettings_();
    return true;
  }
  // A Space the zones take is a command; the character it also produces is not typed.
  eatSpace_ = false;

  if (InApp()) {
    if (drag_.kind != DragKind::None) {
      if (key == VK_ESCAPE) CancelDrag();
      return true;
    }
    // "¿Borrar...?" waits for its answer and nothing else happens meanwhile.
    if (!app_.confirm.empty()) {
      if (key == VK_DELETE || key == VK_RETURN) {
        ConfirmDelete();
      } else if (key == VK_ESCAPE) {
        app_.confirm.clear();
        confirmUid_.clear();
        Invalidate();
      }
      return true;
    }
    if (app_.detail.control >= 0) return OnDetailControlKey(key);
    if (app_.detail.focus >= 0 && !(control && key == 0x4B)) return OnDetailKeyDown(key);
    if (key == VK_TAB && !control) return OnTab(shift);
    if (control && key == 0x4B) {  // Ctrl+K: the capsule, from anywhere in the app
      FocusInput(true);
      RestartCaret();
      Invalidate();
      return true;
    }
    // With the capsule idle the keys are the app's; with it busy only Esc is, to let go of it.
    // What the app does not take and comes with Ctrl -- undo, paste -- is still the capsule's.
    if (!inputFocused_ && OnZoneKey(key)) {
      eatSpace_ = key == VK_SPACE;
      return true;
    }
    if (!inputFocused_ || key == VK_ESCAPE) {
      if (OnAppKeyDown(key)) return true;
      if (!control) return false;
    }
  } else if (control && key == VK_RETURN) {
    Expand(model_.selected);
    return true;
  } else if (key == VK_TAB && !control) {
    return OnTab(shift);
  } else if (!inputFocused_ && OnZoneKey(key)) {
    eatSpace_ = key == VK_SPACE;
    return true;
  }

  if (control) {
    switch (key) {
      case 0x41:  // Ctrl+A
        FocusInput(true);
        input.SelectAll();
        break;
      case 0x43:  // Ctrl+C
        CopySelection(/*cut=*/false);
        break;
      case 0x58:  // Ctrl+X
        CopySelection(/*cut=*/true);
        break;
      case 0x56:  // Ctrl+V
        FocusInput(true);
        Paste();
        break;
      case 0x5A:  // Ctrl+Z
        // Nothing on offer means this is not ours. The capsule keeps no history of its own, so
        // there is nothing else in the popup for Ctrl+Z to mean.
        if (!undo_) return false;
        Undo();
        return true;
      default:
        return false;
    }
    RestartCaret();
    Invalidate();
    return true;
  }

  // The input owns the horizontal arrows while it has something to walk through; otherwise
  // all four move the day. Up and down always belong to the calendar, because a single line
  // of text has nowhere to go vertically.
  const bool editing = inputFocused_ && !input.empty();

  switch (key) {
    case VK_RETURN:
      CreateFromInput();
      return true;

    case VK_ESCAPE:
      Hide();
      return true;

    case VK_LEFT:
      if (editing) {
        input.MoveLeft(shift);
        break;
      }
      SelectDay(AddDays(model_.selected, -1));
      return true;

    case VK_RIGHT:
      if (editing) {
        input.MoveRight(shift);
        break;
      }
      SelectDay(AddDays(model_.selected, 1));
      return true;

    case VK_UP:
      SelectDay(AddDays(model_.selected, -7));
      return true;

    case VK_DOWN:
      SelectDay(AddDays(model_.selected, 7));
      return true;

    case VK_HOME:
      if (!editing) return false;
      input.MoveHome(shift);
      break;

    case VK_END:
      if (!editing) return false;
      input.MoveEnd(shift);
      break;

    case VK_BACK:
      FocusInput(true);
      input.Backspace();
      break;

    case VK_DELETE:
      FocusInput(true);
      input.DeleteForward();
      break;

    default:
      return false;
  }

  RestartCaret();
  Invalidate();
  return true;
}

void PopupWindow::FocusInput(bool focused) {
  if (focused && app_.detail.focus >= 0) {
    CommitField(app_.detail.focus);
    app_.detail.focus = -1;
  }
  if (inputFocused_ == focused) return;
  inputFocused_ = focused;
  // Leaving the input drops the selection, so nothing stays highlighted out of reach.
  if (!focused) model_.input.MoveTo(model_.input.caret(), /*extend=*/false);
  RestartCaret();
  StartTicking();
}

void PopupWindow::SelectDay(Date date) {
  model_.selected = date;

  const Month month{date.year(), date.month()};
  if (month != model_.month) {
    model_.slideFrom = model_.month;
    model_.month = month;
    if (AnimationsEnabled()) {
      model_.slideDir = month > model_.slideFrom ? 1 : -1;
      model_.slideT = 0.0f;
      hoverDay_ = -1;  // the cell under the pointer is about to hold a different day
    }
  }

  Reload();
  StartTicking();
  Invalidate();
}

void PopupWindow::ChangeMonth(int direction) {
  // The selected day travels with the grid, so the list underneath always shows a day that is
  // on screen.
  SelectDay(AddMonths(model_.selected, direction));
}

void PopupWindow::CopySelection(bool cut) {
  TextInput& input = FocusedText();
  if (!input.hasSelection()) return;
  const std::wstring selected{input.selectedText()};

  if (OpenClipboard(hwnd_)) {
    EmptyClipboard();
    const size_t bytes = (selected.size() + 1) * sizeof(wchar_t);
    if (const HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
      if (void* memory = GlobalLock(handle)) {
        std::memcpy(memory, selected.c_str(), bytes);
        GlobalUnlock(handle);
        if (SetClipboardData(CF_UNICODETEXT, handle) == nullptr) GlobalFree(handle);
      } else {
        GlobalFree(handle);
      }
    }
    CloseClipboard();
  }

  if (cut) input.DeleteSelection();
}

void PopupWindow::Paste() {
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(hwnd_)) return;
  if (const HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
    if (const auto* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
      FocusedText().Insert(text);
      GlobalUnlock(handle);
    }
  }
  CloseClipboard();
}

void PopupWindow::OnImeComposition(LPARAM flags) {
  const HIMC context = ImmGetContext(hwnd_);
  if (context == nullptr) return;

  if (flags & GCS_RESULTSTR) {
    model_.composition.clear();
    const LONG bytes = ImmGetCompositionStringW(context, GCS_RESULTSTR, nullptr, 0);
    if (bytes > 0) {
      std::wstring result(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
      ImmGetCompositionStringW(context, GCS_RESULTSTR, result.data(), static_cast<DWORD>(bytes));
      FocusedText().Insert(result);
    }
  }

  // The in-flight text is drawn in the capsule only; a detail field just gets the result.
  if ((flags & GCS_COMPSTR) && app_.detail.focus < 0) {
    const LONG bytes = ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
    const size_t length = bytes > 0 ? static_cast<size_t>(bytes) / sizeof(wchar_t) : 0;
    model_.composition.assign(length, L'\0');
    if (bytes > 0) {
      ImmGetCompositionStringW(context, GCS_COMPSTR, model_.composition.data(),
                               static_cast<DWORD>(bytes));
    }
  }

  ImmReleaseContext(hwnd_, context);
  RestartCaret();
  Invalidate();
}

void PopupWindow::PlaceCandidateWindow() {
  const HIMC context = ImmGetContext(hwnd_);
  if (context == nullptr) return;

  // The candidate list belongs under the caret, not in the corner Windows would pick.
  const D2D1_POINT_2F caret = InputCaretPoint(fonts_, ActiveLayout(), model_);
  const float scale = static_cast<float>(dpi_) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
  CANDIDATEFORM form{};
  form.dwStyle = CFS_CANDIDATEPOS;
  form.ptCurrentPos =
      POINT{static_cast<LONG>(caret.x * scale), static_cast<LONG>(caret.y * scale)};
  ImmSetCandidateWindow(context, &form);
  ImmReleaseContext(hwnd_, context);
}

}  // namespace agenda
