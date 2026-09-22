#pragma once

#include <d2d1.h>

namespace agenda {

constexpr D2D1_COLOR_F Rgb(UINT32 rgb, float alpha = 1.0f) {
  return D2D1_COLOR_F{static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                      static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                      static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
}

// Type scale from CLAUDE.md, in DIPs.
inline constexpr float kFontLabel = 11.0f;   // weekday initials
inline constexpr float kFontDay = 12.0f;     // day numbers
inline constexpr float kFontEvent = 13.0f;   // event cards and the input
inline constexpr float kFontTitle = 15.0f;   // month title, semibold

// Shapes.
inline constexpr float kRadiusPanel = 14.0f;
inline constexpr float kRadiusCard = 8.0f;
inline constexpr float kDayCircleDip = 24.0f;
inline constexpr float kEventDotDip = 4.0f;
inline constexpr float kEventBarDip = 3.0f;

// Spacing, on the four DIP grid.
inline constexpr float kPaddingDip = 16.0f;
inline constexpr float kGapDip = 4.0f;

// How long a hover or focus state takes to fade in or out.
inline constexpr float kStateMs = 100.0f;
// How long the grid takes to slide when the month changes.
inline constexpr float kMonthSlideMs = 160.0f;

// Every colour the popup can paint with. Two of these exist: the dark theme is the one the
// design system describes, and the light one is derived from it.
struct Theme {
  bool light = false;
  D2D1_COLOR_F panel;         // over the acrylic, so it carries the 85% alpha
  D2D1_COLOR_F panelOpaque;   // same colour at full alpha, for Windows 10 with no backdrop
  D2D1_COLOR_F surface;       // event cards and the input capsule
  D2D1_COLOR_F textPrimary;
  D2D1_COLOR_F textSecondary;
  D2D1_COLOR_F textMuted;     // days that belong to the month before or after
  D2D1_COLOR_F accent;        // today, and the focus ring
  D2D1_COLOR_F onAccent;      // the day number inside the today circle
  D2D1_COLOR_F alt;           // the second calendar colour
  D2D1_COLOR_F border;        // hairlines
  D2D1_COLOR_F hover;         // the wash under a hovered day or arrow
  D2D1_COLOR_F selection;     // selected text in the input
};

Theme DarkTheme();
Theme LightTheme();

// Reads AppsUseLightTheme. Read only: Agenda never writes to the registry.
bool SystemUsesLightTheme();

inline Theme SystemTheme() { return SystemUsesLightTheme() ? LightTheme() : DarkTheme(); }

}  // namespace agenda
