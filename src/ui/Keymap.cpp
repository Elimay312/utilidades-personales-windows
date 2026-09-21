#include "ui/Keymap.h"

#include <imgui.h>

namespace Keymap {
namespace {

struct Binding {
    ImGuiKey key;
    ImGuiKeyChord mods;  // comparados exactos: Ctrl+j no es j, y G no es g
    bool repeat;         // mantener pulsado repite (moverse si, navegar no)
    Command command;
};

// Tabla de datos, no ifs repartidos: asi el keymap se puede hacer configurable mas adelante.
constexpr Binding kBindings[] = {
    {ImGuiKey_J, ImGuiMod_None, true, Command::MoveDown},
    {ImGuiKey_DownArrow, ImGuiMod_None, true, Command::MoveDown},
    {ImGuiKey_K, ImGuiMod_None, true, Command::MoveUp},
    {ImGuiKey_UpArrow, ImGuiMod_None, true, Command::MoveUp},
    {ImGuiKey_G, ImGuiMod_Shift, false, Command::MoveBottom},
    {ImGuiKey_D, ImGuiMod_Ctrl, true, Command::HalfPageDown},
    {ImGuiKey_U, ImGuiMod_Ctrl, true, Command::HalfPageUp},
    {ImGuiKey_L, ImGuiMod_None, false, Command::Open},
    {ImGuiKey_RightArrow, ImGuiMod_None, false, Command::Open},
    {ImGuiKey_Enter, ImGuiMod_None, false, Command::Open},
    {ImGuiKey_KeypadEnter, ImGuiMod_None, false, Command::Open},
    {ImGuiKey_H, ImGuiMod_None, false, Command::GoParent},
    {ImGuiKey_LeftArrow, ImGuiMod_None, false, Command::GoParent},
    {ImGuiKey_Q, ImGuiMod_None, false, Command::Quit},
};

// Atajos que son un caracter y no una tecla fisica: '~' es Shift+` en un teclado de EE.UU.
// pero AltGr+4 en uno espanol, y ImGuiKey_GraveAccent es la tecla de la 'n' con virgulilla.
// Mirando el texto que produce el teclado el atajo es el mismo en cualquier distribucion.
constexpr struct {
    ImWchar character;
    Command command;
} kCharBindings[] = {
    {L'~', Command::GoHome},
};

}  // namespace

Command Poll(State& state) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return Command::None;  // fase 7: filtro, renombrar, ir a ruta

    for (const ImWchar character : io.InputQueueCharacters) {
        for (const auto& binding : kCharBindings) {
            if (character != binding.character) continue;
            state.pendingG = false;
            return binding.command;
        }
    }

    // "gg": la primera g no hace nada, la segunda va al principio.
    if (io.KeyMods == ImGuiMod_None && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
        const bool second = state.pendingG;
        state.pendingG = !second;
        return second ? Command::MoveTop : Command::None;
    }

    for (const Binding& binding : kBindings) {
        if (io.KeyMods == binding.mods && ImGui::IsKeyPressed(binding.key, binding.repeat)) {
            state.pendingG = false;
            return binding.command;
        }
    }
    return Command::None;
}

}  // namespace Keymap
