#include "ui/theme.h"

#include <windows.h>

namespace panel {

Theme DarkTheme(UINT32 accent) {
  Theme theme;
  theme.light = false;
  theme.panel = Rgb(0x1E1F24, 0.85f);
  theme.panelOpaque = Rgb(0x1E1F24);
  theme.surface = Rgb(0x2A2B31);
  theme.textPrimary = Rgb(0xF2F2F5);
  theme.textSecondary = Rgb(0x8B8C94);
  theme.accent = Rgb(accent);
  theme.onAccent = Rgb(0xFFFFFF);
  theme.track = Rgb(0xFFFFFF, 0.10f);
  theme.fill = Rgb(0xF2F2F5);
  theme.onFill = Rgb(0x1E1F24, 0.70f);
  theme.border = Rgb(0xFFFFFF, 0.08f);
  theme.hover = Rgb(0xFFFFFF, 0.06f);
  theme.running = Rgb(0x30D158);
  return theme;
}

Theme LightTheme(UINT32 accent) {
  Theme theme;
  theme.light = true;
  theme.panel = Rgb(0xF4F4F7, 0.85f);
  theme.panelOpaque = Rgb(0xF4F4F7);
  theme.surface = Rgb(0xFFFFFF);
  theme.textPrimary = Rgb(0x1B1C21);
  theme.textSecondary = Rgb(0x6C6D75);
  theme.accent = Rgb(accent);
  theme.onAccent = Rgb(0xFFFFFF);
  theme.track = Rgb(0x000000, 0.08f);
  // A light panel wants the fill dark, or a white bar on white cards disappears.
  theme.fill = Rgb(0x1B1C21);
  theme.onFill = Rgb(0xFFFFFF, 0.85f);
  theme.border = Rgb(0x000000, 0.10f);
  theme.hover = Rgb(0x000000, 0.05f);
  theme.running = Rgb(0x248A3D);
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
  theme.accent = sys(COLOR_HIGHLIGHT, 0x8EE3F0);
  theme.onAccent = sys(COLOR_HIGHLIGHTTEXT, 0x263B50);
  theme.track = theme.panel;
  theme.fill = theme.accent;
  theme.onFill = theme.onAccent;
  theme.border = theme.textPrimary;
  theme.hover = theme.accent;
  theme.hover.a = 0.35f;
  theme.running = theme.accent;
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
  return status == ERROR_SUCCESS && value != 0;
}

UINT32 SystemAccent() {
  // DWM keeps the accent as 0xAABBGGRR. Read only, like the theme.
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\DWM)", L"AccentColor",
                   RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
    return kFallbackAccent;
  }
  return ((value & 0xFF) << 16) | (value & 0xFF00) | ((value >> 16) & 0xFF);
}

}  // namespace panel
