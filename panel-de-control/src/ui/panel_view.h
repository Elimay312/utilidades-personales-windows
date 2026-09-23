#pragma once

#include <d2d1.h>

#include "model/state.h"
#include "ui/layout.h"
#include "ui/paint.h"
#include "ui/theme.h"

namespace panel {

// Which slider the mouse is on, so its percentage shows in the card's header. Everything else
// about the view -- which cards are open -- is in Expanded, because the layout needs it too.
enum class Hot { None, Brightness, Volume, Display };

struct ViewState {
  Expanded open;
  Hot hot = Hot::None;
  size_t hotDisplay = 0;  // which row, when hot is Display
};

// Paints the whole panel at the origin of `target`, in DIP. `acrylic` says whether the system
// backdrop is behind it: the panel colour is translucent then and opaque otherwise.
void DrawPanel(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
               const PanelLayout& layout, const PanelState& state, const ViewState& view,
               bool acrylic);

}  // namespace panel
