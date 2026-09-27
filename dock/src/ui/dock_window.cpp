#include "ui/dock_window.h"

#include <shellapi.h>  // ABN_*
#include <shellscalingapi.h>
#include <windowsx.h>  // GET_X_LPARAM

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

#include "app.h"
#include "core/log.h"
#include "system/appbar.h"
#include "system/drop.h"
#include "system/inventory.h"
#include "system/launch.h"
#include "ui/stack.h"

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
constexpr float kDragThreshold = 6;
constexpr float kPullOffDistance = 40;
// Lo que tarda el icono sacado en desvanecerse antes de reconstruir (Puff dura 180).
constexpr UINT kPuffMs = 200;

// Un segundo: a 250 ms el dock de C# gastaba un 4,1% de un núcleo en reposo con tres
// pantallas. El aviso del shell al activarse una ventana reafirma al momento; el reloj es la
// red para lo que no avisa.
constexpr UINT kWatchdogMs = 1000;
constexpr UINT kHideDelayMs = 450;
// El muelle de 70 ms está al 99,9% a los 100 ms: la región se encoge después, porque
// también recorta el dibujo y encogerla antes hacía desaparecer la barra en vez de bajarla.
constexpr UINT kSlideSettleMs = 150;

bool Same(const std::vector<RECT>& a, const std::vector<RECT>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (!EqualRect(&a[i], &b[i])) return false;
  return true;
}

}  // namespace

DockWindow::DockWindow(App& app, const Monitor& monitor, const DockConfig& config, ScreenApps screen)
    : app_(app), monitor_(monitor), config_(config), base_(std::move(screen.base)), apps_(std::move(screen.apps)) {
  Compose();
}

int DockWindow::DraggableEnd() const {
  for (size_t i = 0; i < drawn_.size(); i++)
    if (drawn_[i].separator && drawn_[i].name == L"|abiertas|") return static_cast<int>(i);
  return static_cast<int>(drawn_.size());
}

bool DockWindow::PulledOff(int y) const {
  // Bastante por encima de la barra: el icono se está sacando del dock.
  const int barTop = height_ - Px(config_.iconSize + 2 * kPadding);
  return y < barTop - Px(kPullOffDistance);
}

// Lo anclado se mueve entre lo anclado. Una app abierta sin anclar se mueve desde su sitio
// hacia la izquierda, pasando por el separador: soltarla antes de él la ancla ahí, como en
// macOS. Por eso su orden abarca todo lo que hay hasta ella, separador incluido.
int DockWindow::DragLast() const { return pressedIndex_ > DraggableEnd() ? pressedIndex_ : DraggableEnd() - 1; }

void DockWindow::OnDragMove(int x, int y) {
  if (!dragging_) {
    // El umbral mira las DOS direcciones: mirando solo la X, sacar un icono tirando recto
    // hacia arriba no llegaba a contar nunca como arrastre.
    if (std::max(std::abs(x - press_.x), std::abs(y - press_.y)) < Px(kDragThreshold)) return;
    dragging_ = true;
    dragOrder_.clear();
    for (int i = 0; i <= DragLast(); i++) dragOrder_.push_back(i);
    visuals_->SetLabel(-1);
    visuals_->SetLifted(pressedIndex_, true);
    // Ahora sí: el ratón aunque salga de la ventana, para poder sacarlo hacia arriba.
    SetCapture(hwnd_);
    LogTrace(L"[arrastre] empieza '{}'", drawn_[pressedIndex_].name);
  }
  // Donde va el dedo, sin muelle: interpolar aquí solo añadiría retraso.
  visuals_->SetShift(pressedIndex_, static_cast<float>(x - press_.x));
  if (PulledOff(y)) return;  // arriba, fuera del dock: nadie hace hueco
  const int over = std::clamp(curve_.SlotAt(lastRest_), 0, DragLast());
  const auto from = std::find(dragOrder_.begin(), dragOrder_.end(), pressedIndex_);
  if (curve_.SlotAt(lastRest_) < 0 || from - dragOrder_.begin() == over) return;
  dragOrder_.erase(from);
  dragOrder_.insert(dragOrder_.begin() + over, pressedIndex_);
  ApplyDragShifts();
}

// Cada icono al sitio que le toca con el orden de ahora. En píxeles de PANTALLA (Project), no
// en reposo: bajo la lupa una ranura mide casi el doble y los vecinos se apartarían poco.
// ponytail: se recalcula al cambiar el orden, no en cada movimiento, así que entre dos
// permutaciones la lupa lo desvía un poco; menos que relanzar N muelles por píxel.
void DockWindow::ApplyDragShifts() {
  const float width = static_cast<float>(width_);
  for (size_t position = 0; position < dragOrder_.size(); position++) {
    const int index = dragOrder_[position];
    if (index == pressedIndex_) continue;
    const float now = curve_.Project(curve_.RestLeft(index), width, lastRest_);
    const float target = curve_.Project(curve_.RestLeft(static_cast<int>(position)), width, lastRest_);
    visuals_->SpringShift(index, target - now);
  }
}

std::vector<DockApp> DockWindow::WithTrash(std::vector<DockApp> head) const {
  // Lo abierto sin anclar nunca se guarda: reordenar o quitar anclaría todo lo abierto.
  for (size_t i = DraggableEnd(); i < drawn_.size(); i++)
    if (!drawn_[i].separator && drawn_[i].target == kTrashTarget) head.push_back(drawn_[i]);
  return head;
}

