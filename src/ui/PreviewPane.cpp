#include "ui/PreviewPane.h"

#include <imgui.h>

#include <algorithm>

#include "ui/MillerView.h"
#include "ui/Theme.h"

namespace PreviewPane {
namespace {

void DrawImage(const Preview& preview) {
    if (!preview.texture) return;

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float width = static_cast<float>(preview.width);
    const float height = static_cast<float>(preview.height);
    // Nunca se amplia: la imagen ya se decodifico al tamano del panel, y estirar una mas
    // pequena que el panel solo la emborrona.
    const float scale = std::min({1.0f, avail.x / width, avail.y / height});
    const ImVec2 size(width * scale, height * scale);

    const ImVec2 origin = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(origin.x + (avail.x - size.x) * 0.5f,
                               origin.y + (avail.y - size.y) * 0.5f));
    // ImTextureID es un ImU64 y el atajo que acepta void* vive tras
    // IMGUI_DISABLE_OBSOLETE_FUNCTIONS, que esta puesto: el cast va a mano. El backend de
    // DX11 lo devuelve a ID3D11ShaderResourceView* al pintar.
    ImGui::Image(reinterpret_cast<ImTextureID>(preview.texture.Get()), size);
}

void DrawText(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::kText);
    // Sin formato: el contenido de un archivo puede llevar cualquier cosa, empezando por %.
    ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
    ImGui::PopStyleColor();
}

}  // namespace

void Draw(const DirectoryEntry* entry, const EntryList& entries, const PreviewPtr& preview) {
    if (!entry) return;

    if (entry->IsDirectory()) {
        if (!entries) return;  // aun leyendo
        if (entries->empty()) {
            ImGui::TextColored(Theme::kTextDim, "(vacia)");
            return;
        }
        // Cursor -1: ninguna fila resaltada. Es una vista, no una columna navegable.
        bool scrollToCursor = false;
        MillerView::DrawEntries(*entries, -1, scrollToCursor);
        return;
    }

    if (!preview) return;
    switch (preview->kind) {
    case Preview::Kind::Image: DrawImage(*preview); return;
    case Preview::Kind::Text:
    case Preview::Kind::Info: DrawText(preview->text); return;
    case Preview::Kind::Empty: return;
    }
}

}  // namespace PreviewPane
