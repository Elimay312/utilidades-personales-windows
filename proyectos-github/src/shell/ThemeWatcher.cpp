#include "shell/ThemeWatcher.h"

namespace uvm = winrt::Windows::UI::ViewManagement;

namespace Shell {

namespace {

Theme::Color FromUi(const winrt::Windows::UI::Color& color) {
    return Theme::Color{color.A, color.R, color.G, color.B};
}

}  // namespace

bool ThemeWatcher::Create(HWND hwnd, UINT notifyMessage) {
    m_hwnd = hwnd;
    m_message = notifyMessage;

    try {
        m_settings = uvm::UISettings();
    } catch (const winrt::hresult_error&) {
        // Sin UISettings nos quedamos con el tema oscuro y el acento de respaldo. No es
        // motivo para no abrir la ventana.
        Refresh();
        return false;
    }

    m_token = m_settings.ColorValuesChanged([this](auto&&, auto&&) {
        // Otro hilo. Aquí NO se lee UISettings ni se toca un visual: solo se avisa.
        if (m_hwnd) PostMessageW(m_hwnd, m_message, 0, 0);
    });

    Refresh();
    return true;
}

void ThemeWatcher::Refresh() {
    if (m_settings) {
        m_appearance = Theme::AppearanceFromForeground(
            FromUi(m_settings.GetColorValue(uvm::UIColorType::Foreground)));

        const Theme::Color accent = FromUi(m_settings.GetColorValue(uvm::UIColorType::Accent));
        // Un acento sin color —puede pasar con contraste alto— no vale como acento.
        m_accent = accent.a == 0 ? Theme::kAccentFallback : accent;
    }
    m_tokens = Theme::TokensFor(m_appearance, m_accent);
}

void ThemeWatcher::Close() {
    if (m_settings && m_token) {
        m_settings.ColorValuesChanged(m_token);
        m_token = {};
    }
    m_settings = nullptr;
    m_hwnd = nullptr;
}

}  // namespace Shell
