#include "ui/dock_window.h"

#include <shellapi.h>  // ABN_*
#include <shellscalingapi.h>

#include <cmath>

#include "app.h"
#include "core/log.h"
#include "system/appbar.h"

namespace dock {
namespace {

// El nombre lo usan Panel (para cerrar el dock) y el instalador: no se cambia.
constexpr wchar_t kClassName[] = L"DockWindowClass";

constexpr UINT_PTR kWatchdogTimer = 1;
constexpr UINT_PTR kHideTimer = 2;
constexpr UINT_PTR kSlideTimer = 3;

// Medidas lógicas, a 96 ppp.
constexpr float kPadding = 12;
constexpr float kBottomMargin = 8;    // solo sin autoocultar
constexpr float kRevealStrip = 3;
constexpr float kLabelRoom = 200;     // aire encima para la etiqueta y seis filas de menú
constexpr float kLabelStrip = 36;
constexpr float kRadiusInSlots = 1.75f;

// Un segundo: a 250 ms el dock de C# gastaba un 4,1% de un núcleo en reposo con tres
// pantallas. El aviso del shell al activarse una ventana reafirma al momento; el reloj es la
// red para lo que no avisa.
constexpr UINT kWatchdogMs = 1000;
constexpr UINT kHideDelayMs = 450;
// El muelle de 70 ms está al 99,9% a los 100 ms: la región se encoge después, porque
// también recorta el dibujo y encogerla antes hacía desaparecer la barra en vez de bajarla.
constexpr UINT kSlideSettleMs = 150;

// ponytail: sin iconos todavía (F4), la barra mide como 8 huecos. Se va con la curva real.
constexpr int kPlaceholderSlots = 8;

bool Same(const std::vector<RECT>& a, const std::vector<RECT>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (!EqualRect(&a[i], &b[i])) return false;
  return true;
}

}  // namespace

DockWindow::DockWindow(App& app, const Monitor& monitor, const DockConfig& config)
    : app_(app), monitor_(monitor), config_(config) {}

DockWindow::~DockWindow() {
  if (!hwnd_) return;
  KillTimer(hwnd_, kWatchdogTimer);
  KillTimer(hwnd_, kHideTimer);
  KillTimer(hwnd_, kSlideTimer);
  if (registered_) AppBarRemove(hwnd_);
  visuals_.reset();  // el target de Composition antes que su ventana
  DestroyWindow(hwnd_);
}

int DockWindow::Px(float logical) const { return static_cast<int>(std::ceil(logical * dpi_ / 96.0f)); }

bool DockWindow::Create() {
  static bool classRegistered = false;
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!classRegistered) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
      LogError(L"[dock] RegisterClassExW falló: {}", GetLastError());
      return false;
    }
    classRegistered = true;
  }

  RefreshMonitor();
  // NOACTIVATE + TOOLWINDOW: ni foco ni Alt+Tab. NOREDIRECTIONBITMAP: todo lo pinta
  // Composition, y sin él DWM reservaría un mapa de bits del tamaño de la ventana entera.
  hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
                          kClassName, L"Dock", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
  if (!hwnd_) {
    LogError(L"[dock] CreateWindowExW falló: {}", GetLastError());
    return false;
  }
  try {
    visuals_ = std::make_unique<Visuals>(hwnd_);
  } catch (const winrt::hresult_error& e) {
    LogError(L"[dock] {}: Composition falló ({:#010x})", monitor_.device, static_cast<unsigned>(e.code().value));
    return false;
  }

  registered_ = AppBarRegister(hwnd_);
  Reposition();

  // Tras un rebuild se arranca escondido, salvo que el ratón ya esté encima: si no, el dock
  // desaparecía bajo el cursor al enchufar una pantalla.
  POINT cursor{};
  GetCursorPos(&cursor);
  RECT bar = BarRect(false);
  RECT window{};
  GetWindowRect(hwnd_, &window);
  OffsetRect(&bar, window.left, window.top);
  revealed_ = !config_.autoHide || PtInRect(&bar, cursor);
  visuals_->Slide(!revealed_, /*instant=*/true);
  ApplyRegion();

  // Antes de enseñarla: esperando al vigilante, el dock tapaba un segundo el vídeo a
  // pantalla completa tras cada rebuild (medido: 11:12:55,569 creado, 56,580 apartado).
  CheckFullscreen();
  if (!fullscreen_) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  SetTimer(hwnd_, kWatchdogTimer, kWatchdogMs, nullptr);
  LogInfo(L"[dock] {} al {}%, HWND={:#x}, {},{} {}x{}, appbar={}", monitor_.device, dpi_ * 100 / 96,
          reinterpret_cast<uintptr_t>(hwnd_), window.left, window.top, width_, height_, registered_);
  return true;
}