void DockWindow::FinishDrag(int y) {
  const int dragged = pressedIndex_;
  if (dragged > DraggableEnd()) {
    // Una abierta sin anclar: se ancla solo si se soltó antes del separador. Tirada hacia
    // arriba o devuelta a las abiertas, vuelve a su sitio (no hay nada que quitar).
    const auto at = std::find(dragOrder_.begin(), dragOrder_.end(), dragged);
    const auto separator = std::find(dragOrder_.begin(), dragOrder_.end(), DraggableEnd());
    if (PulledOff(y) || at > separator) {
      CancelDrag();
      return;
    }
    dragging_ = false;
    pressedIndex_ = -1;
    std::vector<DockApp> pinned;
    for (auto it = dragOrder_.begin(); it != separator; ++it) pinned.push_back(drawn_[*it]);
    LogInfo(L"[dock] anclada '{}' arrastrándola, en el puesto {}", drawn_[dragged].name, at - dragOrder_.begin() + 1);
    app_.SaveAndReload(monitor_.device, base_, WithTrash(std::move(pinned)), 1);
    return;
  }
  dragging_ = false;
  pressedIndex_ = -1;
  std::vector<DockApp> order;
  for (int index : dragOrder_) order.push_back(drawn_[index]);
  std::vector<DockApp> pinned = WithTrash(std::move(order));
  const bool removed = PulledOff(y);
  if (removed) {
    visuals_->Puff(dragged);
    pinned.erase(pinned.begin() + (std::find(dragOrder_.begin(), dragOrder_.end(), dragged) - dragOrder_.begin()));
    LogInfo(L"[dock] quitada '{}'", drawn_[dragged].name);
  } else {
    LogInfo(L"[dock] reordenado '{}'", drawn_[dragged].name);
  }
  // Se guarda y se recarga como cualquier cambio de dock.local.json: así se enteran también
  // los docks de las otras pantallas. Si se ha quitado uno, se le deja acabar de desvanecerse.
  app_.SaveAndReload(monitor_.device, base_, pinned, removed ? kPuffMs : 1);
}

namespace {

constexpr UINT kJumpsReady = WM_APP + 2;
constexpr UINT kStackReady = WM_APP + 3;
constexpr size_t kMenuJumpLimit = 4;

struct StackResult {
  int index;
  std::wstring folder;
  StackContents contents;
};

struct JumpResult {
  int index;
  std::wstring appId;
  std::vector<JumpItem> items;
};

std::wstring Shorten(const std::wstring& text, size_t limit) {
  return text.size() <= limit ? text : text.substr(0, limit - 1) + L"…";
}

}  // namespace

void DockWindow::OpenContextMenu() {
  if (visuals_->MenuOpen()) {  // el segundo clic derecho lo cierra
    CloseMenu();
    return;
  }
  const int index = curve_.SlotAt(lastRest_);
  // Fuera de un icono solo hay "Salir": a la derecha del último, Invert satura y el índice
  // es -1, y componer "Quitar..." con él tumbaba el dock de C# (ArgumentOutOfRange).
  menuIndex_ = index >= 0 && index < static_cast<int>(drawn_.size()) && !drawn_[index].separator ? index : -1;
  menuJumps_.clear();
  ShowMenu();
  if (menuIndex_ < 0) return;

  // Los recientes llegan después: el menú no espera al shell. Se pregunta por UNA app, la
  // del icono, y solo desde este gesto.
  const DockApp& app = drawn_[menuIndex_];
  const HWND window = menuIndex_ < static_cast<int>(windows_.size()) && !windows_[menuIndex_].empty()
                          ? windows_[menuIndex_].front()
                          : nullptr;
  const auto appId = AppIdOf(app, window);
  if (!appId) {
    LogTrace(L"[saltos] '{}' sin AppID", app.name);
    return;
  }
  const HWND self = hwnd_;
  app_.PostJob([self, index = menuIndex_, id = *appId] {
    auto* result = new JumpResult{index, id, ReadJumpList(id, kMenuJumpLimit)};
    if (!PostMessageW(self, kJumpsReady, 0, reinterpret_cast<LPARAM>(result))) delete result;
  });
}

void DockWindow::ShowMenu() {
  std::vector<std::wstring> items;
  if (menuIndex_ >= 0) {
    const DockApp& app = drawn_[menuIndex_];
    for (const JumpItem& jump : menuJumps_) items.push_back(Shorten(jump.name, 34));
    // Una app que solo está abierta no se puede quitar: se ancla.
    const bool extra = menuIndex_ > DraggableEnd() && app.target != kTrashTarget;
    items.push_back(std::format(L"{} '{}' {}", extra ? L"Anclar" : L"Quitar", Shorten(app.name, 34),
                                extra ? L"al dock" : L"del dock"));
  }
  items.push_back(L"Salir del dock");
  PlaceMenu(items, menuIndex_, false);
}

void DockWindow::PlaceMenu(const std::vector<std::wstring>& items, int index, bool closable) {
  const float width = static_cast<float>(width_);
  const float anchor =
      index >= 0 ? curve_.Project((curve_.RestLeft(index) + curve_.RestRight(index)) / 2, width, lastRest_) : width / 2;
  // Dentro de la ventana; la región se amplía para cubrirlo (ApplyRegion).
  const float pad = kPadding * dpi_ / 96.0f;
  visuals_->SetLabel(-1);  // la etiqueta ocupa el mismo hueco
  visuals_->OpenMenu(items, anchor, static_cast<float>(height_ - Px(config_.iconSize + 2 * kPadding)), pad, width - pad,
                     dpi_ / 96.0f, closable);
  ApplyRegion();  // el menú sube por encima de lo que la región deja pasar
  const RECT menu = visuals_->MenuRect();
  LogTrace(L"[menu] {} filas, x={}..{}, techo y={} (cliente, {}%)", items.size(), menu.left, menu.right, menu.top,
           dpi_ * 100 / 96);
}

void DockWindow::CloseMenu() {
  HidePreview();
  wheelIndex_ = -1;
  wheelWindows_.clear();
  if (!visuals_->MenuOpen()) return;
  visuals_->CloseMenu();
  menuIndex_ = -1;
  menuJumps_.clear();
  ApplyRegion();
}

