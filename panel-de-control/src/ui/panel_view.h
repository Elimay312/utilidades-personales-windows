#pragma once

#include <d2d1.h>

#include <vector>

#include "model/state.h"
#include "ui/controls.h"
#include "ui/layout.h"
#include "ui/paint.h"
#include "ui/theme.h"

namespace panel {

// How far into its hover and its press a target is, each 0..1. Only targets with something
// showing have one; the window adds them as the mouse arrives and drops them once faded out.
struct Ink {
  Target target;
  float hover = 0.0f;
  float press = 0.0f;
};

// Everything about the view that is not the system's state.
struct ViewState {
  Expanded open;
  Target hot;                 // the slider whose percentage shows in its card's header
  Target focus;               // where the keyboard is
  bool focusVisible = false;  // the ring shows only after a key, like Windows (a click hides it)
  std::vector<Ink> inks;

  const Ink* InkOf(Target target) const {
    for (const Ink& ink : inks) {
      if (ink.target == target) return &ink;
    }
    return nullptr;
  }
  float Hover(Target target) const {
    const Ink* ink = InkOf(target);
    return ink != nullptr ? ink->hover : 0.0f;
  }
  float Press(Target target) const {
    const Ink* ink = InkOf(target);
    return ink != nullptr ? ink->press : 0.0f;
  }
};

// Paints the whole panel at the origin of `target`, in DIP. `acrylic` says whether the system
// backdrop is behind it: the panel colour is translucent then and opaque otherwise.
void DrawPanel(ID2D1RenderTarget* target, const Fonts& fonts, const Theme& theme,
               const PanelLayout& layout, const PanelState& state, const ViewState& view,
               bool acrylic);

}  // namespace panel
