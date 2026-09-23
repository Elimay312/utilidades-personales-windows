#include "ui/panel_window.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "core/autostart.h"
#include "core/config.h"
#include "core/hr.h"
#include "core/i18n.h"
#include "core/log.h"

using Microsoft::WRL::ComPtr;

namespace panel {
namespace {

constexpr wchar_t kClassName[] = L"PanelDeControl";
constexpr UINT_PTR kHideTimer = 1;
constexpr UINT_PTR kAudioRetryTimer = 2;
// A Bluetooth device asked to connect gets this long to do it before its row stops saying
// "a moment": headphones in their case never answer.
constexpr UINT_PTR kBluetoothBusyTimer = 3;
constexpr UINT kBluetoothBusyMs = 12000;
// After a real Core Audio failure: the HUD's two seconds, soon enough to come back by itself
// and rare enough to cost nothing while the audio service restarts.
constexpr UINT kAudioRetryMs = 2000;
// How long after the panel writes the brightness Windows' notices are taken as its echo.
constexpr auto kBrightnessEcho = std::chrono::milliseconds(400);
constexpr UINT kFrameMessage = WM_APP + 1;
// Agenda's timings: one vocabulary of movement for the two popups in the same corner.
constexpr UINT kOpenMs = 160;
constexpr UINT kCloseMs = 120;
// A colour crossing delays no click, so it can take the time it needs to read as a
// fade and not as a cut (Brújula's kInkMs, and the lesson that came with it). Pressing is
// quicker in than out: the sink answers the finger, the return is just the tile settling.
constexpr float kInkSeconds = 0.120f;
constexpr float kPressInSeconds = 0.060f;
constexpr float kPressOutSeconds = 0.120f;

// A tile morphing into a card travels a lot further than a card unfolds, so its spring is
// longer; a hair less damped, so the shape arrives with a settle and not a stop. Tuned like
// every other: by the period, with the app in front (CLAUDE.md, movement).
constexpr float kMorphPeriodSeconds = 0.32f;
constexpr float kMorphDamping = 0.82f;

constexpr float kStep = 0.02f;      // arrows and one notch of the wheel
constexpr float kPageStep = 0.10f;  // Page Up and Page Down

constexpr UINT kMenuOpenConfig = 1;
constexpr UINT kMenuQuit = 2;
constexpr UINT kMenuAutostart = 3;

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

// Hands the pages back to Windows until they are wanted: most are the graphics driver's and
// WMI's, shared with other programs, and they come back from the standby list in the few
// milliseconds an opening can spare.
void Trim() {
  SetProcessWorkingSetSizeEx(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), 0);
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

// Whole percents: what the header shows, what WMI takes and what the keyboard steps by, so the
// number on screen is always the number that was set.
float Quantize(float level) { return std::round(std::clamp(level, 0.0f, 1.0f) * 100.0f) / 100.0f; }

bool Walk(float& value, float goal, float seconds, float duration) {
  if (value == goal) return false;
  const float step = duration > 0.0f ? seconds / duration : 1.0f;
  value = value < goal ? std::min(goal, value + step) : std::max(goal, value - step);
  return value != goal;
}

}  // namespace

bool PanelWindow::Create(HINSTANCE instance, HMONITOR monitor, std::wstring_view theme,
                         std::vector<Utility> utilities) {
  monitor_ = monitor;
  themeChoice_ = theme;
  theme_ = ResolveTheme(themeChoice_);
  wifiSpring_.period = bluetoothSpring_.period = kMorphPeriodSeconds;
  wifiSpring_.damping = bluetoothSpring_.damping = kMorphDamping;
  // Made-up data until each later phase replaces one part of it with the real thing. The volume
  // and its outputs are real since phase 3, the laptop's brightness since 4a; the screens start
  // empty until the worker has looked.
  state_ = SampleState();
  state_.displays.clear();
  // The networks are real since 5b-2, and only listed while the Wi-Fi card is open; the paired
  // Bluetooth devices since 5b-3.
  state_.wifi.networks.clear();
  state_.bluetooth.devices.clear();
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
  if (!audio_.Start(hwnd_)) LogError(L"panel: Core Audio is unavailable, the volume will not work");
  ReadAudio();
  worker_.Start();
  brightness_.Start(hwnd_, worker_);
  radios_.Start(hwnd_, worker_);
  wifi_.Start(hwnd_, worker_);
  night_.Start(hwnd_, worker_);
  // The row of utilities is real since phase 7: panel.json's, or the defaults.
  apps_.Start(hwnd_, worker_, std::move(utilities));
  state_.apps = apps_.Current().apps;
  brightnessNotify_ = RegisterBrightnessNotification(hwnd_);
  if (brightnessNotify_ == nullptr) LogError(L"panel: no brightness notifications, Fn keys will not show");
  Place(work);
  if (!CreateDevices()) return false;
  Rest();
  Render();
  composition_->Commit();
  LogInfo(L"panel: ready, {}x{} px at {} dpi", size_.cx, size_.cy, dpi_);
  // Built and hidden: until the first opening it would otherwise hold everything creating the
  // devices touched (measured: 46 MB at rest before this, against ~2 MB after a first hide).
  Trim();
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
  description.Width = static_cast<UINT>(buffer_.cx);
  description.Height = static_cast<UINT>(buffer_.cy);
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
  work_ = work;
  layout_ = MakeLayout(view_.open, state_);
  RECT rect = PlaceRect(work_, layout_.size(), dpi_);
  SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
               rect.bottom - rect.top, SWP_NOACTIVATE);
  // Now that the window sits on the target monitor its real DPI is known. Fixing it while the
  // window is still hidden means the corrected size never shows as a resize.
  const UINT dpi = MonitorDpi(hwnd_);
  if (dpi != 0 && dpi != dpi_) {
    dpi_ = dpi;
    rect = PlaceRect(work_, layout_.size(), dpi_);
    SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
                 rect.bottom - rect.top, SWP_NOACTIVATE);
  }
  size_ = SIZE{rect.right - rect.left, rect.bottom - rect.top};
  Buffer(size_);
  placing_ = false;
}