void DockWindow::RefreshMonitor() {
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(monitor_.handle, &info)) {
    monitor_.bounds = info.rcMonitor;
    monitor_.work = info.rcWork;
  }
  // Puede fallar (0x80070006) con un monitor que ya no está. Se sigue con el último bueno:
  // un 0 acababa en un radio infinito dentro de una expresión y tumbaba el dock de C#.
  UINT x = 0, y = 0;
  if (SUCCEEDED(GetDpiForMonitor(monitor_.handle, MDT_EFFECTIVE_DPI, &x, &y)) && x > 0) dpi_ = x;
  else LogError(L"[dpi] {}: GetDpiForMonitor falló, sigo con {}", monitor_.device, dpi_);
}

void DockWindow::Reposition() {
  const int barHeight = Px(config_.iconSize + 2 * kPadding);
  width_ = monitor_.work.right - monitor_.work.left;
  height_ = static_cast<int>(std::ceil((config_.iconSize * config_.magnification + 2 * kPadding + kLabelRoom) * dpi_ / 96.0f));

  int bottom = monitor_.work.bottom;
  if (!config_.autoHide && registered_) {
    // Se reserva la barra más el margen, y el dock se apoya en lo que Windows concede.
    if (auto granted = AppBarReserve(hwnd_, monitor_.bounds, barHeight + Px(kBottomMargin))) {
      bottom = granted->bottom;
    }
    bottom -= Px(kBottomMargin);
  }
  SetWindowPos(hwnd_, HWND_TOPMOST, monitor_.work.left, bottom - height_, width_, height_, SWP_NOACTIVATE);

  // ponytail: ancho en reposo de los huecos de relleno; en F4 lo lleva una expresión.
  const float slot = static_cast<float>(config_.iconSize + config_.iconSpacing);
  visuals_->LayoutBar(static_cast<float>(width_), static_cast<float>(height_),
                      static_cast<float>(Px(kPlaceholderSlots * slot + 2 * kPadding)), static_cast<float>(barHeight));
}

RECT DockWindow::BarRect(bool tall) const {
  const float slot = static_cast<float>(config_.iconSize + config_.iconSpacing);
  const float widest = kPlaceholderSlots * slot + (config_.magnification - 1) * kRadiusInSlots * slot;
  const int half = Px(widest / 2 + kPadding) + 2;
  const int barHeight = Px(config_.iconSize + 2 * kPadding);
  int top = height_ - barHeight - Px(config_.iconSize * (config_.magnification - 1));
  if (tall) top -= Px(kLabelStrip);
  return RECT{width_ / 2 - half, top, width_ / 2 + half, height_};
}

void DockWindow::ApplyRegion() {
  // La región es lo único que deja pasar el ratón a otros procesos (HTTRANSPARENT no
  // atraviesa procesos) y además recorta el dibujo: tiene que cubrir todo lo que se pinte.
  std::vector<RECT> rects;
  if (revealed_ || sliding_) rects.push_back(BarRect(hovering_));
  if (config_.autoHide) rects.push_back(RECT{0, height_ - Px(kRevealStrip), width_, height_});
  if (Same(rects, region_)) return;
  region_ = rects;

  HRGN region = CreateRectRgn(0, 0, 0, 0);
  for (const RECT& r : rects) {
    HRGN part = CreateRectRgnIndirect(&r);
    CombineRgn(region, region, part, RGN_OR);
    DeleteObject(part);
  }
  SetWindowRgn(hwnd_, region, TRUE);  // el sistema se queda con la región
}

void DockWindow::Reveal() {
  if (revealed_) return;
  revealed_ = true;
  sliding_ = false;
  KillTimer(hwnd_, kSlideTimer);
  // La región crece ANTES de subir: si no, la barra sube recortada.
  ApplyRegion();
  visuals_->Slide(false, false);
  LogTrace(L"[visible] {}", monitor_.device);
}

void DockWindow::Hide() {
  if (!revealed_ || !config_.autoHide) return;
  revealed_ = false;
  sliding_ = true;
  visuals_->Slide(true, false);
  SetTimer(hwnd_, kSlideTimer, kSlideSettleMs, nullptr);
  LogTrace(L"[escondido] {}", monitor_.device);
}

void DockWindow::CheckFullscreen() {
  // Se decide por la ventana, no por el estado del sistema: SHQueryUserNotificationState y
  // ABN_FULLSCREENAPP dicen "ocupado" un instante al minimizar cualquier cosa, y el dock de
  // C# desaparecía. Pantalla completa = la ventana de primer plano está en ESTE monitor, no
  // tiene título ni borde redimensionable y cubre el monitor entero. IsZoomed no sirve: una
  // maximizada también cubre (con -8 px de borde) y tiene título.
  bool full = false;
  HWND fg = GetForegroundWindow();
  if (fg && fg != GetShellWindow() && MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST) == monitor_.handle) {
    wchar_t cls[32]{};
    GetClassNameW(fg, cls, 32);
    const bool desktop = wcscmp(cls, L"WorkerW") == 0 || wcscmp(cls, L"Progman") == 0;
    const LONG style = GetWindowLongW(fg, GWL_STYLE);
    RECT r{};
    GetWindowRect(fg, &r);
    const RECT& b = monitor_.bounds;
    full = !desktop && (style & WS_CAPTION) != WS_CAPTION && !(style & WS_THICKFRAME) &&
           r.left <= b.left && r.top <= b.top && r.right >= b.right && r.bottom >= b.bottom;
  }
  if (full == fullscreen_) return;
  fullscreen_ = full;
  // Esconder la ventana entera, no deslizarla: la franja de 3 px se comería los clics en la
  // barra de progreso de un vídeo.
  ShowWindow(hwnd_, full ? SW_HIDE : SW_SHOWNOACTIVATE);
  LogInfo(L"[pleno] {}: el dock {}", monitor_.device, full ? L"se aparta" : L"vuelve");
  if (!full) ReassertTopmost();
}

