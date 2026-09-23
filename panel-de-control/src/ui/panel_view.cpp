#include "ui/panel_view.h"

#include <wrl/client.h>

#include <cmath>
#include <format>
#include <string>

#include "core/i18n.h"
#include "ui/glyphs.h"

using Microsoft::WRL::ComPtr;

namespace panel {
namespace {

constexpr float kTileCircleDip = 28.0f;

D2D1_POINT_2F Point(float x, float y) { return D2D1_POINT_2F{x, y}; }

float MidY(const D2D1_RECT_F& rect) { return (rect.top + rect.bottom) / 2.0f; }

std::wstring Clock(int minute) { return std::format(L"{:02}:{:02}", minute / 60, minute % 60); }

std::wstring Percent(float level) {
  return std::format(L"{} %", static_cast<int>(std::lround(level * 100.0f)));
}

// What each tile says under its name. The state in a word, or the one fact worth a glance.
std::wstring WifiSubtitle(const WifiState& wifi) {
  if (!wifi.available) return std::wstring(T(L"No disponible", L"Unavailable"));
  if (!wifi.on) return std::wstring(T(L"Apagado", L"Off"));
  return wifi.ssid.empty() ? std::wstring(T(L"Sin conexión", L"Not connected")) : wifi.ssid;
}

std::wstring BluetoothSubtitle(const BluetoothState& bt) {
  if (!bt.available) return std::wstring(T(L"No disponible", L"Unavailable"));
  if (!bt.on) return std::wstring(T(L"Apagado", L"Off"));
  if (bt.connected == 0) return std::wstring(T(L"Encendido", L"On"));
  return std::format(L"{} {}", bt.connected, T(L"disp.", bt.connected == 1 ? L"device" : L"devices"));
}

std::wstring NightSubtitle(const NightLightState& night) {
  if (!night.supported) return std::wstring(T(L"No compatible", L"Not supported"));
  if (night.on) {
    if (!night.scheduled) return std::wstring(T(L"Encendida", L"On"));
    return std::format(L"{} {}", T(L"Hasta", L"Until"), Clock(night.toMinute));
  }
  // Off with a schedule: when it comes on next, which is the question somebody looking asks.
  return night.scheduled ? Clock(night.fromMinute) : std::wstring(T(L"Apagada", L"Off"));
}

struct Painter {
  ID2D1RenderTarget* target;
  const Fonts& fonts;
  const Theme& theme;
  ID2D1SolidColorBrush* brush;

  ID2D1SolidColorBrush* With(const D2D1_COLOR_F& color) const {
    brush->SetColor(color);
    return brush;
  }

  void Tile(const D2D1_RECT_F& rect, wchar_t glyph, std::wstring_view title,
            std::wstring_view subtitle, bool on, bool enabled) const {
    const float alpha = enabled ? 1.0f : 0.45f;
    FillRound(target, rect, kRadiusCard, With(Fade(on ? theme.accent : theme.surface, alpha)));
    if (theme.highContrast && !on) StrokeRound(target, rect, kRadiusCard, With(theme.border));

    // On: a white disc with the glyph in the accent, like the wireframe's dot. Off: a quiet
    // disc and the glyph in the text colour.
    const D2D1_POINT_2F center = Point(rect.left + 12.0f + kTileCircleDip / 2.0f, MidY(rect));
    FillCircle(target, center, kTileCircleDip / 2.0f,
               With(Fade(on ? theme.onAccent : theme.track, alpha)));
    DrawGlyph(target, fonts.icon.Get(), glyph, center,
              With(Fade(on ? theme.accent : theme.textPrimary, alpha)));

    const float textLeft = center.x + kTileCircleDip / 2.0f + 10.0f;
    const D2D1_COLOR_F primary = on ? theme.onAccent : theme.textPrimary;
    const D2D1_COLOR_F secondary = on ? Fade(theme.onAccent, 0.8f) : theme.textSecondary;
    DrawTextIn(target, fonts.title.Get(), title,
               D2D1_RECT_F{textLeft, center.y - 17.0f, rect.right - 10.0f, center.y},
               With(Fade(primary, alpha)));
    DrawTextIn(target, fonts.caption.Get(), subtitle,
               D2D1_RECT_F{textLeft, center.y + 1.0f, rect.right - 10.0f, center.y + 16.0f},
               With(Fade(secondary, alpha)));
  }

  void Card(const D2D1_RECT_F& rect) const {
    FillRound(target, rect, kRadiusCard, With(theme.surface));
    if (theme.highContrast) StrokeRound(target, rect, kRadiusCard, With(theme.border));
  }

