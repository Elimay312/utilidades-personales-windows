#include "ui/stack.h"

#include <shlobj.h>
#include <shlwapi.h>  // StrCmpLogicalW
#include <wrl/client.h>

#include <algorithm>
#include <cmath>

#include "app.h"
#include "core/log.h"
#include "system/launch.h"

namespace dock {
namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kClassName[] = L"DockStack";
constexpr UINT kStackReload = WM_APP + 5;
constexpr UINT_PTR kCloseTimer = 1;
// Margen para llegar: el camino del dock a la rejilla pasa por fuera de las dos.
constexpr UINT kCloseMs = 700;
// Medidas lógicas, a 96 ppp: las del de C#.
constexpr float kCell = 92;
constexpr float kIcon = 48;
constexpr float kPad = 12;
// Un stack no es un explorador de archivos: si hay más, para eso está abrir la carpeta.
constexpr size_t kLimit = 20;
// ponytail: se ordena lo que hay entre los 500 primeros; una carpeta con más puede dejar
// fuera alguna subcarpeta. Subirlo si hace falta: enumerar nombres es barato, los iconos no.
constexpr size_t kScanLimit = 500;

std::wstring Display(IShellItem* item, SIGDN kind) {
  PWSTR text = nullptr;
  if (FAILED(item->GetDisplayName(kind, &text))) return {};
  std::wstring result = text;
  CoTaskMemFree(text);
  return result;
}

std::wstring Shorten(const std::wstring& text, size_t limit) {
  return text.size() <= limit ? text : text.substr(0, limit - 1) + L"…";
}

}  // namespace

StackContents ReadStack(const std::wstring& folder, const std::wstring& back) {
  StackContents result;
  ComPtr<IShellItem> root;
  ComPtr<IEnumShellItems> children;
  // No es una carpeta o no se deja enumerar: no es un error, el clic la abre como siempre.
  if (FAILED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&root))) ||
      FAILED(root->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&children))))
    return result;
  result.folder = folder;
  std::vector<StackItem> found;
  ComPtr<IShellItem> child;
  while (found.size() < kScanLimit && children->Next(1, &child, nullptr) == S_OK) {
    // La papelera no da ruta de sistema de ficheros para todo: entonces vale la de análisis.
    std::wstring path = Display(child.Get(), SIGDN_FILESYSPATH);
    if (path.empty()) path = Display(child.Get(), SIGDN_DESKTOPABSOLUTEPARSING);
    SFGAOF attributes = 0;
    child->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attributes);
    // Un .zip es carpeta Y fichero para el shell: se abre, no se recorre aquí.
    const bool isFolder = (attributes & SFGAO_FOLDER) && !(attributes & SFGAO_STREAM);
    if (!path.empty()) found.push_back({Display(child.Get(), SIGDN_NORMALDISPLAY), path, isFolder, false});
    child.Reset();
  }
  // Las carpetas primero, como el Explorador, y con su orden: "2" antes que "10".
  std::sort(found.begin(), found.end(), [](const StackItem& a, const StackItem& b) {
    return a.folder != b.folder ? a.folder : StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
  });
  if (found.size() > kLimit) found.resize(kLimit);
  if (!back.empty()) result.items.push_back({L"Atrás", back, true, true});
  result.items.insert(result.items.end(), found.begin(), found.end());
  std::vector<std::wstring> keys;
  for (const StackItem& item : result.items) keys.push_back(item.path);
  result.icons = ExtractIconsOutOfProcess(keys);
  return result;
}

StackWindow::StackWindow(App& app, HWND dock) : app_(app), dock_(dock) {}

StackWindow::~StackWindow() {
  if (!hwnd_) return;
  KillTimer(hwnd_, kCloseTimer);
  visuals_.reset();  // el target de Composition antes que su ventana
  DestroyWindow(hwnd_);
  // Sin esto WARP se queda con las superficies soltadas: con 20 iconos el dock no bajaba de
  // 25 MB al cerrar; con Trim, a 11,3 (por debajo de los 14,5 de antes de abrirlo).
  // ponytail: abierto cuesta ~+11 MB con 20 iconos, porque llegan a 256 px y se dibujan a 84;
  // extraerlos ya al tamaño de la pantalla en el proceso hijo lo bajaría, si llega a apretar.
  Visuals::Trim();
}

bool StackWindow::Open(StackContents contents, int anchorX, int above, const RECT& monitor, UINT dpi) {
  static bool registered = false;
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registered) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    registered = RegisterClassExW(&wc) != 0;
  }
  // Con el dock de dueño: va siempre por encima de él, y se va con él.
  hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, kClassName,
                          L"Stack", WS_POPUP, 0, 0, 1, 1, dock_, nullptr, instance, this);
  if (!hwnd_) return false;
  try {
    visuals_ = std::make_unique<Visuals>(hwnd_);
  } catch (const winrt::hresult_error& e) {
    LogError(L"[stack] Composition falló ({:#010x})", static_cast<unsigned>(e.code().value));
    return false;
  }
  anchorX_ = anchorX;
  above_ = above;
  monitor_ = monitor;
  scale_ = dpi / 96.0f;
  history_ = {contents.folder};
  Show(std::move(contents));
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  return true;
}