void DockWindow::OnMenuChoice(int choice) {
  if (wheelIndex_ >= 0) {
    // En la lista de la rueda cada fila es una ventana, y el clic da el permiso de traerla.
    const int at = choice + wheelFirst_;
    const HWND window = at >= 0 && at < static_cast<int>(wheelWindows_.size()) ? wheelWindows_[at] : nullptr;
    const int total = static_cast<int>(wheelWindows_.size());
    CloseMenu();
    if (!window) return;
    BringToFront(window);
    LogInfo(L"[rueda] al frente la ventana {} de {}", at + 1, total);
    return;
  }
  const int index = menuIndex_;
  const std::vector<JumpItem> jumps = menuJumps_;
  CloseMenu();
  if (index >= 0 && choice < static_cast<int>(jumps.size())) {
    OpenWithDetached(drawn_[index], {jumps[choice].path});
    LogInfo(L"[saltos] abriendo '{}' con '{}'", jumps[choice].name, drawn_[index].name);
    return;
  }
  // La última entrada siempre es salir, tenga el menú una fila o seis.
  const int rest = choice - static_cast<int>(jumps.size());
  if (index < 0 || rest == 1) {
    app_.Quit();
    return;
  }
  const DockApp app = drawn_[index];
  std::vector<DockApp> head(drawn_.begin(), drawn_.begin() + DraggableEnd());
  if (index > DraggableEnd()) {
    head.push_back(app);  // anclada: pasa a lo anclado, delante de la papelera
    LogInfo(L"[dock] anclada '{}'", app.name);
    app_.SaveAndReload(monitor_.device, base_, WithTrash(std::move(head)), 1);
    return;
  }
  visuals_->Puff(index);
  std::vector<DockApp> pinned = WithTrash(std::move(head));
  pinned.erase(std::find_if(pinned.begin(), pinned.end(), [&](const DockApp& a) {
    return a.separator == app.separator && a.target == app.target && a.name == app.name;
  }));
  LogInfo(L"[dock] quitada '{}'", app.name);
  app_.SaveAndReload(monitor_.device, base_, pinned, kPuffMs);
}

void DockWindow::CancelDrag() {
  const bool was = dragging_;
  dragging_ = false;
  if (was) {
    visuals_->SetLifted(pressedIndex_, false);
    for (size_t i = 0; i < drawn_.size(); i++) visuals_->SpringShift(static_cast<int>(i), 0);
    if (GetCapture() == hwnd_) ReleaseCapture();
    LogTrace(L"[arrastre] cancelado");
  }
  pressedIndex_ = -1;
}

