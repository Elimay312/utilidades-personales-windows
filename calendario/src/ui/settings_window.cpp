#include "ui/settings_window.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <format>

#include "core/autostart.h"
#include "core/hr.h"
#include "core/log.h"
#include "data/store.h"
#include "sync/google.h"
#include "ui/components.h"
#include "ui/layout.h"

namespace agenda {
namespace {

constexpr wchar_t kClassName[] = L"AgendaSettings";
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT_PTR kAnimTimer = 2;
constexpr UINT kRefreshMs = 1000;

// The window is written at 96 DPI in DIP, like everything else; the render target scales it.
constexpr float kWidthDip = 560.0f;
constexpr float kPadDip = 24.0f;
constexpr float kSectionDip = 40.0f;
constexpr float kRowDip = 64.0f;
constexpr float kRowGapDip = 4.0f;
constexpr float kControlDip = 32.0f;
constexpr float kInsideDip = 16.0f;  // text and controls from the edge of their card
constexpr float kFooterDip = 36.0f;

enum Control { kHotkey, kLanguage, kTheme, kStartup, kCalendar, kDuration, kGoogle, kControls };
// Under the Google card, one card per account with its "Quitar": their controls come after the
// fixed ones, kControls + the account's place.
constexpr int kMaxAccounts = 6;
constexpr float kAccountButtonDip = 100.0f;
constexpr int kSectionOf[kControls] = {0, 0, 0, 0, 1, 1, 2};
constexpr float kControlWidth[kControls] = {180.0f, 200.0f, 264.0f, 44.0f, 220.0f, 300.0f, 140.0f};

// Ids UIA sees: a control is its index plus one, an option of a segmented one is 100 + ten per
// control + its position, and an entry of the open calendar list is 200 + its position.
constexpr int kOptionIds = 100;
constexpr int kListIds = 200;

int Options(int control) {
  switch (control) {
    case kLanguage:
      return 2;
    case kTheme:
      return 3;
    case kDuration:
      return static_cast<int>(std::size(kDurationChoices));
    default:
      return 0;
  }
}

struct SettingsLayout {
  D2D1_RECT_F sections[3]{};
  D2D1_RECT_F cards[kControls]{};
  D2D1_RECT_F controls[kControls]{};
  int accounts = 0;
  D2D1_RECT_F accountCards[kMaxAccounts]{};
  D2D1_RECT_F accountControls[kMaxAccounts]{};
  D2D1_RECT_F footer{};
  float height = 0.0f;
};

SettingsLayout MakeSettingsLayout(int accounts) {
  SettingsLayout out;
  float y = kPadDip / 2.0f;
  int section = -1;
  for (int c = 0; c < kControls; ++c) {
    if (kSectionOf[c] != section) {
      section = kSectionOf[c];
      out.sections[section] = D2D1_RECT_F{kPadDip + 4.0f, y + 8.0f, kWidthDip - kPadDip, y + kSectionDip};
      y += kSectionDip + 4.0f;
    }
    out.cards[c] = D2D1_RECT_F{kPadDip, y, kWidthDip - kPadDip, y + kRowDip};
    const float height = c == kStartup ? 22.0f : kControlDip;
    const float middle = y + kRowDip / 2.0f;
    const float right = out.cards[c].right - kInsideDip;
    out.controls[c] = D2D1_RECT_F{right - kControlWidth[c], middle - height / 2.0f, right,
                                  middle + height / 2.0f};
    y += kRowDip + kRowGapDip;
  }
  out.accounts = std::clamp(accounts, 0, kMaxAccounts);
  for (int i = 0; i < out.accounts; ++i) {
    out.accountCards[i] = D2D1_RECT_F{kPadDip, y, kWidthDip - kPadDip, y + kRowDip};
    const float middle = y + kRowDip / 2.0f;
    const float right = out.accountCards[i].right - kInsideDip;
    out.accountControls[i] = D2D1_RECT_F{right - kAccountButtonDip, middle - kControlDip / 2.0f,
                                         right, middle + kControlDip / 2.0f};
    y += kRowDip + kRowGapDip;
  }
  out.footer = D2D1_RECT_F{kPadDip + 4.0f, y, kWidthDip - kPadDip, y + kFooterDip};
  out.height = y + kFooterDip + kPadDip / 2.0f;
  return out;
}

// The fixed controls sit in the same place whatever the number of accounts, which only adds
// cards below them; asked without one, the answer is the layout with none.
const SettingsLayout& Layout(int accounts = 0) {
  static const auto layouts = [] {
    std::array<SettingsLayout, kMaxAccounts + 1> all;
    for (int i = 0; i <= kMaxAccounts; ++i) all[static_cast<size_t>(i)] = MakeSettingsLayout(i);
    return all;
  }();
  return layouts[static_cast<size_t>(std::clamp(accounts, 0, kMaxAccounts))];
}

D2D1_RECT_F OptionRect(const D2D1_RECT_F& control, int count, int index) {
  const float width = (control.right - control.left) / static_cast<float>(count);
  const float left = control.left + width * static_cast<float>(index);
  return D2D1_RECT_F{left, control.top, left + width, control.bottom};
}

D2D1_RECT_F ListRect(int index) {
  const D2D1_RECT_F& chooser = Layout().controls[kCalendar];
  const float top = chooser.bottom + 4.0f + static_cast<float>(index) * kControlDip;
  return D2D1_RECT_F{chooser.left, top, chooser.right, top + kControlDip};
}

// What the mouse is over: the control, and for the startup switch the whole card, which is what
// a click on the words next to a switch means everywhere else in Windows.
int ControlAt(float x, float y, int accounts) {
  const SettingsLayout& layout = Layout(accounts);
  for (int c = 0; c < kControls; ++c) {
    const D2D1_RECT_F& hit = c == kStartup ? layout.cards[c] : layout.controls[c];
    if (Inside(hit, x, y)) return c;
  }
  for (int i = 0; i < layout.accounts; ++i) {
    if (Inside(layout.accountControls[i], x, y)) return kControls + i;
  }
  return -1;
}

std::wstring Widen(std::string_view ascii) { return std::wstring(ascii.begin(), ascii.end()); }

std::wstring_view OptionText(int control, int option) {
  static constexpr std::wstring_view kDurations[] = {L"30 min", L"45 min", L"1 h", L"1 h 30",
                                                     L"2 h"};
  switch (control) {
    case kLanguage:
      return option == 0 ? L"Español" : L"English";
    case kTheme:
      return option == 0 ? T(L"Sistema", L"System")
                         : (option == 1 ? T(L"Oscuro", L"Dark") : T(L"Claro", L"Light"));
    case kDuration:
      return kDurations[option];
    default:
      return {};
  }
}

std::wstring_view Label(int control) {
  switch (control) {
    case kHotkey:
      return T(L"Atajo global", L"Global shortcut");
    case kLanguage:
      return T(L"Idioma", L"Language");
    case kTheme:
      return T(L"Tema", L"Theme");
    case kStartup:
      return T(L"Iniciar con Windows", L"Start with Windows");
    case kCalendar:
      return T(L"Calendario por defecto", L"Default calendar");
    case kDuration:
      return T(L"Duración por defecto", L"Default duration");
    case kGoogle:
      return L"Google Calendar · Google Tasks";
    default:
      return {};
  }
}

// The name of a key the way ParseHotkey reads it back, or nothing for a key a shortcut cannot
// be made of.
std::string KeyName(UINT vk) {
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, static_cast<char>(vk));
  if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
  switch (vk) {
    case VK_SPACE:
      return "Space";
    case VK_RETURN:
      return "Enter";
    case VK_TAB:
      return "Tab";
    default:
      return {};
  }
}

bool Down(int vk) { return GetKeyState(vk) < 0; }

}  // namespace

