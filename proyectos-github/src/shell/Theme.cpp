#include "shell/Theme.h"

namespace Theme {

Tokens TokensFor(Appearance appearance, Color accent) {
    Tokens tokens;

    if (appearance == Appearance::Light) {
        tokens.textPrimary = Rgb(0x1d1d1f);
        tokens.textSecondary = Rgb(0x6e6e73);
        tokens.separator = Rgba(0x000000, 0.08f);
        tokens.cardSurface = Rgba(0xffffff, 0.72f);
        tokens.sidebarVeil = Rgba(0xffffff, 0.35f);
    } else {
        tokens.textPrimary = Rgb(0xf5f5f7);
        tokens.textSecondary = Rgb(0xa1a1a6);
        tokens.separator = Rgba(0xffffff, 0.08f);
        tokens.cardSurface = Rgba(0x2c2c2e, 0.72f);
        tokens.sidebarVeil = Rgba(0x000000, 0.20f);
    }

    tokens.accent = accent;

    tokens.priorityFocus = Rgb(0xff9f0a);
    tokens.prioritySecondary = Rgb(0x0a84ff);
    tokens.prioritySomeday = Rgb(0xbf5af2);
    tokens.priorityArchived = Rgb(0x8e8e93);

    tokens.activityActive = Rgb(0x30d158);
    tokens.activityPaused = Rgb(0xffd60a);
    tokens.activityDormant = Rgb(0x8e8e93);

    return tokens;
}

Appearance AppearanceFromForeground(Color foreground) {
    // La luminancia y no solo un canal: con temas de alto contraste el texto puede ser de
    // cualquier color, y decidir por el rojo deja texto claro sobre fondo claro.
    const int luminance = (foreground.r * 299 + foreground.g * 587 + foreground.b * 114) / 1000;
    return luminance > 127 ? Appearance::Dark : Appearance::Light;
}

}  // namespace Theme
