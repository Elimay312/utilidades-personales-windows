#include "ui/genie.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace dock {
namespace {

constexpr wchar_t kClassName[] = L"DockGenie";
constexpr UINT kFrame = WM_APP + 1;
constexpr UINT_PTR kDeadlineTimer = 1;
constexpr int kSlices = 40;
constexpr double kDurationMs = 400;
// El plazo: si los fotogramas dejan de llegar (DWM reiniciándose, un cambio de pantallas a
// mitad), el genio se desmonta igual. La ventana ya está minimizada: no hay nada que esperar.
constexpr UINT kDeadlineMs = 700;
// El último 30 % del tiempo se disuelve: en p=1 la malla mide lo que el icono, pero sigue
// siendo la ventana entera apelmazada encima, y desaparecer de golpe se ve como un corte.
constexpr double kFadeFrom = 0.7;

}  // namespace

bool Genie::Play(HWND window, std::optional<RECT> known, RECT to) {
  auto* genie = new Genie(window, to);
  if (genie->Start(known)) return true;
  delete genie;
  return false;
}

Genie::Genie(HWND window, RECT to) : window_(window), to_(to) {}

RECT Genie::Origin(std::optional<RECT> known) const {
  // La miniatura mide lo que los bordes visibles de la ventana en su último estado (maximizada
  // o no): es la prueba de que lo recordado sigue valiendo.
  const auto fits = [&](const RECT& r) {
    return std::abs((r.right - r.left) - source_.cx) <= 2 && std::abs((r.bottom - r.top) - source_.cy) <= 2;
  };
  if (known && fits(*known)) return *known;
  WINDOWPLACEMENT placement{sizeof(placement)};
  GetWindowPlacement(window_, &placement);
  const RECT normal = placement.rcNormalPosition;
  if (placement.flags & WPF_RESTORETOMAXIMIZED) {
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromRect(&normal, MONITOR_DEFAULTTONEAREST), &info);
    LogTrace(L"[genio] origen: maximizada (área de trabajo)");
    return info.rcWork;
  }
  // ponytail: rcNormalPosition va en coordenadas del área de trabajo, que coinciden con las de
  // pantalla mientras la barra de Windows esté abajo o no esté; y una ventana encajada con
  // Win+flecha guarda aquí su sitio de ANTES de encajarse. Si eso se ve, tocará seguir sus
  // movimientos en vez de deducirlos.
  const LONG border = std::max(0L, ((normal.right - normal.left) - source_.cx) / 2);  // bordes invisibles
  LogTrace(L"[genio] origen: GetWindowPlacement ({})", known ? L"lo recordado no cuadra" : L"sin recordar");
  return RECT{normal.left + border, normal.top, normal.left + border + source_.cx, normal.top + source_.cy};
}

Genie::~Genie() {
  for (HTHUMBNAIL slice : slices_) DwmUnregisterThumbnail(slice);
}

bool Genie::Start(std::optional<RECT> known) {
  static bool registered = false;
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registered) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    registered = RegisterClassExW(&wc) != 0;
  }
  // TRANSPARENT: el ratón la atraviesa (solo vive 400 ms, pero no tiene por qué comerse un
  // clic). NOREDIRECTIONBITMAP: no pinta nada suyo, solo lleva las miniaturas.
  // Se coloca cuando se sepa de dónde sale la ventana: para eso hace falta ya una miniatura.
  overlay_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT |
                                 WS_EX_NOREDIRECTIONBITMAP,
                             kClassName, L"Genie", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, this);
  if (!overlay_) return false;
  // Al fallar el objeto lo borra Play: la ventana se desengancha antes de destruirla, o su
  // WM_NCDESTROY lo borraría dos veces.
  const auto fail = [this] {
    SetWindowLongPtrW(overlay_, GWLP_USERDATA, 0);
    DestroyWindow(overlay_);
    overlay_ = nullptr;
    return false;
  };
  for (int i = 0; i < kSlices; i++) {
    HTHUMBNAIL slice = nullptr;
    if (FAILED(DwmRegisterThumbnail(overlay_, window_, &slice))) {
      LogError(L"[genio] DwmRegisterThumbnail falló en la franja {}", i);
      return fail();
    }
    slices_.push_back(slice);
  }
  if (FAILED(DwmQueryThumbnailSourceSize(slices_.front(), &source_)) || source_.cx <= 0 || source_.cy <= 0) {
    LogError(L"[genio] la ventana no tiene tamaño de miniatura");
    return fail();
  }
  const RECT from = Origin(known);
  const auto box = [](const RECT& r) {
    return Box{static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right),
               static_cast<float>(r.bottom)};
  };
  curve_.emplace(box(from), box(to_), kSlices);
  // Lo que cubre la animación: de la ventana al icono. Una ventana de otra pantalla también:
  // el genio cruza de un monitor a otro.
  UnionRect(&area_, &from, &to_);
  SetWindowPos(overlay_, HWND_TOPMOST, area_.left, area_.top, area_.right - area_.left, area_.bottom - area_.top,
               SWP_NOACTIVATE);
  LogTrace(L"[genio] de {},{} {}x{} a {},{} {}x{}", from.left, from.top, from.right - from.left, from.bottom - from.top,
           to_.left, to_.top, to_.right - to_.left, to_.bottom - to_.top);
  start_ = lastFrame_ = std::chrono::steady_clock::now();
  Step();  // en p=0 la malla ES la ventana: aparece encima sin que se note
  ShowWindow(overlay_, SW_SHOWNOACTIVATE);
  SetTimer(overlay_, kDeadlineTimer, kDeadlineMs, nullptr);
  clock_.Run(overlay_, kFrame);
  return true;
}

