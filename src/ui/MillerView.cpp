#include "ui/MillerView.h"

#include <Windows.h>
#include <imgui.h>
#include <shlwapi.h>

#include <algorithm>
#include <string>

#include "ui/Theme.h"

namespace MillerView {
namespace {

// Icono por color, sin glifos extra: azul carpetas, gris los ocultos y de sistema.
const ImVec4& RowColor(const DirectoryEntry& entry) {
    if (entry.IsHidden()) return Theme::kTextDim;
    return entry.IsDirectory() ? Theme::kAccent : Theme::kText;
}

void DrawText(const char* text, const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    // TextUnformatted y no Text: un nombre puede llevar un % dentro.
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

}  // namespace

int DrawEntries(const std::vector<DirectoryEntry>& entries, int cursor, bool& scrollToCursor) {
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const float viewHeight = ImGui::GetContentRegionAvail().y;
    const float rowWidth = ImGui::GetContentRegionAvail().x;

    // Scroll minimo: solo se mueve si el cursor se ha salido de la vista. ImGui aplica el
    // objetivo en el Begin siguiente, y como pintamos 3 frames por evento no se nota.
    if (scrollToCursor && !entries.empty()) {
        const float top = static_cast<float>(cursor) * rowHeight;
        const float scroll = ImGui::GetScrollY();
        if (top < scroll)
            ImGui::SetScrollY(top);
        else if (top + rowHeight > scroll + viewHeight)
            ImGui::SetScrollY(top + rowHeight - viewHeight);
        scrollToCursor = false;
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(entries.size()), rowHeight);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const DirectoryEntry& entry = entries[static_cast<size_t>(i)];
            const ImVec2 rowStart = ImGui::GetCursorScreenPos();

            // El nombre no va como etiqueta del Selectable: un fichero "a##b.txt" se
            // cortaria. Se pinta encima, volviendo al inicio de la fila.
            ImGui::PushID(i);
            ImGui::Selectable("##row", i == cursor, 0, ImVec2(rowWidth, 0.0f));
            ImGui::PopID();

            ImGui::SetCursorScreenPos(rowStart);
            DrawText(entry.nameUtf8.c_str(), RowColor(entry));
            const float nameEnd = ImGui::GetItemRectMax().x;

            if (entry.IsDirectory()) continue;

            wchar_t bytes[32];
            if (!StrFormatByteSizeW(static_cast<LONGLONG>(entry.size), bytes, ARRAYSIZE(bytes)))
                continue;

            const std::string text = ToUtf8(bytes);
            const float right = rowStart.x + rowWidth - ImGui::CalcTextSize(text.c_str()).x;
            if (right <= nameEnd + ImGui::GetStyle().ItemSpacing.x) continue;

            ImGui::SetCursorScreenPos(ImVec2(right, rowStart.y));
            DrawText(text.c_str(), Theme::kTextDim);
        }
    }
    return std::max(1, static_cast<int>(viewHeight / rowHeight));
}

}  // namespace MillerView
