#include "ui/popup_window.h"

#include <dwmapi.h>
#include <imm.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cstring>

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
constexpr UINT kTickMs = 16;  // one frame at 60 Hz, which is all the content fades need

Theme ResolveTheme(std::wstring_view override) {
  if (override == L"light") return LightTheme();
  if (override == L"dark") return DarkTheme();
  return SystemTheme();
}

// Walks a nought to one fade towards its target and says whether it still has ground to cover.
bool Settle(float& value, bool on, float step) {
  const float target = on ? 1.0f : 0.0f;
  if (value == target) return false;
  if (step <= 0.0f) return true;  // a tick too short to measure; the next one will move it
  if (step >= 1.0f) {
    value = target;
    return false;
  }
  value = value < target ? (std::min)(target, value + step) : (std::max)(target, value - step);
  return value != target;
}

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
  theme_ = ResolveTheme(themeOverride_);
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
  const auto panelFor = [&](UINT dpi) {
    return panelOverride_.width > 0.0f ? panelOverride_ : PanelSize(work, dpi);
  };

  D2D1_SIZE_F panel = panelFor(dpi_);
  RECT rect = PlaceRect(work, panel, dpi_);
  SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
               rect.bottom - rect.top, SWP_NOACTIVATE);

  // Now that the window sits on the target monitor, its real DPI is known. Doing this while it
  // is still hidden means the corrected size never shows up as a resize on screen.
  const UINT dpi = GetDpiForWindow(hwnd_);
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
  // The device context keeps its own DPI and starts at 96, whatever the target bitmap says. On
  // a scaled monitor that drew the panel one DIP to one pixel, so it covered only a corner of
  // the window and the rest showed through as bare backdrop.
  dc_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
  dc_->BeginDraw();
  dc_->Clear(D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.0f});
  DrawPopup(dc_.Get(), fonts_, theme_, layout_, model_, acrylic_);
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

  // The popup always opens on today with an empty line waiting, and picks up a theme the user
  // may have flipped while it was hidden.
  const Theme theme = ResolveTheme(themeOverride_);
  const bool flipped = theme.light != theme_.light;
  theme_ = theme;
  if (flipped) ApplyDwmAttributes();

  model_ = MakeModel(TodayLocal());
  model_.focus = 1.0f;
  hoverDay_ = -1;
  hoverPrev_ = false;
  hoverNext_ = false;
  inputFocused_ = true;
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
  KillTimer(hwnd_, kAnimTimer);
  KillTimer(hwnd_, kCaretTimer);
  ticking_ = false;
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
    case WM_MOUSEMOVE: {
      const D2D1_POINT_2F point = ToDip(lparam);
      OnMouseMove(point.x, point.y);
      return 0;
    }

    case WM_MOUSELEAVE:
      tracking_ = false;
      hoverDay_ = -1;
      hoverPrev_ = false;
      hoverNext_ = false;
      StartTicking();
      return 0;

    case WM_LBUTTONDOWN: {
      const D2D1_POINT_2F point = ToDip(lparam);
      OnLeftDown(point.x, point.y);
      return 0;
    }

    case WM_SETCURSOR: {
      POINT cursor{};
      if (GetCursorPos(&cursor) && ScreenToClient(hwnd_, &cursor)) {
        const float scale =
            static_cast<float>(USER_DEFAULT_SCREEN_DPI) / static_cast<float>(dpi_);
        const bool overInput = Inside(layout_.input(), static_cast<float>(cursor.x) * scale,
                                      static_cast<float>(cursor.y) * scale);
        SetCursor(LoadCursorW(nullptr, overInput ? IDC_IBEAM : IDC_ARROW));
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
      FocusInput(true);
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
      if (wparam == kCaretTimer) {
        caretVisible_ = !caretVisible_;
        model_.caretOn = inputFocused_ && caretVisible_;
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
  if (swapChain_) Render();
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
  return moving;
}

void PopupWindow::RestartCaret() {
  KillTimer(hwnd_, kCaretTimer);
  caretVisible_ = true;
  model_.caretOn = inputFocused_;

  // Windows answers INFINITE when the user has turned the blink off in accessibility.
  const UINT blink = GetCaretBlinkTime();
  if (inputFocused_ && blink != 0 && blink != INFINITE) {
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
  if (Inside(layout_.prevArrow(), x, y)) {
    ChangeMonth(-1);
    return;
  }
  if (Inside(layout_.nextArrow(), x, y)) {
    ChangeMonth(1);
    return;
  }
  if (Inside(layout_.input(), x, y)) {
    FocusInput(true);
    model_.input.MoveTo(InputIndexAt(fonts_, layout_, model_, x), GetKeyState(VK_SHIFT) < 0);
    RestartCaret();
    Invalidate();
    return;
  }

  const int cell = HitDay(x, y);
  if (cell >= 0) {
    FocusInput(false);
    SelectDay(CellDate(model_.month, cell));
  }
}

bool PopupWindow::OnKeyDown(WPARAM key) {
  const bool shift = GetKeyState(VK_SHIFT) < 0;
  // AltGr reports itself as Ctrl plus Alt, and a Spanish keyboard types @ # { } with it, so
  // the shortcuts only fire when Alt is up.
  const bool control = GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) >= 0;
  TextInput& input = model_.input;

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

  StartTicking();
  Invalidate();
}

void PopupWindow::ChangeMonth(int direction) {
  // The selected day travels with the grid, so the list underneath always shows a day that is
  // on screen.
  SelectDay(AddMonths(model_.selected, direction));
}

void PopupWindow::CopySelection(bool cut) {
  if (!model_.input.hasSelection()) return;
  const std::wstring selected{model_.input.selectedText()};

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

  if (cut) model_.input.DeleteSelection();
}

void PopupWindow::Paste() {
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(hwnd_)) return;
  if (const HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
    if (const auto* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
      model_.input.Insert(text);
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
      model_.input.Insert(result);
    }
  }

  if (flags & GCS_COMPSTR) {
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
  const D2D1_POINT_2F caret = InputCaretPoint(fonts_, layout_, model_);
  const float scale = static_cast<float>(dpi_) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
  CANDIDATEFORM form{};
  form.dwStyle = CFS_CANDIDATEPOS;
  form.ptCurrentPos =
      POINT{static_cast<LONG>(caret.x * scale), static_cast<LONG>(caret.y * scale)};
  ImmSetCandidateWindow(context, &form);
  ImmReleaseContext(hwnd_, context);
}

}  // namespace agenda
