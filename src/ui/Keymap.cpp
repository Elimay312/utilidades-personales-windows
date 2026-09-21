#include "ui/Keymap.h"

#include <imgui.h>

#include <string.h>

namespace Keymap {
namespace {

// Tabla de datos, no ifs repartidos: asi el keymap se puede leer del config. El nombre es
// el que se escribe en el archivo, y tambien lo que permite encontrar el comando al releerlo.
struct Binding {
    const char* name;  // solo en las tablas de fabrica; en la activa es null y nadie lo lee
    Command command;
    ImGuiKey key;
    ImGuiKeyChord mods;  // comparados exactos: Ctrl+j no es j, y G no es g
};

// Atajos que son un caracter y no una tecla fisica: '~' es Shift+` en un teclado de EE.UU.
// pero AltGr+4 en uno espanol, y ImGuiKey_GraveAccent es la tecla de la 'n' con virgulilla.
// Mirando el texto que produce el teclado el atajo es el mismo en cualquier distribucion.
struct CharBinding {
    const char* name;
    Command command;
    ImWchar character;
};

constexpr Binding kDefaultKeys[] = {
    {"MoveDown", Command::MoveDown, ImGuiKey_J, ImGuiMod_None},
    {"MoveDown", Command::MoveDown, ImGuiKey_DownArrow, ImGuiMod_None},
    {"MoveUp", Command::MoveUp, ImGuiKey_K, ImGuiMod_None},
    {"MoveUp", Command::MoveUp, ImGuiKey_UpArrow, ImGuiMod_None},
    {"MoveTop", Command::MoveTop, ImGuiKey_Home, ImGuiMod_None},
    {"MoveBottom", Command::MoveBottom, ImGuiKey_G, ImGuiMod_Shift},
    {"MoveBottom", Command::MoveBottom, ImGuiKey_End, ImGuiMod_None},
    {"HalfPageDown", Command::HalfPageDown, ImGuiKey_D, ImGuiMod_Ctrl},
    {"HalfPageUp", Command::HalfPageUp, ImGuiKey_U, ImGuiMod_Ctrl},
    {"Open", Command::Open, ImGuiKey_L, ImGuiMod_None},
    {"Open", Command::Open, ImGuiKey_RightArrow, ImGuiMod_None},
    {"Open", Command::Open, ImGuiKey_Enter, ImGuiMod_None},
    {"Open", Command::Open, ImGuiKey_KeypadEnter, ImGuiMod_None},
    {"GoParent", Command::GoParent, ImGuiKey_H, ImGuiMod_None},
    {"GoParent", Command::GoParent, ImGuiKey_LeftArrow, ImGuiMod_None},
    {"Quit", Command::Quit, ImGuiKey_Q, ImGuiMod_None},
    {"ToggleMark", Command::ToggleMark, ImGuiKey_Space, ImGuiMod_None},
    {"Copy", Command::Copy, ImGuiKey_Y, ImGuiMod_None},
    {"Cut", Command::Cut, ImGuiKey_X, ImGuiMod_None},
    {"Paste", Command::Paste, ImGuiKey_P, ImGuiMod_None},
    // 'd' a secas no choca con Ctrl+d: los modificadores se comparan exactos.
    {"Recycle", Command::Recycle, ImGuiKey_D, ImGuiMod_None},
    {"DeleteForever", Command::DeleteForever, ImGuiKey_D, ImGuiMod_Shift},
    {"Rename", Command::Rename, ImGuiKey_R, ImGuiMod_None},
    {"Create", Command::Create, ImGuiKey_A, ImGuiMod_None},
    // Esc solo llega aqui con los campos y el popup cerrados: los dos se lo quedan antes.
    {"ClearFilter", Command::ClearFilter, ImGuiKey_Escape, ImGuiMod_None},
    {"NewTab", Command::NewTab, ImGuiKey_T, ImGuiMod_None},
    {"CloseTab", Command::CloseTab, ImGuiKey_W, ImGuiMod_Ctrl},
    {"SetBookmark", Command::SetBookmark, ImGuiKey_M, ImGuiMod_None},
    // Nueve entradas y un solo comando: cual se pulso viaja en State::letter. Teclas y no
    // caracteres porque los digitos estan en el mismo sitio en cualquier distribucion, y
    // porque ImGui llama "1" a la tecla 1: escritos en el config volverian como tecla.
    {"SelectTab", Command::SelectTab, ImGuiKey_1, ImGuiMod_None},
    {"SelectTab", Command::SelectTab, ImGuiKey_2, ImGuiMod_None},
    {"SelectTab", Command::SelectTab, ImGuiKey_3, ImGuiMod_None},
    {"SelectTab", Command::SelectTab, ImGuiKey_4, ImGuiMod_None},
    {"SelectTab", Command::SelectTab, ImGuiKey_5, ImGuiMod_None},
    {"SelectTab", Command::SelectTab, ImGuiKey_6, ImGuiMod_None},
    {"SelectTab", Command::SelectTab, ImGuiKey_7, ImGuiMod_None},
    {"SelectTab", Command::SelectTab, ImGuiKey_8, ImGuiMod_None},
    {"SelectTab", Command::SelectTab, ImGuiKey_9, ImGuiMod_None},
};

constexpr CharBinding kDefaultChars[] = {
    {"GoHome", Command::GoHome, L'~'},
    // Los tres de la fase 7 son caracteres por lo mismo: ':' es Shift+. en un teclado
    // espanol y '/' vive en Shift+7, en teclas que no coinciden con las de EE.UU.
    {"Filter", Command::Filter, L'/'},
    {"Goto", Command::Goto, L':'},
    {"ToggleHidden", Command::ToggleHidden, L'.'},
    {"GotoBookmark", Command::GotoBookmark, L'\''},
};

// La tabla activa. Globales porque la app es una ventana y un keymap: llevarlas hasta Poll
// por parametro solo moveria el mismo estado de sitio.
std::vector<Binding> g_keys(std::begin(kDefaultKeys), std::end(kDefaultKeys));
std::vector<CharBinding> g_chars(std::begin(kDefaultChars), std::end(kDefaultChars));

// Mantener pulsado repite, y eso es del comando y no de la tecla: bajar por la lista si,
// bucear tres carpetas por los frames que se pintan por evento no.
bool Repeats(Command command) {
    switch (command) {
    case Command::MoveDown:
    case Command::MoveUp:
    case Command::HalfPageDown:
    case Command::HalfPageUp:
    case Command::ToggleMark:
        return true;
    default:
        return false;
    }
}

bool IsAlnum(ImWchar character) {
    return (character >= L'a' && character <= L'z') || (character >= L'A' && character <= L'Z') ||
           (character >= L'0' && character <= L'9');
}

Command CommandByName(const std::string& name) {
    for (const Binding& binding : kDefaultKeys)
        if (_stricmp(name.c_str(), binding.name) == 0) return binding.command;
    for (const CharBinding& binding : kDefaultChars)
        if (_stricmp(name.c_str(), binding.name) == 0) return binding.command;
    return Command::None;
}

// "Ctrl+Shift+D" -> modificadores + tecla. Un solo caracter que no sea nombre de tecla es
// un atajo de caracter ('~', '/'): lo que produce el teclado, no la tecla fisica. Las
// letras y los digitos sueltos no llegan a ese caso: ImGui llama "J" y "1" a esas teclas,
// asi que ganan como tecla fisica, que es lo que se quiere para ellas.
bool ParseSpec(std::string spec, ImGuiKey& key, ImGuiKeyChord& mods, ImWchar& character) {
    key = ImGuiKey_None;
    mods = ImGuiMod_None;
    character = 0;

    for (size_t plus = spec.find('+'); plus != std::string::npos && plus > 0;
         plus = spec.find('+')) {
        const std::string token = spec.substr(0, plus);
        if (_stricmp(token.c_str(), "ctrl") == 0)
            mods |= ImGuiMod_Ctrl;
        else if (_stricmp(token.c_str(), "shift") == 0)
            mods |= ImGuiMod_Shift;
        else if (_stricmp(token.c_str(), "alt") == 0)
            mods |= ImGuiMod_Alt;
        else
            return false;
        spec.erase(0, plus + 1);
    }
    if (spec.empty()) return false;

    for (int named = ImGuiKey_NamedKey_BEGIN; named < ImGuiKey_NamedKey_END; ++named) {
        const char* name = ImGui::GetKeyName(static_cast<ImGuiKey>(named));
        if (name && name[0] && _stricmp(spec.c_str(), name) == 0) {
            key = static_cast<ImGuiKey>(named);
            return true;
        }
    }

    // Solo ASCII: un caracter fuera de el ocuparia varios bytes en UTF-8 y habria que
    // descodificarlo. Los atajos de un caracter que usamos caben todos aqui.
    if (spec.size() == 1 && mods == ImGuiMod_None && static_cast<unsigned char>(spec[0]) > ' ') {
        character = static_cast<ImWchar>(spec[0]);
        return true;
    }
    return false;
}

std::string SpecOf(ImGuiKey key, ImGuiKeyChord mods) {
    std::string spec;
    if (mods & ImGuiMod_Ctrl) spec += "Ctrl+";
    if (mods & ImGuiMod_Shift) spec += "Shift+";
    if (mods & ImGuiMod_Alt) spec += "Alt+";
    return spec + ImGui::GetKeyName(key);
}

// m y ' no hacen nada por si solos: se quedan esperando la letra del marcador.
Command Take(State& state, Command command) {
    state.pendingG = false;
    if (command != Command::SetBookmark && command != Command::GotoBookmark) return command;
    state.pending = command;
    return Command::None;
}

}  // namespace

Command Poll(State& state) {
    const ImGuiIO& io = ImGui::GetIO();
    // Con el campo de renombrar o el de crear abierto, el teclado es suyo.
    if (io.WantTextInput) return Command::None;

    // Esperando la letra de un marcador: la tecla siguiente es un nombre, no un atajo.
    if (state.pending != Command::None) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) state.pending = Command::None;
        if (io.InputQueueCharacters.empty()) return Command::None;

