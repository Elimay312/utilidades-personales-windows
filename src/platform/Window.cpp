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

bool Window::Create(const wchar_t* title, int widthDip, int heightDip) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Window::Thunk;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // sin borrado GDI: pinta D3D, asi no parpadea
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;

    // Sin WS_VISIBLE: se muestra tras el primer frame para no ver un flash blanco.
    if (!CreateWindowExW(0, kClassName, title, kStyle,
                         CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                         nullptr, nullptr, wc.hInstance, this))
        return false;

    const UINT dpi = GetDpiForWindow(m_hwnd);
    m_dpiScale = static_cast<float>(dpi) / 96.0f;

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

    const BOOL dark = TRUE;  // barra de titulo a juego con el tema oscuro
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    return true;
}

void Window::Destroy() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void Window::Show() const {
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
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
        ValidateRect(hwnd, nullptr);  // ya repintamos nosotros
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
        m_hwnd = nullptr;
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}
