#include "ui/Field.h"

#include <Windows.h>

#include <algorithm>

#include "compositor/Paint.h"
#include "ui/Host.h"

namespace wuc = winrt::Windows::UI::Composition;

namespace Ui {

namespace {

constexpr float kPadding = Metrics::kSpace2;
constexpr float kCaretWidth = 1.0f;
// Sin límite de anchura: en un campo de una línea lo que no cabe se sale y el campo se
// desplaza. Recortar con elipsis cambiaría los índices y el cursor caería donde no es.
constexpr float kNoLimit = 1.0e6f;
// Ocho selecciones de sobra para una línea: lo normal es una, y dos si hay un tramo de
// derecha a izquierda por medio.
constexpr std::size_t kMaxSelectionBoxes = 8;

D2D1_RECT_F ToBox(const Rect& rect) {
    return D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom());
}

}  // namespace

Field::Field(std::wstring placeholder) : m_placeholder(std::move(placeholder)) {}

bool Field::OnAttach() {
    if (!CreateMaterial(Metrics::RadiusOf(Metrics::Radius::Control))) return false;
    if (!CreateRing(Metrics::RadiusOf(Metrics::Radius::Control), Metrics::kFocusGap,
                    Metrics::kFocusRing)) {
        return false;
    }
    if (!CreateLayer()) return false;
    // El borde del campo: un pozo necesita que se le vea el canto o no se lee como un
    // sitio donde escribir.
    if (Gfx::Material* material = MaterialOf()) material->CreateStroke(1.0f);

    m_caretBrush = HostRef().Compositor().CreateColorBrush();
    m_caret = HostRef().Compositor().CreateSpriteVisual();
    m_caret.Brush(m_caretBrush);
    m_caret.IsVisible(false);
    // Encima del contenido: el cursor va sobre el texto, no debajo.
    Visual().Children().InsertAtTop(m_caret);
    return true;
}

Rect Field::Inner() const {
    const Rect frame = Frame();
    return Rect{kPadding, 0.0f, std::max(frame.width - kPadding * 2.0f, 1.0f), frame.height};
}

void Field::SetText(std::wstring text) {
    if (!m_editor.SetText(text)) return;
    m_scrollX = 0.0f;
    Refresh(true);
    if (m_changed) m_changed(m_editor.Text());
}

void Field::OnArrange() { Refresh(true); }

void Field::Refresh(bool textChanged) {
    if (!Attached()) return;
    HostRef().Text().Lay(m_layout, m_editor.Text(), Style::Body, Weight::Regular, kNoLimit,
                         false);
    ScrollToCaret();
    PlaceCaretVisual();
    if (textChanged) Invalidate();
}

void Field::ScrollToCaret() {
    if (!m_layout.Valid()) {
        m_scrollX = 0.0f;
        return;
    }
    const float inner = Inner().width;
    const float caretX = m_layout.CaretAt(m_editor.Cursor().head).x;

    // Solo lo justo para que se vea, y por el lado por el que se salió: mover más de la
    // cuenta hace que el texto salte bajo el dedo mientras se escribe.
    if (caretX - m_scrollX < 0.0f) {
        m_scrollX = caretX;
    } else if (caretX - m_scrollX > inner) {
        m_scrollX = caretX - inner;
    }

    // Y nunca dejar hueco por la derecha si el texto entero cabe.
    const float total = m_layout.Width();
    m_scrollX = std::clamp(m_scrollX, 0.0f, std::max(total - inner, 0.0f));
}

void Field::PlaceCaretVisual() {
    if (!m_caret || !m_layout.Valid()) return;

    const Rect inner = Inner();
    const auto caret = m_layout.CaretAt(m_editor.Cursor().head);
    const float height = caret.height > 0.0f ? caret.height : inner.height * 0.6f;
    const float x = inner.x + caret.x - m_scrollX;
    const float y = (Frame().height - height) * 0.5f;

    m_caret.Offset({x, y, 0.0f});
    m_caret.Size({kCaretWidth, height});

    const bool show = Focused() && m_editor.Cursor().Empty();
    m_caret.IsVisible(show);
    if (show) {
        // Encendido del todo y el parpadeo desde cero: un cursor que parpadea mientras se
        // escribe se lee como un fallo de dibujo, no como un cursor.
        Motion::Animator& animator = HostRef().Animator();
        animator.Solid(m_caret);
        const float period = static_cast<float>(GetCaretBlinkTime()) * 2.0f;
        if (period > 0.0f && period < 20000.0f) animator.Blink(m_caret, period);
    }
}

