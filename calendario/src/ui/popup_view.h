#pragma once

#include <d2d1.h>

#include <string>
#include <vector>

#include "core/dates.h"
#include "data/model.h"
#include "nlp/parser.h"
#include "ui/layout.h"
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

  // What the day list paints and which days of the grid carry a dot. The window fills both
  // from the store; drawing never touches SQLite, because this same DrawPopup is what the
  // offscreen snapshot calls.
  std::vector<DayItem> day;
  std::vector<DayDot> dots;

  // One card animates at a time, and each is held by uid rather than by position: the list is
  // rebuilt from the database whenever the worker finishes, and an index would then point at
  // whatever moved into that slot.
  std::wstring enterUid;   // the card that was just created, sliding in
  float enterT = 1.0f;
  std::wstring strikeUid;  // the task whose line is drawing itself; the rest are already struck
  float strikeT = 0.0f;

  // The discreet notice over the tail of the list: "Creado / Deshacer" while undo is still on
  // the table. It borrows the preview card's rectangle, which is free right after Enter.
  std::wstring toast;
  float toastT = 0.0f;

  // There is a Google account and the last thing tried did not reach it. A dot in the header
  // and nothing else: what was written is safe in the cache and goes up on its own when the
  // network comes back, so this is information and not a problem to solve.
  bool offline = false;

  // Where the keyboard is, outlined, once the keyboard has been used: a day of the grid, a
  // card, a calendar, a control of the detail panel. The window works it out from the layout
  // it already has; drawing only draws it, last, over everything.
  bool ringOn = false;
  D2D1_RECT_F ring{};
  float ringRadius = 0.0f;
  // The card the keyboard is on, which the list keeps on screen the way it keeps the one that
  // was just created; -1 when the keyboard is elsewhere.
  int focusCard = -1;

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

// The live preview and the notice share one rectangle above the capsule, and they never share
// a moment: Enter is what empties the capsule, which is what makes the preview go away.
inline bool ShowingPreview(const PopupModel& model) { return !model.input.empty(); }
inline bool ShowingToast(const PopupModel& model) {
  return !ShowingPreview(model) && model.toastT > 0.0f && !model.toast.empty();
}

// Where the day's cards land. Asked from one place so the drawing and the click cannot end up
// with different answers about how much room the list had.
inline D2D1_RECT_F DayListRect(const PanelLayout& layout, const PopupModel& model) {
  return DayListRect(layout, ShowingPreview(model) || ShowingToast(model));
}

// Paints the panel and everything in it: this is the one place the popup is described, so the
// live window and the offscreen snapshot cannot drift apart. `acrylic` false means the system
// backdrop is unavailable (Windows 10), so the panel is painted opaque instead of letting the
// desktop through.
void DrawPopup(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
               const PanelLayout& layout, const PopupModel& model, bool acrylic);

// The three layers DrawPopup is made of, apart so the expansion can put the app between them:
// the rounded panel at any size and radius, the month and the day list (the list at
// `listAlpha`, fading out as the app arrives), and the capsule with its preview and notice on
// top of everything, wherever `layout.input()` says it is now.
void DrawPanel(ID2D1RenderTarget* target, const Theme& theme, D2D1_SIZE_F size, float radius,
               bool acrylic);
void DrawPopupBody(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   const PanelLayout& layout, const PopupModel& model, float listAlpha);
void DrawPopupInput(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                    const PanelLayout& layout, const PopupModel& model);

// The keyboard's ring, when there is one: two DIP in the primary text colour, the focus visual
// Windows draws around its own controls.
void DrawFocusRing(ID2D1RenderTarget* target, const Theme& theme, const PanelLayout& layout,
                   const PopupModel& model);

// The card the list keeps on screen: the one arriving, or else the one with the keyboard.
int KeptCard(const PopupModel& model);

}  // namespace agenda