void PanelWindow::Relayout() {
  layout_ = MakeLayout(view_.open, state_);
  const RECT rect = PlaceRect(work_, layout_.size(), dpi_);
  const SIZE size{rect.right - rect.left, rect.bottom - rect.top};
  if (size.cx != size_.cx || size.cy != size_.cy) {
    // Anchored at the bottom: a card that opens pushes the top of the panel up, and the corner
    // it sits in stays put, like the taskbar's own flyouts.
    SetWindowPos(hwnd_, nullptr, rect.left, rect.top, size.cx, size.cy,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    size_ = size;
  }
  // While a card moves, the buffer is already the size the panel is going to, so growing the
  // window every frame is a SetWindowPos and never a ResizeBuffers, which is what would flicker
  // (Agenda's expansion does the same). It comes back to the window's size once at rest.
  SIZE want = size_;
  if (animating_) {
    // The larger of where it is going and where it is: a card closing while another opens.
    const Expanded goals = Goals();
    const PanelLayout goal = MakeLayout(
        Expanded{std::max(goals.brightness, view_.open.brightness), std::max(goals.audio, view_.open.audio),
                 std::max(goals.wifi, view_.open.wifi), std::max(goals.bluetooth, view_.open.bluetooth)},
        state_);
    const RECT goalRect = PlaceRect(work_, goal.size(), dpi_);
    want.cy = std::max(want.cy, goalRect.bottom - goalRect.top);
  }
  Buffer(want);
  // The same pointer is over something else now that everything below the card moved.
  if (mouseIn_ && dragging_.empty()) hover_ = HitTest(layout_, state_, mouse_.x, mouse_.y);
}

void PanelWindow::Buffer(SIZE want) {
  if (want.cx == buffer_.cx && want.cy == buffer_.cy) return;
  buffer_ = want;
  if (!swapChain_) return;  // still creating the devices, which will pick up the new size
  dc_->SetTarget(nullptr);
  Failed(swapChain_->ResizeBuffers(0, static_cast<UINT>(buffer_.cx), static_cast<UINT>(buffer_.cy),
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
  // It always opens the same way: cards closed, nothing hovered, no keyboard ring.
  view_ = ViewState{};
  brightnessGoal_ = audioGoal_ = wifiGoal_ = bluetoothGoal_ = false;
  brightnessSpring_.Snap(0.0f);
  audioSpring_.Snap(0.0f);
  wifiSpring_.Snap(0.0f);
  bluetoothSpring_.Snap(0.0f);
  hover_ = pressed_ = dragging_ = Target{};
  const Theme theme = ResolveTheme(themeChoice_);
  const bool flipped = theme.light != theme_.light;
  theme_ = theme;
  if (flipped) ApplyDwmAttributes();
  // Which utilities are running: one look per opening, never polled (SEGURIDAD.md 2.7).
  apps_.Refresh();
  // Hidden, the panel ignores the audio's notifications; it reads once here instead. A change
  // of device while it was hidden is already flagged, and this read acts on it.
  ReadAudio();
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
  // Hidden, the panel never looks for networks (SEGURIDAD.md 1.2), open card or not.
  wifi_.StopScanning();
  if (!dragging_.empty()) {
    dragging_ = Target{};
    ReleaseCapture();
  }
  clock_.Pause();
  animating_ = false;
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

void PanelWindow::Shutdown() {
  KillTimer(hwnd_, kAudioRetryTimer);
  if (brightnessNotify_ != nullptr) UnregisterPowerSettingNotification(brightnessNotify_);
  brightnessNotify_ = nullptr;
  // The connections are let go on the worker's thread, then the worker ends.
  night_.Stop();
  wifi_.Stop();
  radios_.Stop();
  brightness_.Stop();
  worker_.Stop();
  audio_.Stop();
}

void PanelWindow::TakeBrightness() {
  const Brightness::Internal internal = brightness_.Current();
  if (!internal.known) return;
  state_.displays.clear();
  if (internal.present) {
    DisplayState display;
    display.name = std::wstring(T(L"Portátil", L"Laptop"));
    display.id = internal.instance;
    display.level = internal.level;
    display.internal = true;
    display.reachable = true;
    // 4a: the only screen with a slider is the laptop's, so it is the one the card talks about.
    // 4b decides this by the monitor the panel opened on.
    display.here = true;
    state_.displays.push_back(std::move(display));
  }
  if (!CanUnfold(state_.displays.size()) && brightnessGoal_) ToggleCard(brightnessGoal_);
  KeepFocusValid();
}

void PanelWindow::TakeRadios() {
  Radios::Snapshot snapshot = radios_.Current();
  if (!snapshot.known) return;
  // The networks are the Wi-Fi card's (system/wifi.cpp), not the radios': keep them.
  snapshot.wifi.networks = std::move(state_.wifi.networks);
  snapshot.wifi.scanning = state_.wifi.scanning;
  snapshot.wifi.locationDenied = state_.wifi.locationDenied;
  const bool busy = std::any_of(snapshot.bluetooth.devices.begin(), snapshot.bluetooth.devices.end(),
                                [](const BluetoothDevice& device) { return device.busy; });
  if (busy) SetTimer(hwnd_, kBluetoothBusyTimer, kBluetoothBusyMs, nullptr);
  else KillTimer(hwnd_, kBluetoothBusyTimer);
  state_.wifi = snapshot.wifi;
  state_.bluetooth = snapshot.bluetooth;
  if (!snapshot.problem.empty()) SetNotice(std::move(snapshot.problem));
}

void PanelWindow::TakeWifi() {
  Wifi::Snapshot snapshot = wifi_.Current();
  state_.wifi.networks = std::move(snapshot.networks);
  state_.wifi.scanning = snapshot.scanning;
  state_.wifi.locationDenied = snapshot.denied;
  if (!snapshot.problem.empty()) SetNotice(std::move(snapshot.problem));
}

void PanelWindow::TakeApps() {
  Apps::Snapshot snapshot = apps_.Current();
  state_.apps = std::move(snapshot.apps);
  if (!snapshot.problem.empty()) SetNotice(std::move(snapshot.problem));
}

void PanelWindow::TakeNight() {
  NightLight::Snapshot snapshot = night_.Current();
  if (!snapshot.known) return;
  state_.night = snapshot.state;
  if (!snapshot.problem.empty()) SetNotice(std::move(snapshot.problem));
}

void PanelWindow::OpenWindowsNetworks() {
  // What the panel leaves to Windows: a network that wants a password, or all of them. The
  // URI is written out whole (SEGURIDAD.md 1.6, amended in 5b-2).
  const HINSTANCE opened = ShellExecuteW(nullptr, L"open", L"ms-availablenetworks:", nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(opened) <= 32) {
    LogError(L"panel: could not open Windows' network list (error {})", reinterpret_cast<INT_PTR>(opened));
  }
  Hide();
}

void PanelWindow::OpenWindowsAddDevice() {
  // Pairing is Windows' (SEGURIDAD.md 2.6): its add-a-device screen, URI written out whole.
  const HINSTANCE opened = ShellExecuteW(nullptr, L"open", L"ms-settings-connectabledevices:devicediscovery",
                                         nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(opened) <= 32) {
    LogError(L"panel: could not open Windows' add-a-device screen (error {})", reinterpret_cast<INT_PTR>(opened));
  }
  Hide();
}

bool PanelWindow::OpenSettingsFor(Target target) {
  // Right click on a tile: its page in Settings, like Windows' own quick settings. Each URI is
  // written out whole (SEGURIDAD.md 1.6: ShellExecute only with a fixed ms-settings: page).
  if (target.part != Part::Tile) return false;
  HINSTANCE opened = nullptr;
  switch (target.index) {
    case 0:
      opened = ShellExecuteW(nullptr, L"open", L"ms-settings:network-wifi", nullptr, nullptr, SW_SHOWNORMAL);
      break;
    case 1:
      opened = ShellExecuteW(nullptr, L"open", L"ms-settings:bluetooth", nullptr, nullptr, SW_SHOWNORMAL);
      break;
    case 2:
      opened = ShellExecuteW(nullptr, L"open", L"ms-settings:nightlight", nullptr, nullptr, SW_SHOWNORMAL);
      break;
    default:
      return false;  // settings: nothing to open yet, and the menu is more use there
  }
  if (reinterpret_cast<INT_PTR>(opened) <= 32) {
    LogError(L"panel: could not open the settings page (error {})", reinterpret_cast<INT_PTR>(opened));
  }
  Hide();
  return true;
}

void PanelWindow::BrightnessFromWindows(float level) {
  // The keys, Windows' own slider, or the echo of a write of ours.
  if (dragging_.part == Part::BrightnessSlider || dragging_.part == Part::DisplaySlider) return;
  if (std::chrono::steady_clock::now() - brightnessWritten_ < kBrightnessEcho) return;
  for (DisplayState& display : state_.displays) {
    if (display.internal) display.level = level;
  }
}

void PanelWindow::ReadAudio() {
  if (audio_.Read(state_.audio) == Audio::Result::Failed) {
    SetTimer(hwnd_, kAudioRetryTimer, kAudioRetryMs, nullptr);
  } else {
    KillTimer(hwnd_, kAudioRetryTimer);
  }
  audio_.ReadOutputs(state_.audio);
  // A list that emptied under an open card folds it; the keyboard goes back to its header.
  if (!CanUnfold(state_.audio.outputs.size()) && audioGoal_) ToggleCard(audioGoal_);
  KeepFocusValid();
}

void PanelWindow::Toggle() {
  if (visible_) {
    Hide();
  } else {
    Show();
  }
}

void PanelWindow::Shelve() {
  // Back to rest for the next opening, and the memory back to Windows (Trim).
  Rest();
  composition_->Commit();
  // A notice is about the time it was said in; the next opening starts without it. Cleared
  // here, hidden, so the panel never changes height in the middle of its fade.
  state_.notice.clear();
  Trim();
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
  // as WM_DPICHANGED (Agenda, phase 7), so it is laid out again where it is.
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
  AppendMenuW(menu, MF_STRING | (StartsWithWindows() ? MF_CHECKED : MF_UNCHECKED), kMenuAutostart,
              T(L"Iniciar con Windows", L"Start with Windows").data());
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
  } else if (chosen == kMenuAutostart) {
    if (!SetStartWithWindows(!StartsWithWindows())) LogError(L"panel: could not change the Run value");
  } else if (chosen == kMenuQuit) {
    PostQuitMessage(0);
  }
}

// --- Input ------------------------------------------------------------------------------

D2D1_POINT_2F PanelWindow::ToDip(LPARAM lparam) const {
  const float scale = static_cast<float>(USER_DEFAULT_SCREEN_DPI) / static_cast<float>(dpi_);
  return D2D1_POINT_2F{static_cast<float>(GET_X_LPARAM(lparam)) * scale,
                       static_cast<float>(GET_Y_LPARAM(lparam)) * scale};
}

void PanelWindow::OnMouseMove(float x, float y) {
  mouse_ = D2D1_POINT_2F{x, y};
  mouseIn_ = true;
  if (!tracking_) {
    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
    tracking_ = TrackMouseEvent(&track) != FALSE;
  }
  if (!dragging_.empty()) {
    // A drag follows the pointer even outside the slider, and past its ends it just pins.
    SetSliderLevel(dragging_, SliderValueAt(RectOf(layout_, dragging_), x));
    Render();
    return;
  }
  const Target over = HitTest(layout_, state_, x, y);
  if (over == hover_) return;
  hover_ = over;
  UpdateHot();
  StartAnimating();
  Render();
}

void PanelWindow::OnLeftDown(float x, float y) {
  // The mouse is in charge again: the keyboard's ring goes until a key brings it back.
  view_.focusVisible = false;
  const Target target = HitTest(layout_, state_, x, y);
  hover_ = target;
  pressed_ = target;
  if (!target.empty() && target.part != Part::Mute) view_.focus = target;
  if (IsSlider(target.part)) {
    // Straight to where the click was, then it follows the pointer until the button comes up.
    dragging_ = target;
    SetCapture(hwnd_);
    SetSliderLevel(target, SliderValueAt(RectOf(layout_, target), x));
  }
  UpdateHot();
  StartAnimating();
  Render();
}

void PanelWindow::OnLeftUp(float x, float y) {
  if (!dragging_.empty()) {
    // The last value, on release: DDC/CI writes at most every so often while dragging and
    // always once here, so the monitor ends exactly where the pointer did (SEGURIDAD.md 2.4).
    SetSliderLevel(dragging_, SliderValueAt(RectOf(layout_, dragging_), x));
    dragging_ = Target{};
    pressed_ = Target{};
    ReleaseCapture();
  } else {
    const Target released = HitTest(layout_, state_, x, y);
    const Target pressed = pressed_;
    pressed_ = Target{};
    // Only if it comes up on what it went down on: sliding off a tile is how you change your
    // mind about pressing it.
    if (!pressed.empty() && pressed == released) Activate(pressed);
  }
  hover_ = HitTest(layout_, state_, x, y);
  UpdateHot();
  StartAnimating();
  Render();
}

void PanelWindow::OnWheel(int delta) {
  Target target = hover_;
  if (target.part == Part::Mute) target = Target{Part::VolumeSlider};
  if (!IsSlider(target.part)) return;
  Nudge(target, kStep * static_cast<float>(delta) / WHEEL_DELTA);
  UpdateHot();
  Render();
}

bool PanelWindow::OnKey(WPARAM key) {
  const std::vector<Target> order = FocusOrder(layout_, state_);
  Target& focus = view_.focus;
  const bool slider = IsSlider(focus.part);
  const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

  switch (key) {
    case VK_ESCAPE:
      // Layer by layer, like Agenda: an open Wi-Fi or Bluetooth card first, then the panel.
      if (OpenModuleTile() >= 0) {
        CloseModule();
        break;
      }
      Hide();
      return true;
    case VK_TAB:
      focus = NextFocus(order, focus, shift);
      break;
    case VK_LEFT:
    case VK_DOWN:
      if (slider) Nudge(focus, -kStep);
      else focus = NextFocus(order, focus, /*backwards=*/true);
      break;
    case VK_RIGHT:
    case VK_UP:
      if (slider) Nudge(focus, kStep);
      else focus = NextFocus(order, focus, /*backwards=*/false);
      break;
    case VK_PRIOR:
    case VK_NEXT:
      if (!slider) return false;
      Nudge(focus, key == VK_PRIOR ? kPageStep : -kPageStep);
      break;
    case VK_HOME:
    case VK_END:
      if (!slider) return false;
      SetSliderLevel(focus, key == VK_END ? 1.0f : 0.0f);
      break;
    case VK_SPACE:
    case VK_RETURN:
      if (focus.empty()) return false;
      // Unfolding from the keyboard puts the keyboard inside the card it opened.
      if (focus.part == Part::TileChevron) {
        OpenModule(static_cast<int>(focus.index));
        focus = Target{Part::ModuleHeader};
        break;
      }
      // On the volume slider, Space is the mute button it carries at its left end.
      Activate(focus.part == Part::VolumeSlider ? Target{Part::Mute} : focus);
      break;
    default:
      return false;
  }
  view_.focusVisible = true;
  UpdateHot();
  Render();
  return true;
}

void PanelWindow::Activate(Target target) {
  // Phase 2: every action changes the made-up state and nothing else. Each later phase puts the
  // real call here, one part at a time.
  switch (target.part) {
    case Part::Tile:
      // The tile flips now; Windows' notice that the radio changed confirms it a moment later,
      // or puts it back if it did not.
      if (target.index == 0 && state_.wifi.available) {
        state_.wifi.on = !state_.wifi.on;
        if (!state_.wifi.on) state_.wifi.ssid.clear();
        radios_.SetWifi(state_.wifi.on);
      }
      if (target.index == 1 && state_.bluetooth.available) {
        state_.bluetooth.on = !state_.bluetooth.on;
        if (!state_.bluetooth.on) state_.bluetooth.connected = 0;
        radios_.SetBluetooth(state_.bluetooth.on);
      }
      if (target.index == 2 && state_.night.supported) {
        // Windows watches the value and warms the screen; the notice of the change confirms it.
        state_.night.on = !state_.night.on;
        night_.Set(state_.night.on);
      }
      // Index 3, settings: not wired to anything yet. The press shows; nothing happens.
      break;
    case Part::TileChevron:
      OpenModule(static_cast<int>(target.index));
      break;
    case Part::ModuleHeader:
      CloseModule();
      break;
    case Part::ModuleSwitch:
      // The same as a click on the tile it came from: the radio, for real (phase 5).
      Activate(Target{Part::Tile, static_cast<size_t>(OpenModuleTile())});
      break;
    case Part::ModuleRow:
      if (OpenModuleTile() == 0 && target.index < state_.wifi.networks.size()) {
        const WifiNetwork& network = state_.wifi.networks[target.index];
        if (network.connected) break;
        if (network.saved) {
          // Windows says when it is done; the list is read again then and the row says so.
          wifi_.Connect(network.ssid);
        } else {
          // A network Windows has no profile for wants a password, which is Windows' to ask.
          OpenWindowsNetworks();
        }
      } else if (OpenModuleTile() == 1 && target.index < state_.bluetooth.devices.size()) {
        // Audio only: keyboards and mice connect by themselves when they are switched on.
        BluetoothDevice& device = state_.bluetooth.devices[target.index];
        if (device.kind != BluetoothDevice::Kind::Audio || device.busy) break;
        device.busy = true;
        radios_.SetAudioConnected(device.id, !device.connected);
      }
      break;
    case Part::ModuleFooter:
      if (OpenModuleTile() == 0) OpenWindowsNetworks();
      if (OpenModuleTile() == 1) OpenWindowsAddDevice();
      break;
    case Part::BrightnessHeader:
      if (CanUnfold(state_.displays.size()) || brightnessGoal_) ToggleCard(brightnessGoal_);
      break;
    case Part::AudioHeader:
      if (CanUnfold(state_.audio.outputs.size()) || audioGoal_) ToggleCard(audioGoal_);
      break;
    case Part::Mute:
      if (state_.audio.available && audio_.SetMuted(!state_.audio.muted)) {
        state_.audio.muted = !state_.audio.muted;
      } else {
        ReadAudio();
      }
      break;
    case Part::Output: {
      if (target.index >= state_.audio.outputs.size()) break;
      if (!state_.audio.canSwitch) {
        // SEGURIDAD.md 2.2: shown but not changeable, and the panel says why.
        SetNotice(std::wstring(T(L"Este Windows no deja cambiar la salida desde aquí. Usa Win+A.",
                                 L"This Windows does not let the output be changed from here. Use Win+A.")));
        break;
      }
      const AudioOutput chosen = state_.audio.outputs[target.index];
      if (chosen.isDefault) break;
      if (audio_.SetDefault(chosen.id)) {
        // Said now, not when Windows' notice arrives a moment later: the check moves with the
        // click. The notice then reads everything again anyway.
        for (AudioOutput& output : state_.audio.outputs) output.isDefault = output.id == chosen.id;
        state_.audio.device = chosen.name;
      } else {
        ReadAudio();
        Relayout();
      }
      break;
    }
    case Part::App:
      if (target.index < state_.apps.size()) {
        // Running: ask it to close. Stopped: start it. What it is afterwards is looked at again.
        if (state_.apps[target.index].running) apps_.Close(target.index);
        else apps_.Launch(target.index);
      }
      break;
    default:
      break;
  }
}

void PanelWindow::ToggleCard(bool& goal) {
  // The spring starts from wherever it is: interrupting a card halfway is just a new target.
  goal = !goal;
  KeepFocusValid();
  StartAnimating();
}

float PanelWindow::SliderLevel(Target target) const {
  switch (target.part) {
    case Part::BrightnessSlider: {
      const DisplayState* here = HereDisplay(state_);
      return here != nullptr ? here->level : 0.0f;
    }
    case Part::DisplaySlider:
      return target.index < state_.displays.size() ? state_.displays[target.index].level : 0.0f;
    case Part::VolumeSlider:
      return state_.audio.level;
    default:
      return 0.0f;
  }
}

void PanelWindow::SetSliderLevel(Target target, float level) {
  level = Quantize(level);
  switch (target.part) {
    case Part::BrightnessSlider:
    case Part::DisplaySlider: {
      const DisplayState* here = HereDisplay(state_);
      const size_t index = target.part == Part::DisplaySlider
                               ? target.index
                               : (here == nullptr ? state_.displays.size()
                                                  : static_cast<size_t>(here - state_.displays.data()));
      if (index >= state_.displays.size()) return;
      DisplayState& display = state_.displays[index];
      if (display.level == level) return;
      display.level = level;
      if (display.internal) {
        brightness_.Set(level);
        brightnessWritten_ = std::chrono::steady_clock::now();
      }
      break;
    }
    case Part::VolumeSlider: {
      if (!state_.audio.available) return;
      // Turning it up is wanting to hear it, like the HUD does with the keys.
      const bool unmute = level > 0.0f && state_.audio.muted;
      // A drag sends many moves inside one percent; only a new percent reaches Core Audio.
      if (level != state_.audio.level && !audio_.SetLevel(level)) {
        ReadAudio();
        return;
      }
      if (unmute && !audio_.SetMuted(false)) {
        ReadAudio();
        return;
      }
      state_.audio.level = level;
      if (unmute) state_.audio.muted = false;
      break;
    }
    default:
      return;
  }
}

void PanelWindow::Nudge(Target target, float by) { SetSliderLevel(target, SliderLevel(target) + by); }

int PanelWindow::OpenModuleTile() const {
  if (wifiGoal_) return 0;
  if (bluetoothGoal_) return 1;
  return -1;
}

Expanded PanelWindow::Goals() const {
  return Expanded{brightnessGoal_ ? 1.0f : 0.0f, audioGoal_ ? 1.0f : 0.0f, wifiGoal_ ? 1.0f : 0.0f,
                  bluetoothGoal_ ? 1.0f : 0.0f};
}

void PanelWindow::OpenModule(int tile) {
  // Only from the tile grid, which only shows while no card is: never two at once.
  if (tile < 0 || tile > 1 || OpenModuleTile() >= 0) return;
  if (view_.open.wifi > 0.0f || view_.open.bluetooth > 0.0f) return;  // the other still folding
  (tile == 0 ? wifiGoal_ : bluetoothGoal_) = true;
  if (tile == 0) {
    // Looking starts with the card, and only then (SEGURIDAD.md 1.2).
    state_.wifi.scanning = true;
    wifi_.Scan();
  }
  StartAnimating();
}

void PanelWindow::CloseModule() {
  const int tile = OpenModuleTile();
  if (tile < 0) return;
  wifiGoal_ = bluetoothGoal_ = false;
  if (tile == 0) wifi_.StopScanning();
  // The keyboard goes back to the strip it opened from.
  if (view_.focus.part == Part::ModuleHeader || view_.focus.part == Part::ModuleSwitch ||
      view_.focus.part == Part::ModuleRow || view_.focus.part == Part::ModuleFooter) {
    view_.focus = Target{Part::TileChevron, static_cast<size_t>(tile)};
  }
  StartAnimating();
}

void PanelWindow::KeepFocusValid() {
  // A row that is folding away takes the keyboard back to its card's header, which is where
  // the key that folded it was.
  Target& focus = view_.focus;
  if (focus.part == Part::DisplaySlider && !brightnessGoal_) focus = Target{Part::BrightnessHeader};
  if (focus.part == Part::BrightnessSlider && brightnessGoal_) focus = Target{Part::BrightnessHeader};
  if (focus.part == Part::Output &&
      (!audioGoal_ || focus.index >= state_.audio.outputs.size())) {
    focus = Target{Part::AudioHeader};
  }
}

void PanelWindow::UpdateHot() {
  // The percentage shows for the slider being dragged, else the one under the mouse, else the
  // one the keyboard is on.
  if (!dragging_.empty()) {
    view_.hot = dragging_;
  } else if (IsSlider(hover_.part) || hover_.part == Part::Mute) {
    view_.hot = hover_;
  } else if (view_.focusVisible && IsSlider(view_.focus.part)) {
    view_.hot = view_.focus;
  } else {
    view_.hot = Target{};
  }
}

// --- Animation ------------------------------------------------------------------------

void PanelWindow::StartAnimating() {
  if (!AnimationsEnabled()) {
    // No movement: every fade and every card lands where it is going, now.
    brightnessSpring_.Snap(brightnessGoal_ ? 1.0f : 0.0f);
    audioSpring_.Snap(audioGoal_ ? 1.0f : 0.0f);
    wifiSpring_.Snap(wifiGoal_ ? 1.0f : 0.0f);
    bluetoothSpring_.Snap(bluetoothGoal_ ? 1.0f : 0.0f);
    view_.open = Expanded{brightnessSpring_.x, audioSpring_.x, wifiSpring_.x, bluetoothSpring_.x};
    StepInks(1000.0f);
    Relayout();
    return;
  }
  if (animating_) return;
  animating_ = true;
  lastFrame_ = std::chrono::steady_clock::now();
  clock_.Run(hwnd_, kFrameMessage);
}

bool PanelWindow::StepInks(float seconds) {
  // Whatever the mouse is on or pressing gets an ink, so it can fade in.
  for (const Target& wanted : {hover_, pressed_}) {
    if (!wanted.empty() && view_.InkOf(wanted) == nullptr) view_.inks.push_back(Ink{wanted});
  }
  bool moving = false;
  for (Ink& ink : view_.inks) {
    const float hoverGoal = ink.target == hover_ ? 1.0f : 0.0f;
    const float pressGoal = ink.target == pressed_ && pressed_ == hover_ ? 1.0f : 0.0f;
    moving |= Walk(ink.hover, hoverGoal, seconds, kInkSeconds);
    moving |= Walk(ink.press, pressGoal, seconds, pressGoal > ink.press ? kPressInSeconds : kPressOutSeconds);
  }
  std::erase_if(view_.inks, [this](const Ink& ink) {
    return ink.hover == 0.0f && ink.press == 0.0f && ink.target != hover_ && ink.target != pressed_;
  });
  return moving;
}

void PanelWindow::OnFrame() {
  clock_.Taken();
  if (!animating_) return;
  const auto now = std::chrono::steady_clock::now();
  const float seconds = std::chrono::duration<float>(now - lastFrame_).count();
  lastFrame_ = now;

  bool moving = brightnessSpring_.Step(seconds, brightnessGoal_ ? 1.0f : 0.0f);
  moving |= audioSpring_.Step(seconds, audioGoal_ ? 1.0f : 0.0f);
  moving |= wifiSpring_.Step(seconds, wifiGoal_ ? 1.0f : 0.0f);
  moving |= bluetoothSpring_.Step(seconds, bluetoothGoal_ ? 1.0f : 0.0f);
  const Expanded open{std::max(0.0f, brightnessSpring_.x), std::max(0.0f, audioSpring_.x),
                      std::max(0.0f, wifiSpring_.x), std::max(0.0f, bluetoothSpring_.x)};
  const bool resized = open.brightness != view_.open.brightness || open.audio != view_.open.audio ||
                       open.wifi != view_.open.wifi || open.bluetooth != view_.open.bluetooth;
  view_.open = open;
  moving |= StepInks(seconds);

  if (resized && cardFrames_ == 0) cardStart_ = now;
  if (!moving) {
    // At rest: the clock stops and the buffer comes back to the window's size.
    clock_.Pause();
    animating_ = false;
  }
  if (resized || !moving) Relayout();
  UpdateHot();
  const auto drawStart = std::chrono::steady_clock::now();
  Render();
  if (resized) {
    ++cardFrames_;
    cardRenderMs_ +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - drawStart).count();
  }
  if ((!resized || !moving) && cardFrames_ > 0) {
    const double total = std::chrono::duration<double, std::milli>(now - cardStart_).count();
    LogInfo(L"panel: card moved in {} frames over {:.0f} ms ({:.1f} ms a frame, {:.2f} ms to draw)",
            cardFrames_, total, total / cardFrames_, cardRenderMs_ / cardFrames_);
    cardFrames_ = 0;
    cardRenderMs_ = 0.0;
  }
}

// --- Window procedure -----------------------------------------------------------------

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
    case WM_MOUSEMOVE: {
      const D2D1_POINT_2F point = ToDip(lparam);
      OnMouseMove(point.x, point.y);
      return 0;
    }

    case WM_MOUSELEAVE:
      tracking_ = false;
      mouseIn_ = false;
      if (dragging_.empty()) {
        hover_ = Target{};
        UpdateHot();
        StartAnimating();
        Render();
      }
      return 0;

    case WM_LBUTTONDOWN: {
      const D2D1_POINT_2F point = ToDip(lparam);
      OnLeftDown(point.x, point.y);
      return 0;
    }

    case WM_LBUTTONUP: {
      const D2D1_POINT_2F point = ToDip(lparam);
      OnLeftUp(point.x, point.y);
      return 0;
    }

    case WM_CAPTURECHANGED:
      // Somebody else took the mouse mid-drag -- an alt-tab, a menu. The value stays where it
      // got to; there is no drag left to finish.
      if (!dragging_.empty() && reinterpret_cast<HWND>(lparam) != hwnd_) {
        dragging_ = Target{};
        pressed_ = Target{};
        UpdateHot();
        Render();
      }
      break;

    case WM_MOUSEWHEEL:
      OnWheel(GET_WHEEL_DELTA_WPARAM(wparam));
      return 0;

    case kFrameMessage:
      OnFrame();
      return 0;

    case kAppsMessage:
      TakeApps();
      if (visible_) Render();
      return 0;

    case kNightMessage:
      TakeNight();
      if (visible_) Render();
      return 0;

    case kWifiMessage:
      TakeWifi();
      if (visible_) {
        Relayout();
        UpdateHot();
        Render();
      }
      return 0;

    case kRadiosMessage:
      TakeRadios();
      if (visible_) {
        UpdateHot();
        Render();
      }
      return 0;

    case WM_RBUTTONUP: {
      const D2D1_POINT_2F point = ToDip(lparam);
      if (OpenSettingsFor(HitTest(layout_, state_, point.x, point.y))) return 0;
      break;  // anywhere else: DefWindowProc turns it into WM_CONTEXTMENU, the menu
    }

    case kBrightnessMessage:
      TakeBrightness();
      if (visible_) {
        Relayout();
        UpdateHot();
        Render();
      } else {
        // The worker's first read loads WMI into the process, after Create already trimmed.
        Trim();
      }
      return 0;

    case WM_POWERBROADCAST: {
      const float level = BrightnessFromPowerBroadcast(wparam, lparam);
      if (level < 0.0f) break;
      BrightnessFromWindows(level);
      if (visible_) Render();
      return TRUE;
    }

    case kAudioChangedMessage:
    case kAudioDeviceMessage:
      // From outside: the keys, the HUD, Windows' own mixer, another output. Hidden, nothing
      // is done now -- Show reads it all again anyway.
      if (visible_) {
        ReadAudio();
        // An output plugged in or out changes how many rows an open card has.
        Relayout();
        UpdateHot();
        Render();
      }
      return 0;

    case WM_ACTIVATE:
      // It goes when something else is clicked, like Windows' own quick settings.
      if (LOWORD(wparam) == WA_INACTIVE) {
        Hide();
        return 0;
      }
      break;

    case WM_KEYDOWN:
      if (OnKey(wparam)) return 0;
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
      if (wparam == kBluetoothBusyTimer) {
        KillTimer(hwnd_, kBluetoothBusyTimer);
        radios_.ClearBusy();
        return 0;
      }
      if (wparam == kAudioRetryTimer) {
        ReadAudio();
        if (visible_) Render();
        return 0;
      }
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