namespace {

constexpr wchar_t kPreviewClass[] = L"DockPreview";
// Lo que cabe de miniatura, en lógicas: la ventana entra por el lado que le apriete.
constexpr float kPreviewWidth = 300;
constexpr float kPreviewHeight = 190;
constexpr float kPreviewPad = 8;

std::wstring TitleOf(HWND window) {
  wchar_t text[256]{};
  return GetWindowTextW(window, text, 256) > 0 ? std::wstring(text) : std::wstring(L"(sin título)");
}

LRESULT CALLBACK PreviewProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;  // como el dock: nunca el foco
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

void DockWindow::OnWheel(int delta, POINT screen) {
  // lParam de WM_MOUSEWHEEL viene en coordenadas de PANTALLA, no de cliente.
  RECT window{};
  GetWindowRect(hwnd_, &window);
  const int index = curve_.SlotAt(curve_.Invert(static_cast<float>(screen.x - window.left), static_cast<float>(width_)));
  if (index < 0 || index >= static_cast<int>(windows_.size()) || windows_[index].size() < 2) return;
  if (wheelIndex_ != index || !visuals_->MenuOpen()) {
    CloseMenu();
    wheelIndex_ = index;
    wheelWindows_ = windows_[index];
    wheelAt_ = 0;
    wheelFirst_ = -1;
  } else {
    // Hacia arriba sube por la lista, que se lee de arriba abajo; da la vuelta en los extremos.
    const int count = static_cast<int>(wheelWindows_.size());
    wheelAt_ = ((wheelAt_ + (delta > 0 ? -1 : 1)) % count + count) % count;
  }
  ShowWheelList();
  LogInfo(L"[rueda] '{}' ventana {} de {}", drawn_[index].name, wheelAt_ + 1, wheelWindows_.size());
}

void DockWindow::ShowWheelList() {
  const int total = static_cast<int>(wheelWindows_.size());
  const int fit = std::min(
      total, Visuals::RowsThatFit(static_cast<float>(height_ - Px(config_.iconSize + 2 * kPadding)), dpi_ / 96.0f));
  const int first = std::clamp(wheelAt_ - fit / 2, 0, total - fit);
  if (first != wheelFirst_ || !visuals_->MenuOpen()) {
    wheelFirst_ = first;
    std::vector<std::wstring> items;
    for (int i = first; i < first + fit; i++) items.push_back(Shorten(TitleOf(wheelWindows_[i]), 40));
    PlaceMenu(items, wheelIndex_, true);
  }
  visuals_->MenuSetHot(wheelAt_ - wheelFirst_);
  ShowPreview();
}

// Clic en el ICONO con la lista abierta: a la ventana que dejó elegida la rueda, como un clic
// normal. Sin esto había que bajar a pinchar el título, que es el viaje que la rueda ahorra.
void DockWindow::OnWheelPick() {
  const HWND window = wheelWindows_[wheelAt_];
  const int index = wheelIndex_;
  const std::wstring trace = std::format(L"'{}' ({} de {})", drawn_[index].name, wheelAt_ + 1, wheelWindows_.size());
  CloseMenu();  // antes de actuar: la lista tapa el icono
  const HWND foreground = app_.ForeignForeground();
  if (window == foreground || window == GetAncestor(foreground, GA_ROOT)) {
    Minimize(window);
    LogInfo(L"[dock] minimizada {}", trace);
    return;
  }
  visuals_->Bounce(index, config_.iconSize * dpi_ / 96.0f * 0.35f, false);
  BringToFront(window);
  LogInfo(L"[dock] al frente {}", trace);
}

// WM_CLOSE, el mismo mensaje que la X de la propia ventana: la app puede preguntar si guardar,
// y entonces contesta el usuario. Nunca se mata un proceso.
void DockWindow::OnCloseWindow(int row) {
  const int at = row + wheelFirst_;
  if (at < 0 || at >= static_cast<int>(wheelWindows_.size())) return;
  PostMessageW(wheelWindows_[at], WM_CLOSE, 0, 0);
  LogInfo(L"[cerrar] pedida la ventana {} de {}", at + 1, wheelWindows_.size());
  // La fila se va ya aunque la app tarde o acabe negándose (la siguiente vuelta de rueda la
  // vuelve a enseñar): esperar dejaría una fila muerta durante todo el "¿guardar cambios?".
  wheelWindows_.erase(wheelWindows_.begin() + at);
  if (wheelWindows_.size() < 2) {
    CloseMenu();
    return;
  }
  wheelAt_ = std::min(at, static_cast<int>(wheelWindows_.size()) - 1);
  wheelFirst_ = -1;
  ShowWheelList();
}

// Miniatura DWM, no captura: es en vivo, sale también de una minimizada (medido en F1) y no
// copia la ventana. El de C# la capturaba con PrintWindow cada 250 ms: +27 MB con la lista
// abierta. DWM no pinta miniaturas dentro de un visual de Composition, así que va en una
// ventana propia, encima del menú.
void DockWindow::ShowPreview() {
  const HWND source = wheelWindows_[wheelAt_];
  if (!preview_) {
    static bool registered = false;
    if (!registered) {
      WNDCLASSEXW wc{sizeof(wc)};
      wc.lpfnWndProc = PreviewProc;
      wc.hInstance = GetModuleHandleW(nullptr);
      wc.hbrBackground = CreateSolidBrush(RGB(32, 32, 38));  // el fondo del menú
      wc.lpszClassName = kPreviewClass;
      registered = RegisterClassExW(&wc) != 0;
    }
    // Propiedad del dock: una ventana con dueño va siempre por encima de él.
    preview_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, kPreviewClass, L"", WS_POPUP, 0, 0, 1,
                               1, hwnd_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!preview_) return;
    const DWM_WINDOW_CORNER_PREFERENCE round = DWMWCP_ROUND;
    DwmSetWindowAttribute(preview_, DWMWA_WINDOW_CORNER_PREFERENCE, &round, sizeof(round));
  }
  if (source != thumbSource_) {
    if (thumb_) DwmUnregisterThumbnail(thumb_);
    thumb_ = nullptr;
    thumbSource_ = nullptr;
    if (const HRESULT hr = DwmRegisterThumbnail(preview_, source, &thumb_); FAILED(hr)) {
      LogError(L"[miniatura] DwmRegisterThumbnail falló ({:#010x})", static_cast<unsigned>(hr));
      HidePreview();
      return;
    }
    thumbSource_ = source;
  }
  SIZE size{};
  if (FAILED(DwmQueryThumbnailSourceSize(thumb_, &size)) || size.cx <= 0 || size.cy <= 0) {
    HidePreview();  // mejor sin miniatura que con la de otra ventana
    return;
  }
  const float s = dpi_ / 96.0f;
  const float fit = std::min(kPreviewWidth * s / size.cx, kPreviewHeight * s / size.cy);
  const int w = static_cast<int>(size.cx * fit), h = static_cast<int>(size.cy * fit);
  const int pad = Px(kPreviewPad);
  RECT window{};
  GetWindowRect(hwnd_, &window);
  const RECT menu = visuals_->MenuRect();
  const RECT& screen = monitor_.bounds;
  const int outerW = w + 2 * pad, outerH = h + 2 * pad;
  const int x = std::clamp(static_cast<int>(window.left + (menu.left + menu.right) / 2 - outerW / 2), static_cast<int>(screen.left),
                           static_cast<int>(screen.right - outerW));
  const int y = std::max(static_cast<int>(screen.top), static_cast<int>(window.top + menu.top - pad - outerH));
  SetWindowPos(preview_, HWND_TOPMOST, x, y, outerW, outerH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  DWM_THUMBNAIL_PROPERTIES props{};
  props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
  props.rcDestination = RECT{pad, pad, pad + w, pad + h};
  props.fVisible = TRUE;
  props.opacity = 255;
  props.fSourceClientAreaOnly = FALSE;
  DwmUpdateThumbnailProperties(thumb_, &props);
  LogTrace(L"[miniatura] {}x{} -> {}x{} en {},{}", size.cx, size.cy, w, h, x, y);
}

// Se destruye, no se esconde: la ventana y el registro de DWM se liberan con la lista cerrada.
void DockWindow::HidePreview() {
  if (thumb_) DwmUnregisterThumbnail(thumb_);
  thumb_ = nullptr;
  thumbSource_ = nullptr;
  if (preview_) DestroyWindow(preview_);
  preview_ = nullptr;
}

bool DockWindow::DropOver(POINT screen) {
  const bool entering = !dropping_;
  if (entering) {
    dropping_ = true;
    // Con algo arrastrado encima el dock tiene que estar a la vista, o no habría dónde
    // soltarlo. Durante el arrastre OLE no llega WM_MOUSEMOVE: la lupa se mueve desde aquí.
    KillTimer(hwnd_, kHideTimer);
    CloseMenu();
    Reveal();
    if (!hoverShown_) {
      hoverShown_ = true;
      visuals_->SetHover(true);
    }
  }
  visuals_->SetAddZone(true);  // no-op si ya está; si una app abierta rehízo la barra, vuelve
  ApplyRegion();
  if (curve_.Count() == 0) return false;
  RECT window{};
  GetWindowRect(hwnd_, &window);
  const float width = static_cast<float>(width_);
  const float x = static_cast<float>(screen.x - window.left);
  lastRest_ = curve_.Invert(x, width);
  visuals_->SetCursor(lastRest_);
  // A la derecha del borde de la barra solo está el "+" (la región no llega más allá). Solo
  // él añade: el de C# añadía en cualquier hueco, y soltar sobre la papelera anclaba el fichero.
  const bool add = x > curve_.Project(curve_.RestWidth(), width, lastRest_) + kPadding * dpi_ / 96.0f;
  const int slot = curve_.SlotAt(lastRest_);
  const bool valid = !add && slot >= 0 && slot < static_cast<int>(drawn_.size());
  const bool trash = valid && !drawn_[slot].separator && drawn_[slot].target == kTrashTarget;
  const int target = valid && (trash || IsApp(drawn_[slot])) ? slot : -1;
  visuals_->SetDropTarget(target, config_.iconSize * dpi_ / 96.0f * 0.25f);
  visuals_->SetAddZoneHot(add);
  visuals_->SetLabel(target);
  if (entering || target != dropSlot_ || add != dropAdd_)
    LogTrace(L"[soltar] {}", trash    ? std::wstring(L"a la papelera")
                             : target >= 0 ? std::format(L"abrir con '{}'", drawn_[target].name)
                             : add    ? std::wstring(L"añadir al dock")
                                      : std::wstring(L"nada"));
  dropSlot_ = target;
  dropAdd_ = add;
  return target >= 0 || add;
}

void DockWindow::DropLeave() {
  if (!dropping_) return;
  dropping_ = false;
  dropSlot_ = -1;
  dropAdd_ = false;
  visuals_->SetDropTarget(-1, 0);
  visuals_->SetAddZone(false);
  visuals_->SetLabel(-1);
  if (!hovering_ && hoverShown_) {
    hoverShown_ = false;
    visuals_->SetHover(false);
  }
  ApplyRegion();
  if (config_.autoHide && !hovering_) SetTimer(hwnd_, kHideTimer, kHideDelayMs, nullptr);
}

void DockWindow::OnDropped(std::vector<Dropped> items) {
  const int slot = dropSlot_;
  const bool add = dropAdd_;
  DropLeave();
  if (items.empty()) return;
  const float bounce = config_.iconSize * dpi_ / 96.0f * 0.35f;

  if (slot >= 0) {
    // Solo lo que tiene fichero: a una app no se le puede pasar un objeto virtual, y una app
    // de la Store arrastrada desde Inicio no es algo que tirar a la papelera.
    std::vector<std::wstring> paths;
    for (const Dropped& item : items)
      if (!item.path.empty()) paths.push_back(item.path);
    if (paths.empty()) return;
    LogTrace(L"[soltar] {} fichero(s) sobre '{}': {}", paths.size(), drawn_[slot].name, paths.front());
    visuals_->Bounce(slot, bounce, false);
    // Los dos en su hilo: ShellExecuteEx puede tardar segundos, y la papelera puede preguntar.
    if (drawn_[slot].target == kTrashTarget) RecycleDetached(paths);
    else OpenWithDetached(drawn_[slot], paths);
    return;
  }
  if (!add) return;

  // Al final de lo anclado, delante de la papelera si la hay.
  std::vector<DockApp> pinned(drawn_.begin(), drawn_.begin() + DraggableEnd());
  bool changed = false;
  for (const Dropped& item : items) {
    // Por el mismo filtro que dock.json: un target que no existe quedaría en dock.local.json
    // para siempre y desaparecería en cada arranque sin decir por qué.
    const std::vector<DockApp> valid = Validate({item.app});
    if (valid.empty()) continue;
    const DockApp& app = valid.front();
    const auto same = std::find_if(pinned.begin(), pinned.end(), [&](const DockApp& a) {
      return !a.separator && _wcsicmp(a.target.c_str(), app.target.c_str()) == 0;
    });
    if (same != pinned.end()) {
      // Ya estaba: bota el que hay en vez de duplicarlo. Más barato que un diálogo y se entiende.
      const int index = static_cast<int>(same - pinned.begin());
      if (index < DraggableEnd()) visuals_->Bounce(index, bounce, false);
      LogInfo(L"[dock] '{}' ya estaba en el dock", app.name);
      continue;
    }
    auto at = pinned.end();
    if (!pinned.empty() && !pinned.back().separator && pinned.back().target == kTrashTarget) --at;
    pinned.insert(at, app);
    LogInfo(L"[dock] añadida '{}' -> {}", app.name, app.target);
    changed = true;
  }
  if (changed) app_.SaveAndReload(monitor_.device, base_, WithTrash(std::move(pinned)), 1);
}

int DockWindow::IconPx() const {
  return static_cast<int>(std::ceil(config_.iconSize * dpi_ / 96.0f * config_.magnification));
}

// Lo anclado, un separador, lo abierto sin anclar, y la papelera siempre la última, como en
// macOS. El separador de las abiertas no es del usuario: nunca se guarda.
void DockWindow::Compose() {
  if (extras_.empty()) {
    drawn_ = apps_;
    return;
  }
  drawn_.clear();
  std::optional<DockApp> trash;
  for (const DockApp& app : apps_) {
    if (!app.separator && app.target == kTrashTarget) trash = app;
    else drawn_.push_back(app);
  }
  drawn_.push_back(DockApp{L"|abiertas|", L"", L"", L"", true});
  drawn_.insert(drawn_.end(), extras_.begin(), extras_.end());
  if (trash) drawn_.push_back(*trash);
}

void DockWindow::OnClick(bool middle) {
  const int index = curve_.SlotAt(lastRest_);
  if (index < 0 || index >= static_cast<int>(drawn_.size()) || drawn_[index].separator) return;
  const DockApp& app = drawn_[index];
  const float bounce = config_.iconSize * dpi_ / 96.0f * 0.35f;
  const std::vector<HWND> none;
  const std::vector<HWND>& windows = index < static_cast<int>(windows_.size()) ? windows_[index] : none;
  const HWND foreground = app_.ForeignForeground();
  LogTrace(L"[clic] '{}' {} ventanas={} primerPlano={:#x}", app.name, middle ? L"central" : L"izquierdo", windows.size(),
           reinterpret_cast<uintptr_t>(foreground));
  // Cualquier clic en el dock cierra el stack abierto; el de su propia carpeta, además, no lo
  // vuelve a abrir.
  const bool stackWasOpen = stack_ && stackFolder_ == app.target;
  stack_.reset();
  stackFolder_.clear();

  // Clic central: una instancia NUEVA aunque ya haya ventana, como la barra de Windows.
  if (middle) {
    if (!IsApp(app)) return;
    LaunchDetached(app);
    visuals_->Bounce(index, bounce, false);
    return;
  }

  // Una carpeta (o la papelera) se despliega en rejilla en vez de abrir el Explorador; el
  // mismo clic la cierra.
  const DWORD attributes = IsUrl(app.target) ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(app.target.c_str());
  if (app.target == kTrashTarget || (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))) {
    if (stackWasOpen) return;
    visuals_->Bounce(index, bounce * 0.7f, false);
    const HWND self = hwnd_;
    app_.PostJob([self, index, folder = app.target] {
      auto* result = new StackResult{index, folder, ReadStack(folder, L"")};
      if (!PostMessageW(self, kStackReady, 0, reinterpret_cast<LPARAM>(result))) delete result;
    });
    return;
  }