void Field::OnTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    if (Gfx::Material* material = MaterialOf()) {
        material->SetColor(tokens.fieldSurface, HostRef().Animator(), crossfadeMs);
        material->SetStroke(tokens.controlStroke, HostRef().Animator(), crossfadeMs);
    }
    if (m_caretBrush) {
        if (crossfadeMs > 0.0f) {
            HostRef().Animator().Color(m_caretBrush, Gfx::ToUi(tokens.textPrimary), crossfadeMs);
        } else {
            m_caretBrush.StopAnimation(L"Color");
            m_caretBrush.Color(Gfx::ToUi(tokens.textPrimary));
        }
    }
    Element::OnTheme(tokens, crossfadeMs);
}

void Field::OnFocusChanged() {
    if (!Focused()) {
        // Salir del campo termina «lo que acabo de escribir»: el siguiente Ctrl+Z no debe
        // juntar lo de antes con lo de después.
        m_editor.BreakUndo();
        m_dragging = false;
    }
    PlaceCaretVisual();
    Invalidate();
}

void Field::OnPaint(const Paint& paint, const Rect& box) {
    const Rect inner = Rect{box.x + kPadding, box.y, std::max(box.width - kPadding * 2.0f, 1.0f),
                            box.height};

    if (!m_layout.Valid()) return;

    const float textHeight = m_layout.Height();
    const float textY = box.y + (box.height - textHeight) * 0.5f;
    const float textX = inner.x - m_scrollX;

    // El texto de sugerencia, cuando no hay nada escrito.
    if (m_editor.Text().empty()) {
        if (!m_placeholder.empty()) {
            winrt::com_ptr<ID2D1SolidColorBrush> hint;
            paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->textSecondary), hint.put());
            paint.text->DrawLine(paint.dc, m_placeholder, Style::Body, Weight::Regular,
                                 ToBox(inner), hint.get());
        }
        return;
    }

    // La selección, debajo del texto.
    const Range selection = m_editor.Cursor().Ordered();
    if (!selection.Empty()) {
        D2D1_RECT_F boxes[kMaxSelectionBoxes];
        const std::size_t count =
            m_layout.SelectionBoxes(selection.begin, selection.end, boxes, kMaxSelectionBoxes);
        if (count > 0) {
            winrt::com_ptr<ID2D1SolidColorBrush> fill;
            paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->selectionText), fill.put());
            for (std::size_t i = 0; i < std::min(count, kMaxSelectionBoxes); ++i) {
                const D2D1_RECT_F rect = D2D1::RectF(boxes[i].left + textX, boxes[i].top + textY,
                                                     boxes[i].right + textX,
                                                     boxes[i].bottom + textY);
                paint.dc->FillRectangle(rect, fill.get());
            }
        }
    }

    winrt::com_ptr<ID2D1SolidColorBrush> ink;
    paint.dc->CreateSolidColorBrush(
        Gfx::ToD2D(Enabled() ? paint.tokens->textPrimary : paint.tokens->textDisabled), ink.put());
    paint.text->DrawLayout(paint.dc, m_layout, textX, textY, ink.get());
}

std::size_t Field::IndexAtLocal(float localX) {
    if (!m_layout.Valid()) return 0;
    return m_layout.IndexAt(localX - Inner().x + m_scrollX, 0.0f);
}

bool Field::OnPointer(const Input::Pointer& e) {
    if (!Enabled() || e.button != Input::Button::Left) {
        // La rueda no es cosa suya, pero moverse por encima sí: si devolviera true a
        // todo, la lista de debajo no podría desplazarse con el ratón sobre el campo.
        return false;
    }

    switch (e.action) {
    case Input::Action::Down: {
        const std::size_t index = IndexAtLocal(e.x);
        if (e.clicks >= 3) {
            m_editor.SelectAll();
        } else if (e.clicks == 2) {
            m_editor.SelectWordAt(index);
        } else {
            m_editor.PlaceCaret(index, Input::Has(e.modifiers, Input::Modifiers::Shift));
            m_dragging = true;
            // Sin captura, arrastrar para seleccionar se corta en cuanto el puntero se
            // sale del campo, que es justo cuando más falta hace.
            HostRef().Input().Capture(this);
        }
        Refresh(false);
        Invalidate();
        return true;
    }
    case Input::Action::Move:
        if (!m_dragging) return false;
        m_editor.PlaceCaret(IndexAtLocal(e.x), true);
        Refresh(false);
        Invalidate();
        return true;
    case Input::Action::Up:
    case Input::Action::Cancel:
        if (m_dragging) {
            m_dragging = false;
            HostRef().Input().Release(this);
        }
        return true;
    default:
        return false;
    }
}

