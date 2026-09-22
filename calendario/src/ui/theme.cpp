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
