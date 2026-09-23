#include "ui/panel_view.h"

#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include "core/i18n.h"
#include "ui/glyphs.h"

using Microsoft::WRL::ComPtr;

namespace panel {
namespace {

constexpr float kTileCircleDip = 28.0f;
constexpr float kPressScale = 0.97f;    // how far a pressed tile or chip sinks
constexpr float kRingGapDip = 3.0f;     // the focus ring sits this far outside what it rings
constexpr float kRingWidthDip = 2.0f;
constexpr float kWashRadiusDip = 6.0f;  // hover wash behind a header or a row

D2D1_POINT_2F Point(float x, float y) { return D2D1_POINT_2F{x, y}; }

float MidY(const D2D1_RECT_F& rect) { return (rect.top + rect.bottom) / 2.0f; }

D2D1_POINT_2F Middle(const D2D1_RECT_F& rect) {
  return Point((rect.left + rect.right) / 2.0f, MidY(rect));
}

float Unit(float t) { return std::clamp(t, 0.0f, 1.0f); }

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

// Scales what is drawn inside it around `center`, on top of whatever transform was there (the
// snapshot's margin, for one), and puts that transform back when it goes.
class Sink {
 public:
  Sink(ID2D1RenderTarget* target, D2D1_POINT_2F center, float press) : target_(target) {
    target_->GetTransform(&saved_);
    if (press <= 0.0f) return;
    const float scale = 1.0f - (1.0f - kPressScale) * press;
    target_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, center) * saved_);
  }
  ~Sink() { target_->SetTransform(saved_); }
  Sink(const Sink&) = delete;
  Sink& operator=(const Sink&) = delete;

 private:
  ID2D1RenderTarget* target_;
  D2D1_MATRIX_3X2_F saved_{};
};

struct Painter {
  ID2D1RenderTarget* target;
  const Fonts& fonts;
  const Theme& theme;
  ID2D1SolidColorBrush* brush;
  const ViewState& view;
  float alpha = 1.0f;  // everything painted fades by this: content crossing inside an opening card

  ID2D1SolidColorBrush* With(const D2D1_COLOR_F& color) const {
    brush->SetColor(Fade(color, alpha));
    return brush;
  }

  void Wash(const D2D1_RECT_F& rect, float radius, float hover) const {
    if (hover <= 0.0f) return;
    FillRound(target, rect, radius, With(Fade(theme.hover, hover)));
  }

  void Tile(size_t index, const D2D1_RECT_F& rect, wchar_t glyph, std::wstring_view title,
            std::wstring_view subtitle, bool on, bool enabled) const {
    const Target self{Part::Tile, index};
    const Sink sink(target, Middle(rect), view.Press(self));
    const float dim = enabled ? 1.0f : 0.45f;
    FillRound(target, rect, kRadiusCard, With(Fade(on ? theme.accent : theme.surface, dim)));
    if (theme.highContrast && !on) StrokeRound(target, rect, kRadiusCard, With(theme.border));
    Wash(rect, kRadiusCard, view.Hover(self));

    // On: a white disc with the glyph in the accent, like the wireframe's dot. Off: a quiet
    // disc and the glyph in the text colour.
    const D2D1_POINT_2F center = Point(rect.left + 12.0f + kTileCircleDip / 2.0f, MidY(rect));
    FillCircle(target, center, kTileCircleDip / 2.0f,
               With(Fade(on ? theme.onAccent : theme.track, dim)));
    DrawGlyph(target, fonts.icon.Get(), glyph, center,
              With(Fade(on ? theme.accent : theme.textPrimary, dim)));

    const float textLeft = center.x + kTileCircleDip / 2.0f + 10.0f;
    const D2D1_COLOR_F primary = on ? theme.onAccent : theme.textPrimary;
    const D2D1_COLOR_F secondary = on ? Fade(theme.onAccent, 0.8f) : theme.textSecondary;
    DrawTextIn(target, fonts.title.Get(), title,
               D2D1_RECT_F{textLeft, center.y - 17.0f, rect.right - 10.0f, center.y},
               With(Fade(primary, dim)));
    DrawTextIn(target, fonts.caption.Get(), subtitle,
               D2D1_RECT_F{textLeft, center.y + 1.0f, rect.right - 10.0f, center.y + 16.0f},
               With(Fade(secondary, dim)));
  }

