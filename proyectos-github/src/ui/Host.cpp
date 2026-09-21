#include "ui/Host.h"

#include <algorithm>

#include "compositor/Paint.h"

namespace wuc = winrt::Windows::UI::Composition;

namespace Ui {

// ============================================================================ Painter ==

void Painter::Attach(HWND hwnd) { m_hwnd = hwnd; }

void Painter::Invalidate(Element* owner, float fadeMs) {
    if (!owner) return;

    for (auto& entry : m_dirty) {
        if (entry.first == owner) {
            // El mayor gana: si en el mismo mensaje un hover pide 0 y el cambio de tema
            // pide 250, lo que se ve tiene que ser el cruce.
            entry.second = std::max(entry.second, fadeMs);
            return;
        }
    }
    m_dirty.emplace_back(owner, fadeMs);

    // Uno solo en vuelo. El mensaje cae DESPUÉS del que se está tratando, así que todos
    // los cambios de estado de un mismo evento de entrada se funden en un repintado.
    if (!m_posted && m_hwnd) {
        m_posted = PostMessageW(m_hwnd, kFlushMessage, 0, 0) != FALSE;
    }
}

void Painter::Forget(Element* element) {
    m_dirty.erase(std::remove_if(m_dirty.begin(), m_dirty.end(),
                                 [element](const auto& entry) { return entry.first == element; }),
                  m_dirty.end());
}

void Painter::Flush() {
    m_posted = false;
    // Se saca la lista antes de recorrerla: lo que se ensucie mientras se pinta cae en
    // una lista nueva y pide su propio mensaje. Sin esto, un repintado que invalida algo
    // invalidaría el iterador, o daría vueltas sin parar.
    std::vector<std::pair<Element*, float>> batch;
    batch.swap(m_dirty);
    for (const auto& entry : batch) entry.first->Repaint(entry.second);
}

// ============================================================================= Router ==

void Router::Attach(Host& host) { m_host = &host; }

void Router::Collect(Element* root, std::vector<Element*>& out) {
    if (!root || !root->Visible()) return;
    out.push_back(root);
    for (const auto& child : root->Children()) Collect(child.get(), out);
}

Element* Router::HitTestIn(Element* root, float x, float y) {
    if (!root || !root->Visible()) return nullptr;

    const Rect frame = root->Frame();
    const float lx = x - frame.x;
    const float ly = y - frame.y;

    // Un contenedor que recorta no deja pasar nada de fuera, ni a sus hijos.
    const bool inside = root->HitTest(lx, ly);
    if (root->ClipsInput() && !inside) return nullptr;

    // Al revés que el vector, porque Attach inserta arriba: el último hijo es el que se
    // pinta encima y por tanto el primero que tiene que contestar.
    const auto& children = root->Children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (Element* hit = HitTestIn(it->get(), lx, ly)) return hit;
    }
    return inside && root->Enabled() ? root : nullptr;
}

Element* Router::HitTest(float x, float y) const {
    if (!m_host) return nullptr;

    // La capa flotante primero, de arriba abajo. Con un modal puesto, el recorrido
    // empieza y acaba en él: lo de debajo no existe.
    if (!m_modals.empty()) return HitTestIn(m_modals.back().layer, x, y);
    return HitTestIn(m_host->Root(), x, y);
}

void Router::SetHoverChain(Element* deepest) {
    std::vector<Element*> next;
    for (Element* node = deepest; node != nullptr; node = node->Parent()) next.push_back(node);

    // Sale el que ya no está.
    for (Element* old : m_hover) {
        if (std::find(next.begin(), next.end(), old) == next.end()) old->SetHovered(false);
    }
    // Entra el que no estaba.
    for (Element* node : next) {
        if (std::find(m_hover.begin(), m_hover.end(), node) == m_hover.end()) {
            node->SetHovered(true);
        }
    }
    m_hover = std::move(next);
}

Element* Router::Dispatch(Element* target, const Input::Pointer& e) {
    // Sube por el árbol hasta que alguien lo quiera: el ratón sobre la etiqueta de una
    // fila lo tiene que atender la fila.
    for (Element* node = target; node != nullptr; node = node->Parent()) {
        Input::Pointer local = e;
        const Rect window = node->WindowRect();
        local.x = e.x - window.x;
        local.y = e.y - window.y;
        if (node->OnPointer(local)) return node;
    }
    return nullptr;
}