void StackWindow::Show(StackContents contents) {
  items_ = std::move(contents.items);
  const int count = static_cast<int>(items_.size());
  columns_ = std::clamp(count, 1, 5);
  const int rows = (count + columns_ - 1) / columns_;
  const float cell = kCell * scale_, pad = kPad * scale_;
  const int w = static_cast<int>(std::ceil(columns_ * cell + pad * 2));
  const int h = static_cast<int>(std::ceil(rows * cell + pad * 2));
  // Centrada sobre el icono, justo encima del dock, sin salirse del monitor.
  const int margin = static_cast<int>(8 * scale_);
  const int x = std::clamp(anchorX_ - w / 2, static_cast<int>(monitor_.left) + margin,
                           std::max(static_cast<int>(monitor_.left) + margin, static_cast<int>(monitor_.right) - w - margin));
  const int y = std::max(static_cast<int>(monitor_.top) + margin, above_ - h - margin);
  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
  std::vector<DockItem> items;
  for (const StackItem& item : items_) items.push_back({Shorten(item.name, 14), item.path, false});
  visuals_->BuildStack(items, contents.icons, columns_, cell, kIcon * scale_, pad, scale_);
  LogInfo(L"[stack] {} elementos de {} ({} iconos), {},{} {}x{}", count, history_.back(), contents.icons.size(), x, y, w, h);
}

int StackWindow::HitTest(int x, int y) const {
  const float cell = kCell * scale_, pad = kPad * scale_;
  // floor y no truncar: justo a la izquierda del margen daría la columna 0.
  const int column = static_cast<int>(std::floor((x - pad) / cell));
  const int row = static_cast<int>(std::floor((y - pad) / cell));
  if (column < 0 || column >= columns_ || row < 0) return -1;
  const int index = row * columns_ + column;
  return index < static_cast<int>(items_.size()) ? index : -1;
}

void StackWindow::OnClick(int index) {
  if (index < 0) return;
  const StackItem item = items_[index];
  if (item.folder) {
    // Una carpeta se recorre aquí dentro, que es de lo que va esto: mirar sin abrir el
    // Explorador. La lectura va al worker, como la primera.
    if (item.back) history_.pop_back();
    else history_.push_back(item.path);
    const std::wstring folder = history_.back();
    const std::wstring back = history_.size() > 1 ? history_[history_.size() - 2] : L"";
    LogInfo(L"[stack] {} {}", item.back ? L"vuelve a" : L"entra en", folder);
    const HWND self = hwnd_;
    app_.PostJob([self, folder, back] {
      auto* contents = new StackContents(ReadStack(folder, back));
      if (!PostMessageW(self, kStackReload, 0, reinterpret_cast<LPARAM>(contents))) delete contents;
    });
    return;
  }
  LaunchDetached(DockApp{item.name, item.path, L"", L"", false});
  LogInfo(L"[stack] abre {}", item.path);
  Close();
}

void StackWindow::ScheduleClose() { SetTimer(hwnd_, kCloseTimer, kCloseMs, nullptr); }

void StackWindow::Close() {
  KillTimer(hwnd_, kCloseTimer);
  LogTrace(L"[stack] cerrado");
  ShowWindow(hwnd_, SW_HIDE);
  PostMessageW(dock_, kStackClosed, 0, 0);
}

LRESULT CALLBACK StackWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto* self = reinterpret_cast<StackWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (!self || !self->hwnd_) return DefWindowProcW(hwnd, message, wparam, lparam);
  return self->Handle(message, wparam, lparam);
}

LRESULT StackWindow::Handle(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_MOUSEACTIVATE:  // como el dock: se clica sin quitarle el foco a lo que usabas
      return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
      KillTimer(hwnd_, kCloseTimer);
      if (!tracking_) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
        tracking_ = TrackMouseEvent(&track) != FALSE;
      }
      visuals_->StackSetHot(HitTest(static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))));
      return 0;
    case WM_MOUSELEAVE:
      tracking_ = false;
      visuals_->StackSetHot(-1);
      ScheduleClose();
      return 0;
    case WM_TIMER:
      if (wparam == kCloseTimer) Close();
      return 0;
    case WM_LBUTTONUP:
      OnClick(HitTest(static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))));
      return 0;
    case kStackReload: {
      std::unique_ptr<StackContents> contents(reinterpret_cast<StackContents*>(lparam));
      // Ilegible o vacía: se queda donde estaba en vez de enseñar una rejilla vacía.
      if (!contents->folder.empty() && !contents->items.empty()) Show(std::move(*contents));
      return 0;
    }
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

}  // namespace dock
