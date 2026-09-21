#pragma once

#include <Windows.h>

#include <functional>

// Ventana Win32 pura: crea, propaga eventos y sabe su DPI. No conoce D3D ni ImGui.
class Window {
public:
    // true si el mensaje ya fue consumido (p. ej. por ImGui).
    std::function<bool(HWND, UINT, WPARAM, LPARAM)> onMessage;
    std::function<void()> onWake;              // algo cambio: hay que redibujar
    std::function<void(int, int)> onResize;    // tambien durante el bucle modal de resize
    std::function<void(float)> onDpiChanged;

    // Posicion y tamano guardados entre sesiones, en pixeles de pantalla.
    struct Layout {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        bool maximized = false;
    };

    // `saved` nulo o fuera de todo monitor = tamano por defecto centrado. `background` es
    // el color con el que se pinta la ventana hasta que D3D toma el relevo.
    bool Create(const wchar_t* title, int widthDip, int heightDip, COLORREF background,
                const Layout* saved);
    void Destroy();
    void Show() const;
    Layout Placement() const;

    // A partir de aqui pinta D3D: el fondo GDI deja de tocar la ventana.
    void EndGdiBackground() { m_gdiBackground = false; }

    HWND Handle() const { return m_hwnd; }
    float DpiScale() const { return m_dpiScale; }

private:
    static LRESULT CALLBACK Thunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT Proc(HWND, UINT, WPARAM, LPARAM);
    void Decorate() const;

    HWND m_hwnd = nullptr;
    float m_dpiScale = 1.0f;
    Layout m_lastLayout;  // lo ultimo que se supo antes de WM_DESTROY
    HBRUSH m_brush = nullptr;
    // La ventana se muestra antes de que exista el dispositivo D3D (el driver tarda
    // ~200 ms en cargar): hasta el primer frame el fondo lo pinta GDI.
    bool m_gdiBackground = true;
    int m_showCmd = SW_SHOW;
};