  if (!windows.empty()) {
    // Como la barra de Windows: si una de sus ventanas tiene el foco, se minimiza; si no,
    // la de más arriba (EnumWindows va en orden Z) viene al frente, esté minimizada o solo
    // tapada. El de C# minimizaba lo que "se veía" aunque no tuviera el foco, y enfocar una
    // ventana visible costaba dos clics.
    const HWND root = GetAncestor(foreground, GA_ROOT);
    for (HWND window : windows) {
      if (window == foreground || window == root) {
        Minimize(window);
        LogInfo(L"[dock] minimizada '{}'", app.name);
        return;
      }
    }
    visuals_->Bounce(index, bounce, false);  // acuse de recibo, ya mismo
    BringToFront(windows.front());
    LogInfo(L"[dock] al frente '{}'", app.name);
    return;
  }

  LaunchDetached(app);
  if (IsApp(app)) {
    // Bota hasta que la app abra una ventana: es el único aviso de que el clic llegó cuando
    // una app tarda en arrancar.
    visuals_->Bounce(index, bounce, true);
    launchingTarget_ = app.target;
    launchingUntil_ = GetTickCount64() + 20000;
  } else {
    visuals_->Bounce(index, bounce * 0.7f, false);  // un documento o una URL
  }
}

bool DockWindow::UpdateRunning(Snapshot& snapshot, bool showRunning) {
  // Las abiertas sin anclar salen de las ANCLADAS, no de lo dibujado: si no, las de la vuelta
  // anterior contarían como presentes y la lista no menguaría nunca.
  std::vector<DockApp> extras = showRunning ? UnpinnedApps(apps_, snapshot) : std::vector<DockApp>{};
  const bool changed = extras.size() != extras_.size() ||
                       !std::equal(extras.begin(), extras.end(), extras_.begin(), [](const DockApp& a, const DockApp& b) {
                         return _wcsicmp(a.target.c_str(), b.target.c_str()) == 0;
                       });
  if (changed) {
    // Rehacer la barra es caro y se ve: solo cuando cambia el juego de apps abiertas.
    extras_ = std::move(extras);
    Compose();
    BuildVisuals({});
  }

  const std::vector<AppState> states = CheckApps(drawn_, snapshot);
  running_.assign(states.size(), false);
  windows_.assign(states.size(), {});
  std::wstring trace;
  for (size_t i = 0; i < states.size(); i++) {
    running_[i] = states[i].Open();
    windows_[i] = states[i].windows;
    // El icono de la app que se abría deja de botar en cuanto aparece su ventana.
    if (running_[i] && !launchingTarget_.empty() && _wcsicmp(drawn_[i].target.c_str(), launchingTarget_.c_str()) == 0) {
      visuals_->StopBounce(static_cast<int>(i));
      launchingTarget_.clear();
      LogInfo(L"[dock] '{}' ya tiene ventana", drawn_[i].name);
    }
    if (running_[i]) trace += std::format(L"{}{}:{}", trace.empty() ? L"" : L" ", drawn_[i].name, states[i].windows.size());
  }
  visuals_->SetRunning(running_);
  if (!launchingTarget_.empty() && GetTickCount64() > launchingUntil_) {
    for (size_t i = 0; i < drawn_.size(); i++)
      if (_wcsicmp(drawn_[i].target.c_str(), launchingTarget_.c_str()) == 0) visuals_->StopBounce(static_cast<int>(i));
    launchingTarget_.clear();
  }
  // Solo cuando cambia: es la señal de texto de que los avisos del shell llegan.
  if (trace != lastRunningTrace_) {
    lastRunningTrace_ = trace;
    LogInfo(L"[abiertas] {}: {}", monitor_.device, trace.empty() ? L"ninguna" : trace);
  }
  UpdateSmartHide();
  return changed;
}

