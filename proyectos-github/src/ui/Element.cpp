#include "ui/Element.h"

#include <algorithm>

#include "ui/Host.h"

namespace wuc = winrt::Windows::UI::Composition;

namespace Ui {

namespace {

// Composition rechaza una superficie de lado cero, y la ventana se puede arrastrar hasta
// el mínimo.
constexpr float kMinSide = 1.0f;

// Lo que crece el anillo de foco antes de asentarse.
constexpr float kRingOvershoot = 1.04f;

}  // namespace

Element::~Element() {
    // Los hijos primero: cada uno se desapunta a sí mismo al morir. Y después nosotros.
    // Sin esto, el conjunto sucio y el enrutador se quedan con punteros a memoria
    // liberada, que es el fallo más probable de toda la fase —una fila reciclada y un
    // menú cerrado mueren mientras los dos todavía les apuntan—.
    m_children.clear();
    Unhook();
    if (m_host) {
        m_host->Paints().Forget(this);
        m_host->Input().Forget(this);
    }
}

void Element::Unhook() {
    if (!m_visual) return;
    // Soltar NUESTRA referencia no basta. El contenedor del padre tiene la suya, así que el
    // visual se queda en el árbol de composición y se sigue dibujando: al cambiar de raíz,
    // la vista vieja se quedaba detrás de la nueva. Se ve poco porque casi todo lo que se
    // cierra tiene además su superficie cerrada y deja de pintar nada, y eso es justo lo
    // que lo hace difícil de encontrar: un contenedor sin superficie propia no deja rastro
    // hasta que alguien cuenta los visuales.
    if (auto parent = m_visual.Parent()) parent.Children().Remove(m_visual);
}

void Element::Adopt(std::unique_ptr<Element> child) {
    Element* raw = child.get();
    m_children.push_back(std::move(child));
    if (m_host && m_childHost) raw->Attach(*m_host, this, m_childHost);
}

void Element::RemoveAll() { m_children.clear(); }

bool Element::Attach(Host& host, Element* parent, const wuc::ContainerVisual& parentVisual) {
    m_host = &host;
    m_parent = parent;
    m_tokens = parent ? parent->m_tokens : host.Tokens();

    const auto& compositor = host.Compositor();
    m_visual = compositor.CreateContainerVisual();
    parentVisual.Children().InsertAtTop(m_visual);

    m_childHost = compositor.CreateContainerVisual();
    m_childHost.RelativeSizeAdjustment({1.0f, 1.0f});
    m_visual.Children().InsertAtTop(m_childHost);

    if (!OnAttach()) return false;

    // Los hijos que se hayan añadido antes de estar enganchados —en el constructor—
    // todavía no tienen visual. Los que añadiera OnAttach ya los enganchó Adopt.
    for (auto& child : m_children) {
        if (!child->Attached() && !child->Attach(host, this, m_childHost)) return false;
    }
    return true;
}

void Element::Close() {
    m_children.clear();
    if (m_layer) m_layer->Close();
    if (m_material) m_material->Close();
    if (m_ring) m_ring->Close();
    if (m_shadow) m_shadow->Close();
    m_layer.reset();
    m_material.reset();
    m_ring.reset();
    m_shadow.reset();
    Unhook();
    m_childHost = nullptr;
    m_visual = nullptr;
}

const wuc::ContainerVisual& Element::ContentParent() const {
    return m_shadow && m_shadow->HasShadow() ? m_shadow->Content() : m_visual;
}

bool Element::CreateShadow(Metrics::Elevation elevation) {
    if (m_shadow) return true;
    if (!m_host || !m_visual) return false;

    m_shadow.emplace();
    if (!m_shadow->Create(m_host->Compositor(), m_visual, elevation)) {
        m_shadow.reset();
        return false;
    }
    // Los hijos se mudan dentro: la sombra sale del alfa de TODO lo que haya en la capa,
    // y lo que se quedara fuera no proyectaría.
    if (m_shadow->HasShadow() && m_childHost) {
        m_visual.Children().Remove(m_childHost);
        m_shadow->Content().Children().InsertAtTop(m_childHost);
    }
    return true;
}

bool Element::CreateLayer() {
    if (m_layer) return true;
    if (!m_host || !m_visual) return false;

    m_layer.emplace();
    if (!m_layer->Create(m_host->Compositor())) {
        m_layer.reset();
        return false;
    }
    // Arriba del todo: el contenido dibujado va por encima del material propio y de los
    // materiales de los hijos.
    ContentParent().Children().InsertAtTop(m_layer->Visual());
    return true;
}

bool Element::CreateMaterial(float radiusDip) {
    if (m_material) return true;
    if (!m_host || !m_visual) return false;

    m_material.emplace();
    if (!m_material->Create(m_host->Compositor(), ContentParent(), radiusDip)) {
        m_material.reset();
        return false;
    }
    return true;
}

bool Element::CreateRing(float radiusDip, float outsetDip, float thicknessDip) {
    if (m_ring) return true;
    if (!m_host || !m_visual) return false;

    m_ring.emplace();
    if (!m_ring->Create(m_host->Compositor(), m_visual, radiusDip + outsetDip)) {
        m_ring.reset();
        return false;
    }
    m_ring->SetOutset(outsetDip);
    if (!m_ring->CreateStroke(thicknessDip)) {
        m_ring.reset();
        return false;
    }
    // Sin relleno: solo el trazo. El color se pone en ApplyTheme.
    m_ring->SetColor(Theme::Color{0, 0, 0, 0}, m_host->Animator(), 0.0f);
    m_ring->Visual().Opacity(0.0f);
    // El centro atado al tamaño, o la escala de entrada tiraría del anillo hacia la
    // esquina superior izquierda en vez de abrirlo desde el medio.
    m_host->Animator().BindCenterPoint(m_ring->Visual());
    m_ring->Visual().Scale({kRingOvershoot, kRingOvershoot, 1.0f});
    return true;
}

void Element::SetFrame(const Rect& frame) {
    // Cambiar de tamaño obliga a repintar, y cambiar de sitio no. Reservar la textura la
    // VACÍA —Composition devuelve un hueco del atlas con los píxeles del inquilino
    // anterior, ver Surface.h— así que un elemento que se ensancha y no se repinta se queda
    // en blanco. No puede esperar a que cambie su contenido: un título que dice siempre lo
    // mismo no cambia nunca, y desaparecería al redimensionar la ventana para no volver.
    const bool resized = m_frame.width != frame.width || m_frame.height != frame.height ||
                         (m_host != nullptr && m_host->Scale() != m_reservedScale);
    m_frame = frame;
    if (!m_visual || !m_host) return;

    m_visual.StopAnimation(L"Offset");
    m_visual.Offset({frame.x, frame.y, 0.0f});
    m_visual.Size({frame.width, frame.height});

    if (m_material) m_material->SetSize(frame.width, frame.height);
    if (m_ring) m_ring->SetSize(frame.width, frame.height);

    if (m_layer) {
        const Rect safe = frame.AtLeast(kMinSide);
        m_layer->Place(0.0f, 0.0f, safe.width, safe.height);
        m_layer->Resize(m_host->Device(), m_host->Scale());
        if (resized) Invalidate();
    }
    m_reservedScale = m_host->Scale();

    OnArrange();
}

void Element::SlideTo(float xDip, float yDip, Motion::Kind kind) {
    m_frame.x = xDip;
    m_frame.y = yDip;
    if (!m_visual || !m_host) return;
    // Solo el desplazamiento se anima. El tamaño no: cambiarlo a mitad de un muelle
    // reasignaría la textura en cada fotograma, que es justo lo que la fase 1 midió que
    // no había que hacer.
    m_host->Animator().Offset(m_visual, {xDip, yDip, 0.0f}, kind);
}

Rect Element::WindowRect() const {
    Rect rect = m_frame;
    for (const Element* parent = m_parent; parent != nullptr; parent = parent->m_parent) {
        rect.x += parent->m_frame.x;
        rect.y += parent->m_frame.y;
    }
    return rect;
}

void Element::SetVisible(bool visible) {
    if (m_visible == visible) return;
    m_visible = visible;
    if (m_visual) m_visual.IsVisible(visible);
}

void Element::SetEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!enabled) {
        m_hovered = false;
        m_pressed = false;
    }
    OnStateChanged();
}

