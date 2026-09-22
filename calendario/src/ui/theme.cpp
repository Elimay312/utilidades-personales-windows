#include "ui/theme.h"

#include <windows.h>

namespace agenda {

Theme DarkTheme() {
  Theme theme;
  theme.light = false;
  theme.panel = Rgb(0x1E1F24, 0.85f);
  theme.panelOpaque = Rgb(0x1E1F24);
  theme.surface = Rgb(0x2A2B31);
  theme.textPrimary = Rgb(0xF2F2F5);
  theme.textSecondary = Rgb(0x8B8C94);
  theme.textMuted = Rgb(0x8B8C94, 0.45f);
  theme.accent = Rgb(0x4A8BF5);
  theme.onAccent = Rgb(0xFFFFFF);
  theme.alt = Rgb(0xF5A623);
  theme.border = Rgb(0xFFFFFF, 0.08f);
  theme.hover = Rgb(0xFFFFFF, 0.06f);
  theme.selection = Rgb(0x4A8BF5, 0.35f);
  theme.now = Rgb(0xFF5A5F);
  return theme;
}

Theme LightTheme() {
  Theme theme;
  theme.light = true;
  theme.panel = Rgb(0xF4F4F7, 0.85f);
  theme.panelOpaque = Rgb(0xF4F4F7);
  theme.surface = Rgb(0xFFFFFF);
  theme.textPrimary = Rgb(0x1B1C21);
  theme.textSecondary = Rgb(0x6C6D75);
  theme.textMuted = Rgb(0x6C6D75, 0.45f);
  // Darker than the #4A8BF5 of the dark theme: the day number sits in white on top of this
  // circle, and the original blue does not carry white text on a light panel.
  theme.accent = Rgb(0x2F6FE0);
  theme.onAccent = Rgb(0xFFFFFF);
  theme.alt = Rgb(0xF5A623);
  theme.border = Rgb(0x000000, 0.10f);
  theme.hover = Rgb(0x000000, 0.05f);
  theme.selection = Rgb(0x2F6FE0, 0.28f);
  // A shade darker for the same reason the accent is: a thin red line has to hold its own
  // against a pale panel.
  theme.now = Rgb(0xE0393E);
  return theme;
}

Theme HighContrastTheme() {
  const bool live = HighContrastOn();
  const auto sys = [live](int index, UINT32 fallback) {
    if (!live) return Rgb(fallback);
    const COLORREF color = GetSysColor(index);
    return Rgb((static_cast<UINT32>(GetRValue(color)) << 16) |
               (static_cast<UINT32>(GetGValue(color)) << 8) | GetBValue(color));
  };
  Theme theme;
  theme.highContrast = true;
  theme.panel = sys(COLOR_WINDOW, 0x000000);
  theme.panelOpaque = theme.panel;
  theme.surface = theme.panel;
  theme.textPrimary = sys(COLOR_WINDOWTEXT, 0xFFFFFF);
  theme.textSecondary = theme.textPrimary;
  theme.textMuted = sys(COLOR_GRAYTEXT, 0xA6A6A6);
  theme.accent = sys(COLOR_HIGHLIGHT, 0x8EE3F0);
  theme.onAccent = sys(COLOR_HIGHLIGHTTEXT, 0x263B50);
  theme.alt = theme.accent;
  theme.border = theme.textPrimary;
  theme.hover = theme.accent;
  theme.hover.a = 0.35f;
  theme.selection = theme.accent;
  theme.selection.a = 0.5f;
  theme.now = sys(COLOR_HOTLIGHT, 0xFFFF00);
  // Which way DWM draws the frame, and which way anything picked by lightness goes.
  const D2D1_COLOR_F& back = theme.panel;
  theme.light = 0.2126f * back.r + 0.7152f * back.g + 0.0722f * back.b > 0.5f;
  return theme;
}

bool HighContrastOn() {
  HIGHCONTRASTW contrast{};
  contrast.cbSize = sizeof(contrast);
  return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
         (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

bool SystemUsesLightTheme() {
  DWORD value = 0;
  DWORD size = sizeof(value);
  const LSTATUS status = RegGetValueW(
      HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
      L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
  // Missing key means the dark theme, which is the one the design system is written for.
  return status == ERROR_SUCCESS && value != 0;
}

}  // namespace agenda
