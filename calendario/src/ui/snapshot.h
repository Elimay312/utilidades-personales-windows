#pragma once

#include <filesystem>
#include <string_view>

namespace agenda {

// Renders a view offscreen to a PNG with Direct2D over a WIC bitmap, which is how CLAUDE.md
// says to review the design: no desktop screenshots. Only "popup" exists so far.
bool RenderSnapshot(std::wstring_view view, const std::filesystem::path& out);

}  // namespace agenda
