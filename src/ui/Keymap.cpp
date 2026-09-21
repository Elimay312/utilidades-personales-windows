#include "ui/Keymap.h"

#include <imgui.h>

namespace Keymap {
namespace {

struct Binding {
    ImGuiKey key;
    ImGuiKeyChord mods;  // comparados exactos: Ctrl+j no es j, y G no es g
    Command command;
};

// Tabla de datos, no ifs repartidos: asi el keymap se puede hacer configurable mas adelante.
constexpr Binding kBindings[] = {
    {ImGuiKey_J, ImGuiMod_None, Command::MoveDown},
    {ImGuiKey_DownArrow, ImGuiMod_None, Command::MoveDown},
    {ImGuiKey_K, ImGuiMod_None, Command::MoveUp},
    {ImGuiKey_UpArrow, ImGuiMod_None, Command::MoveUp},
    {ImGuiKey_G, ImGuiMod_Shift, Command::MoveBottom},
    {ImGuiKey_D, ImGuiMod_Ctrl, Command::HalfPageDown},
    {ImGuiKey_U, ImGuiMod_Ctrl, Command::HalfPageUp},
    {ImGuiKey_Q, ImGuiMod_None, Command::Quit},
};

}  // namespace

Command Poll(State& state) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return Command::None;  // fase 7: filtro, renombrar, ir a ruta

    // "gg": la primera g no hace nada, la segunda va al principio.
    if (io.KeyMods == ImGuiMod_None && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
        const bool second = state.pendingG;
        state.pendingG = !second;
        return second ? Command::MoveTop : Command::None;
    }

    // repeat = true: mantener j pulsado baja de forma continua.
    for (const Binding& binding : kBindings) {
        if (io.KeyMods == binding.mods && ImGui::IsKeyPressed(binding.key, true)) {
            state.pendingG = false;
            return binding.command;
        }
    }
    return Command::None;
}

}  // namespace Keymap