  void Card(const D2D1_RECT_F& rect) const {
    FillRound(target, rect, kRadiusCard, With(theme.surface));
    if (theme.highContrast) StrokeRound(target, rect, kRadiusCard, With(theme.border));
  }

  // The title line of a card: the title on the left, then on the right the chevron and, while
  // a slider under it is being touched, its percentage. The chevron turns a quarter as the card
  // opens, on the card's own spring, so mid-way it still says which way the card is going.
  void Header(Target self, const D2D1_RECT_F& rect, std::wstring_view title, float open,
              std::wstring_view percent) const {
    Wash(Inset(rect, -6.0f, -4.0f), kWashRadiusDip, view.Hover(self));
    const float chevron = 16.0f;
    const D2D1_POINT_2F pivot = Point(rect.right - chevron / 2.0f, MidY(rect));
    D2D1_MATRIX_3X2_F saved{};
    target->GetTransform(&saved);
    target->SetTransform(D2D1::Matrix3x2F::Rotation(90.0f * Unit(open), pivot) * saved);
    DrawGlyph(target, fonts.iconSmall.Get(), glyph::kChevronRight, pivot, With(theme.textSecondary));
    target->SetTransform(saved);

    const float percentWidth = percent.empty() ? 0.0f : 56.0f;
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
  void Slider(const D2D1_RECT_F& rect, float level, wchar_t glyph, bool dimmed,
              float glyphHover = 0.0f) const {
    const float height = rect.bottom - rect.top;
    const float radius = height / 2.0f;
    FillRound(target, rect, radius, With(theme.track));
    if (theme.highContrast) StrokeRound(target, rect, radius, With(theme.border));

    const float width = height + (rect.right - rect.left - height) * Unit(level);
    const D2D1_RECT_F fill{rect.left, rect.top, rect.left + width, rect.bottom};
    FillRound(target, fill, radius, With(Fade(theme.fill, dimmed ? 0.4f : 1.0f)));
    const D2D1_POINT_2F center = Point(rect.left + radius, MidY(rect));
    // The mute button: a disc under the glyph while the mouse is on it.
    if (glyphHover > 0.0f) {
      FillCircle(target, center, radius - 3.0f, With(Fade(theme.onFill, 0.18f * glyphHover)));
    }
    DrawGlyph(target, fonts.icon.Get(), glyph, center, With(theme.onFill));
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

void DrawBrightness(Painter& p, const PanelLayout& layout, const PanelState& state) {
  p.Card(layout.brightnessCard);
  const ViewState& view = p.view;
  const float open = view.open.brightness;
  const DisplayState* here = HereDisplay(state);

  // The title names the screen while the one slider is what shows, and just says "Brillo" once
  // the rows do. It switches halfway, where both contents are faint.
  std::wstring title(T(L"Brillo", L"Brightness"));
  if (open < 0.5f && here != nullptr) title += L" · " + here->name;
  std::wstring percent;
  if (view.hot == Target{Part::BrightnessSlider} && here != nullptr && here->reachable) {
    percent = Percent(here->level);
  }
  p.Header(Target{Part::BrightnessHeader}, layout.brightnessHeader, title, open, percent);

  // Everything under the header is clipped to the card while it grows, and the two contents
  // cross. The one slider does not just fade where it is -- it would sit on the first row's
  // name -- it travels to its own screen's row as it fades, so the two meet in one place and
  // the slider reads as having become that row.
  p.target->PushAxisAlignedClip(layout.brightnessCard, D2D1_ANTIALIAS_MODE_ALIASED);
  const float rows = layout.displayRows.empty() ? 0.0f : Unit(open);
  if (rows < 1.0f) {
    D2D1_RECT_F at = layout.brightnessSlider;
    if (rows > 0.0f && here != nullptr) {
      const size_t index = static_cast<size_t>(here - state.displays.data());
      if (index < layout.displayRows.size()) {
        const D2D1_RECT_F& to = layout.displayRows[index].slider;
        at = D2D1_RECT_F{Lerp(at.left, to.left, rows), Lerp(at.top, to.top, rows),
                         Lerp(at.right, to.right, rows), Lerp(at.bottom, to.bottom, rows)};
      }
    }
    // Gone by halfway: the two never show at full strength in the same frame.
    p.alpha = Unit(1.0f - 2.0f * rows);
    if (here == nullptr || !here->reachable) {
      p.Unreachable(at);
    } else {
      p.Slider(at, here->level, glyph::kBrightness, false);
    }
  }
  if (rows > 0.0f) {
    p.alpha = rows;
    for (size_t i = 0; i < layout.displayRows.size() && i < state.displays.size(); ++i) {
      const DisplayRow& row = layout.displayRows[i];
      const DisplayState& display = state.displays[i];
      std::wstring label = display.name;
      if (display.here) label += std::wstring(L" · ") + std::wstring(T(L"aquí", L"here"));
      DrawTextIn(p.target, p.fonts.body.Get(), label, row.label, p.With(p.theme.textPrimary));
      if (view.hot == Target{Part::DisplaySlider, i} && display.reachable) {
        DrawTextIn(p.target, p.fonts.caption.Get(), Percent(display.level), row.label,
                   p.With(p.theme.textSecondary), Align::Right);
      }
      if (display.reachable) {
        p.Slider(row.slider, display.level, display.internal ? glyph::kLaptop : glyph::kMonitor,
                 false);
      } else {
        p.Unreachable(row.slider);
      }
    }
  }
  p.alpha = 1.0f;
  p.target->PopAxisAlignedClip();
}

void DrawAudio(Painter& p, const PanelLayout& layout, const PanelState& state) {
  const AudioState& audio = state.audio;
  const ViewState& view = p.view;
  p.Card(layout.audioCard);

  std::wstring title(T(L"Volumen", L"Volume"));
  if (!audio.available) {
    title += L" · " + std::wstring(T(L"sin salida", L"no output"));
  } else if (!audio.device.empty()) {
    title += L" · " + audio.device;
  }
  std::wstring percent;
  if ((view.hot == Target{Part::VolumeSlider} || view.hot == Target{Part::Mute}) &&
      audio.available) {
    percent = audio.muted ? std::wstring(T(L"Silencio", L"Muted")) : Percent(audio.level);
  }
  p.Header(Target{Part::AudioHeader}, layout.audioHeader, title, view.open.audio, percent);
  p.Slider(layout.audioSlider, audio.level, audio.muted ? glyph::kMute : glyph::kVolume,
           audio.muted, view.Hover(Target{Part::Mute}));

  if (layout.outputRows.empty()) return;
  p.target->PushAxisAlignedClip(layout.audioCard, D2D1_ANTIALIAS_MODE_ALIASED);
  p.alpha = Unit(view.open.audio);
  p.target->FillRectangle(layout.outputsDivider, p.With(p.theme.border));
  const float usable = audio.canSwitch ? 1.0f : 0.5f;
  for (size_t i = 0; i < layout.outputRows.size() && i < audio.outputs.size(); ++i) {
    const D2D1_RECT_F& row = layout.outputRows[i];
    const AudioOutput& output = audio.outputs[i];
    const float mid = MidY(row);
    p.Wash(row, kWashRadiusDip, view.Hover(Target{Part::Output, i}));
    DrawGlyph(p.target, p.fonts.icon.Get(),
              output.headphones ? glyph::kHeadphones : glyph::kSpeakers,
              Point(row.left + 18.0f, mid), p.With(Fade(p.theme.textPrimary, usable)));
    DrawTextIn(p.target, p.fonts.body.Get(), output.name,
               D2D1_RECT_F{row.left + 40.0f, row.top, row.right - 32.0f, row.bottom},
               p.With(Fade(p.theme.textPrimary, usable)));
    if (output.isDefault) {
      DrawGlyph(p.target, p.fonts.iconSmall.Get(), glyph::kCheck, Point(row.right - 16.0f, mid),
                p.With(p.theme.accent));
    }
  }
  p.alpha = 1.0f;
  p.target->PopAxisAlignedClip();
}

D2D1_RECT_F AppCircle(const D2D1_RECT_F& cell) {
  const float radius = kAppCircleDip / 2.0f;
  const float cx = (cell.left + cell.right) / 2.0f;
  return D2D1_RECT_F{cx - radius, cell.top, cx + radius, cell.top + kAppCircleDip};
}

void DrawApps(const Painter& p, const PanelLayout& layout, const PanelState& state) {
  for (size_t i = 0; i < layout.apps.size() && i < state.apps.size(); ++i) {
    const D2D1_RECT_F& cell = layout.apps[i];
    const AppEntry& app = state.apps[i];
    const Target self{Part::App, i};
    // Not installed on this machine: still there, so the row keeps its shape, but faded.
    const float dim = app.installed ? 1.0f : 0.35f;
    const float radius = kAppCircleDip / 2.0f;
    const D2D1_POINT_2F center = Middle(AppCircle(cell));
    const Sink sink(p.target, center, p.view.Press(self));

    FillCircle(p.target, center, radius, p.With(Fade(p.theme.surface, dim)));
    const float hover = p.view.Hover(self);
    if (hover > 0.0f) FillCircle(p.target, center, radius, p.With(Fade(p.theme.hover, hover)));
    if (p.theme.highContrast) {
      p.target->DrawEllipse(D2D1_ELLIPSE{center, radius - 0.5f, radius - 0.5f},
                            p.With(p.theme.border));
    }
    DrawGlyph(p.target, p.fonts.icon.Get(), app.glyph, center,
              p.With(Fade(app.running ? p.theme.textPrimary : p.theme.textSecondary, dim)));
    if (app.running) {
      // The dot sits on the circle's edge at the bottom right, ringed in the panel's colour so
      // it reads as a badge and not as part of the icon.
      const D2D1_POINT_2F dot = Point(center.x + radius * 0.72f, center.y + radius * 0.72f);
      FillCircle(p.target, dot, 5.5f, p.With(p.theme.panelOpaque));
      FillCircle(p.target, dot, 4.0f, p.With(p.theme.running));
    }
    DrawTextIn(p.target, p.fonts.caption.Get(), app.name,
               D2D1_RECT_F{cell.left, cell.top + kAppCircleDip + 3.0f, cell.right, cell.bottom},
               p.With(Fade(p.theme.textSecondary, dim)), Align::Center);
  }
}

// The keyboard's ring: 2 DIP in the text colour, 3 DIP outside what has the focus, with that
// thing's radius plus the gap so it follows its shape (Agenda's ring, calendario/CLAUDE.md).
void DrawFocus(const Painter& p, const PanelLayout& layout) {
  const ViewState& view = p.view;
  if (!view.focusVisible || view.focus.empty()) return;
  D2D1_RECT_F rect = RectOf(layout, view.focus);
  float radius = kRadiusCard;
  switch (view.focus.part) {
    case Part::BrightnessSlider:
    case Part::DisplaySlider:
    case Part::VolumeSlider:
      radius = kSliderHeightDip / 2.0f;
      break;
    case Part::App:
      rect = AppCircle(rect);
      radius = kAppCircleDip / 2.0f;
      break;
    case Part::BrightnessHeader:
    case Part::AudioHeader:
      rect = Inset(rect, -6.0f, -4.0f);
      radius = kWashRadiusDip;
      break;
    case Part::Output:
      radius = kWashRadiusDip;
      break;
    default:
      break;
  }
  const float out = kRingGapDip + kRingWidthDip;
  StrokeRound(p.target, Inset(rect, -out, -out), radius + out, p.With(p.theme.textPrimary),
              kRingWidthDip);
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

  Painter p{target, fonts, theme, brush.Get(), view};
  p.Tile(0, layout.tiles[0], glyph::kWifi, L"Wi-Fi", WifiSubtitle(state.wifi), state.wifi.on,
         state.wifi.available);
  p.Tile(1, layout.tiles[1], glyph::kBluetooth, L"Bluetooth", BluetoothSubtitle(state.bluetooth),
         state.bluetooth.on, state.bluetooth.available);
  p.Tile(2, layout.tiles[2], glyph::kMoon, T(L"Luz nocturna", L"Night light"),
         NightSubtitle(state.night), state.night.on, state.night.supported);
  // Settings: not wired to anything yet (CLAUDE.md), and honest about it.
  p.Tile(3, layout.tiles[3], glyph::kSettings, T(L"Configuración", L"Settings"),
         T(L"Próximamente", L"Coming soon"), false, true);

  DrawBrightness(p, layout, state);
  DrawAudio(p, layout, state);
  DrawApps(p, layout, state);
  if (!state.notice.empty()) {
    FillRound(target, layout.notice, kRadiusCard, p.With(theme.surface));
    DrawTextIn(target, fonts.note.Get(), state.notice, Inset(layout.notice, 12.0f, 0.0f),
               p.With(theme.textSecondary));
  }
  DrawFocus(p, layout);
}

}  // namespace panel
