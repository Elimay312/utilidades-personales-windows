#pragma once

// El campo de texto de una línea.
//
// Todo lo que decide —dónde está el cursor, qué hay seleccionado, qué deshace un
// Ctrl+Z— vive en Ui::Editor, que es puro y está probado. Aquí queda solo lo que
// necesita una pantalla: dónde cae el cursor en píxeles, qué se dibuja y de dónde salen
// las teclas.
//
// El cursor parpadea con una animación en bucle EN LA GPU, no con un WM_TIMER. El hilo
// de UI se queda dormido en GetMessageW y así sigue; despertarlo dos veces por segundo
// para invertir un rectángulo de un píxel sería tirar por la borda que toda la animación
// de esta aplicación corra en el proceso de DWM.
//
// Sin HWND, claro: con WS_EX_NOREDIRECTIONBITMAP un EDIT no se pintaría. El cursor es un
// SpriteVisual y la selección un FillRectangle debajo del texto.

#include <cstddef>
#include <functional>
#include <string>

#include "ui/Edit.h"
#include "ui/Element.h"
#include "ui/Text.h"

namespace Ui {

class Field : public Element {
public:
    explicit Field(std::wstring placeholder = std::wstring());

    void SetText(std::wstring text);
    const std::wstring& Text() const { return m_editor.Text(); }
    void SetMaxLength(std::size_t units) { m_editor.SetMaxLength(units); }

    // Un campo para escribir una credencial. Apaga el historial de deshacer, que si no se
    // queda con una copia del texto pegado. Ver el comentario de Ui::Editor::SetHistoryEnabled.
    void SetSecret(bool secret) { m_editor.SetHistoryEnabled(!secret); }

    void OnChanged(std::function<void(const std::wstring&)> handler) {
        m_changed = std::move(handler);
    }
    void OnSubmit(std::function<void()> handler) { m_submit = std::move(handler); }

    bool Focusable() const override { return true; }
    const wchar_t* CursorId() const override;
    bool CaretRect(Rect& localDip) const override;

protected:
    bool OnAttach() override;
    void OnArrange() override;
    void OnPaint(const Paint& paint, const Rect& box) override;
    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override;
    void OnFocusChanged() override;
    bool OnPointer(const Input::Pointer& e) override;
    bool OnKey(const Input::Key& e) override;
    bool OnChar(wchar_t unit) override;

private:
    // Rehace la maquetación si hace falta y recoloca el cursor. Se llama después de cada
    // cambio: es barato porque Text::Lay no rehace nada si el texto no cambió.
    void Refresh(bool textChanged);
    void PlaceCaretVisual();
    // Mantiene el cursor a la vista cuando el texto es más largo que el campo.
    void ScrollToCaret();
    std::size_t IndexAtLocal(float localX);
    Rect Inner() const;

    void CopyToClipboard(bool cut);
    void PasteFromClipboard();

    Editor m_editor;
    Ui::Text::Layout m_layout;
    std::wstring m_placeholder;

    winrt::Windows::UI::Composition::SpriteVisual m_caret{nullptr};
    winrt::Windows::UI::Composition::CompositionColorBrush m_caretBrush{nullptr};

    // Cuánto se ha corrido el texto hacia la izquierda porque no cabe.
    float m_scrollX = 0.0f;
    bool m_dragging = false;

    std::function<void(const std::wstring&)> m_changed;
    std::function<void()> m_submit;
};

}  // namespace Ui
