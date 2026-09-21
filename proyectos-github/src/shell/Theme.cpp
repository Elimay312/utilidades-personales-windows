#include "shell/Theme.h"

#include <algorithm>
#include <cmath>

namespace Theme {

namespace {

// El mismo color con otro alfa. Se repite tanto que escribirlo a mano invita a que una
// de las veces salga un alfa que no era.
constexpr Color WithAlpha(Color color, float alpha) {
    color.a = static_cast<std::uint8_t>(alpha * 255.0f + 0.5f);
    return color;
}

}  // namespace

Tokens TokensFor(Appearance appearance, Color accent) {
    Tokens tokens;

    if (appearance == Appearance::Light) {
        tokens.textPrimary = Rgb(0x1d1d1f);
        tokens.textSecondary = Rgb(0x6e6e73);
        tokens.separator = Rgba(0x000000, 0.08f);
        tokens.cardSurface = Rgba(0xffffff, 0.72f);
        tokens.sidebarVeil = Rgba(0xffffff, 0.35f);

        // Sobre la Mica clara, que está medida en (241,244,244).
        tokens.controlFill = Rgba(0x000000, 0.05f);
        tokens.controlHover = Rgba(0x000000, 0.08f);
        tokens.controlPressed = Rgba(0x000000, 0.12f);
        tokens.controlStroke = Rgba(0x000000, 0.10f);

        tokens.textDisabled = Rgba(0x1d1d1f, 0.36f);
        tokens.selectionRow = Rgba(0x000000, 0.07f);

        tokens.menuSurface = Rgba(0xfcfcfd, 0.92f);
        tokens.toastSurface = Rgba(0xfcfcfd, 0.94f);
        // Un pozo y no un relieve: el campo se hunde respecto a lo que lo rodea.
        tokens.fieldSurface = Rgba(0xffffff, 0.60f);
        tokens.scrim = Rgba(0x000000, 0.20f);
        tokens.shadow = Rgba(0x000000, 0.18f);
    } else {
        tokens.textPrimary = Rgb(0xf5f5f7);
        tokens.textSecondary = Rgb(0xa1a1a6);
        tokens.separator = Rgba(0xffffff, 0.08f);
        tokens.cardSurface = Rgba(0x2c2c2e, 0.72f);
        tokens.sidebarVeil = Rgba(0x000000, 0.20f);

        // Sobre la Mica oscura, medida en (32,32,32).
        tokens.controlFill = Rgba(0xffffff, 0.06f);
        tokens.controlHover = Rgba(0xffffff, 0.10f);
        tokens.controlPressed = Rgba(0xffffff, 0.14f);
        tokens.controlStroke = Rgba(0xffffff, 0.09f);

        tokens.textDisabled = Rgba(0xf5f5f7, 0.36f);
        tokens.selectionRow = Rgba(0xffffff, 0.09f);

        tokens.menuSurface = Rgba(0x303032, 0.92f);
        tokens.toastSurface = Rgba(0x38383a, 0.94f);
        tokens.fieldSurface = Rgba(0x000000, 0.22f);
        // Más denso en oscuro: sobre un fondo oscuro un velo al 20 % no separa nada.
        tokens.scrim = Rgba(0x000000, 0.40f);
        tokens.shadow = Rgba(0x000000, 0.50f);
    }

    tokens.accent = accent;
    tokens.accentHover = Shade(accent, 0.10f);
    tokens.accentPressed = Shade(accent, -0.12f);
    tokens.textOnAccent = OnColor(accent);
    tokens.focusRing = accent;
    // El resalte de la selección va más translúcido en claro: sobre fondo claro el mismo
    // alfa tapa el texto y sobre fondo oscuro se queda corto.
    tokens.selectionText =
        WithAlpha(accent, appearance == Appearance::Light ? 0.25f : 0.35f);

    tokens.priorityFocus = Rgb(0xff9f0a);
    tokens.prioritySecondary = Rgb(0x0a84ff);
    tokens.prioritySomeday = Rgb(0xbf5af2);
    tokens.priorityArchived = Rgb(0x8e8e93);

    tokens.activityActive = Rgb(0x30d158);
    tokens.activityPaused = Rgb(0xffd60a);
    tokens.activityDormant = Rgb(0x8e8e93);

    return tokens;
}

Color OnColor(Color background) {
    // Los mismos pesos que abajo. Que sean los mismos importa: son la misma pregunta
    // hecha dos veces, y si se separan acabarán discrepando en algún tono.
    const int luminance =
        (background.r * 299 + background.g * 587 + background.b * 114) / 1000;
    // 140 y no 127: el blanco sobre un acento medio se lee peor que el negro, así que el
    // punto de corte se sube un poco respecto al del tema.
    return luminance > 140 ? Rgb(0x1d1d1f) : Rgb(0xffffff);
}

Color Shade(Color color, float amount) {
    const auto mezcla = [amount](std::uint8_t channel) {
        const float value = static_cast<float>(channel);
        // Hacia el blanco o hacia el negro según el signo, en proporción: así un acento
        // ya muy claro no se pasa de 255 y uno muy oscuro sigue moviéndose.
        const float target = amount >= 0.0f ? 255.0f : 0.0f;
        const float mixed = value + (target - value) * std::fabs(amount);
        return static_cast<std::uint8_t>(std::clamp(mixed, 0.0f, 255.0f) + 0.5f);
    };
    return Color{color.a, mezcla(color.r), mezcla(color.g), mezcla(color.b)};
}

Appearance AppearanceFromForeground(Color foreground) {
    // La luminancia y no solo un canal: con temas de alto contraste el texto puede ser de
    // cualquier color, y decidir por el rojo deja texto claro sobre fondo claro.
    const int luminance = (foreground.r * 299 + foreground.g * 587 + foreground.b * 114) / 1000;
    return luminance > 127 ? Appearance::Dark : Appearance::Light;
}

}  // namespace Theme
