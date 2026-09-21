#include "ui/Edit.h"

#include <algorithm>

namespace Ui {

namespace {

constexpr bool IsHighSurrogate(wchar_t c) { return (c & 0xFC00) == 0xD800; }
constexpr bool IsLowSurrogate(wchar_t c) { return (c & 0xFC00) == 0xDC00; }

// Las marcas que se dibujan encima de la letra anterior y no ocupan sitio propio. No es
// la lista completa de Unicode —eso serían tablas—, son los bloques con los que se
// escribe de verdad en un teclado europeo más los de puntuación combinante.
constexpr bool IsCombining(wchar_t c) {
    return (c >= 0x0300 && c <= 0x036F) ||  // diacríticos combinantes
           (c >= 0x1AB0 && c <= 0x1AFF) ||  // extendidos
           (c >= 0x1DC0 && c <= 0x1DFF) ||  // suplemento
           (c >= 0x20D0 && c <= 0x20F0) ||  // para símbolos
           (c >= 0xFE20 && c <= 0xFE2F);    // medias marcas
}

// La eñe y las vocales con tilde del español son letras de pleno derecho: están en el
// Latín-1 suplementario, por encima de 0x00C0. Ahí arriba casi todo es letra, salvo el
// signo de multiplicar y el de dividir, que se colaron en medio del bloque.
constexpr bool IsWordChar(wchar_t c) {
    if (c >= L'0' && c <= L'9') return true;
    if (c >= L'A' && c <= L'Z') return true;
    if (c >= L'a' && c <= L'z') return true;
    if (c == L'_') return true;
    if (c < 0x00C0) return false;
    return c != 0x00D7 && c != 0x00F7;
}

enum class Kind { Space, Word, Punct };

constexpr Kind KindOf(wchar_t c) {
    if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') return Kind::Space;
    return IsWordChar(c) ? Kind::Word : Kind::Punct;
}

// Un solo carácter, contando el par suplente como uno. Lo usa la regla de agrupar el
// historial: teclear es de uno en uno, pegar no.
bool IsSingleCharacter(std::wstring_view chunk) {
    if (chunk.size() == 1) return !IsLowSurrogate(chunk[0]);
    return chunk.size() == 2 && IsHighSurrogate(chunk[0]) && IsLowSurrogate(chunk[1]);
}

}  // namespace

std::size_t PrevBoundary(std::wstring_view text, std::size_t index) {
    index = std::min(index, text.size());
    if (index == 0) return 0;

    // Primero las marcas que cuelgan: una "o" con tilde combinante son dos unidades, y
    // borrar solo la tilde deja una letra que el usuario no escribió.
    while (index > 0 && IsCombining(text[index - 1])) --index;
    if (index == 0) return 0;

    --index;
    // Y si lo que queda es la mitad baja de un par, la otra mitad va con ella: cortar
    // entre las dos deja un carácter inválido que se dibuja como un cuadro.
    if (index > 0 && IsLowSurrogate(text[index]) && IsHighSurrogate(text[index - 1])) --index;
    return index;
}

std::size_t NextBoundary(std::wstring_view text, std::size_t index) {
    if (index >= text.size()) return text.size();

    if (IsHighSurrogate(text[index]) && index + 1 < text.size() &&
        IsLowSurrogate(text[index + 1])) {
        index += 2;
    } else {
        ++index;
    }
    while (index < text.size() && IsCombining(text[index])) ++index;
    return index;
}

Range WordAt(std::wstring_view text, std::size_t index) {
    if (text.empty()) return Range{0, 0};
    index = std::min(index, text.size());

    // Justo al final del texto no hay carácter debajo: se toma el de la izquierda, que es
    // lo que espera quien hace doble clic detrás de la última palabra.
    const std::size_t probe = index < text.size() ? index : text.size() - 1;
    const Kind kind = KindOf(text[probe]);

    std::size_t begin = probe;
    while (begin > 0 && KindOf(text[begin - 1]) == kind) --begin;
    std::size_t end = probe;
    while (end < text.size() && KindOf(text[end]) == kind) ++end;
    return Range{begin, end};
}

std::wstring_view Editor::Selected() const {
    const Range range = m_caret.Ordered();
    return std::wstring_view{m_text}.substr(range.begin, range.Length());
}

std::wstring Editor::Display() const {
    if (!m_composing) return m_text;
    std::wstring out;
    out.reserve(m_text.size() + m_composition.size());
    out.append(m_text, 0, m_compositionAt);
    out.append(m_composition);
    out.append(m_text, m_compositionAt, std::wstring::npos);
    return out;
}

std::size_t Editor::DisplayCaret() const {
    if (!m_composing) return m_caret.head;
    return m_compositionAt + std::min(m_compositionCaret, m_composition.size());
}

Range Editor::Composition() const {
    if (!m_composing) return Range{0, 0};
    return Range{m_compositionAt, m_compositionAt + m_composition.size()};
}

std::size_t Editor::Clamp(std::size_t index) const {
    index = std::min(index, m_text.size());
    // Nunca en medio de un par: un cursor ahí mide mal y borra peor.
    if (index > 0 && index < m_text.size() && IsLowSurrogate(m_text[index]) &&
        IsHighSurrogate(m_text[index - 1])) {
        --index;
    }
    return index;
}

bool Editor::SetText(std::wstring_view value) {
    std::wstring next{value};
    if (m_max > 0 && next.size() > m_max) {
        std::size_t cut = m_max;
        // Sin partir un par al truncar.
        if (cut > 0 && IsLowSurrogate(next[cut]) && IsHighSurrogate(next[cut - 1])) --cut;
        next.resize(cut);
    }
    if (next == m_text && !m_composing) return false;

    m_text = std::move(next);
    m_caret = Caret{m_text.size(), m_text.size()};
    // Un cambio programático no es algo que el usuario pueda deshacer: el historial de lo
    // que había deja de tener sentido cuando el texto lo pone otro.
    m_undo.clear();
    m_redo.clear();
    m_group = Group::None;
    m_composing = false;
    m_composition.clear();
    return true;
}

void Editor::SetHistoryEnabled(bool enabled) {
    m_history = enabled;
    if (!enabled) {
        m_undo.clear();
        m_redo.clear();
        m_group = Group::None;
    }
}

void Editor::Push(Step step, Group group) {
    m_redo.clear();

    // Con el historial apagado no se guarda nada, ni siquiera el paso actual: es lo que hace
    // que un campo secreto no deje el texto pegado dentro de una pila de deshacer.
    if (!m_history) {
        m_group = Group::None;
        return;
    }

    if (group != Group::None && group == m_group && !m_undo.empty()) {
        Step& last = m_undo.back();

        // Teclear seguido: la entrada anterior es una inserción pura que acaba justo
        // donde empieza esta.
        if (group == Group::Typing && last.removed.empty() && step.removed.empty() &&
            last.at + last.inserted.size() == step.at) {
            last.inserted.append(step.inserted);
            last.after = step.after;
            return;
        }

        // Borrar seguido. Hacia atrás la entrada nueva termina donde empezaba la anterior;
        // hacia delante las dos empiezan en el mismo sitio.
        if (group == Group::Deleting && last.inserted.empty() && step.inserted.empty()) {
            if (step.at + step.removed.size() == last.at) {  // retroceso
                last.removed.insert(0, step.removed);
                last.at = step.at;
                last.after = step.after;
                return;
            }
            if (step.at == last.at) {  // suprimir
                last.removed.append(step.removed);
                last.after = step.after;
                return;
            }
        }
    }

    m_undo.push_back(std::move(step));
    m_group = group;
}

bool Editor::Apply(std::size_t at, std::size_t removeLen, std::wstring_view inserted,
                   Group group) {
    at = std::min(at, m_text.size());
    removeLen = std::min(removeLen, m_text.size() - at);
    if (removeLen == 0 && inserted.empty()) return false;

    std::wstring chunk{inserted};
    if (m_max > 0) {
        const std::size_t kept = m_text.size() - removeLen;
        const std::size_t room = m_max > kept ? m_max - kept : 0;
        if (chunk.size() > room) {
            std::size_t cut = room;
            if (cut > 0 && cut < chunk.size() && IsLowSurrogate(chunk[cut]) &&
                IsHighSurrogate(chunk[cut - 1])) {
                --cut;
            }
            chunk.resize(cut);
        }
        if (chunk.empty() && removeLen == 0) return false;
    }

    Step step;
    step.at = at;
    step.removed = m_text.substr(at, removeLen);
    step.inserted = chunk;
    step.before = m_caret;

    m_text.replace(at, removeLen, chunk);
    const std::size_t caret = at + chunk.size();
    m_caret = Caret{caret, caret};
    step.after = m_caret;

    Push(std::move(step), group);
    return true;
}

bool Editor::Insert(std::wstring_view chunk) {
    if (chunk.empty()) return DeleteSelection();

    const Range sel = m_caret.Ordered();
    // Teclear se agrupa; pegar es siempre un paso suyo, y reemplazar una selección
    // también: son cosas que el usuario quiere deshacer de una pieza.
    const bool typing = sel.Empty() && IsSingleCharacter(chunk);
    const Group group = typing ? Group::Typing : Group::None;

    const bool changed = Apply(sel.begin, sel.Length(), chunk, group);

    // El espacio cierra el grupo, y así un deshacer devuelve la última palabra entera en
    // vez de la frase entera o de una sola letra.
    if (!typing || KindOf(chunk[0]) == Kind::Space) m_group = Group::None;
    return changed;
}

bool Editor::Backspace() {
    if (!m_caret.Empty()) return DeleteSelection();
    if (m_caret.head == 0) return false;

    const std::size_t from = PrevBoundary(m_text, m_caret.head);
    return Apply(from, m_caret.head - from, {}, Group::Deleting);
}

bool Editor::DeleteForward() {
    if (!m_caret.Empty()) return DeleteSelection();
    if (m_caret.head >= m_text.size()) return false;

    const std::size_t to = NextBoundary(m_text, m_caret.head);
    return Apply(m_caret.head, to - m_caret.head, {}, Group::Deleting);
}

bool Editor::DeleteSelection() {
    const Range sel = m_caret.Ordered();
    if (sel.Empty()) return false;
    const bool changed = Apply(sel.begin, sel.Length(), {}, Group::None);
    m_group = Group::None;
    return changed;
}

bool Editor::MoveCaret(Move move, bool extend) {
    const Caret before = m_caret;
    std::size_t head = m_caret.head;

    switch (move) {
    case Move::Left:
        // Sin Shift y con selección, la flecha colapsa al extremo en vez de moverse: es
        // lo que hacen todos los campos de texto y lo que la mano espera.
        head = (!extend && !m_caret.Empty()) ? m_caret.Ordered().begin
                                             : PrevBoundary(m_text, head);
        break;
    case Move::Right:
        head = (!extend && !m_caret.Empty()) ? m_caret.Ordered().end
                                             : NextBoundary(m_text, head);
        break;
    case Move::WordLeft: {
        // Saltar primero los espacios que haya justo detrás y luego la palabra entera.
        while (head > 0 && KindOf(m_text[head - 1]) == Kind::Space) --head;
        if (head > 0) {
            const Kind kind = KindOf(m_text[head - 1]);
            while (head > 0 && KindOf(m_text[head - 1]) == kind) --head;
        }
        break;
    }
    case Move::WordRight: {
        if (head < m_text.size()) {
            const Kind kind = KindOf(m_text[head]);
            while (head < m_text.size() && KindOf(m_text[head]) == kind) ++head;
        }
        while (head < m_text.size() && KindOf(m_text[head]) == Kind::Space) ++head;
        break;
    }
    case Move::Start: head = 0; break;
    case Move::End:   head = m_text.size(); break;
    }

    m_caret.head = Clamp(head);
    if (!extend) m_caret.anchor = m_caret.head;
    // Mover el cursor termina lo que se acababa de escribir.
    if (m_caret != before) m_group = Group::None;
    return m_caret != before;
}

bool Editor::PlaceCaret(std::size_t index, bool extend) {
    const Caret before = m_caret;
    m_caret.head = Clamp(index);
    if (!extend) m_caret.anchor = m_caret.head;
    if (m_caret != before) m_group = Group::None;
    return m_caret != before;
}

bool Editor::SelectRange(Range range) {
    const Caret before = m_caret;
    m_caret.anchor = Clamp(range.begin);
    m_caret.head = Clamp(range.end);
    if (m_caret != before) m_group = Group::None;
    return m_caret != before;
}

bool Editor::SelectAll() { return SelectRange(Range{0, m_text.size()}); }

bool Editor::SelectWordAt(std::size_t index) { return SelectRange(WordAt(m_text, index)); }

void Editor::BreakUndo() { m_group = Group::None; }

bool Editor::Undo() {
    if (m_undo.empty()) return false;

    Step step = std::move(m_undo.back());
    m_undo.pop_back();
    m_text.replace(step.at, step.inserted.size(), step.removed);
    m_caret = step.before;
    m_redo.push_back(std::move(step));
    m_group = Group::None;
    return true;
}

bool Editor::Redo() {
    if (m_redo.empty()) return false;

    Step step = std::move(m_redo.back());
    m_redo.pop_back();
    m_text.replace(step.at, step.removed.size(), step.inserted);
    m_caret = step.after;
    m_undo.push_back(std::move(step));
    m_group = Group::None;
    return true;
}

bool Editor::BeginComposition() {
    if (m_composing) return false;
    // Lo que hubiera seleccionado se va: el IME escribe donde estaba la selección.
    DeleteSelection();
    m_composing = true;
    m_compositionAt = m_caret.head;
    m_compositionCaret = 0;
    m_composition.clear();
    m_group = Group::None;
    return true;
}

bool Editor::UpdateComposition(std::wstring_view text, std::size_t caretInComposition) {
    if (!m_composing) BeginComposition();
    if (m_composition == text && m_compositionCaret == caretInComposition) return false;
    m_composition.assign(text);
    m_compositionCaret = std::min(caretInComposition, m_composition.size());
    return true;
}

bool Editor::CommitComposition(std::wstring_view text) {
    if (!m_composing && text.empty()) return false;
    m_composing = false;
    m_composition.clear();
    m_compositionCaret = 0;
    if (text.empty()) return true;
    // Entra como un paso suyo: una palabra compuesta se deshace de una pieza, no sílaba
    // a sílaba.
    const bool changed = Apply(m_caret.head, 0, text, Group::None);
    m_group = Group::None;
    return changed;
}

bool Editor::CancelComposition() {
    if (!m_composing) return false;
    m_composing = false;
    m_composition.clear();
    m_compositionCaret = 0;
    return true;
}

}  // namespace Ui
