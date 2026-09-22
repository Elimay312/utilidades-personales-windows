#pragma once

#include <d2d1.h>

#include <string>

#include "core/dates.h"
#include "nlp/parser.h"
#include "ui/paint.h"
#include "ui/text_input.h"
#include "ui/theme.h"

namespace agenda {

// Everything the popup paints from. The window owns one and changes it as the mouse and the
// keyboard arrive; the snapshot builds one and throws it away. Drawing only ever reads it.
struct PopupModel {
  Date today{};
  Date selected{};
  Month month{};  // the month the grid is showing

  TextInput input;
  std::wstring composition;  // in-flight IME text, drawn underlined at the caret
  bool caretOn = true;

  // What the input says right now, reread on every change. Its spans light up inside the
  // capsule and the rest of it becomes the preview card above.
  nlp::ParsedInput preview;

  // Hover and focus, each walking to its target over kStateMs.
  float dayHover[kGridCells] = {};
  float prevHover = 0.0f;
  float nextHover = 0.0f;
  float focus = 0.0f;

  // The month slide. slideT walks from 0 to 1 over kMonthSlideMs while slideDir is +1 going
  // forward or -1 going back; outside a slide slideDir is 0 and slideFrom is meaningless.
  Month slideFrom{};
  float slideT = 1.0f;
  int slideDir = 0;
};

inline PopupModel MakeModel(Date today) {
  PopupModel model;
  model.today = today;
  model.selected = today;
  model.month = Month{today.year(), today.month()};
  model.slideFrom = model.month;
  return model;
}

// Paints the panel and everything in it: this is the one place the popup is described, so the
// live window and the offscreen snapshot cannot drift apart. `acrylic` false means the system
// backdrop is unavailable (Windows 10), so the panel is painted opaque instead of letting the
// desktop through.
void DrawPopup(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
               const PanelLayout& layout, const PopupModel& model, bool acrylic);

}  // namespace agenda