void Genie::Step() {
  const auto now = std::chrono::steady_clock::now();
  const double elapsed = std::chrono::duration<double, std::milli>(now - start_).count();
  const double gap = std::chrono::duration<double, std::milli>(now - lastFrame_).count();
  lastFrame_ = now;
  if (frames_++ > 0) {
    worst_ = std::max(worst_, gap);
    if (gap > 25) late_++;
  }
  const float t = static_cast<float>(std::min(1.0, elapsed / kDurationMs));
  const float p = GenieEase(t);
  const BYTE opacity = static_cast<BYTE>(t < kFadeFrom ? 255 : std::lround(255 * (1 - t) / (1 - kFadeFrom)));
  for (int i = 0; i < kSlices; i++) {
    const auto [left, right] = curve_->HorizontalAt(i, p);
    // Bordes redondeados por su cuenta y no alto a alto: así el de abajo de una franja es
    // exactamente el de arriba de la siguiente y no aparecen rayas.
    const LONG top = std::lround(curve_->TopOf(i, p)), bottom = std::lround(curve_->BottomOf(i, p));
    DWM_THUMBNAIL_PROPERTIES props{};
    props.dwFlags = DWM_TNP_RECTSOURCE | DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY |
                    DWM_TNP_SOURCECLIENTAREAONLY;
    props.rcSource = RECT{0, source_.cy * i / kSlices, source_.cx, source_.cy * (i + 1) / kSlices};
    props.rcDestination = RECT{std::lround(left) - area_.left, top - area_.top, std::lround(right) - area_.left,
                               std::max(bottom, top + 1) - area_.top};
    props.fVisible = TRUE;
    props.opacity = opacity;
    props.fSourceClientAreaOnly = FALSE;
    DwmUpdateThumbnailProperties(slices_[i], &props);
  }
  if (t >= 1) Finish();
}

void Genie::Finish() {
  clock_.Pause();
  KillTimer(overlay_, kDeadlineTimer);
  LogTrace(L"[genio] {} fotogramas, {} tarde (>25 ms), peor {:.1f} ms", frames_, late_, worst_);
  DestroyWindow(overlay_);  // WM_NCDESTROY borra el objeto
}

LRESULT CALLBACK Genie::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto* self = reinterpret_cast<Genie*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (message) {
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case kFrame:
      if (self) {
        self->clock_.Taken();
        self->Step();
      }
      return 0;
    case WM_TIMER:
      if (self && wparam == kDeadlineTimer) {
        LogError(L"[genio] plazo cumplido sin acabar ({} fotogramas): se desmonta", self->frames_);
        self->Finish();
      }
      return 0;
    case WM_NCDESTROY:
      // Aquí y no en Finish: una ventana que se destruye por otro camino (el proceso saliendo)
      // también suelta sus miniaturas y su reloj.
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      if (self && self->overlay_ == hwnd) delete self;
      return DefWindowProcW(hwnd, message, wparam, lparam);
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace dock
