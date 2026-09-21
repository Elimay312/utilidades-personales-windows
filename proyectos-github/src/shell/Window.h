#pragma once

// La ventana: marco propio, Mica, DPI y el bucle de mensajes. No sabe nada de
// Composition ni de Direct2D; avisa por callbacks y quien escucha decide qué dibujar.
//
// No hay una sola llamada a GDI aquí dentro, y es lo que hace que no haya destellos
// blancos: sin pincel de fondo, sin pintar en WM_PAINT y sin enseñar la ventana hasta que
// el árbol de composición ya tiene su primer fotograma, no existe ningún momento en el
// que Windows tenga algo que pintar y nosotros no.

#include <Windows.h>

#include <functional>

#include "shell/Caption.h"

namespace Shell {

class Window {
public:
    struct Callbacks {
        // Tamaño del cliente en DIP y escala del monitor. Llega al crear, al
        // redimensionar y al cambiar de pantalla.
        std::function<void(float widthDip, float heightDip, float scale)> onLayout;
        std::function<void()> onTheme;
        std::function<void()> onMotionSetting;
        // El ratón entró, salió o pulsó un botón de la barra de título.
        std::function<void()> onCaptionState;
        std::function<void(float xDip, float yDip)> onPointerDown;
        std::function<void(float xDip, float yDip)> onPointerUp;
        std::function<void(int virtualKey)> onKeyDown;
    };

    // El aviso que publica ThemeWatcher desde su hilo.
    static constexpr UINT kThemeMessage = WM_APP + 0;

    bool Create(HINSTANCE instance, const wchar_t* title, float widthDip, float heightDip);
    void Show(int showCommand);
    int Run();

    HWND Handle() const { return m_hwnd; }
    float Scale() const { return m_scale; }
    float WidthDip() const { return m_widthDip; }
    float HeightDip() const { return m_heightDip; }
    const Caption::Layout& CaptionLayout() const { return m_caption; }
    Caption::Zone Hovered() const { return m_hovered; }
    Caption::Zone Pressed() const { return m_pressed; }
    bool Maximized() const { return IsZoomed(m_hwnd) != FALSE; }
    // false en Windows 10: no hay Mica y hay que pintar un fondo de los tokens.
    bool HasMica() const { return m_mica; }

    Callbacks callbacks;

private:
    static LRESULT CALLBACK Thunk(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT Proc(UINT message, WPARAM wparam, LPARAM lparam);

    LRESULT OnNcCalcSize(WPARAM wparam, LPARAM lparam);
    LRESULT OnNcHitTest(LPARAM lparam);
    void OnNcMouseMove(WPARAM wparam);
    void SetCaptionState(Caption::Zone hovered, Caption::Zone pressed);
    void UpdateLayout();

    HWND m_hwnd = nullptr;
    float m_scale = 1.0f;
    float m_widthDip = 0.0f;
    float m_heightDip = 0.0f;
    bool m_mica = false;
    bool m_trackingNc = false;
    Caption::Layout m_caption;
    Caption::Zone m_hovered = Caption::Zone::Client;
    Caption::Zone m_pressed = Caption::Zone::Client;
};

}  // namespace Shell