SettingsWindow::~SettingsWindow() {
  if (hwnd_ != nullptr) DestroyWindow(hwnd_);
}

void SettingsWindow::Init(HINSTANCE instance, Preferences* prefs, Store* store,
                          sync::GoogleSync* sync, Hooks hooks) {
  instance_ = instance;
  prefs_ = prefs;
  store_ = store;
  sync_ = sync;
  hooks_ = std::move(hooks);
}

void SettingsWindow::Show(HMONITOR monitor) {
  if (hwnd_ != nullptr) {
    ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd_);
    return;
  }

  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    windowClass.lpszClassName = kClassName;
    if (RegisterClassExW(&windowClass) == 0) {
      LogError(L"settings: RegisterClassExW failed with error {}", GetLastError());
      return;
    }
    registered = true;
  }
  if (!factory_ && Failed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                            factory_.GetAddressOf()),
                          L"D2D1CreateFactory")) {
    return;
  }
  if (!fonts_.ok() && !fonts_.Create(BaseLayout())) return;

  MONITORINFO info{};
  info.cbSize = sizeof(info);
  GetMonitorInfoW(monitor, &info);
  // Born on the target monitor, so Windows gives it that monitor's DPI from the first message.
  constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  hwnd_ = CreateWindowExW(0, kClassName, L"", kStyle, info.rcWork.left, info.rcWork.top, 100, 100,
                          nullptr, nullptr, instance_, this);
  if (hwnd_ == nullptr) {
    LogError(L"settings: CreateWindowExW failed with error {}", GetLastError());
    return;
  }
  dpi_ = GetDpiForWindow(hwnd_);
  Retitle();
  ApplyTheme();
  Refresh();  // the accounts, which the height depends on

  RECT frame{0, 0, ScaleDip(kWidthDip, dpi_),
             ScaleDip(Layout(static_cast<int>(accounts_.size())).height, dpi_)};
  AdjustWindowRectExForDpi(&frame, kStyle, FALSE, 0, dpi_);
  const int width = frame.right - frame.left;
  const int height = frame.bottom - frame.top;
  const RECT& work = info.rcWork;
  SetWindowPos(hwnd_, nullptr, work.left + (work.right - work.left - width) / 2,
               work.top + (work.bottom - work.top - height) / 2, width, height,
               SWP_NOZORDER | SWP_NOACTIVATE);

  RECT client{};
  GetClientRect(hwnd_, &client);
  if (Failed(factory_->CreateHwndRenderTarget(
                 D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                              D2D1::PixelFormat(), static_cast<float>(dpi_),
                                              static_cast<float>(dpi_)),
                 D2D1::HwndRenderTargetProperties(
                     hwnd_, D2D1::SizeU(static_cast<UINT32>(client.right),
                                        static_cast<UINT32>(client.bottom))),
                 &target_),
             L"CreateHwndRenderTarget")) {
    DestroyWindow(hwnd_);
    return;
  }

  a11y_.Attach(hwnd_, this);
  focus_ = 0;
  focusVisible_ = false;
  listOpen_ = false;
  hotkeyError_.clear();
  Refresh();
  toggleT_ = startup_ ? 1.0f : 0.0f;
  SetTimer(hwnd_, kRefreshTimer, kRefreshMs, nullptr);
  ShowWindow(hwnd_, SW_SHOW);
  SetForegroundWindow(hwnd_);
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    auto* created = static_cast<SettingsWindow*>(create->lpCreateParams);
    // Known from the first message: Handle hands everything it does not take to DefWindowProc
    // with it, and a null there aborts the creation.
    created->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
  }
  auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self == nullptr) return DefWindowProcW(hwnd, message, wparam, lparam);
  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    self->hwnd_ = nullptr;
    self->target_.Reset();
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }
  return self->Handle(message, wparam, lparam);
}

