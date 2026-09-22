#pragma once

#include <d2d1.h>

namespace agenda {

// Paints the popup panel and nothing else: this is the one place the panel is described, so
// the live window and the offscreen snapshot cannot drift apart. `acrylic` false means the
// system backdrop is unavailable (Windows 10), so the panel is painted opaque instead of
// letting the desktop through.
void DrawPopup(ID2D1RenderTarget* target, D2D1_SIZE_F sizeDip, bool acrylic);

}  // namespace agenda