        const ImWchar character = io.InputQueueCharacters[0];
        const Command command = state.pending;
        state.pending = Command::None;
        if (!IsAlnum(character)) return Command::None;  // cualquier otra cosa cancela
        state.letter = static_cast<char>(character);
        return command;
    }

    for (const ImWchar character : io.InputQueueCharacters) {
        for (const CharBinding& binding : g_chars) {
            if (character != binding.character) continue;
            return Take(state, binding.command);
        }
    }

    // "gg": la primera g no hace nada, la segunda va al principio.
    if (io.KeyMods == ImGuiMod_None && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
        const bool second = state.pendingG;
        state.pendingG = !second;
        return second ? Command::MoveTop : Command::None;
    }

    for (const Binding& binding : g_keys) {
        if (io.KeyMods != binding.mods || !ImGui::IsKeyPressed(binding.key, Repeats(binding.command)))
            continue;
        // Un digito tiene que decir cual es: es lo que distingue una pestana de otra.
        if (binding.key >= ImGuiKey_0 && binding.key <= ImGuiKey_9)
            state.letter = static_cast<char>('0' + (binding.key - ImGuiKey_0));
        return Take(state, binding.command);
    }
    return Command::None;
}

std::string Defaults() {
    std::string text;
    for (const Binding& binding : kDefaultKeys)
        text += SpecOf(binding.key, binding.mods) + "=" + binding.name + "\r\n";
    for (const CharBinding& binding : kDefaultChars)
        text += std::string(1, static_cast<char>(binding.character)) + "=" + binding.name + "\r\n";
    return text;
}

int Load(const std::vector<std::pair<std::string, std::string>>& entries) {
    if (entries.empty()) return 0;

    g_keys.clear();
    g_chars.clear();
    int bad = 0;
    for (const auto& [spec, name] : entries) {
        ImGuiKey key = ImGuiKey_None;
        ImGuiKeyChord mods = ImGuiMod_None;
        ImWchar character = 0;
        const Command command = CommandByName(name);
        if (command == Command::None || !ParseSpec(spec, key, mods, character)) {
            ++bad;
            continue;
        }
        if (character != 0)
            g_chars.push_back(CharBinding{nullptr, command, character});
        else
            g_keys.push_back(Binding{nullptr, command, key, mods});
    }
    return bad;
}

}  // namespace Keymap