void Router::Pointer(const Input::Pointer& e) {
    if (!m_host) return;

    if (e.action == Input::Action::Leave) {
        SetHoverChain(nullptr);
        return;
    }

    if (e.action == Input::Action::Cancel) {
        // La captura se fue a otra ventana. Sin soltar el pulsado, el botón se queda
        // hundido para siempre.
        if (m_pressed) m_pressed->SetPressed(false);
        m_pressed = nullptr;
        m_captured = nullptr;
        return;
    }

    // Con captura, todo va al capturador esté donde esté el puntero. Es lo que hace que
    // arrastrar para seleccionar siga funcionando al salirse del campo.
    if (m_captured) {
        Input::Pointer local = e;
        const Rect window = m_captured->WindowRect();
        local.x = e.x - window.x;
        local.y = e.y - window.y;
        m_captured->OnPointer(local);
        if (e.action == Input::Action::Up && m_pressed) {
            m_pressed->SetPressed(false);
            m_pressed = nullptr;
        }
        return;
    }

    Element* target = HitTest(e.x, e.y);

    if (e.action == Input::Action::Down) {
        // Un clic fuera cierra los menús, y NO se reenvía: si se reenviara, cerrar un
        // menú pulsaría de paso el botón que hubiera debajo.
        if (!m_modals.empty() && m_modals.back().lightDismiss && target == nullptr) {
            PopLightDismiss();
            return;
        }
        // Pulsar con el ratón apaga el anillo de foco: solo se enseña a quien navega con
        // el teclado, que es la conducta de macOS y la de Fluent.
        m_focusRing = false;
        Element* focusable = nullptr;
        for (Element* node = target; node != nullptr; node = node->Parent()) {
            if (node->Focusable() && node->Enabled()) {
                focusable = node;
                break;
            }
        }
        Focus(focusable, false);
    }

    SetHoverChain(target);

    Element* handler = Dispatch(target, e);

    if (e.action == Input::Action::Down && handler) {
        m_pressed = handler;
        handler->SetPressed(true);
    } else if (e.action == Input::Action::Up && m_pressed) {
        m_pressed->SetPressed(false);
        m_pressed = nullptr;
    }
}

bool Router::Key(const Input::Key& e) {
    if (!m_host || !e.down) return false;

    // Esc cierra lo de arriba antes de que nadie más lo vea.
    if (e.virtualKey == VK_ESCAPE && !m_modals.empty()) {
        PopLightDismiss();
        return true;
    }

    // Tab lo consume el enrutador: mover el foco es cosa suya y no de los componentes.
    if (e.virtualKey == VK_TAB) {
        m_focusRing = true;
        FocusNext(Input::Has(e.modifiers, Input::Modifiers::Shift));
        return true;
    }

    m_focusRing = true;
    if (m_focused && m_focused->OnKey(e)) return true;

    // Y si el foco no la quiere, que la vea el modal o la raíz: ahí viven los atajos.
    Element* root = m_modals.empty() ? m_host->Root() : m_modals.back().layer;
    return root && root != m_focused && root->OnKey(e);
}

bool Router::Char(wchar_t unit) { return m_focused && m_focused->OnChar(unit); }

void Router::WindowFocus(bool focused) {
    if (!focused) {
        // Se pierde el foco de la ventana: fuera los menús y el hover, o se quedan
        // encendidos encima de otra aplicación.
        PopLightDismiss();
        SetHoverChain(nullptr);
        if (m_pressed) m_pressed->SetPressed(false);
        m_pressed = nullptr;
        m_captured = nullptr;
    }
    if (m_focused) m_focused->SetFocused(focused && m_focused->Focused());
}

const wchar_t* Router::CursorAt(float x, float y) const {
    const Element* node = m_captured ? m_captured : HitTest(x, y);
    for (; node != nullptr; node = node->Parent()) {
        if (const wchar_t* cursor = node->CursorId()) return cursor;
    }
    return nullptr;
}