void Element::Invalidate(float fadeMs) {
    if (!m_host) return;
    Element* owner = SurfaceOwner();
    if (owner) m_host->Paints().Invalidate(owner, fadeMs);
}

Element* Element::SurfaceOwner() {
    for (Element* node = this; node != nullptr; node = node->m_parent) {
        if (node->m_layer) return node;
    }
    return nullptr;
}

void Element::Repaint(float fadeMs) {
    if (!m_layer || !m_host) return;

    Paint paint;
    paint.tokens = &m_tokens;
    paint.text = &m_host->Text();
    paint.scale = m_host->Scale();

    m_layer->Redraw(
        m_host->Device(),
        [this, &paint](const Gfx::Surface::Canvas& canvas) {
            paint.dc = canvas.dc;
            PaintSubtree(paint, 0.0f, 0.0f);
        },
        fadeMs, m_host->Animator());
}

void Element::PaintSubtree(Paint& paint, float ox, float oy) {
    // Los tokens se cambian al entrar y se devuelven al salir: un contenedor puede
    // sustituirlos y sus descendientes se pintan con los suyos sin enterarse.
    const Theme::Tokens* saved = paint.tokens;
    paint.tokens = &m_tokens;

    OnPaint(paint, Rect{ox, oy, m_frame.width, m_frame.height});

    for (const auto& child : m_children) {
        if (!child->m_visible) continue;
        // El que tiene superficie propia se pinta en la suya, no aquí.
        if (child->m_layer) continue;
        child->PaintSubtree(paint, ox + child->m_frame.x, oy + child->m_frame.y);
    }

    paint.tokens = saved;
}