  // The title line of a card: the title on the left, then on the right the chevron and, while
  // a slider under it is being touched, its percentage.
  void Header(const D2D1_RECT_F& rect, std::wstring_view title, bool open,
              std::wstring_view percent) const {
    const float chevron = 16.0f;
    DrawGlyph(target, fonts.iconSmall.Get(), open ? glyph::kChevronDown : glyph::kChevronRight,
              Point(rect.right - chevron / 2.0f, MidY(rect)), With(theme.textSecondary));
    const float percentWidth = percent.empty() ? 0.0f : 44.0f;
    DrawTextIn(target, fonts.caption.Get(), percent,
               D2D1_RECT_F{rect.right - chevron - 4.0f - percentWidth, rect.top,
                           rect.right - chevron - 4.0f, rect.bottom},
               With(theme.textSecondary), Align::Right);
    DrawTextIn(target, fonts.title.Get(), title,
               D2D1_RECT_F{rect.left, rect.top, rect.right - chevron - 8.0f - percentWidth,
                           rect.bottom},
               With(theme.textPrimary));
  }

  // A thick pill with the glyph inside its left end, filled from the left. The fill never gets
  // narrower than the glyph's circle, so the icon always sits on the fill: at zero the pill
  // reads as a knob at rest, not as a missing icon.
  void Slider(const D2D1_RECT_F& rect, float level, wchar_t glyph, bool dimmed) const {
    const float height = rect.bottom - rect.top;
    const float radius = height / 2.0f;
    FillRound(target, rect, radius, With(theme.track));
    if (theme.highContrast) StrokeRound(target, rect, radius, With(theme.border));

    const float clamped = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
    const float width = height + (rect.right - rect.left - height) * clamped;
    const D2D1_RECT_F fill{rect.left, rect.top, rect.left + width, rect.bottom};
    FillRound(target, fill, radius, With(Fade(theme.fill, dimmed ? 0.4f : 1.0f)));
    DrawGlyph(target, fonts.icon.Get(), glyph, Point(rect.left + radius, MidY(rect)),
              With(theme.onFill));
  }

