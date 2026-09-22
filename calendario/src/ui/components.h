#pragma once

#include <d2d1.h>
#include <dwrite.h>

#include "ui/layout.h"
#include "ui/paint.h"
#include "ui/popup_view.h"
#include "ui/sample_data.h"

namespace agenda {

// The pieces the popup is made of. They are free functions over a render target because there
// is exactly one of each on screen: a class per widget would be a hierarchy with a single
// leaf. `brush` is the one scratch solid brush the frame reuses, recoloured as it goes, and
// `layout` carries every measurement, already scaled to the panel this monitor got.

// One month of six by seven cells. `hover` is the 42 entry fade array, or null for the month
// that is sliding away. `offsetX` shifts the whole grid during that slide.
void DrawMonthGrid(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   const PanelLayout& layout, ID2D1SolidColorBrush* brush, Month month,
                   Date today, Date selected, const float* hover, float offsetX);

// A card: a bar of calendar colour down the left, the time in secondary and the title in
// primary. `more` above zero adds the "+N" counter that says the day has more than fits.
void DrawEventCard(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   const PanelLayout& layout, ID2D1SolidColorBrush* brush,
                   const D2D1_RECT_F& rect, const SampleEvent& event, int more);

// The capsule: placeholder, text, selection, IME composition and caret. `accent` is a second
// brush, and not the scratch one, because the recognised spans hand it to DirectWrite as a
// drawing effect: it has to still hold the accent colour when the layout is finally drawn.
void DrawTextInput(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                   const PanelLayout& layout, ID2D1SolidColorBrush* brush,
                   ID2D1SolidColorBrush* accent, const PopupModel& model);

// What the line will become, above the capsule: a card like an event card, with the same bar
// of colour down the left, saying "Mañana · 17:00–18:00 · Dentista" or "Tarea sin fecha".
void DrawPreviewCard(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
                     const PanelLayout& layout, ID2D1SolidColorBrush* brush,
                     const PopupModel& model);

// Where in the text a click at panel coordinate `x` lands, so the mouse can place the caret.
size_t InputIndexAt(const Fonts& fonts, const PanelLayout& layout, const PopupModel& model,
                    float x);

// Where the caret sits, in panel coordinates, so the IME can put its candidate window there.
D2D1_POINT_2F InputCaretPoint(const Fonts& fonts, const PanelLayout& layout,
                              const PopupModel& model);

}  // namespace agenda
