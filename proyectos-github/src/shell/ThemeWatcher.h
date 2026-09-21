#pragma once

// Sigue el tema y el color de acento de Windows, y avisa cuando cambian.
//
// Windows no dice "claro" u "oscuro": dice de qué color pinta el fondo. UISettings lo
// responde, y de paso da el acento, que es el único color de la tabla de CLAUDE.md que no
// elegimos nosotros.
//
// ColorValuesChanged salta en OTRO HILO. Esta clase no toca nada desde ahí: solo hace
// PostMessageW. Es el puente trabajador -> UI de la regla 1 de arquitectura, estrenado
// aquí y con el mismo aspecto que tendrá el de la sincronización en la fase 3.

#include <Windows.h>

#include "compositor/Winrt.h"
#include "shell/Theme.h"

namespace Shell {

class ThemeWatcher {
public:
    // notifyMessage se publica en hwnd cada vez que Windows cambia de tema o de acento.
    bool Create(HWND hwnd, UINT notifyMessage);

    // Relee el sistema. Hay que llamarlo al recibir el aviso y en WM_SETTINGCHANGE.
    void Refresh();

    Theme::Appearance Appearance() const { return m_appearance; }
    Theme::Color Accent() const { return m_accent; }
    const Theme::Tokens& Tokens() const { return m_tokens; }

    void Close();

private:
    winrt::Windows::UI::ViewManagement::UISettings m_settings{nullptr};
    winrt::event_token m_token{};
    HWND m_hwnd = nullptr;
    UINT m_message = 0;
    Theme::Appearance m_appearance = Theme::Appearance::Dark;
    Theme::Color m_accent = Theme::kAccentFallback;
    Theme::Tokens m_tokens;
};

}  // namespace Shell
