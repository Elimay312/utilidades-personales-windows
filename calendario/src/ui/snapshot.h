#pragma once

#include <d2d1.h>

#include <algorithm>
#include <filesystem>
#include <string_view>

namespace agenda {

// The views a snapshot can render. Named once: the flag parser and the renderer both ask here,
// and a view added to one but not the other is a flag that is refused before it is used.
inline constexpr std::wstring_view kSnapshotViews[] = {
    L"popup",         // the panel as it opens
    L"popup-creado",  // and a moment after Enter, with the notice up and the card still rising
    L"popup-sin-conexion",  // and with the dot up, which is the only thing being offline shows
    L"app-dia",             // the expanded app, on the day view
    L"app-semana",          // on the week
    L"app-mes",             // on the month
    L"app-transicion",      // halfway through the expansion, over a whole work area
    L"app-detalle",         // the week with an event open in the detail panel
    L"app-arrastre",        // an event halfway through being dragged to another day
    L"app-borrar",          // Supr pressed: the question before anything is deleted
    L"app-repeticion",      // a repetition moved: this one, or all of them
    L"popup-buscar",        // "?con" in the capsule: what it finds, over the day list
    L"app-buscar",          // the same search in the app, hanging below the capsule
    L"configuracion",       // the settings window's client area, as it opens
};

inline bool KnowsSnapshotView(std::wstring_view view) {
  return std::find(std::begin(kSnapshotViews), std::end(kSnapshotViews), view) !=
         std::end(kSnapshotViews);
}

// Renders a view offscreen to a PNG with Direct2D over a WIC bitmap, which is how CLAUDE.md
// says to review the design: no desktop screenshots. The views are listed above.
// `panel` is the panel size in DIP to render; a zero size means the one the design was drawn
// at, which is what the committed PNGs use so they never depend on the machine. `text` is what
// the input should already have typed in it, which is the only way to see the live preview in
// a PNG.
bool RenderSnapshot(std::wstring_view view, std::wstring_view theme, D2D1_SIZE_F panel,
                    std::wstring_view text, const std::filesystem::path& out);

}  // namespace agenda
