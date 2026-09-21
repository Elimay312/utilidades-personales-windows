#include "platform/Window.h"

#include <dwmapi.h>

#include <algorithm>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {

constexpr wchar_t kClassName[] = L"RayoWindow";
constexpr DWORD kStyle = WS_OVERLAPPEDWINDOW;

// Solo estos mensajes justifican un frame. Todo lo demas se ignora para que
// la app consuma 0 % de CPU en reposo.
bool NeedsRedraw(UINT msg) {
    if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) return true;
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) return true;
    if (msg >= WM_APP && msg < WM_APP + 0x100) return true;  // resultados de hilos de trabajo
    switch (msg) {
    case WM_PAINT:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ACTIVATE:
    case WM_MOUSELEAVE:
        return true;
    default:
        return false;
    }
}

}  // namespace

bool Window::Create(const wchar_t* title, int widthDip, int heightDip, COLORREF background,
                    const Layout* saved) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Window::Thunk;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // sin borrado GDI: pinta D3D, asi no parpadea
    wc.lpszClassName = kClassName;
    // El icono 1 del .rc. El pequeno se pide aparte y al tamano exacto: si se deja nulo,
    // Windows encoge el grande y la barra de titulo se ve emborronada. LR_SHARED los hace
    // propiedad del sistema, asi que no hay nada que destruir.
    wc.hIcon = static_cast<HICON>(
        LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    wc.hIconSm = static_cast<HICON>(
        LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                   GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!RegisterClassExW(&wc)) return false;

    m_brush = CreateSolidBrush(background);

    // Sin WS_VISIBLE: la posicion se decide abajo y Show() la ensena ya colocada.
    if (!CreateWindowExW(0, kClassName, title, kStyle,
                         CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                         nullptr, nullptr, wc.hInstance, this))
        return false;

    const UINT dpi = GetDpiForWindow(m_hwnd);
    m_dpiScale = static_cast<float>(dpi) / 96.0f;

    // Lo guardado manda, pero solo si sigue cayendo en algun monitor: con la pantalla
    // secundaria desenchufada, la ventana se abriria fuera de cualquier pantalla y ya no
    // habria forma de traerla de vuelta.
    if (saved && saved->width > 0 && saved->height > 0) {
        const RECT r{saved->x, saved->y, saved->x + saved->width, saved->y + saved->height};
        if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) {
            // Con la ventana aun oculta esta es la posicion "restaurada", asi que
            // maximizar despues no la pierde.
            SetWindowPos(m_hwnd, nullptr, r.left, r.top, saved->width, saved->height,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (saved->maximized) m_showCmd = SW_SHOWMAXIMIZED;
            Decorate();
            return true;
        }
    }

    // El tamano pedido esta en DIP; llevarlo a pixeles del monitor donde cayo.
    RECT r{0, 0, static_cast<LONG>(widthDip * m_dpiScale), static_cast<LONG>(heightDip * m_dpiScale)};
    AdjustWindowRectExForDpi(&r, kStyle, FALSE, 0, dpi);
    LONG width = r.right - r.left;
    LONG height = r.bottom - r.top;
    LONG x = CW_USEDEFAULT;
    LONG y = CW_USEDEFAULT;

    // Encajar y centrar en el area de trabajo: 1280x800 DIP no caben en un 1080p al 125 %.
    MONITORINFO mi{sizeof(mi)};
    if (GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        width = std::min(width, mi.rcWork.right - mi.rcWork.left);
        height = std::min(height, mi.rcWork.bottom - mi.rcWork.top);
        x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - width) / 2;
        y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - height) / 2;
    }
    SetWindowPos(m_hwnd, nullptr, x, y, width, height,
                 (x == CW_USEDEFAULT ? SWP_NOMOVE : 0) | SWP_NOZORDER | SWP_NOACTIVATE);

    Decorate();
    return true;
}

void Window::Decorate() const {
    const BOOL dark = TRUE;  // barra de titulo a juego con el tema oscuro
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

void Window::Destroy() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_brush) {
        DeleteObject(m_brush);
        m_brush = nullptr;
    }
}

void Window::Show() const {
    ShowWindow(m_hwnd, m_showCmd);
    SetForegroundWindow(m_hwnd);
    // El WM_PAINT del fondo, ya: no hay bucle de mensajes hasta que el dispositivo D3D
    // este creado, y sin esto la ventana se quedaria en blanco todo ese rato.
    UpdateWindow(m_hwnd);
}

Window::Layout Window::Placement() const {
    // Cerrar con la X destruye la ventana antes de que nadie pregunte: lo ultimo que se
    // supo se guarda en WM_DESTROY y es lo que se devuelve a partir de ahi.
    Layout layout = m_lastLayout;
    WINDOWPLACEMENT placement{sizeof(placement)};
    if (!m_hwnd || !GetWindowPlacement(m_hwnd, &placement)) return layout;

    layout.maximized = placement.showCmd == SW_SHOWMAXIMIZED;
    // Maximizada, lo unico que hay es la posicion restaurada de WINDOWPLACEMENT (que va
    // en coordenadas del area de trabajo); normal, GetWindowRect es exacto.
    RECT r = placement.rcNormalPosition;
    if (!layout.maximized) GetWindowRect(m_hwnd, &r);

    layout.x = r.left;
    layout.y = r.top;
    layout.width = r.right - r.left;
    layout.height = r.bottom - r.top;
    return layout;
}

LRESULT CALLBACK Window::Thunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Window* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->Proc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Window::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Antes de ImGui: aunque ImGui consuma la tecla, el frame hay que pedirlo igual.
    if (onWake && NeedsRedraw(msg)) onWake();

    if (onMessage && onMessage(hwnd, msg, wp, lp)) return 1;

    switch (msg) {
    case WM_PAINT:
        if (m_gdiBackground) {
            // Antes de que exista D3D. Es lo que hace que la ventana salga oscura en
            // cuanto aparece, sin esperar a que cargue el driver de la GPU.
            PAINTSTRUCT ps;
            const HDC dc = BeginPaint(hwnd, &ps);
            FillRect(dc, &ps.rcPaint, m_brush);
            EndPaint(hwnd, &ps);
        } else {
            ValidateRect(hwnd, nullptr);  // ya repintamos nosotros
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        if (wp != SIZE_MINIMIZED && onResize)
            onResize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_DPICHANGED: {
        m_dpiScale = static_cast<float>(LOWORD(wp)) / 96.0f;
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (onDpiChanged) onDpiChanged(m_dpiScale);
        return 0;
    }

    case WM_DESTROY:
        m_lastLayout = Placement();
        m_hwnd = nullptr;
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
