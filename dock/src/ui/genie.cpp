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

constexpr wchar_t kFrozenClass[] = L"DockGenieFrozen";
constexpr UINT_PTR kHoldTimer = 1;
// Lo que se queda la imagen quieta después de que vuelva SW_RESTORE. Medido con Edge
// maximizado: el negro dura 1-3 fotogramas y el más largo acabó ~150 ms después de llamar
// a ShowWindow, que tarda ~35 ms en volver; 120 cubre eso con margen.
constexpr UINT kHoldMs = 120;

LRESULT CALLBACK FrozenProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_TIMER:
      DestroyWindow(hwnd);  // se va sola: el genio ya no existe cuando vence el plazo
      return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

// Resta a la región de la imagen congelada la de cada dock de este hilo: así el dock sigue vivo
// encima (la lupa no se congela y su acrílico no difumina una foto de sí mismo).
struct Cut {
  HRGN region;
  POINT origin;  // esquina de la imagen en pantalla: la región va en coordenadas de ventana
};

BOOL CALLBACK CutDock(HWND hwnd, LPARAM lparam) {
  wchar_t name[32];
  // La clase de dock_window.cpp: el genio no ve esa constante.
  if (!IsWindowVisible(hwnd) || GetClassNameW(hwnd, name, 32) == 0 || wcscmp(name, L"DockWindowClass") != 0)
    return TRUE;
  const auto& cut = *reinterpret_cast<Cut*>(lparam);
  RECT rect;
  GetWindowRect(hwnd, &rect);
  HRGN dock = CreateRectRgn(0, 0, 0, 0);
  if (GetWindowRgn(hwnd, dock) == ERROR) SetRectRgn(dock, 0, 0, rect.right - rect.left, rect.bottom - rect.top);
  OffsetRgn(dock, rect.left - cut.origin.x, rect.top - cut.origin.y);
  CombineRgn(cut.region, cut.region, dock, RGN_DIFF);
  DeleteObject(dock);
  return TRUE;
}

// Copia lo que hay en pantalla en `area` y lo deja encima como imagen quieta. Al acabar el
// genio de vuelta lo que hay ahí ES la ventana, dibujada por las miniaturas; al restaurarla,
// Chromium (Brave, Discord, Spotify...) enseña 1-3 fotogramas negros mientras pinta, y la
// miniatura también, porque es la ventana en vivo (medido: 5 de 5). La animación de Windows lo
// tapa reteniendo la ventana, pero una ventana ajena no se puede esconder (DWMWA_CLOAK da
// E_ACCESSDENIED): lo único que no cambia es una copia. Con ella: 0 de 10.
//
// ponytail: plazo fijo, no se detecta el primer fotograma bueno (una app de tema oscuro no se
// distingue de una sin pintar); y solo se recortan los docks, no otras ventanas topmost que
// estuvieran encima. Cuesta ~50 ms y ~11 MB en una pantalla de 2560x1080, que se sueltan en
// cuanto UpdateLayeredWindow ha copiado.
HWND Freeze(const RECT& area) {
  const auto start = std::chrono::steady_clock::now();
  const int width = area.right - area.left, height = area.bottom - area.top;
  if (width <= 0 || height <= 0) return nullptr;
  static bool registered = false;
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registered) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = FrozenProc;
    wc.hInstance = instance;
    wc.lpszClassName = kFrozenClass;
    registered = RegisterClassExW(&wc) != 0;
  }
  HDC screen = GetDC(nullptr);
  BITMAPINFO info{};
  info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
  void* bits = nullptr;
  HBITMAP shot = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  HDC memory = CreateCompatibleDC(screen);
  HWND frozen = nullptr;
  if (shot && memory) {
    const HGDIOBJ old = SelectObject(memory, shot);
    if (BitBlt(memory, 0, 0, width, height, screen, area.left, area.top, SRCCOPY)) {
      frozen = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
                               kFrozenClass, L"Genie", WS_POPUP, area.left, area.top, width, height, nullptr, nullptr,
                               instance, nullptr);
      POINT from{0, 0}, to{area.left, area.top};
      SIZE size{width, height};
      if (frozen && !UpdateLayeredWindow(frozen, screen, &to, &size, memory, &from, 0, nullptr, ULW_OPAQUE)) {
        DestroyWindow(frozen);
        frozen = nullptr;
      }
    }
    SelectObject(memory, old);
  }
  if (memory) DeleteDC(memory);
  if (shot) DeleteObject(shot);
  ReleaseDC(nullptr, screen);
  if (!frozen) {
    LogError(L"[genio] no se pudo congelar la imagen ({}x{}); se restaura sin ella", width, height);
    return nullptr;
  }
  Cut cut{CreateRectRgn(0, 0, width, height), POINT{area.left, area.top}};
  EnumThreadWindows(GetCurrentThreadId(), CutDock, reinterpret_cast<LPARAM>(&cut));
  SetWindowRgn(frozen, cut.region, FALSE);  // la región pasa a ser del sistema
  ShowWindow(frozen, SW_SHOWNOACTIVATE);
  DwmFlush();  // en pantalla ANTES de restaurar la de verdad
  LogInfo(L"[genio] congelada {}x{} en {:.1f} ms, fuera a los {} ms de restaurar", width, height,
           std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(), kHoldMs);
  return frozen;
}

}  // namespace

