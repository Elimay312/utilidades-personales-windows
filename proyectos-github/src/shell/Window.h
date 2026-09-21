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
#include "shell/Input.h"

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

        // Una sola puerta para el ratón: mover, pulsar, soltar, salir, cancelar y rueda.
        // La ventana ya no interpreta nada, solo traduce a DIP y cuenta los clics.
        std::function<void(const Input::Pointer&)> onPointer;

        // true si alguien pidió un cursor propio —la viga sobre el campo de texto—.
        std::function<bool(float xDip, float yDip)> onSetCursor;

        // true consume la tecla; false la deja bajar a DefWindowProc, que es lo que hace
        // falta para que Alt+F4 y Alt+Espacio sigan funcionando.
        std::function<bool(const Input::Key&)> onKey;
        std::function<void(wchar_t unit)> onChar;

        std::function<void(bool focused)> onWindowFocus;
        // La ventana dejó de estar activa: hay que cerrar menús y avisos, o se quedan
        // encendidos encima de otra aplicación.
        std::function<void()> onDeactivate;

        // Menú contextual. keyboard = llegó por Shift+F10 o por la tecla de menú, y
        // entonces no hay coordenadas que valgan.
        std::function<void(float xDip, float yDip, bool keyboard)> onContextMenu;

        // Dónde está el cursor de texto, en DIP, para colocar la ventana del IME.
        std::function<bool(float& xDip, float& yDip, float& heightDip)> onCaretRect;

        // Toca vaciar el conjunto de repintado.
        std::function<void()> onFlush;

        // El hilo de sincronización tiene algo nuevo. Como el de ThemeWatcher, el aviso no
        // lleva carga: quien escucha vuelve a leer el estado. Un puntero dentro del WPARAM
        // sería un puntero cruzando hilos, y el día que el mensaje llegue tarde apuntará a
        // algo que ya no existe.
        std::function<void()> onSync;
    };

    // El aviso que publica ThemeWatcher desde su hilo.
    static constexpr UINT kThemeMessage = WM_APP + 0;
    // El que se publica a sí misma para repintar lo sucio de una sola vez.
    static constexpr UINT kFlushMessage = WM_APP + 1;
    // El que publica el hilo de sincronización de la fase 3.
    static constexpr UINT kSyncMessage = WM_APP + 2;

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
    void TrackClientLeave();
    void PlaceImeWindow();
    Input::Modifiers CurrentModifiers() const;
    void Emit(Input::Action action, Input::Button button, LPARAM lparam, int clicks = 1);
    void EmitWheel(bool horizontal, int delta, POINT clientPx);

    HWND m_hwnd = nullptr;
    float m_scale = 1.0f;
    float m_widthDip = 0.0f;
    float m_heightDip = 0.0f;
    bool m_mica = false;
    // El WM_CHAR que sigue a una tecla que ya se usó como atajo. Ver WM_KEYDOWN.
    bool m_swallowChar = false;
    bool m_trackingNc = false;
    bool m_trackingClient = false;
    Input::Clicks m_clicks;
    Caption::Layout m_caption;
    Caption::Zone m_hovered = Caption::Zone::Client;
    Caption::Zone m_pressed = Caption::Zone::Client;
};

}  // namespace Shell
