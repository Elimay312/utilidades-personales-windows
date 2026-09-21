#pragma once

// Un nodo del kit. No es un framework y no quiere serlo: no hay medir y colocar, no hay
// estilos y no hay enlace de datos. El padre escribe los marcos de sus hijos en DIP,
// igual que Demo::Layout de la fase 1.
//
// Lo que sí resuelve una vez, porque copiado ocho veces es una fábrica de fallos:
//
//   1. Marco en DIP -> Visual.Offset/Size -> reservar la textura al tamaño físico.
//   2. Hover, pulsado, foco y deshabilitado -> un solo OnPaint.
//   3. Entrar y salir del conjunto de repintado, incluido al morir. Una fila reciclada y
//      un menú cerrado se destruyen mientras el enrutador y el conjunto sucio todavía les
//      apuntan; esa es la caída más probable de toda la fase.
//   4. Hit-testing con el origen acumulado.
//
// **Dueños de superficie y pintores.** No todo elemento tiene textura. Un Gfx::Layer son
// dos superficies de Direct2D, y quinientas filas serían mil: la fase 1 ya midió que
// cincuenta repintados dejaban 105 MB. Así que un elemento o tiene su Gfx::Layer —y
// entonces es dueño de superficie— o se pinta dentro de la del ancestro más cercano que
// la tenga, con el origen acumulado. Un menú es una superficie; sus opciones no lo son.
// Una fila de lista sí, porque tiene que fundirse y deslizarse por su cuenta, pero solo
// hay veinticinco vivas a la vez.
//
// Consecuencia del orden en z: dentro de un elemento van primero su material, luego los
// contenedores de sus hijos y encima su Gfx::Layer. O sea que el texto queda por encima
// de todos los materiales, que es lo que se quiere. A cambio, un contenedor cuyos hijos
// sean dueños de superficie no debe pintar contenido propio, porque quedaría por encima
// de ellos. En la práctica un contenedor así no pinta nada.

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "compositor/Layer.h"
#include "compositor/Material.h"
#include "compositor/Shadow.h"
#include "compositor/Winrt.h"
#include "shell/Input.h"
#include "shell/Theme.h"
#include "ui/Metrics.h"

namespace Ui {

class Host;
class Text;

// Lo que hace falta para pintarse. Los tokens van por PUNTERO y no por copia, y eso es
// justo lo que permite pintar dos subárboles con paletas distintas sin ninguna máquina
// añadida: es la costura de las dos columnas del catálogo.
struct Paint {
    ID2D1DeviceContext* dc = nullptr;
    const Theme::Tokens* tokens = nullptr;
    Ui::Text* text = nullptr;
    float scale = 1.0f;
};

class Element {
public:
    Element() = default;
    virtual ~Element();

    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;

    // --- Árbol. El padre es dueño; Add presta un puntero ------------------------------
    template <class T, class... Args>
    T* Add(Args&&... args) {
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = owned.get();
        Adopt(std::move(owned));
        return raw;
    }
    void RemoveAll();
    Element* Parent() const { return m_parent; }
    const std::vector<std::unique_ptr<Element>>& Children() const { return m_children; }

    // --- Vida --------------------------------------------------------------------------
    bool Attach(Host& host, Element* parent,
                const winrt::Windows::UI::Composition::ContainerVisual& parentVisual);
    bool Attached() const { return m_host != nullptr; }
    void Close();

    // --- Geometría, siempre en DIP -------------------------------------------------------
    // Sin animar: un layout nuevo no es una transición, es la realidad nueva. Lo que se
    // mueve con muelle es SlideTo, y solo el desplazamiento: cambiar el tamaño a mitad de
    // un muelle reasignaría la textura en cada fotograma.
    void SetFrame(const Rect& frame);
    void SlideTo(float xDip, float yDip, Motion::Kind kind);
    const Rect& Frame() const { return m_frame; }
    Rect WindowRect() const;

    void SetVisible(bool visible);
    bool Visible() const { return m_visible; }
    void SetEnabled(bool enabled);
    bool Enabled() const { return m_enabled; }

    // --- Pintado --------------------------------------------------------------------------
    // No pinta: apunta. El Host reúne todo lo sucio de un mismo mensaje y lo pinta una vez.
    void Invalidate(float fadeMs = 0.0f);
    // Recoloca el subárbol y lo repinta. Lo llama quien cambia de tamaño.
    void Relayout();
    // Baja por el subárbol. Substitute permite a un contenedor cambiar la paleta.
    void ApplyTheme(const Theme::Tokens& tokens, float crossfadeMs);
    const Theme::Tokens& Tokens() const { return m_tokens; }

    // --- Estado -----------------------------------------------------------------------------
    bool Hovered() const { return m_hovered; }
    bool Pressed() const { return m_pressed; }
    bool Focused() const { return m_focused; }

    // --- Entrada. Coordenadas locales al elemento; true = consumido ----------------------------
    virtual bool HitTest(float lx, float ly) const;
    // Recorta la entrada a su marco. Una lista dice que sí; un contenedor suelto dice que
    // no y deja pasar a los hijos que se salgan de él.
    virtual bool ClipsInput() const { return false; }
    virtual bool Focusable() const { return false; }
    // IDC_IBEAM, IDC_HAND... nullptr para la flecha de siempre.
    virtual const wchar_t* CursorId() const { return nullptr; }