RECT DockWindow::BarOnScreen() const {
  RECT bar = BarRect(false);
  RECT window{};
  GetWindowRect(hwnd_, &window);
  OffsetRect(&bar, window.left, window.top);
  return bar;
}

// Sin nada debajo no hay de qué esconderse: con el escritorio a la vista el dock se queda.
// Se reevalúa en cada barrido, porque abrir, cerrar o activar ventanas es justo cuando cambia.
void DockWindow::UpdateSmartHide() {
  if (!config_.autoHide || hovering_ || dragging_ || dropping_ || stack_ || fullscreen_ || visuals_->MenuOpen()) return;
  const bool covered = AnythingOver(BarOnScreen());
  if (covered == !revealed_) return;
  if (covered) Hide(/*force=*/true);
  else Reveal();
  LogInfo(L"[autoocultar] {}: {}", monitor_.device, covered ? L"escondido, hay algo debajo" : L"a la vista, no hay nada debajo");
}

void DockWindow::Apply(const DockConfig& config, ScreenApps screen) {
  const bool autoHideChanged = config.autoHide != config_.autoHide;
  config_ = config;
  base_ = std::move(screen.base);
  apps_ = std::move(screen.apps);
  Compose();
  // Pasar de reservar la franja a autoocultar (o al revés) no se deshace con ABM_SETPOS:
  // se da de baja la appbar y se vuelve a registrar limpia.
  if (autoHideChanged && registered_) {
    AppBarRemove(hwnd_);
    registered_ = AppBarRegister(hwnd_);
  }
  if (!config_.autoHide) revealed_ = true;
  if (!Reposition()) BuildVisuals({});
  visuals_->Slide(!revealed_, /*instant=*/true);
}

DockWindow::~DockWindow() {
  if (!hwnd_) return;
  KillTimer(hwnd_, kWatchdogTimer);
  KillTimer(hwnd_, kHideTimer);
  KillTimer(hwnd_, kSlideTimer);
  if (registered_) AppBarRemove(hwnd_);
  RevokeDragDrop(hwnd_);  // el DropTarget apunta a este objeto: fuera antes de que muera
  HidePreview();
  stack_.reset();  // antes que su dueña, que al destruirse se llevaría su ventana por delante
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
  // Nace ya en su monitor: creada en 0,0 (la principal) y movida después, llegaba un
  // WM_DPICHANGED por cada pantalla con otra escala y se reconstruía todo dos veces.
  hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
                          kClassName, L"Dock", WS_POPUP, monitor_.work.left, monitor_.work.top, 1, 1, nullptr,
                          nullptr, instance, this);
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

  // RegisterDragDrop se queda con su referencia y RevokeDragDrop la suelta: no hay que
  // guardarla. Pide OleInitialize en el hilo, no CoInitialize (main lo hace lo primero).
  const auto drop = Microsoft::WRL::Make<DropTarget>(
      hwnd_, [this](POINT at) { return DropOver(at); }, [this] { DropLeave(); },
      [this](std::vector<Dropped> items) { OnDropped(std::move(items)); });
  if (const HRESULT hr = RegisterDragDrop(hwnd_, drop.Get()); FAILED(hr))
    LogError(L"[soltar] {}: RegisterDragDrop falló ({:#010x})", monitor_.device, static_cast<unsigned>(hr));

  registered_ = AppBarRegister(hwnd_);
  Reposition();
  BuildVisuals({});  // la barra con su ancho ya; los iconos llegan del worker

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

