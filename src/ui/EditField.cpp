#include "ui/EditField.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "fs/FileOps.h"

namespace EditField {
namespace {

// Un solo callback para los dos eventos: el texto del campo activo vive en el estado interno
// del InputText, no en el buffer de fuera, asi que tocarlo solo se puede desde aqui.
int Callback(ImGuiInputTextCallbackData* data) {
    State& state = *static_cast<State*>(data->UserData);

    // Tab. Se pasa y se recoge el texto entero: completar es reescribirlo.
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        std::string text(data->Buf, static_cast<size_t>(data->BufTextLen));
        state.onTab(text);
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, text.c_str());
        return 0;
    }

    // Preseleccion al renombrar. Se desarma a si misma porque a partir de la segunda pasada
    // estaria peleandose con la seleccion del usuario.
    state.selectStem = false;
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
    if (state.onTab) flags |= ImGuiInputTextFlags_CallbackCompletion;

    // Sin padding de marco: la altura del campo es exactamente la de una fila y la lista no
    // se descoloca al renombrar.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::SetNextItemWidth(width);
    const bool confirmed = ImGui::InputText(
        id, &state.text, flags, (state.selectStem || state.onTab) ? Callback : nullptr, &state);
    ImGui::PopStyleVar();

    // El orden importa: con EnterReturnsTrue, Enter tambien desactiva el campo.
    if (confirmed)
        state.result = Result::Confirm;
    else if (ImGui::IsItemDeactivated())
        state.result = Result::Cancel;
}

}  // namespace EditField