  // A screen whose brightness cannot be reached keeps its row, with the reason where the
  // slider would be: a row that vanished would look like a monitor that was not detected.
  void Unreachable(const D2D1_RECT_F& rect) const {
    const float radius = (rect.bottom - rect.top) / 2.0f;
    StrokeRound(target, rect, radius, With(theme.border));
    DrawTextIn(target, fonts.caption.Get(),
               T(L"Sin control de brillo (DDC/CI)", L"No brightness control (DDC/CI)"), rect,
               With(theme.textSecondary), Align::Center);
  }
};

void DrawBrightness(const Painter& p, const PanelLayout& layout, const PanelState& state,
                    const ViewState& view) {
  p.Card(layout.brightnessCard);
  const DisplayState* here = HereDisplay(state);

  if (!layout.displayRows.empty()) {
    p.Header(layout.brightnessHeader, T(L"Brillo", L"Brightness"), true, L"");
    for (size_t i = 0; i < layout.displayRows.size() && i < state.displays.size(); ++i) {
      const DisplayRow& row = layout.displayRows[i];
      const DisplayState& display = state.displays[i];
      std::wstring label = display.name;
      if (display.here) label += std::wstring(L" · ") + std::wstring(T(L"aquí", L"here"));
      const bool hot = view.hot == Hot::Display && view.hotDisplay == i;
      DrawTextIn(p.target, p.fonts.body.Get(), label, row.label, p.With(p.theme.textPrimary));
      if (hot && display.reachable) {
        DrawTextIn(p.target, p.fonts.caption.Get(), Percent(display.level), row.label,
                   p.With(p.theme.textSecondary), Align::Right);
      }
      if (display.reachable) {
        p.Slider(row.slider, display.level,
                 display.internal ? glyph::kLaptop : glyph::kMonitor, false);
      } else {
        p.Unreachable(row.slider);
      }
    }
    return;
  }

  std::wstring title(T(L"Brillo", L"Brightness"));
  if (here != nullptr) title += L" · " + here->name;
  const bool hot = view.hot == Hot::Brightness && here != nullptr && here->reachable;
  p.Header(layout.brightnessHeader, title, false, hot ? Percent(here->level) : L"");
  if (here == nullptr || !here->reachable) {
    p.Unreachable(layout.brightnessSlider);
  } else {
    p.Slider(layout.brightnessSlider, here->level, glyph::kBrightness, false);
  }
}

void DrawAudio(const Painter& p, const PanelLayout& layout, const PanelState& state,
               const ViewState& view) {
  const AudioState& audio = state.audio;
  p.Card(layout.audioCard);

  std::wstring title(T(L"Volumen", L"Volume"));
  if (!audio.available) {
    title += L" · " + std::wstring(T(L"sin salida", L"no output"));
  } else if (!audio.device.empty()) {
    title += L" · " + audio.device;
  }
  std::wstring percent;
  if (view.hot == Hot::Volume && audio.available) {
    percent = audio.muted ? std::wstring(T(L"Silencio", L"Muted")) : Percent(audio.level);
  }
  p.Header(layout.audioHeader, title, !layout.outputRows.empty(), percent);
  p.Slider(layout.audioSlider, audio.level, audio.muted ? glyph::kMute : glyph::kVolume,
           audio.muted);

  if (layout.outputRows.empty()) return;
  p.target->FillRectangle(layout.outputsDivider, p.With(p.theme.border));
  const float alpha = audio.canSwitch ? 1.0f : 0.5f;
  for (size_t i = 0; i < layout.outputRows.size() && i < audio.outputs.size(); ++i) {
    const D2D1_RECT_F& row = layout.outputRows[i];
    const AudioOutput& output = audio.outputs[i];
    const float mid = MidY(row);
    DrawGlyph(p.target, p.fonts.icon.Get(),
              output.headphones ? glyph::kHeadphones : glyph::kSpeakers,
              Point(row.left + 18.0f, mid), p.With(Fade(p.theme.textPrimary, alpha)));
    DrawTextIn(p.target, p.fonts.body.Get(), output.name,
               D2D1_RECT_F{row.left + 40.0f, row.top, row.right - 32.0f, row.bottom},
               p.With(Fade(p.theme.textPrimary, alpha)));
    if (output.isDefault) {
      DrawGlyph(p.target, p.fonts.iconSmall.Get(), glyph::kCheck, Point(row.right - 16.0f, mid),
                p.With(p.theme.accent));
    }
  }
}

void DrawApps(const Painter& p, const PanelLayout& layout, const PanelState& state) {
  for (size_t i = 0; i < layout.apps.size() && i < state.apps.size(); ++i) {
    const D2D1_RECT_F& cell = layout.apps[i];
    const AppEntry& app = state.apps[i];
    // Not installed on this machine: still there, so the row keeps its shape, but faded.
    const float alpha = app.installed ? 1.0f : 0.35f;
    const float radius = kAppCircleDip / 2.0f;
    const D2D1_POINT_2F center = Point((cell.left + cell.right) / 2.0f, cell.top + radius);

    FillCircle(p.target, center, radius, p.With(Fade(p.theme.surface, alpha)));
    if (p.theme.highContrast) {
      p.target->DrawEllipse(D2D1_ELLIPSE{center, radius - 0.5f, radius - 0.5f},
                            p.With(p.theme.border));
    }
    DrawGlyph(p.target, p.fonts.icon.Get(), app.glyph, center,
              p.With(Fade(app.running ? p.theme.textPrimary : p.theme.textSecondary, alpha)));
    if (app.running) {
      // The dot sits on the circle's edge at the bottom right, ringed in the panel's colour so
      // it reads as a badge and not as part of the icon.
      const D2D1_POINT_2F dot = Point(center.x + radius * 0.72f, center.y + radius * 0.72f);
      FillCircle(p.target, dot, 5.5f, p.With(p.theme.panelOpaque));
      FillCircle(p.target, dot, 4.0f, p.With(p.theme.running));
    }
    DrawTextIn(p.target, p.fonts.caption.Get(), app.name,
               D2D1_RECT_F{cell.left, cell.top + kAppCircleDip + 3.0f, cell.right, cell.bottom},
               p.With(Fade(p.theme.textSecondary, alpha)), Align::Center);
  }
}

}  // namespace

void DrawPanel(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
               const PanelLayout& layout, const PanelState& state, const ViewState& view,
               bool acrylic) {
  ComPtr<ID2D1SolidColorBrush> brush;
  if (FAILED(target->CreateSolidColorBrush(acrylic ? theme.panel : theme.panelOpaque, &brush))) {
    return;
  }
  // ClearType needs opaque pixels under the glyphs, and this panel is translucent over the
  // acrylic, so both the window and the snapshot antialias text in greyscale.
  target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

  const D2D1_RECT_F whole{0.0f, 0.0f, layout.width, layout.height};
  FillRound(target, whole, kRadiusPanel, brush.Get());
  brush->SetColor(theme.border);
  StrokeRound(target, whole, kRadiusPanel, brush.Get());
  if (!fonts.ok()) return;  // no text formats means an empty panel, not a crash

  const Painter p{target, fonts, theme, brush.Get()};
  p.Tile(layout.tiles[0], glyph::kWifi, L"Wi-Fi", WifiSubtitle(state.wifi), state.wifi.on,
         state.wifi.available);
  p.Tile(layout.tiles[1], glyph::kBluetooth, L"Bluetooth", BluetoothSubtitle(state.bluetooth),
         state.bluetooth.on, state.bluetooth.available);
  p.Tile(layout.tiles[2], glyph::kMoon, T(L"Luz nocturna", L"Night light"),
         NightSubtitle(state.night), state.night.on, state.night.supported);
  // Settings: not wired to anything yet (CLAUDE.md), and honest about it.
  p.Tile(layout.tiles[3], glyph::kSettings, T(L"Configuración", L"Settings"),
         T(L"Próximamente", L"Coming soon"), false, true);

  DrawBrightness(p, layout, state, view);
  DrawAudio(p, layout, state, view);
  DrawApps(p, layout, state);
  if (!state.notice.empty()) {
    FillRound(target, layout.notice, kRadiusCard, p.With(p.theme.surface));
    DrawTextIn(target, fonts.note.Get(), state.notice, Inset(layout.notice, 12.0f, 0.0f),
               p.With(p.theme.textSecondary));
  }
}

}  // namespace panel