Curve DockWindow::CurveFor() const {
  const float s = dpi_ / 96.0f;
  const float icon = config_.iconSize * s;
  const float spacing = config_.iconSpacing * s;
  const float separator = std::max(2.0f, spacing * 0.2f);
  std::vector<Slot> slots;
  for (const DockApp& app : drawn_)
    slots.push_back(app.separator ? Slot{separator + spacing, separator} : Slot{icon + spacing, icon});
  // Sin nada configurado la ventana necesita igualmente un tamaño con sentido.
  if (slots.empty()) slots.push_back(Slot{icon + spacing, icon});
  // 1,75 huecos: con 2,5 el dock se ensanchaba un 62% al pasar el ratón; así crecen ~3 iconos.
  return Curve(std::move(slots), (icon + spacing) * kRadiusInSlots, config_.magnification);
}

void DockWindow::BuildVisuals(const IconSet& icons) {
  // Cambiar la lista con algo cogido deja el índice apuntando a otra cosa: en C# una app
  // que se cerraba a mitad de pulsación acababa en RemoveAt(-1) y tumbaba el dock.
  if (pressedIndex_ >= 0) CancelDrag();
  // El menú se va con el árbol, y sus índices ya no apuntarían a lo mismo.
  CloseMenu();
  curve_ = CurveFor();
  std::vector<DockItem> items;
  std::wstring names;
  for (const DockApp& app : drawn_) {
    items.push_back({app.name, app.IconSource(), app.separator});
    names += (names.empty() ? L"" : L", ") + (app.separator ? std::wstring(L"|") : app.name);
  }
  // La lista que se ve, por pantalla: la señal de texto para comparar con el dock de C#.
  if (icons.empty()) LogInfo(L"[dock] {}: {}", monitor_.device, names);
  const float s = dpi_ / 96.0f;
  try {
    visuals_->Build(curve_, items, icons, static_cast<float>(width_), static_cast<float>(height_), kPadding * s,
                    config_.iconSize * s, s);
  } catch (const winrt::hresult_error& e) {
    LogError(L"[dock] {}: construir el dock falló ({:#010x}) {}", monitor_.device,
             static_cast<unsigned>(e.code().value), std::wstring(e.message()));
  }
  region_.clear();  // el ancho de la barra ha cambiado: la región se recalcula sí o sí
  ApplyRegion();
  // Reconstruir crea visuals nuevos: el de la app que se está abriendo vuelve a botar.
  if (!launchingTarget_.empty())
    for (size_t i = 0; i < drawn_.size(); i++)
      if (_wcsicmp(drawn_[i].target.c_str(), launchingTarget_.c_str()) == 0)
        visuals_->Bounce(static_cast<int>(i), config_.iconSize * dpi_ / 96.0f * 0.35f, true);
}

void DockWindow::ShowIcons(const IconSet& icons) { BuildVisuals(icons); }

bool DockWindow::Reposition() {
  const int barHeight = Px(config_.iconSize + 2 * kPadding);
  const int oldWidth = width_, oldHeight = height_;
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

  // Las expresiones llevan el ancho de la ventana dentro (para centrar la fila): si cambia,
  // hay que reconstruir, y los píxeles de los iconos ya no están, así que se piden otra vez.
  if (oldWidth && (oldWidth != width_ || oldHeight != height_)) {
    BuildVisuals({});
    app_.RequestIcons();
    return true;
  }
  return false;
}

RECT DockWindow::BarRect(bool tall) const {
  // Lo más ancho que llega a ser la fila, magnificada, más el margen de la barra.
  const float widest = curve_.RestWidth() + curve_.MaxGrowth();
  const int half = static_cast<int>(std::ceil(widest / 2 + kPadding * dpi_ / 96.0f)) + 2;
  const int barHeight = Px(config_.iconSize + 2 * kPadding);
  int top = height_ - barHeight - Px(config_.iconSize * (config_.magnification - 1));
  if (tall) top -= Px(kLabelStrip);
  return RECT{width_ / 2 - half, top, width_ / 2 + half, height_};
}

