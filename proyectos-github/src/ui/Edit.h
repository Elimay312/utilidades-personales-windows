#pragma once

// El campo de texto de una línea, sin una sola llamada a Windows: una cadena, un cursor,
// una selección y un historial. Lo que NO está aquí es dónde cae el cursor en píxeles,
// porque eso lo sabe DirectWrite; aquí está lo que decide mal en silencio.
//
// Los índices van en UNIDADES UTF-16, que es como los guarda std::wstring, como llegan
// los WM_CHAR y como los cuenta IDWriteTextLayout. Convertir en el borde sería inventar
// un tercer sistema de coordenadas para el mismo texto.
//
// Pero moverse de una unidad en una NO es moverse de carácter en carácter: un emoji son
// dos unidades y una tilde combinante es una unidad detrás de su letra. De eso se
// encargan PrevBoundary y NextBoundary, y por eso retroceder sobre una "ó" borra la letra
// y no deja media.
//
// La composición del IME vive FUERA de m_text: se dibuja encima y entra de golpe al
// confirmarse. Así deshacer no ve sílabas a medias y quien lee Text() no necesita saber
// que existe un IME.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Ui {

struct Range {
    std::size_t begin = 0;
    std::size_t end = 0;

    constexpr bool Empty() const { return begin == end; }
    constexpr std::size_t Length() const { return end - begin; }
    constexpr bool operator==(const Range&) const = default;
};

// Ancla y extremo, no un rango ordenado: con un rango se pierde por qué lado crece la
// selección, y entonces Shift+izquierda después de Shift+derecha la agranda en vez de
// encogerla.
struct Caret {
    std::size_t anchor = 0;
    std::size_t head = 0;

    constexpr bool Empty() const { return anchor == head; }
    constexpr Range Ordered() const {
        return anchor <= head ? Range{anchor, head} : Range{head, anchor};
    }
    constexpr bool operator==(const Caret&) const = default;
};

enum class Move { Left, Right, WordLeft, WordRight, Start, End };

// El límite de carácter anterior y siguiente: salta pares suplentes enteros y se lleva
// por delante las marcas combinantes que cuelguen de la letra.
std::size_t PrevBoundary(std::wstring_view text, std::size_t index);
std::size_t NextBoundary(std::wstring_view text, std::size_t index);

// La palabra bajo el índice. Doble clic y Ctrl+flecha.
Range WordAt(std::wstring_view text, std::size_t index);

class Editor {
public:
    // --- Lectura ---------------------------------------------------------------------
    const std::wstring& Text() const { return m_text; }
    Caret Cursor() const { return m_caret; }
    std::wstring_view Selected() const;
    bool Empty() const { return m_text.empty(); }

    // Texto confirmado más la composición en curso: lo que hay que medir y pintar.
    std::wstring Display() const;
    // Dónde va el cursor dentro de Display(), que durante una composición no es Cursor().
    std::size_t DisplayCaret() const;
    bool Composing() const { return m_composing; }
    // El tramo subrayado dentro de Display(). Vacío si no hay composición.
    Range Composition() const;

    // --- Edición. Devuelven true si algo cambió (y entonces hay que repintar) ----------
    bool SetText(std::wstring_view value);  // programático: vacía el historial
    bool Insert(std::wstring_view chunk);   // teclear y pegar; reemplaza la selección
    bool Backspace();
    bool DeleteForward();
    bool DeleteSelection();

    // --- Cursor ------------------------------------------------------------------------
    bool MoveCaret(Move move, bool extend);
    bool PlaceCaret(std::size_t index, bool extend);  // clic y arrastre
    bool SelectRange(Range range);
    bool SelectAll();
    bool SelectWordAt(std::size_t index);

    // --- Historial -----------------------------------------------------------------------
    // Sin reloj: agrupar por tiempo obligaría a pasar un now() y a que las pruebas
    // mintieran sobre él. Se agrupa por forma —letras seguidas en el mismo sitio— y el
    // grupo se cierra solo al escribir un espacio, al pegar, al mover el cursor y al
    // perder el foco, que es cuando de verdad termina "lo que acabo de escribir".
    void BreakUndo();
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }

    // --- IME ---------------------------------------------------------------------------
    bool BeginComposition();
    bool UpdateComposition(std::wstring_view text, std::size_t caretInComposition);
    bool CommitComposition(std::wstring_view text);
    bool CancelComposition();

    // Un límite duro, no una sugerencia. 0 = sin límite.
    void SetMaxLength(std::size_t units) { m_max = units; }
    std::size_t MaxLength() const { return m_max; }

private:
    // Un paso del historial es un reemplazo: en [at, at+removed) había `removed` y ahora
    // hay `inserted`. Deshacer es el mismo reemplazo al revés, así que no hacen falta dos
    // tipos de entrada.
    struct Step {
        std::size_t at = 0;
        std::wstring removed;
        std::wstring inserted;
        Caret before;
        Caret after;
    };

    enum class Group { None, Typing, Deleting };

    bool Apply(std::size_t at, std::size_t removeLen, std::wstring_view inserted, Group group);
    void Push(Step step, Group group);
    std::size_t Clamp(std::size_t index) const;

    std::wstring m_text;
    Caret m_caret;

    std::wstring m_composition;
    std::size_t m_compositionAt = 0;
    std::size_t m_compositionCaret = 0;
    bool m_composing = false;

    Group m_group = Group::None;
    std::size_t m_max = 0;
    std::vector<Step> m_undo;
    std::vector<Step> m_redo;
};

}  // namespace Ui