void DockWindow::ReassertTopmost() {
  // Windows saca a veces la ventana de la banda topmost dejando puesto el bit WS_EX_TOPMOST.
  // El primero la devuelve a la banda; el segundo la pone encima de las de su misma banda,
  // como la barra de tareas cuando asoma.
  SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void DockWindow::OnWindowActivated() {
  CheckFullscreen();
  if (!fullscreen_) ReassertTopmost();
}

void DockWindow::OnTaskbarCreated() {
  // Reiniciar explorer vacía la lista de appbars sin avisar: sin esto, la franja reservada
  // desaparece y las ventanas maximizadas tapan el dock.
  registered_ = AppBarRegister(hwnd_);
  Reposition();
  LogInfo(L"[appbar] {}: registrada de nuevo tras reiniciar explorer: {}", monitor_.device, registered_);
}

LRESULT CALLBACK DockWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  // Llega durante CreateWindowEx, antes de tener instancia: sin marco, el área cliente es la
  // ventana entera.
  if (message == WM_NCCALCSIZE) return 0;
  auto* self = reinterpret_cast<DockWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (!self || !self->hwnd_) return DefWindowProcW(hwnd, message, wparam, lparam);
  return self->Handle(message, wparam, lparam);
}

LRESULT DockWindow::Handle(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    // WS_EX_NOACTIVATE no basta: sin esto llegan WM_ACTIVATE(WA_CLICKACTIVE) y WM_SETFOCUS.
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;

    // DWM deja de pintar el acrílico en las ventanas inactivas y el dock se volvía invisible
    // (microsoft-ui-xaml#10570): se le dice que sigue activa.
    case WM_NCACTIVATE:
      return DefWindowProcW(hwnd_, message, TRUE, lparam);

    case WM_WINDOWPOSCHANGING: {
      auto* pos = reinterpret_cast<WINDOWPOS*>(lparam);
      if (!(pos->flags & SWP_NOZORDER)) pos->hwndInsertAfter = HWND_TOPMOST;
      return 0;
    }

    case WM_WINDOWPOSCHANGED:
      if (registered_) AppBarWindowPosChanged(hwnd_);
      return DefWindowProcW(hwnd_, message, wparam, lparam);

    case WM_ACTIVATE:
      if (registered_) AppBarActivate(hwnd_);
      return 0;

    case WM_NCHITTEST:
      return HTCLIENT;  // la región ya decidió; lo que llega aquí es del dock

    case WM_MOUSEMOVE:
      if (!hovering_) {
        hovering_ = true;
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&track);  // se desarma al disparar: hay que rearmarlo en cada entrada
      }
      KillTimer(hwnd_, kHideTimer);
      if (!revealed_) Reveal();
      else ApplyRegion();
      return 0;

    case WM_MOUSELEAVE:
      hovering_ = false;
      ApplyRegion();
      if (config_.autoHide) SetTimer(hwnd_, kHideTimer, kHideDelayMs, nullptr);
      return 0;

    case WM_TIMER:
      if (wparam == kWatchdogTimer) {
        CheckFullscreen();
        if (!fullscreen_) ReassertTopmost();
      } else if (wparam == kHideTimer) {
        KillTimer(hwnd_, kHideTimer);
        if (!hovering_) Hide();
      } else if (wparam == kSlideTimer) {
        KillTimer(hwnd_, kSlideTimer);
        sliding_ = false;
        ApplyRegion();
      }
      return 0;

    case kAppBarCallback:
      if (wparam == ABN_POSCHANGED) Reposition();
      // Solo como "mira ahora": el estado del sistema miente (ver CheckFullscreen).
      else if (wparam == ABN_FULLSCREENAPP) CheckFullscreen();
      return 0;

    case WM_DPICHANGED:
      // Se ignora el rectángulo sugerido: al pintar llega un 96 ppp transitorio y el dock de
      // C# encogía un 20% cada vez. Se recalcula desde el monitor.
      RefreshMonitor();
      Reposition();
      region_.clear();
      ApplyRegion();
      LogInfo(L"[dpi] {}: {}%", monitor_.device, dpi_ * 100 / 96);
      return 0;

    // Panel y el instalador cierran el dock mandando WM_CLOSE a cualquier DockWindowClass:
    // se cierra el proceso entero, no una pantalla.
    case WM_CLOSE:
      app_.Quit();
      return 0;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

}  // namespace dock