void DockWindow::ApplyRegion() {
  // La región es lo único que deja pasar el ratón a otros procesos (HTTRANSPARENT no
  // atraviesa procesos) y además recorta el dibujo: tiene que cubrir todo lo que se pinte.
  std::vector<RECT> rects;
  if (revealed_ || sliding_) {
    RECT bar = BarRect(hovering_ || dropping_);
    // El "+" sobresale por la derecha de la barra mientras se arrastra algo encima.
    if (dropping_ && visuals_) bar.right += static_cast<LONG>(std::ceil(visuals_->AddZoneReach())) + 2;
    rects.push_back(bar);
  }
  // El menú entero, no solo subir el techo de la barra: con un dock estrecho el menú es más
  // ancho que la barra y la región, que también recorta el dibujo, le cortaba el final de las
  // filas (medido: "Quitar 'Explorado" con un solo icono). En C# se encajaba dentro de la barra.
  if (visuals_ && visuals_->MenuOpen()) rects.push_back(visuals_->MenuRect());
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

void DockWindow::Hide(bool force) {
  if (!revealed_ || !config_.autoHide) return;
  if (!force && !AnythingOver(BarOnScreen())) return;
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
      if (curve_.Count() > 0) {
        // Lo ÚNICO que hace el hilo de UI por cada movimiento: invertir la curva y escribir
        // un escalar. El resto lo evalúa DWM.
        const float rest = curve_.Invert(static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(width_));
        lastRest_ = rest;
        visuals_->SetCursor(rest);
        if (!hoverShown_) {
          hoverShown_ = true;
          visuals_->SetHover(true);
        }
        const int slot = curve_.SlotAt(rest);
        if (visuals_->MenuOpen()) {
          int hot = visuals_->MenuHitTest(static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam)));
          // Fuera de las filas, en la lista de la rueda sigue resaltada la elegida.
          if (hot < 0 && wheelIndex_ >= 0) hot = wheelAt_ - wheelFirst_;
          visuals_->MenuSetHot(hot);
        } else if (!dragging_) {
          visuals_->SetLabel(slot);
        }
        LogTrace(L"[hover] x={} idx={} reposo={:.1f}", GET_X_LPARAM(lparam), slot, rest);
      }
      if (pressedIndex_ >= 0) {
        // Un movimiento sin el botón pulsado con algo cogido: se soltó fuera del dock y el
        // WM_LBUTTONUP no llegó. En C# el icono seguía al cursor sin botón (70 px -> 0).
        if (!(wparam & MK_LBUTTON)) CancelDrag();
        else OnDragMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      }
      return 0;

    case WM_LBUTTONDOWN: {
      const int index = curve_.SlotAt(lastRest_);
      // Lo anclado, y las abiertas sin anclar (para anclarlas arrastrando); la papelera no.
      const bool pinned = index >= 0 && index < DraggableEnd();
      const bool open = index > DraggableEnd() && index < static_cast<int>(drawn_.size()) &&
                        !drawn_[index].separator && drawn_[index].target != kTrashTarget;
      pressedIndex_ = pinned || open ? index : -1;
      press_ = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      return 0;
    }
    case WM_RBUTTONUP:
      CancelDrag();
      OpenContextMenu();
      return 0;

    case kJumpsReady: {
      std::unique_ptr<JumpResult> result(reinterpret_cast<JumpResult*>(lparam));
      LogTrace(L"[saltos] appId={} recientes={}", result->appId, result->items.size());
      // Solo si sigue abierto el menú de ese mismo icono.
      if (visuals_->MenuOpen() && menuIndex_ == result->index && !result->items.empty()) {
        menuJumps_ = std::move(result->items);
        ShowMenu();
      }
      return 0;
    }

    case kStackReady: {
      std::unique_ptr<StackResult> result(reinterpret_cast<StackResult*>(lparam));
      const int index = result->index;
      // La lista pudo cambiar mientras el worker leía: solo si ese icono sigue siendo esa carpeta.
      if (index >= static_cast<int>(drawn_.size()) || drawn_[index].target != result->folder) return 0;
      if (result->contents.items.empty()) {
        // Vacía o ilegible: se abre como siempre, en el Explorador.
        LogInfo(L"[stack] {} no tiene nada que desplegar", result->folder);
        LaunchDetached(drawn_[index]);
        return 0;
      }
      RECT window{};
      GetWindowRect(hwnd_, &window);
      const float center = curve_.Project((curve_.RestLeft(index) + curve_.RestRight(index)) / 2, static_cast<float>(width_), lastRest_);
      stack_ = std::make_unique<StackWindow>(app_, hwnd_);
      // Encima de lo más alto que llega el icono magnificado, no del techo de la ventana.
      if (!stack_->Open(std::move(result->contents), window.left + static_cast<int>(center), window.top + BarRect(false).top,
                        monitor_.bounds, dpi_)) {
        stack_.reset();
        return 0;
      }
      stackFolder_ = result->folder;
      return 0;
    }
    case kStackClosed:
      stack_.reset();
      stackFolder_.clear();
      // Mientras estaba abierto el dock no se escondía, y su WM_MOUSELEAVE ya pasó hace rato.
      if (config_.autoHide && !hovering_) SetTimer(hwnd_, kHideTimer, kHideDelayMs, nullptr);
      return 0;

    // Al soltar, no al pulsar: si se pasó el umbral fue un arrastre, si no un clic.
    case WM_LBUTTONUP:
      if (visuals_->MenuOpen()) {
        pressedIndex_ = -1;
        const float x = static_cast<float>(GET_X_LPARAM(lparam)), y = static_cast<float>(GET_Y_LPARAM(lparam));
        // El ✕ primero: vive DENTRO de su fila y el hit-test de filas daría la misma.
        if (const int close = visuals_->MenuHitTestClose(x, y); close >= 0) OnCloseWindow(close);
        else if (const int choice = visuals_->MenuHitTest(x, y); choice >= 0) OnMenuChoice(choice);
        else if (wheelIndex_ >= 0 && curve_.SlotAt(lastRest_) == wheelIndex_) OnWheelPick();
        else CloseMenu();
        return 0;
      }
      if (dragging_) {
        // Antes de soltar la captura: ReleaseCapture manda WM_CAPTURECHANGED, que cancelaría
        // lo que se acaba de decidir.
        FinishDrag(GET_Y_LPARAM(lparam));
        ReleaseCapture();
        return 0;
      }
      pressedIndex_ = -1;
      OnClick(false);
      return 0;
    case WM_CAPTURECHANGED:
      if (dragging_) CancelDrag();
      return 0;
    case WM_MBUTTONUP:
      OnClick(true);
      return 0;
    // Llega aunque el dock no tenga el foco: Windows manda la rueda a la ventana bajo el
    // cursor (el ajuste por defecto "desplazar ventanas inactivas").
    case WM_MOUSEWHEEL:
      if (!dragging_) OnWheel(GET_WHEEL_DELTA_WPARAM(wparam), POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
      return 0;

    case WM_MOUSELEAVE:
      if (dragging_) return 0;  // con la captura puesta el arrastre sigue fuera de la ventana
      hovering_ = false;
      CloseMenu();  // los clics fuera del dock no llegan: salir es la única forma de cerrarlo
      if (stack_) stack_->ScheduleClose();  // salvo que el ratón vaya a la rejilla
      if (hoverShown_) {
        hoverShown_ = false;
        visuals_->SetHover(false);
        visuals_->SetLabel(-1);
      }
      ApplyRegion();
      if (config_.autoHide) SetTimer(hwnd_, kHideTimer, kHideDelayMs, nullptr);
      return 0;

    case WM_TIMER:
      if (wparam == kWatchdogTimer) {
        CheckFullscreen();
        if (!fullscreen_) ReassertTopmost();
      } else if (wparam == kHideTimer) {
        KillTimer(hwnd_, kHideTimer);
        if (!hovering_ && !dropping_ && !stack_) Hide();
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