void Router::Capture(Element* element) { m_captured = element; }

void Router::Release(Element* element) {
    if (m_captured == element) m_captured = nullptr;
}

void Router::Focus(Element* element, bool fromKeyboard) {
    if (fromKeyboard) m_focusRing = true;
    if (m_focused == element) {
        // Aunque no cambie, el anillo puede tener que encenderse o apagarse.
        if (m_focused) m_focused->SetFocused(true);
        return;
    }
    if (m_focused) m_focused->SetFocused(false);
    m_focused = element;
    if (m_focused) m_focused->SetFocused(true);
}

void Router::FocusNext(bool backwards) {
    if (!m_host) return;

    // Dentro del modal si lo hay: el foco queda atrapado en la hoja mientras esté abierta.
    Element* root = m_modals.empty() ? m_host->Root() : m_modals.back().layer;
    if (!root) return;

    std::vector<Element*> all;
    Collect(root, all);

    std::vector<Element*> stops;
    for (Element* node : all) {
        if (node->Focusable() && node->Enabled()) stops.push_back(node);
    }
    if (stops.empty()) return;

    const auto found = std::find(stops.begin(), stops.end(), m_focused);
    std::size_t index = 0;
    if (found == stops.end()) {
        index = backwards ? stops.size() - 1 : 0;
    } else {
        const std::size_t current = static_cast<std::size_t>(found - stops.begin());
        // Da la vuelta por los dos lados.
        index = backwards ? (current + stops.size() - 1) % stops.size()
                          : (current + 1) % stops.size();
    }
    Focus(stops[index], true);
}

void Router::PushModal(const Modal& modal) {
    if (!modal.layer) return;
    m_modals.push_back(modal);
    SetHoverChain(nullptr);
    // El foco se va dentro. Lo que tuviera el foco fuera lo recupera al cerrarse, que lo
    // gestiona quien abrió la capa.
    Focus(nullptr, false);
    FocusNext(false);
}

void Router::PopModal(Element* layer) {
    m_modals.erase(std::remove_if(m_modals.begin(), m_modals.end(),
                                  [layer](const Modal& modal) { return modal.layer == layer; }),
                   m_modals.end());
    SetHoverChain(nullptr);
    Focus(nullptr, false);
}

void Router::PopLightDismiss() {
    if (m_modals.empty() || !m_host) return;
    Element* layer = m_modals.back().layer;
    m_host->PopLayer(layer);
}

Element* Router::TopModal() const {
    return m_modals.empty() ? nullptr : m_modals.back().layer;
}

void Router::Forget(Element* element) {
    m_hover.erase(std::remove(m_hover.begin(), m_hover.end(), element), m_hover.end());
    if (m_captured == element) m_captured = nullptr;
    if (m_pressed == element) m_pressed = nullptr;
    if (m_focused == element) m_focused = nullptr;
    m_modals.erase(std::remove_if(m_modals.begin(), m_modals.end(),
                                  [element](const Modal& m) { return m.layer == element; }),
                   m_modals.end());
}

// =============================================================================== Host ==

bool Host::Create(Gfx::Scene& scene, Gfx::Device& device, Motion::Animator& animator,
                  Ui::Text& text, HWND hwnd) {
    m_device = &device;
    m_animator = &animator;
    m_text = &text;
    m_hwnd = hwnd;
    m_compositor = scene.Compositor();
    m_queue = scene.Queue();

    m_contentRoot = m_compositor.CreateContainerVisual();
    m_contentRoot.RelativeSizeAdjustment({1.0f, 1.0f});
    scene.Content().Children().InsertAtTop(m_contentRoot);

    // El velo, entre el contenido y la capa flotante. Siempre presente y a opacidad cero:
    // crearlo y destruirlo con cada hoja es un parpadeo garantizado.
    m_scrimBrush = m_compositor.CreateColorBrush();
    m_scrim = m_compositor.CreateSpriteVisual();
    m_scrim.RelativeSizeAdjustment({1.0f, 1.0f});
    m_scrim.Brush(m_scrimBrush);
    m_scrim.Opacity(0.0f);
    m_scrim.IsHitTestVisible(false);
    scene.Content().Children().InsertAtTop(m_scrim);

    m_overlayRoot = m_compositor.CreateContainerVisual();
    m_overlayRoot.RelativeSizeAdjustment({1.0f, 1.0f});
    scene.Content().Children().InsertAtTop(m_overlayRoot);

    m_painter.Attach(hwnd);
    m_router.Attach(*this);
    return true;
}