LRESULT SettingsWindow::Handle(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT paint;
      BeginPaint(hwnd_, &paint);
      Render();
      EndPaint(hwnd_, &paint);
      return 0;
    }

    case WM_SIZE:
      Resize();
      return 0;

    case WM_DPICHANGED: {
      // Dragged onto a monitor with another scale: the size Windows suggests keeps the window
      // the same size in DIP, and the render target only has to be told the new DPI.
      dpi_ = HIWORD(wparam);
      const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left, suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      if (target_) target_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
      Resize();
      LogInfo(L"settings: now at {} dpi", dpi_);
      return 0;
    }

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
      ApplyTheme();
      InvalidateRect(hwnd_, nullptr, FALSE);
      break;

    case WM_GETOBJECT: {
      LRESULT result = 0;
      if (a11y_.OnGetObject(wparam, lparam, result)) return result;
      break;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      if (capturing_) {
        OnCapture(wparam);
        return 0;
      }
      if (message == WM_KEYDOWN) {
        OnKeyDown(wparam);
        return 0;
      }
      break;

    case WM_SYSCHAR:
    case WM_SYSCOMMAND:
      // Alt+Space would open the window menu instead of being recorded as the shortcut.
      if (capturing_ && (message == WM_SYSCHAR || (wparam & 0xFFF0) == SC_KEYMENU)) return 0;
      break;

    case WM_LBUTTONDOWN: {
      const D2D1_POINT_2F point = ToDip(lparam);
      OnLeftDown(point.x, point.y);
      return 0;
    }

    case WM_MOUSEMOVE: {
      const D2D1_POINT_2F point = ToDip(lparam);
      OnMouseMove(point.x, point.y);
      return 0;
    }

    case WM_MOUSELEAVE:
      tracking_ = false;
      hover_ = -1;
      hoverOption_ = -1;
      StartTicking();
      return 0;

    case WM_KILLFOCUS:
      // Leaving the window mid-recording gives the hotkey back rather than leaving it off.
      if (capturing_) StopCapture();
      break;

    case WM_TIMER:
      if (wparam == kRefreshTimer) {
        Refresh();
        return 0;
      }
      if (wparam == kAnimTimer) {
        const ULONGLONG now = GetTickCount64();
        const bool moving = Tick(static_cast<float>(now - lastTick_));
        lastTick_ = now;
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (!moving) {
          KillTimer(hwnd_, kAnimTimer);
          ticking_ = false;
        }
        return 0;
      }
      break;

    case WM_CLOSE:
      DestroyWindow(hwnd_);
      return 0;

    case WM_DESTROY:
      if (capturing_) StopCapture();
      KillTimer(hwnd_, kRefreshTimer);
      KillTimer(hwnd_, kAnimTimer);
      ticking_ = false;
      a11y_.Detach();
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void SettingsWindow::Retitle() {
  SetWindowTextW(hwnd_, T(L"Configuración de Agenda", L"Agenda settings").data());
}

void SettingsWindow::ApplyTheme() {
  theme_ = ResolveTheme(prefs_->theme);
  if (hwnd_ == nullptr) return;
  const BOOL dark = theme_.light ? FALSE : TRUE;
  DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
  // The title bar in the panel's own colour, so window and content read as one surface. In high
  // contrast the system's colours stay: they are the user's.
  const auto ref = [](const D2D1_COLOR_F& color) {
    return RGB(static_cast<BYTE>(color.r * 255.0f), static_cast<BYTE>(color.g * 255.0f),
               static_cast<BYTE>(color.b * 255.0f));
  };
  const COLORREF caption = theme_.highContrast ? DWMWA_COLOR_DEFAULT : ref(theme_.panelOpaque);
  const COLORREF text = theme_.highContrast ? DWMWA_COLOR_DEFAULT : ref(theme_.textPrimary);
  DwmSetWindowAttribute(hwnd_, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
  DwmSetWindowAttribute(hwnd_, DWMWA_TEXT_COLOR, &text, sizeof(text));
}

void SettingsWindow::Resize() {
  if (!target_) return;
  RECT client{};
  GetClientRect(hwnd_, &client);
  target_->Resize(D2D1::SizeU(static_cast<UINT32>(client.right), static_cast<UINT32>(client.bottom)));
  InvalidateRect(hwnd_, nullptr, FALSE);
}

int SettingsWindow::Controls() const {
  return kControls + static_cast<int>((std::min)(accounts_.size(), size_t{kMaxAccounts}));
}

void SettingsWindow::FitHeight() {
  if (hwnd_ == nullptr) return;
  constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  RECT frame{0, 0, ScaleDip(kWidthDip, dpi_),
             ScaleDip(Layout(static_cast<int>(accounts_.size())).height, dpi_)};
  AdjustWindowRectExForDpi(&frame, kStyle, FALSE, 0, dpi_);
  SetWindowPos(hwnd_, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void SettingsWindow::Refresh() {
  const bool configured = sync_ != nullptr && sync_->Configured();
  const bool connected = sync_ != nullptr && sync_->Connected();
  std::vector<AccountRow> accounts;
  if (sync_ != nullptr) {
    for (const sync::GoogleSync::AccountState& state : sync_->Accounts()) {
      accounts.push_back(AccountRow{state.id, state.email, state.connected});
    }
  }
  if (accounts != accounts_) {
    const bool resized = accounts.size() != accounts_.size();
    accounts_ = std::move(accounts);
    focus_ = (std::min)(focus_, Controls() - 1);
    if (resized) FitHeight();
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
    a11y_.Changed();
  }
  std::vector<CalendarInfo> calendars;
  if (store_ != nullptr && store_->IsOpen()) calendars = store_->Calendars(/*tasklists=*/false);
  const bool startup = StartsWithWindows();

  bool same = configured == configured_ && connected == connected_ && startup == startup_ &&
              calendars.size() == calendars_.size();
  for (size_t i = 0; same && i < calendars.size(); ++i) {
    same = calendars[i].id == calendars_[i].id && calendars[i].isDefault == calendars_[i].isDefault &&
           calendars[i].title == calendars_[i].title;
  }
  if (same) return;
  configured_ = configured;
  connected_ = connected;
  startup_ = startup;
  // The open list keeps its place while it is open; it is rebuilt under the pointer otherwise.
  if (!listOpen_) calendars_ = std::move(calendars);
  StartTicking();
  if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
  a11y_.Changed();
}

int SettingsWindow::Selected(int control) const {
  switch (control) {
    case kLanguage:
      return prefs_->lang == Lang::En ? 1 : 0;
    case kTheme:
      return prefs_->theme == L"dark" ? 1 : (prefs_->theme == L"light" ? 2 : 0);
    case kDuration:
      for (int i = 0; i < Options(kDuration); ++i) {
        if (kDurationChoices[i] == prefs_->durationMin) return i;
      }
      return 2;
    default:
      return -1;
  }
}

D2D1_POINT_2F SettingsWindow::ToDip(LPARAM lparam) const {
  const float scale = static_cast<float>(USER_DEFAULT_SCREEN_DPI) / static_cast<float>(dpi_);
  return D2D1_POINT_2F{static_cast<float>(GET_X_LPARAM(lparam)) * scale,
                       static_cast<float>(GET_Y_LPARAM(lparam)) * scale};
}

void SettingsWindow::StartTicking() {
  if (ticking_ || hwnd_ == nullptr) return;
  ticking_ = true;
  lastTick_ = GetTickCount64();
  SetTimer(hwnd_, kAnimTimer, 16, nullptr);
}

bool SettingsWindow::Tick(float ms) {
  BOOL animations = TRUE;
  SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
  const float step = animations ? ms / kStateMs : 1.0f;
  bool moving = false;
  for (int c = 0; c < Controls(); ++c) {
    if (Settle(hoverT_[c], hover_ == c, step)) moving = true;
  }
  // The knob travels in the 160 ms everything that arrives takes.
  if (Settle(toggleT_, startup_, animations ? ms / kCardEnterMs : 1.0f)) moving = true;
  return moving;
}

// --- Drawing ---------------------------------------------------------------------------------

void SettingsWindow::Render() {
  if (!target_ || !fonts_.ok()) return;
  target_->BeginDraw();
  Paint(target_.Get());
  if (target_->EndDraw() == D2DERR_RECREATE_TARGET) target_.Reset();
}

D2D1_SIZE_F SettingsWindow::SizeDip() {
  return D2D1_SIZE_F{kWidthDip, Layout(kSnapshotAccounts).height};
}

bool SettingsWindow::PaintForSnapshot(ID2D1RenderTarget* target, const Theme& theme,
                                      std::vector<CalendarInfo> calendars) {
  if (!fonts_.ok() && !fonts_.Create(BaseLayout())) return false;
  theme_ = theme;
  calendars_ = std::move(calendars);
  configured_ = true;
  connected_ = true;
  startup_ = true;
  toggleT_ = 1.0f;
  accounts_ = {AccountRow{1, L"ana.garcia@gmail.com", true},
               AccountRow{2, L"ana@estudio-norte.co", true}};
  Paint(target);
  return true;
}

void SettingsWindow::Paint(ID2D1RenderTarget* target) {
  const SettingsLayout& layout = Layout(static_cast<int>(accounts_.size()));
  bool anyLost = false;
  for (const AccountRow& account : accounts_) anyLost = anyLost || !account.connected;
  const Theme& theme = theme_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
  target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  target->Clear(theme.panelOpaque);
  if (FAILED(target->CreateSolidColorBrush(theme.textPrimary, &brush))) return;
  Microsoft::WRL::ComPtr<ID2D1StrokeStyle> rounded = RoundedStroke(target);
  ID2D1SolidColorBrush* b = brush.Get();
  const float radius = kRadiusCard;

  const std::wstring_view sections[3] = {T(L"General", L"General"), T(L"Eventos", L"Events"),
                                         T(L"Cuentas de Google", L"Google accounts")};
  for (int s = 0; s < 3; ++s) {
    b->SetColor(theme.textPrimary);
    DrawTextIn(target, fonts_.title.Get(), sections[s], layout.sections[s], b);
  }

  for (int c = 0; c < kControls; ++c) {
    const D2D1_RECT_F& card = layout.cards[c];
    const D2D1_RECT_F& control = layout.controls[c];
    b->SetColor(theme.surface);
    FillRound(target, card, radius, b);
    b->SetColor(theme.border);
    StrokeRound(target, card, radius, b, 1.0f);

    // The label, and under it what the setting does or what is wrong with it.
    std::wstring detail;
    D2D1_COLOR_F detailColor = theme.textSecondary;
    switch (c) {
      case kHotkey:
        detail = hotkeyError_.empty() ? std::wstring(T(L"Abre y cierra el popup desde cualquier sitio",
                                                       L"Opens and closes the popup from anywhere"))
                                      : hotkeyError_;
        if (!hotkeyError_.empty()) detailColor = theme.now;
        break;
      case kLanguage:
        detail = T(L"De toda la interfaz; el campo entiende los dos",
                   L"For the whole interface; the field understands both");
        break;
      case kTheme:
        detail = theme.highContrast ? T(L"Alto contraste de Windows activado",
                                        L"Windows high contrast is on")
                                    : T(L"Oscuro, claro o el de Windows",
                                        L"Dark, light or the one Windows uses");
        break;
      case kStartup:
        detail = T(L"Agenda espera en la bandeja, lista para el atajo",
                   L"Agenda waits in the tray, ready for the shortcut");
        break;
      case kCalendar:
        detail = T(L"Donde cae lo que creas", L"Where what you create lands");
        break;
      case kDuration:
        detail = T(L"Con hora y sin duración", L"With a time, no length");
        break;
      case kGoogle:
        detail = !configured_ ? T(L"Faltan las credenciales: mira docs/google-setup.md",
                                  L"Credentials missing: see docs/google-setup.md")
                 : accounts_.empty() ? T(L"Sin conectar: todo se queda en este equipo",
                                         L"Not connected: everything stays on this PC")
                 : anyLost ? T(L"Una cuenta perdió el permiso: vuelve a conectarla",
                               L"An account lost its permission: connect it again")
                           : T(L"Los calendarios de cada cuenta, juntos aquí",
                               L"Every account's calendars, together here");
        if (configured_ && anyLost) detailColor = theme.now;
        break;
      default:
        break;
    }
    const float textRight = control.left - kInsideDip;
    b->SetColor(theme.textPrimary);
    const std::wstring_view label =
        c == kGoogle && !accounts_.empty() ? T(L"Añadir otra cuenta", L"Add another account")
                                           : Label(c);
    DrawTextIn(target, fonts_.event.Get(), label,
               D2D1_RECT_F{card.left + kInsideDip, card.top + 10.0f, textRight, card.top + 32.0f}, b);
    b->SetColor(detailColor);
    DrawTextIn(target, fonts_.label.Get(), detail,
               D2D1_RECT_F{card.left + kInsideDip, card.top + 32.0f, textRight, card.bottom - 10.0f},
               b);

    const float hover = hoverT_[c];
    switch (c) {
      case kHotkey: {
        b->SetColor(theme.panelOpaque);
        FillRound(target, control, radius, b);
        if (hover > 0.0f && !capturing_) {
          b->SetColor(Fade(theme.hover, hover));
          FillRound(target, control, radius, b);
        }
        b->SetColor(capturing_ ? theme.accent : theme.border);
        StrokeRound(target, control, radius, b, capturing_ ? 1.5f : 1.0f);
        b->SetColor(capturing_ ? theme.textSecondary : theme.textPrimary);
        DrawTextIn(target, fonts_.event.Get(),
                   capturing_ ? std::wstring(T(L"Pulsa el atajo…", L"Press the shortcut…"))
                              : Widen(prefs_->hotkey),
                   control, b, Align::Center);
        break;
      }
      case kLanguage:
      case kTheme:
      case kDuration: {
        // The app's view tabs, the same capsule and the same pill.
        const float capsule = (control.bottom - control.top) / 2.0f;
        b->SetColor(theme.panelOpaque);
        FillRound(target, control, capsule, b);
        b->SetColor(theme.border);
        StrokeRound(target, control, capsule, b, 1.0f);
        const int count = Options(c);
        const int selected = Selected(c);
        for (int i = 0; i < count; ++i) {
          const D2D1_RECT_F option = OptionRect(control, count, i);
          const D2D1_RECT_F pill = Inset(option, 3.0f);
          const bool on = i == selected;
          if (on) {
            b->SetColor(theme.highContrast ? theme.accent
                                           : Fade(theme.textPrimary, theme.light ? 0.08f : 0.12f));
            FillRound(target, pill, capsule - 3.0f, b);
          } else if (hover_ == c && hoverOption_ == i && hover > 0.0f) {
            b->SetColor(Fade(theme.hover, hover));
            FillRound(target, pill, capsule - 3.0f, b);
          }
          b->SetColor(on ? (theme.highContrast ? theme.onAccent : theme.textPrimary)
                         : theme.textSecondary);
          DrawTextIn(target, fonts_.event.Get(), OptionText(c, i), option, b, Align::Center);
        }
        break;
      }
      case kStartup: {
        const float t = EaseOutCubic(toggleT_);
        const float capsule = (control.bottom - control.top) / 2.0f;
        const float knob = capsule - 5.0f + t;
        if (t > 0.0f) {
          b->SetColor(Fade(theme.accent, t));
          FillRound(target, control, capsule, b);
        }
        if (t < 1.0f) {
          b->SetColor(Fade(theme.textSecondary, 1.0f - t));
          StrokeRound(target, control, capsule, b, 1.5f);
        }
        if (hover > 0.0f) {
          b->SetColor(Fade(theme.hover, hover));
          FillRound(target, control, capsule, b);
        }
        const float x = Lerp(control.left + capsule, control.right - capsule, t);
        b->SetColor(Lerp(theme.textSecondary, theme.onAccent, t));
        FillCircle(target, D2D1_POINT_2F{x, (control.top + control.bottom) / 2.0f}, knob, b);
        break;
      }
      case kCalendar: {
        b->SetColor(theme.panelOpaque);
        FillRound(target, control, radius, b);
        if (hover > 0.0f) {
          b->SetColor(Fade(theme.hover, hover));
          FillRound(target, control, radius, b);
        }
        b->SetColor(listOpen_ ? theme.accent : theme.border);
        StrokeRound(target, control, radius, b, listOpen_ ? 1.5f : 1.0f);
        const CalendarInfo* current = nullptr;
        for (const CalendarInfo& calendar : calendars_) {
          if (calendar.isDefault) current = &calendar;
        }
        if (current == nullptr && !calendars_.empty()) current = &calendars_.front();
        if (current != nullptr) {
          b->SetColor(Rgb(current->color != 0 ? current->color : 0x4A8BF5));
          FillCircle(target, D2D1_POINT_2F{control.left + 15.0f, (control.top + control.bottom) / 2.0f},
                     5.0f, b);
          b->SetColor(theme.textPrimary);
          DrawTextIn(target, fonts_.event.Get(), current->title,
                     D2D1_RECT_F{control.left + 28.0f, control.top, control.right - 30.0f,
                                 control.bottom},
                     b);
        }
        const D2D1_POINT_2F c0{control.right - 16.0f, (control.top + control.bottom) / 2.0f};
        b->SetColor(theme.textSecondary);
        target->DrawLine(D2D1_POINT_2F{c0.x - 4.0f, c0.y - 2.0f}, D2D1_POINT_2F{c0.x, c0.y + 2.0f},
                         b, 1.5f, rounded.Get());
        target->DrawLine(D2D1_POINT_2F{c0.x, c0.y + 2.0f}, D2D1_POINT_2F{c0.x + 4.0f, c0.y - 2.0f},
                         b, 1.5f, rounded.Get());
        break;
      }
      case kGoogle: {
        // The one thing to do stands out: the first connection and a lost one are the accent,
        // adding one more is quiet.
        const bool primary = configured_ && (accounts_.empty() || anyLost);
        b->SetColor(primary ? theme.accent : theme.panelOpaque);
        FillRound(target, control, radius, b);
        if (!primary) {
          b->SetColor(theme.border);
          StrokeRound(target, control, radius, b, 1.0f);
        }
        if (hover > 0.0f && configured_) {
          b->SetColor(Fade(theme.hover, hover));
          FillRound(target, control, radius, b);
        }
        b->SetColor(!configured_ ? theme.textMuted : (primary ? theme.onAccent : theme.textPrimary));
        DrawTextIn(target, fonts_.event.Get(),
                   accounts_.empty() ? T(L"Conectar…", L"Connect…")
                   : anyLost         ? T(L"Reconectar…", L"Reconnect…")
                                     : T(L"Añadir…", L"Add…"),
                   control, b, Align::Center);
        break;
      }
      default:
        break;
    }
  }

  // One card per account: its address, whether it answers, and taking it out of Agenda.
  for (int i = 0; i < layout.accounts; ++i) {
    const AccountRow& account = accounts_[static_cast<size_t>(i)];
    const D2D1_RECT_F& card = layout.accountCards[i];
    const D2D1_RECT_F& control = layout.accountControls[i];
    b->SetColor(theme.surface);
    FillRound(target, card, radius, b);
    b->SetColor(theme.border);
    StrokeRound(target, card, radius, b, 1.0f);
    const float textRight = control.left - kInsideDip;
    b->SetColor(theme.textPrimary);
    DrawTextIn(target, fonts_.event.Get(),
               account.email.empty() ? std::format(L"{} {}", T(L"Cuenta", L"Account"), account.id)
                                     : account.email,
               D2D1_RECT_F{card.left + kInsideDip, card.top + 10.0f, textRight, card.top + 32.0f}, b);
    b->SetColor(account.connected ? theme.textSecondary : theme.now);
    DrawTextIn(target, fonts_.label.Get(),
               account.connected ? T(L"Conectada", L"Connected")
                                 : T(L"Sin permiso: reconéctala arriba",
                                     L"No permission: reconnect it above"),
               D2D1_RECT_F{card.left + kInsideDip, card.top + 32.0f, textRight, card.bottom - 10.0f},
               b);
    b->SetColor(theme.panelOpaque);
    FillRound(target, control, radius, b);
    b->SetColor(theme.border);
    StrokeRound(target, control, radius, b, 1.0f);
    if (const float hover = hoverT_[kControls + i]; hover > 0.0f) {
      b->SetColor(Fade(theme.hover, hover));
      FillRound(target, control, radius, b);
    }
    b->SetColor(theme.textPrimary);
    DrawTextIn(target, fonts_.event.Get(), T(L"Quitar", L"Remove"), control, b, Align::Center);
  }

  b->SetColor(theme.textMuted);
  DrawTextIn(target, fonts_.label.Get(),
             std::format(L"Agenda {}  ·  %LOCALAPPDATA%\\Agenda", AGENDA_VERSION_TEXT),
             layout.footer, b);

  // The keyboard's place, drawn only once the keyboard has been used: a ring around every
  // control a mouse user clicks is noise.
  if (focusVisible_ && focus_ >= kControls && focus_ < Controls()) {
    b->SetColor(theme.textPrimary);
    StrokeRound(target, Inset(layout.accountControls[focus_ - kControls], -3.0f), radius + 3.0f,
                b, 2.0f);
  } else if (focusVisible_ && focus_ >= 0 && focus_ < kControls) {
    D2D1_RECT_F ring = layout.controls[focus_];
    const int count = Options(focus_);
    if (count > 0) ring = OptionRect(ring, count, (std::max)(0, Selected(focus_)));
    const float round = focus_ == kStartup || count > 0 ? (ring.bottom - ring.top) / 2.0f + 3.0f
                                                        : radius + 3.0f;
    b->SetColor(theme.textPrimary);
    StrokeRound(target, Inset(ring, -3.0f), round, b, 2.0f);
  }

  if (listOpen_ && !calendars_.empty()) {
    const D2D1_RECT_F all{ListRect(0).left, ListRect(0).top, ListRect(0).right,
                          ListRect(static_cast<int>(calendars_.size()) - 1).bottom};
    b->SetColor(theme.surface);
    FillRound(target, all, radius, b);
    b->SetColor(theme.highContrast ? theme.border : theme.accent);
    StrokeRound(target, all, radius, b, 1.0f);
    for (int i = 0; i < static_cast<int>(calendars_.size()); ++i) {
      const D2D1_RECT_F row = ListRect(i);
      if (i == listIndex_) {
        b->SetColor(theme.hover);
        FillRound(target, Inset(row, 2.0f), radius - 2.0f, b);
      }
      b->SetColor(Rgb(calendars_[i].color != 0 ? calendars_[i].color : 0x4A8BF5));
      FillCircle(target, D2D1_POINT_2F{row.left + 15.0f, (row.top + row.bottom) / 2.0f}, 5.0f, b);
      b->SetColor(theme.textPrimary);
      DrawTextIn(target, fonts_.event.Get(), calendars_[i].title,
                 D2D1_RECT_F{row.left + 28.0f, row.top, row.right - 8.0f, row.bottom}, b);
    }
  }
}

// --- Input ------------------------------------------------------------------------------------

void SettingsWindow::OnKeyDown(WPARAM key) {
  focusVisible_ = true;
  if (listOpen_) {
    const int count = static_cast<int>(calendars_.size());
    switch (key) {
      case VK_UP:
      case VK_DOWN:
        if (count > 0) listIndex_ = (listIndex_ + (key == VK_UP ? count - 1 : 1)) % count;
        break;
      case VK_RETURN:
      case VK_SPACE:
        PickCalendar(listIndex_);
        break;
      case VK_ESCAPE:
      case VK_TAB:
        listOpen_ = false;
        break;
      default:
        break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    a11y_.Changed();
    return;
  }

  const bool shift = Down(VK_SHIFT);
  switch (key) {
    case VK_TAB:
      focus_ = (focus_ + (shift ? Controls() - 1 : 1)) % Controls();
      break;
    case VK_ESCAPE:
      DestroyWindow(hwnd_);
      return;
    case VK_LEFT:
    case VK_UP:
      Step(focus_, -1);
      break;
    case VK_RIGHT:
    case VK_DOWN:
      Step(focus_, 1);
      break;
    case VK_RETURN:
    case VK_SPACE:
      Activate(focus_);
      break;
    default:
      return;
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
  a11y_.Changed();
}

void SettingsWindow::StartCapture() {
  capturing_ = true;
  hotkeyError_.clear();
  if (hooks_.pauseHotkey) hooks_.pauseHotkey(true);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::StopCapture() {
  capturing_ = false;
  if (hooks_.pauseHotkey) hooks_.pauseHotkey(false);
  if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

bool SettingsWindow::OnCapture(WPARAM key) {
  switch (key) {
    case VK_SHIFT:
    case VK_CONTROL:
    case VK_MENU:
    case VK_LWIN:
    case VK_RWIN:
      return true;  // a modifier on its own: still waiting for the key it goes with
    default:
      break;
  }
  const bool control = Down(VK_CONTROL);
  const bool alt = Down(VK_MENU);
  const bool win = Down(VK_LWIN) || Down(VK_RWIN);
  const bool shift = Down(VK_SHIFT);
  const bool modified = control || alt || win;

  if (!modified && key == VK_ESCAPE) {
    StopCapture();
    return true;
  }
  if (!modified && (key == VK_BACK || key == VK_DELETE)) {
    key = 'C';  // back to Alt+Shift+C, the one Agenda ships with
  }

  std::string text;
  if (!modified && (key == 'C')) {
    text = "Alt+Shift+C";
  } else {
    const std::string name = KeyName(static_cast<UINT>(key));
    if (!modified) {
      // Shift alone would take a capital letter away from every app on the machine.
      hotkeyError_ = T(L"Usa también Ctrl, Alt o Win", L"Add Ctrl, Alt or Win");
      InvalidateRect(hwnd_, nullptr, FALSE);
      return true;
    }
    if (name.empty()) {
      hotkeyError_ = T(L"Esa tecla no sirve para un atajo", L"That key cannot be a shortcut");
      InvalidateRect(hwnd_, nullptr, FALSE);
      return true;
    }
    if (control) text += "Ctrl+";
    if (alt) text += "Alt+";
    if (shift) text += "Shift+";
    if (win) text += "Win+";
    text += name;
  }

  capturing_ = false;
  const bool taken = hooks_.setHotkey && hooks_.setHotkey(text);
  if (taken) {
    prefs_->hotkey = text;
    SaveSetting("hotkey", text);
    hotkeyError_.clear();
    LogInfo(L"settings: el atajo es ahora {}", Widen(text));
  } else {
    const std::wstring wide = Widen(text);
    hotkeyError_ = std::vformat(T(L"Otra aplicación ya usa {}", L"Another app already uses {}"),
                                std::make_wformat_args(wide));
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
  a11y_.Changed();
  return true;
}

void SettingsWindow::Activate(int control) {
  switch (control) {
    case kHotkey:
      StartCapture();
      break;
    case kLanguage:
    case kTheme:
    case kDuration:
      Step(control, 1);
      break;
    case kStartup:
      if (!SetStartWithWindows(!startup_)) LogError(L"settings: no se pudo cambiar el arranque");
      startup_ = StartsWithWindows();
      StartTicking();
      break;
    case kCalendar:
      if (calendars_.empty()) break;
      listOpen_ = true;
      listIndex_ = 0;
      for (int i = 0; i < static_cast<int>(calendars_.size()); ++i) {
        if (calendars_[i].isDefault) listIndex_ = i;
      }
      break;
    case kGoogle:
      // Connecting, reconnecting the account that lost its permission, or adding one more: the
      // sync knows which (GoogleSync::Connect), and main asks before the browser opens.
      if (!configured_ || sync_ == nullptr) break;
      if (hooks_.connectGoogle) hooks_.connectGoogle(hwnd_);
      Refresh();
      break;
    default:
      if (control >= kControls && control < Controls()) RemoveAccount(control - kControls);
      break;
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::RemoveAccount(int index) {
  if (sync_ == nullptr || index < 0 || index >= static_cast<int>(accounts_.size())) return;
  const AccountRow account = accounts_[static_cast<size_t>(index)];
  const std::wstring name =
      account.email.empty() ? std::format(L"{} {}", T(L"Cuenta", L"Account"), account.id)
                            : account.email;
  // Asked first: what hangs from it leaves this PC, and whatever had not gone up yet is lost.
  const std::wstring question =
      std::wstring(T(L"¿Quitar ", L"Remove ")) + name +
      std::wstring(T(L" de Agenda?\n\nSus calendarios y sus eventos dejan de verse aquí y lo que "
                     L"faltara por subir se pierde. En Google no se borra nada.",
                     L" from Agenda?\n\nIts calendars and events stop showing here, and "
                     L"anything not yet sent is lost. Nothing is deleted at Google."));
  if (MessageBoxW(hwnd_, question.c_str(), T(L"Quitar cuenta", L"Remove account").data(),
                  MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
    return;
  }
  sync_->Forget(account.id);
  Refresh();
}

void SettingsWindow::Step(int control, int direction) {
  if (control == kCalendar) {
    const int count = static_cast<int>(calendars_.size());
    if (count == 0) return;
    int current = 0;
    for (int i = 0; i < count; ++i) {
      if (calendars_[i].isDefault) current = i;
    }
    PickCalendar((current + direction + count) % count);
    return;
  }
  const int count = Options(control);
  if (count == 0) return;
  Choose(control, (Selected(control) + direction + count) % count);
}

void SettingsWindow::Choose(int control, int option) {
  switch (control) {
    case kLanguage:
      prefs_->lang = option == 1 ? Lang::En : Lang::Es;
      CurrentLang() = prefs_->lang;
      SaveSetting("language", option == 1 ? "en" : "es");
      Retitle();
      break;
    case kTheme: {
      static constexpr const char* kNames[] = {"system", "dark", "light"};
      prefs_->theme = option == 1 ? L"dark" : (option == 2 ? L"light" : L"");
      SaveSetting("theme", kNames[option]);
      ApplyTheme();
      break;
    }
    case kDuration:
      prefs_->durationMin = kDurationChoices[option];
      SaveSetting("defaultDuration", prefs_->durationMin);
      break;
    default:
      return;
  }
  if (hooks_.changed) hooks_.changed();
  InvalidateRect(hwnd_, nullptr, FALSE);
  a11y_.Changed();
}

void SettingsWindow::PickCalendar(int index) {
  listOpen_ = false;
  if (index < 0 || index >= static_cast<int>(calendars_.size()) || store_ == nullptr) return;
  for (CalendarInfo& calendar : calendars_) calendar.isDefault = false;
  calendars_[static_cast<size_t>(index)].isDefault = true;
  store_->SetDefaultCalendar(calendars_[static_cast<size_t>(index)].id, /*isTask=*/false);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::OnLeftDown(float x, float y) {
  focusVisible_ = false;
  if (listOpen_) {
    for (int i = 0; i < static_cast<int>(calendars_.size()); ++i) {
      if (Inside(ListRect(i), x, y)) {
        PickCalendar(i);
        a11y_.Changed();
        return;
      }
    }
    listOpen_ = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (Inside(Layout().controls[kCalendar], x, y)) return;  // a click on it again closes it
  }
  const int control = ControlAt(x, y, static_cast<int>(accounts_.size()));
  if (capturing_ && control != kHotkey) StopCapture();
  if (control < 0) return;
  focus_ = control;
  const int count = control < kControls ? Options(control) : 0;
  if (count > 0) {
    for (int i = 0; i < count; ++i) {
      if (Inside(OptionRect(Layout().controls[control], count, i), x, y)) Choose(control, i);
    }
  } else if (!(control == kHotkey && capturing_)) {
    Activate(control);
  }
  a11y_.Changed();
}

void SettingsWindow::OnMouseMove(float x, float y) {
  if (!tracking_) {
    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
    TrackMouseEvent(&track);
    tracking_ = true;
  }
  const int control = ControlAt(x, y, static_cast<int>(accounts_.size()));
  int option = -1;
  if (control >= 0 && control < kControls && Options(control) > 0) {
    for (int i = 0; i < Options(control); ++i) {
      if (Inside(OptionRect(Layout().controls[control], Options(control), i), x, y)) option = i;
    }
  }
  if (listOpen_) {
    for (int i = 0; i < static_cast<int>(calendars_.size()); ++i) {
      if (Inside(ListRect(i), x, y) && i != listIndex_) {
        listIndex_ = i;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
    }
  }
  if (control == hover_ && option == hoverOption_) return;
  hover_ = control;
  hoverOption_ = option;
  StartTicking();
}

// --- UI Automation ---------------------------------------------------------------------------

std::vector<A11yNode> SettingsWindow::A11yNodes() {
  std::vector<A11yNode> nodes;
  const SettingsLayout& layout = Layout(static_cast<int>(accounts_.size()));
  for (int c = 0; c < kControls; ++c) {
    A11yNode node;
    node.id = c + 1;
    node.rect = layout.controls[c];
    node.focusable = true;
    node.name = std::wstring(Label(c));
    const int count = Options(c);
    switch (c) {
      case kHotkey:
        node.type = UIA_ButtonControlTypeId;
        node.name += L": " + (capturing_ ? std::wstring(T(L"Pulsa el atajo", L"Press the shortcut"))
                                         : Widen(prefs_->hotkey));
        if (!hotkeyError_.empty()) node.name += L". " + hotkeyError_;
        node.invokable = true;
        break;
      case kStartup:
        node.type = UIA_CheckBoxControlTypeId;
        node.toggle = startup_ ? 1 : 0;
        break;
      case kCalendar:
        node.type = UIA_ComboBoxControlTypeId;
        for (const CalendarInfo& calendar : calendars_) {
          if (calendar.isDefault) node.name += L": " + calendar.title;
        }
        node.invokable = true;
        break;
      case kGoogle:
        node.type = UIA_ButtonControlTypeId;
        node.name = accounts_.empty()
                        ? std::wstring(T(L"Conectar con Google", L"Connect to Google"))
                        : std::wstring(T(L"Añadir otra cuenta de Google",
                                         L"Add another Google account"));
        node.invokable = true;
        node.enabled = configured_;
        break;
      default:
        node.type = UIA_GroupControlTypeId;
        node.focusable = false;
        break;
    }
    node.focused = focus_ == c && count == 0 && !listOpen_ && GetFocus() == hwnd_;
    nodes.push_back(std::move(node));
    for (int i = 0; i < count; ++i) {
      A11yNode option;
      option.id = kOptionIds + c * 10 + i;
      option.parent = c + 1;
      option.type = UIA_RadioButtonControlTypeId;
      option.name = std::wstring(OptionText(c, i));
      option.rect = OptionRect(layout.controls[c], count, i);
      option.focusable = true;
      option.selected = i == Selected(c) ? 1 : 0;
      option.focused = focus_ == c && option.selected == 1 && GetFocus() == hwnd_;
      nodes.push_back(std::move(option));
    }
  }
  for (int i = 0; i < layout.accounts; ++i) {
    const AccountRow& account = accounts_[static_cast<size_t>(i)];
    A11yNode node;
    node.id = kControls + i + 1;
    node.rect = layout.accountControls[i];
    node.type = UIA_ButtonControlTypeId;
    node.name = std::wstring(T(L"Quitar la cuenta ", L"Remove the account ")) + account.email;
    node.invokable = true;
    node.focusable = true;
    node.focused = focus_ == kControls + i && GetFocus() == hwnd_;
    nodes.push_back(std::move(node));
  }
  if (listOpen_) {
    for (int i = 0; i < static_cast<int>(calendars_.size()); ++i) {
      A11yNode item;
      item.id = kListIds + i;
      item.parent = kCalendar + 1;
      item.type = UIA_ListItemControlTypeId;
      item.name = calendars_[static_cast<size_t>(i)].title;
      item.rect = ListRect(i);
      item.focusable = true;
      item.selected = i == listIndex_ ? 1 : 0;
      item.focused = i == listIndex_;
      nodes.push_back(std::move(item));
    }
  }
  return nodes;
}

void SettingsWindow::A11yInvoke(int id) {
  if (id >= kListIds) {
    PickCalendar(id - kListIds);
  } else if (id >= kOptionIds) {
    A11ySelect(id);
    return;
  } else {
    focus_ = id - 1;
    Activate(id - 1);
  }
  a11y_.Changed();
}

void SettingsWindow::A11ySelect(int id) {
  if (id >= kListIds) {
    PickCalendar(id - kListIds);
  } else if (id >= kOptionIds) {
    const int control = (id - kOptionIds) / 10;
    focus_ = control;
    Choose(control, (id - kOptionIds) % 10);
  }
  a11y_.Changed();
}

void SettingsWindow::A11yFocus(int id) {
  if (id >= kListIds) {
    listIndex_ = id - kListIds;
  } else {
    focus_ = id >= kOptionIds ? (id - kOptionIds) / 10 : id - 1;
  }
  focusVisible_ = true;
  InvalidateRect(hwnd_, nullptr, FALSE);
  a11y_.Changed();
}

}  // namespace agenda