bool Field::OnKey(const Input::Key& e) {
    if (!Enabled() || !e.down) return false;

    const bool shift = Input::Has(e.modifiers, Input::Modifiers::Shift);
    const bool ctrl = Input::Has(e.modifiers, Input::Modifiers::Control);
    bool changed = false;
    bool handled = true;

    switch (e.virtualKey) {
    case VK_LEFT:
        m_editor.MoveCaret(ctrl ? Move::WordLeft : Move::Left, shift);
        break;
    case VK_RIGHT:
        m_editor.MoveCaret(ctrl ? Move::WordRight : Move::Right, shift);
        break;
    case VK_HOME:
        m_editor.MoveCaret(Move::Start, shift);
        break;
    case VK_END:
        m_editor.MoveCaret(Move::End, shift);
        break;
    case VK_BACK:
        changed = m_editor.Backspace();
        break;
    case VK_DELETE:
        if (shift) {
            CopyToClipboard(true);
            changed = true;
        } else {
            changed = m_editor.DeleteForward();
        }
        break;
    case VK_RETURN:
        if (m_submit) m_submit();
        break;
    case 'A':
        if (!ctrl) return false;
        m_editor.SelectAll();
        break;
    case 'C':
        if (!ctrl) return false;
        CopyToClipboard(false);
        break;
    case 'X':
        if (!ctrl) return false;
        CopyToClipboard(true);
        changed = true;
        break;
    case 'V':
        if (!ctrl) return false;
        PasteFromClipboard();
        changed = true;
        break;
    case 'Z':
        if (!ctrl) return false;
        // Ctrl+Mayús+Z rehace, como en todas partes; Ctrl+Y también, más abajo.
        changed = shift ? m_editor.Redo() : m_editor.Undo();
        break;
    case 'Y':
        if (!ctrl) return false;
        changed = m_editor.Redo();
        break;
    default:
        handled = false;
        break;
    }

    if (!handled) return false;
    Refresh(true);
    if (changed && m_changed) m_changed(m_editor.Text());
    return true;
}

bool Field::OnChar(wchar_t unit) {
    if (!Enabled()) return false;
    // Los controles ya los filtró la ventana; aquí solo llegan caracteres de verdad, y
    // por eso las tildes y la eñe entran sin nada especial: TranslateMessage ya compuso
    // la tecla muerta antes de que esto existiera.
    if (!m_editor.Insert(std::wstring_view{&unit, 1})) return true;
    Refresh(true);
    if (m_changed) m_changed(m_editor.Text());
    return true;
}

const wchar_t* Field::CursorId() const { return Enabled() ? IDC_IBEAM : nullptr; }

bool Field::CaretRect(Rect& localDip) const {
    if (!m_layout.Valid()) return false;
    const auto caret = m_layout.CaretAt(m_editor.Cursor().head);
    const float height = caret.height > 0.0f ? caret.height : Frame().height * 0.6f;
    localDip = Rect{kPadding + caret.x - m_scrollX, (Frame().height - height) * 0.5f,
                    kCaretWidth, height};
    return true;
}

void Field::CopyToClipboard(bool cut) {
    const std::wstring_view selected = m_editor.Selected();
    if (selected.empty()) return;

    if (!OpenClipboard(HostRef().Window())) return;
    if (EmptyClipboard()) {
        const std::size_t bytes = (selected.size() + 1) * sizeof(wchar_t);
        if (const HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            if (auto* target = static_cast<wchar_t*>(GlobalLock(handle))) {
                std::copy(selected.begin(), selected.end(), target);
                target[selected.size()] = L'\0';
                GlobalUnlock(handle);
                // Si SetClipboardData funciona, el portapapeles se queda con la memoria;
                // si no, es nuestra y hay que soltarla.
                if (!SetClipboardData(CF_UNICODETEXT, handle)) GlobalFree(handle);
            } else {
                GlobalFree(handle);
            }
        }
    }
    CloseClipboard();

    if (cut) m_editor.DeleteSelection();
}

void Field::PasteFromClipboard() {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return;
    if (!OpenClipboard(HostRef().Window())) return;

    if (const HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* source = static_cast<const wchar_t*>(GlobalLock(handle))) {
            std::wstring text{source};
            GlobalUnlock(handle);
            // Un campo de una línea: lo pegado se aplana. Pegar un párrafo de dos líneas
            // y que aparezcan cuadraditos es peor que pegar la primera línea.
            const std::size_t cut = text.find_first_of(L"\r\n");
            if (cut != std::wstring::npos) text.resize(cut);
            m_editor.Insert(text);
        }
    }
    CloseClipboard();
}

}  // namespace Ui