    virtual bool OnPointer(const Input::Pointer& e);
    virtual bool OnKey(const Input::Key& e);
    virtual bool OnChar(wchar_t unit);
    virtual void OnFocusChanged() { Invalidate(); }
    // El rectángulo del cursor de texto, en coordenadas locales. Solo lo contesta un
    // campo de texto; sirve para colocar la ventana del IME donde se está escribiendo.
    virtual bool CaretRect(Rect& localDip) const;

    // Las escribe el enrutador.
    void SetHovered(bool value);
    void SetPressed(bool value);
    void SetFocused(bool value);

    Host* HostOrNull() const { return m_host; }

protected:
    // --- Lo que implementa cada componente -------------------------------------------------
    virtual bool OnAttach() { return true; }
    virtual void OnArrange() {}
    // box es el rectángulo del elemento dentro de la superficie de su dueño, ya con el
    // origen acumulado. Un elemento que no pinta nada no lo sobrescribe.
    virtual void OnPaint(const Paint& paint, const Rect& box);
    // Por defecto repinta. Quien tenga material lo sobrescribe para animar la brocha y no
    // repintar un solo píxel, que es la diferencia entre un cruce de tema en la GPU y uno
    // que reconstruye veinte texturas.
    virtual void OnTheme(const Theme::Tokens& tokens, float crossfadeMs);
    virtual Theme::Tokens Substitute(const Theme::Tokens& tokens) const { return tokens; }
    virtual void OnStateChanged() { Invalidate(); }

    // --- Las tres piezas, por composición y no por herencia ---------------------------------
    bool CreateLayer();
    bool CreateMaterial(float radiusDip);
    // El anillo de foco. Está en la base y no en cada componente porque lo quieren todos
    // los que se pueden enfocar, y porque entonces la animación —entra con opacidad y una
    // escala de 1,04 a 1 con muelle rígido— se escribe una vez y sale igual en todos.
    bool CreateRing(float radiusDip, float outsetDip, float thicknessDip);
    // La sombra suave. Tiene que crearse ANTES que el material y el contenido: mete todo
    // el subárbol dentro de un LayerVisual para sacar la sombra de su alfa, y lo que ya
    // estuviera fuera se quedaría fuera.
    //
    // Solo para lo que flota. Un LayerVisual aplana su subárbol en una superficie fuera
    // de pantalla cada vez que cambia: para un menú no se nota y para una lista de 500
    // filas sería el final del criterio de los 60 fps.
    bool CreateShadow(Metrics::Elevation elevation);
    Gfx::Shadow* ShadowOf() { return m_shadow ? &*m_shadow : nullptr; }
    Gfx::Material* MaterialOf() { return m_material ? &*m_material : nullptr; }
    Gfx::Material* RingOf() { return m_ring ? &*m_ring : nullptr; }
    bool OwnsSurface() const { return m_layer.has_value(); }

    const winrt::Windows::UI::Composition::ContainerVisual& Visual() const { return m_visual; }
    Host& HostRef() const { return *m_host; }

private:
    friend class Painter;
    friend class Router;

    void Adopt(std::unique_ptr<Element> child);
    // Saca el visual del árbol de composición. Ver el comentario de la implementación: es
    // lo que hace que cerrar un elemento se vea, y no solo se cumpla.
    void Unhook();
    Element* SurfaceOwner();
    void Repaint(float fadeMs);
    void PaintSubtree(Paint& paint, float ox, float oy);
    void ArrangeTree();
    void UpdateRing();
    // Dónde cuelgan el material y el contenido: dentro de la sombra si la hay.
    const winrt::Windows::UI::Composition::ContainerVisual& ContentParent() const;

    Host* m_host = nullptr;
    Element* m_parent = nullptr;
    std::vector<std::unique_ptr<Element>> m_children;

    winrt::Windows::UI::Composition::ContainerVisual m_visual{nullptr};
    // Los hijos van en su propio contenedor, en medio. Así el orden queda fijo —material
    // abajo, hijos en medio, contenido arriba— sin tener que reordenar nada cada vez que
    // se añade uno.
    winrt::Windows::UI::Composition::ContainerVisual m_childHost{nullptr};
    std::optional<Gfx::Layer> m_layer;
    std::optional<Gfx::Material> m_material;
    std::optional<Gfx::Material> m_ring;
    std::optional<Gfx::Shadow> m_shadow;

    Rect m_frame;
    // La escala con la que se reservó la textura. Junto con el tamaño del marco es lo que
    // decide si SetFrame tiene que repintar: al cambiar de monitor el marco no cambia y la
    // textura sí, así que sin esto la aplicación entera se quedaría en blanco al arrastrar
    // la ventana a una pantalla con otra escala.
    float m_reservedScale = 0.0f;
    Theme::Tokens m_tokens;
    bool m_visible = true;
    bool m_enabled = true;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_focused = false;
};

}  // namespace Ui
