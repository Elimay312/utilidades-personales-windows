#pragma once

#include <d2d1.h>

#include <filesystem>
#include <string_view>

namespace agenda {

// Renders a view offscreen to a PNG with Direct2D over a WIC bitmap, which is how CLAUDE.md
// says to review the design: no desktop screenshots. Only "popup" exists so far.
// `panel` is the panel size in DIP to render; a zero size means the one the design was drawn
// at, which is what the committed PNGs use so they never depend on the machine.
bool RenderSnapshot(std::wstring_view view, std::wstring_view theme, D2D1_SIZE_F panel,
                    const std::filesystem::path& out);

}  // namespace agenda