void Element::OnPaint(const Paint&, const Rect&) {}

void Element::ArrangeTree() {
    // Volver a escribir el mismo marco no es redundante: reserva las texturas a la escala
    // de ahora, que es lo que hace falta al cambiar de monitor.
    SetFrame(m_frame);
    for (const auto& child : m_children) child->ArrangeTree();
}

void Element::Relayout() {
    ArrangeTree();
    Invalidate();
}

void Element::ApplyTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    m_tokens = Substitute(tokens);
    OnTheme(m_tokens, crossfadeMs);
    for (const auto& child : m_children) child->ApplyTheme(m_tokens, crossfadeMs);
}

void Element::OnTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    if (m_ring) m_ring->SetStroke(tokens.focusRing, m_host->Animator(), crossfadeMs);
    // Solo el dueño de superficie se apunta: los descendientes que pintan en la suya ya
    // entran en el mismo repintado.
    if (m_layer) Invalidate(crossfadeMs);
}

void Element::SetHovered(bool value) {
    if (m_hovered == value) return;
    m_hovered = value;
    OnStateChanged();
}

void Element::SetPressed(bool value) {
    if (m_pressed == value) return;
    m_pressed = value;
    OnStateChanged();
}

void Element::SetFocused(bool value) {
    const bool changed = m_focused != value;
    m_focused = value;
    UpdateRing();
    if (changed) OnFocusChanged();
}

void Element::UpdateRing() {
    if (!m_ring || !m_host) return;

    Motion::Animator& animator = m_host->Animator();
    const bool show = m_focused && m_host->Input().FocusRingVisible();
    const float fade = animator.FadeMs(Motion::Kind::Snappy);

    if (show) {
        // Desde 1,04 y con muelle rígido. Se escribe el valor inicial a mano porque el
        // muelle retoma desde donde esté, y si el anillo se quedó en 1 de la última vez
        // no habría nada que animar.
        m_ring->Visual().StopAnimation(L"Scale");
        m_ring->Visual().Scale({kRingOvershoot, kRingOvershoot, 1.0f});
        animator.Scale(m_ring->Visual(), {1.0f, 1.0f, 1.0f}, Motion::Kind::Snappy);
    }
    animator.Opacity(m_ring->Visual(), show ? 1.0f : 0.0f, fade);
}

bool Element::HitTest(float lx, float ly) const {
    return lx >= 0.0f && lx < m_frame.width && ly >= 0.0f && ly < m_frame.height;
}

bool Element::OnPointer(const Input::Pointer&) { return false; }
bool Element::OnKey(const Input::Key&) { return false; }
bool Element::OnChar(wchar_t) { return false; }
bool Element::CaretRect(Rect&) const { return false; }

}  // namespace Ui