void Host::Close() {
    PopAllLayers();
    ClearRoot();
    m_scrim = nullptr;
    m_scrimBrush = nullptr;
    m_overlayRoot = nullptr;
    m_contentRoot = nullptr;
    m_queue = nullptr;
    m_compositor = nullptr;
}

void Host::AdoptRoot(std::unique_ptr<Element> root) {
    ClearRoot();
    m_root = std::move(root);
    m_root->Attach(*this, nullptr, m_contentRoot);
    m_root->SetFrame(Rect{0.0f, 0.0f, m_width, m_height});
    m_root->ApplyTheme(m_tokens, 0.0f);
}

void Host::ClearRoot() {
    if (!m_root) return;
    m_root->Close();
    m_root.reset();
}

void Host::AdoptLayer(std::unique_ptr<Element> layer, const LayerOptions& options) {
    Element* raw = layer.get();
    m_layers.push_back(Layer{std::move(layer), options});
    raw->Attach(*this, nullptr, m_overlayRoot);
    raw->SetFrame(Rect{0.0f, 0.0f, m_width, m_height});
    raw->ApplyTheme(m_tokens, 0.0f);

    if (options.modal || options.lightDismiss) {
        m_router.PushModal(Router::Modal{raw, options.lightDismiss});
    }
    LayoutScrim();
}

void Host::PopLayer(Element* layer) {
    const auto found = std::find_if(m_layers.begin(), m_layers.end(),
                                    [layer](const Layer& l) { return l.element.get() == layer; });
    if (found == m_layers.end()) return;

    m_router.PopModal(layer);
    found->element->Close();
    m_layers.erase(found);
    LayoutScrim();
}

void Host::PopAllLayers() {
    while (!m_layers.empty()) PopLayer(m_layers.back().element.get());
}

void Host::LayoutScrim() {
    if (!m_scrim) return;
    // Encendido si alguna capa lo pide. Lo hace el velo y no cada hoja para que dos hojas
    // encadenadas no atenúen el doble.
    const bool wanted = std::any_of(m_layers.begin(), m_layers.end(),
                                    [](const Layer& l) { return l.options.scrim; });
    m_animator->Opacity(m_scrim, wanted ? 1.0f : 0.0f,
                        m_animator->FadeMs(Motion::Kind::Expressive));
}

void Host::Layout(float widthDip, float heightDip, float scale) {
    m_width = widthDip;
    m_height = heightDip;
    m_scale = scale;

    if (m_root) {
        m_root->SetFrame(Rect{0.0f, 0.0f, widthDip, heightDip});
        m_root->Relayout();
    }
    for (auto& layer : m_layers) {
        layer.element->SetFrame(Rect{0.0f, 0.0f, widthDip, heightDip});
        layer.element->Relayout();
    }
}

void Host::ApplyTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    m_tokens = tokens;
    if (m_scrimBrush) {
        if (crossfadeMs > 0.0f) {
            m_animator->Color(m_scrimBrush, Gfx::ToUi(tokens.scrim), crossfadeMs);
        } else {
            m_scrimBrush.StopAnimation(L"Color");
            m_scrimBrush.Color(Gfx::ToUi(tokens.scrim));
        }
    }
    if (m_root) m_root->ApplyTheme(tokens, crossfadeMs);
    for (auto& layer : m_layers) layer.element->ApplyTheme(tokens, crossfadeMs);
}

bool Host::CaretRect(Rect& caretDip) const {
    const Element* focused = m_router.Focused();
    if (!focused) return false;

    Rect local;
    if (!focused->CaretRect(local)) return false;

    const Rect window = focused->WindowRect();
    caretDip = local.Moved(window.x, window.y);
    return true;
}

}  // namespace Ui