bool Genie::Play(HWND window, std::optional<RECT> known, RECT to, bool reverse, std::function<void()> done) {
  auto* genie = new Genie(window, to, reverse, std::move(done));
  if (genie->Start(known)) return true;
  genie->done_ = {};  // quien llama se entera por el false y hace la acción él
  delete genie;
  return false;
}

Genie::Genie(HWND window, RECT to, bool reverse, std::function<void()> done)
    : window_(window), to_(to), reverse_(reverse), done_(std::move(done)) {}

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
  const RECT from = from_ = Origin(known);
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
  // De vuelta, el mismo recorrido de p=1 a p=0 con el mismo ritmo, y la disolución al
  // principio: la ventana se materializa saliendo del icono.
  const float p = reverse_ ? 1 - GenieEase(t) : GenieEase(t);
  const float visible = reverse_ ? std::min(1.0f, t / static_cast<float>(1 - kFadeFrom))
                                 : (t < kFadeFrom ? 1.0f : static_cast<float>((1 - t) / (1 - kFadeFrom)));
  const BYTE opacity = static_cast<BYTE>(std::lround(255 * visible));
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
  if (t >= 1) Finish(/*played=*/true);
}

void Genie::Finish(bool played) {
  if (finished_) return;
  finished_ = true;
  clock_.Pause();
  KillTimer(overlay_, kDeadlineTimer);
  LogInfo(L"[genio] {}{} fotogramas, {} tarde (>25 ms), peor {:.1f} ms", reverse_ ? L"de vuelta, " : L"", frames_,
           late_, worst_);
  if (done_) {
    // De vuelta, la imagen quieta tapa los fotogramas negros de la ventana recién restaurada.
    // Solo si el genio acabó: cortado por el plazo, lo que hay en pantalla puede no ser la
    // ventana entera. El DwmFlush de antes deja en pantalla el último Step (p=0) para copiarlo.
    HWND frozen = nullptr;
    if (reverse_ && played) {
      DwmFlush();
      frozen = Freeze(from_);
    }
    // Primero la ventana de verdad y un fotograma compuesto, luego el desmontaje: así no hay
    // ni un fotograma sin ninguna de las dos.
    done_();
    if (frozen) SetTimer(frozen, kHoldTimer, kHoldMs, nullptr);  // cuenta desde que ya está restaurada
    DwmFlush();
  }
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
        self->Finish(/*played=*/false);
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
