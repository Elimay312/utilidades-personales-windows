#include "ui/EditField.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "fs/FileOps.h"

namespace EditField {
namespace {

// La preseleccion solo puede hacerse desde el callback: la seleccion vive en el estado
// interno del InputText, no en el buffer. Se desarma a si misma porque a partir de la
// segunda pasada estaria peleandose con la seleccion del usuario.
int SelectStem(ImGuiInputTextCallbackData* data) {
    static_cast<State*>(data->UserData)->selectStem = false;

    const int stem = static_cast<int>(
        StemLength(std::string(data->Buf, static_cast<size_t>(data->BufTextLen))));
    data->CursorPos = stem;
    data->SelectionStart = 0;
    data->SelectionEnd = stem;
    return 0;
}

}  // namespace

void Draw(State& state, float width, const char* id) {
    state.result = Result::None;

    // SetKeyboardFocusHere no activa el campo en este frame: la peticion de navegacion se
    // resuelve al final y el widget se activa en el siguiente. Por eso la preseleccion sigue
    // armada hasta que el callback corre de verdad (solo corre con el campo ya activo) y no
    // basta con pedirla en el frame del foco.
    if (state.focus) {
        ImGui::SetKeyboardFocusHere();
        state.focus = false;
    }

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
    if (state.selectStem) flags |= ImGuiInputTextFlags_CallbackAlways;

    // Sin padding de marco: la altura del campo es exactamente la de una fila y la lista no
    // se descoloca al renombrar.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::SetNextItemWidth(width);
    const bool confirmed =
        ImGui::InputText(id, &state.text, flags, state.selectStem ? SelectStem : nullptr, &state);
    ImGui::PopStyleVar();

    // El orden importa: con EnterReturnsTrue, Enter tambien desactiva el campo.
    if (confirmed)
        state.result = Result::Confirm;
    else if (ImGui::IsItemDeactivated())
        state.result = Result::Cancel;
}

}  // namespace EditField
