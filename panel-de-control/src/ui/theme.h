#pragma once

#include <d2d1.h>

#include <string_view>

namespace panel {

constexpr D2D1_COLOR_F Rgb(UINT32 rgb, float alpha = 1.0f) {
  return D2D1_COLOR_F{static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                      static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                      static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
}

// The accent when Windows does not say one, and always in a snapshot: a committed PNG must not
// change colour with the machine that rendered it.
inline constexpr UINT32 kFallbackAccent = 0x0A84FF;

// Every colour the panel paints with. The dark palette is Agenda's, so the two popups that open
// in the same corner look like they belong together (calendario/CLAUDE.md, design system).
struct Theme {
  bool light = false;
  // Windows high contrast is on: every colour is one the user picked, nothing is translucent,
  // and what was told apart by a tint gets an outline instead.
  bool highContrast = false;
  D2D1_COLOR_F panel;          // over the acrylic, so it carries the 85% alpha
  D2D1_COLOR_F panelOpaque;    // the same at full alpha: closing, and Windows 10
  D2D1_COLOR_F surface;        // tiles that are off, cards
  D2D1_COLOR_F textPrimary;
  D2D1_COLOR_F textSecondary;
  D2D1_COLOR_F accent;         // tiles that are on, the slider fill's tint, the default output
  D2D1_COLOR_F onAccent;       // text and glyphs on the accent
  D2D1_COLOR_F track;          // the empty part of a slider
  D2D1_COLOR_F fill;           // the filled part of a slider
  D2D1_COLOR_F onFill;         // the glyph sitting inside the fill
  D2D1_COLOR_F border;         // hairlines
  D2D1_COLOR_F hover;          // the wash under something the mouse is on
  D2D1_COLOR_F running;        // the dot on a utility that is running
};

// `accent` is 0xRRGGBB.
Theme DarkTheme(UINT32 accent = kFallbackAccent);
Theme LightTheme(UINT32 accent = kFallbackAccent);
// Built from GetSysColor while high contrast is on. Off, it falls back to the colours of
// "Contraste nocturno", so --theme=contrast renders the same PNG on any machine.
Theme HighContrastTheme();

// Reads AppsUseLightTheme. Read only: the theme never writes to the registry.
bool SystemUsesLightTheme();
bool HighContrastOn();
// The Windows accent colour as 0xRRGGBB, or kFallbackAccent when it cannot be read.
UINT32 SystemAccent();

// "dark", "light" or "contrast" from --theme or the config; anything else follows Windows.
// High contrast wins over a preference: somebody who turned it on needs it everywhere.
// `systemAccent` is false for snapshots.
inline Theme ResolveTheme(std::wstring_view choice, bool systemAccent = true) {
  if (choice == L"contrast" || HighContrastOn()) return HighContrastTheme();
  const UINT32 accent = systemAccent ? SystemAccent() : kFallbackAccent;
  const bool light = choice == L"light" || (choice != L"dark" && SystemUsesLightTheme());
  return light ? LightTheme(accent) : DarkTheme(accent);
}

}  // namespace panel
