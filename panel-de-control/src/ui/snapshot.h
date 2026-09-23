#pragma once

#include <filesystem>
#include <string_view>

namespace panel {

// Renders a view (core/options.h, kSnapshotViews) offscreen to a PNG with Direct2D over a WIC
// bitmap, always from SampleState at 96 dpi: it judges the design, never the DPI handling.
bool RenderSnapshot(std::wstring_view view, std::wstring_view theme,
                    const std::filesystem::path& out);

}  // namespace panel
