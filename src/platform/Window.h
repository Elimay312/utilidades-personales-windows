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

    bool Create(const wchar_t* title, int widthDip, int heightDip);
    void Destroy();
    void Show() const;

    HWND Handle() const { return m_hwnd; }
    float DpiScale() const { return m_dpiScale; }

private:
    static LRESULT CALLBACK Thunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT Proc(HWND, UINT, WPARAM, LPARAM);

    HWND m_hwnd = nullptr;
    float m_dpiScale = 1.0f;
};
