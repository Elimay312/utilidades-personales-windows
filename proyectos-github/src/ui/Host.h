#pragma once

// El anfitrión del kit: lo que un elemento necesita del exterior, quién reparte la
// entrada y quién decide cuándo se repinta.
//
// Painter, Router y Host van en el mismo archivo a propósito. Son tres clases pequeñas
// que se llaman entre ellas en cada evento; separarlas en tres cabeceras obligaría a
// declaraciones adelantadas cruzadas para no ganar nada.
//
// **Dos raíces y no una.** Gfx::Scene::Content() recibe dos hijos: el contenido y la capa
// flotante. Está forzado por la API: las sombras no las recorta el clip implícito del
// tamaño, pero sí las recorta un Visual.Clip explícito, y Gfx::Morph pone uno en su raíz.
// Un menú colgado dentro de un Morph tendría la sombra cortada. De paso, el orden de
// hit-testing sale solo: primero la capa flotante, luego el contenido.

#include <Windows.h>

#include <memory>
#include <utility>
#include <vector>

#include "compositor/Device.h"
#include "compositor/Motion.h"
#include "compositor/Scene.h"
#include "compositor/Winrt.h"
#include "shell/Input.h"
#include "shell/Theme.h"
#include "ui/Element.h"
#include "ui/Text.h"

namespace Ui {

// Reúne los repintados. No hay tic por fotograma y no debe haberlo: las animaciones
// corren en el proceso de DWM y este hilo no tiene por qué despertarse. Lo único que hay
// que evitar es que un WM_MOUSEMOVE que apaga un hover y enciende otro provoque dos
// BeginDraw donde cabía uno.
class Painter {
public:
    static constexpr UINT kFlushMessage = WM_APP + 1;

    void Attach(HWND hwnd);

    // fadeMs se queda con el MAYOR pedido: si en el mismo mensaje un hover pide 0 y el
    // cambio de tema pide 250, gana el cruce.
    void Invalidate(Element* owner, float fadeMs);
    // Obligatorio desde ~Element. Sin esto, una fila reciclada deja un puntero muerto.
    void Forget(Element* element);

    void Flush();
    bool Pending() const { return !m_dirty.empty(); }

private:
    HWND m_hwnd = nullptr;
    bool m_posted = false;
    // Un vector y no un conjunto: son tres o cuatro entradas, y pintar en el orden en que
    // se ensuciaron es el único orden que no sorprende.
    std::vector<std::pair<Element*, float>> m_dirty;
};

// Reparte la entrada por el árbol: hit-testing, hover, captura, foco y modales.
class Router {
public:
    void Attach(Host& host);

    void Pointer(const Input::Pointer& e);
    bool Key(const Input::Key& e);
    bool Char(wchar_t unit);
    void WindowFocus(bool focused);
    // Devuelve el cursor que pida quien esté debajo del punto, o nullptr.
    const wchar_t* CursorAt(float x, float y) const;

    // La pide un elemento en su Down y se suelta en el Up. Mientras dura, todo el ratón
    // va a él esté donde esté el puntero, que es lo que hace que arrastrar para
    // seleccionar siga funcionando al salirse del campo.
    void Capture(Element* element);
    void Release(Element* element);
    Element* Captured() const { return m_captured; }

    void Focus(Element* element, bool fromKeyboard);
    Element* Focused() const { return m_focused; }
    void FocusNext(bool backwards);
    // El anillo solo se enseña si el foco llegó por teclado, que es la conducta de macOS
    // y la de Fluent: pinchar un campo no debe dibujarle un halo.
    bool FocusRingVisible() const { return m_focusRing; }

    struct Modal {
        Element* layer = nullptr;
        bool lightDismiss = true;
    };
    void PushModal(const Modal& modal);
    void PopModal(Element* layer);
    void PopLightDismiss();
    Element* TopModal() const;

    void Forget(Element* element);

private:
    Element* HitTest(float x, float y) const;
    static Element* HitTestIn(Element* root, float x, float y);
    void SetHoverChain(Element* deepest);
    static void Collect(Element* root, std::vector<Element*>& out);
    Element* Dispatch(Element* target, const Input::Pointer& e);

    Host* m_host = nullptr;
    // La CADENA y no un puntero: el ratón sobre la píldora de una fila tiene que resaltar
    // la fila también. Entrar y salir se emiten para la diferencia entre la vieja y la
    // nueva.
    std::vector<Element*> m_hover;
    Element* m_captured = nullptr;
    Element* m_pressed = nullptr;
    Element* m_focused = nullptr;
    bool m_focusRing = false;
    std::vector<Modal> m_modals;
};

class Host {
public:
    bool Create(Gfx::Scene& scene, Gfx::Device& device, Motion::Animator& animator,
                Ui::Text& text, HWND hwnd);
    void Close();

    // --- Recursos que piden los elementos ------------------------------------------------
    Gfx::Device& Device() const { return *m_device; }
    Motion::Animator& Animator() const { return *m_animator; }
    Ui::Text& Text() const { return *m_text; }
    const winrt::Windows::UI::Composition::Compositor& Compositor() const { return m_compositor; }
    Painter& Paints() { return m_painter; }
    Router& Input() { return m_router; }
    HWND Window() const { return m_hwnd; }
    // La cola de este hilo, la que ya crea Gfx::Scene. Sirve para programar algo «dentro
    // de un rato» sin meter un WM_TIMER en la ventana.
    winrt::Windows::System::DispatcherQueue Queue() const { return m_queue; }

    float Scale() const { return m_scale; }
    float WidthDip() const { return m_width; }
    float HeightDip() const { return m_height; }
    const Theme::Tokens& Tokens() const { return m_tokens; }

    const winrt::Windows::UI::Composition::ContainerVisual& ContentRoot() const {
        return m_contentRoot;
    }
    const winrt::Windows::UI::Composition::ContainerVisual& OverlayRoot() const {
        return m_overlayRoot;
    }

    // --- La vista -------------------------------------------------------------------------
    template <class T, class... Args>
    T* SetRoot(Args&&... args) {
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = owned.get();
        AdoptRoot(std::move(owned));
        return raw;
    }
    Element* Root() const { return m_root.get(); }
    void ClearRoot();

    // --- Capas flotantes: menú, aviso y hoja ------------------------------------------------
    struct LayerOptions {
        bool modal = false;        // se lleva toda la entrada
        bool lightDismiss = true;  // un clic fuera lo cierra
        bool scrim = false;        // fondo atenuado detrás
    };
    template <class T, class... Args>
    T* PushLayer(const LayerOptions& options, Args&&... args) {
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = owned.get();
        AdoptLayer(std::move(owned), options);
        return raw;
    }
    void PopLayer(Element* layer);
    void PopAllLayers();
    bool HasLayers() const { return !m_layers.empty(); }

    // --- Ciclo de vida de la ventana -----------------------------------------------------------
    void Layout(float widthDip, float heightDip, float scale);
    void ApplyTheme(const Theme::Tokens& tokens, float crossfadeMs);
    // Repinta lo pendiente ya mismo. Lo llama App antes de enseñar la ventana: el primer
    // fotograma no puede esperar a un bucle de mensajes que todavía no corre.
    void FlushNow() { m_painter.Flush(); }

    // El rectángulo del cursor de texto del elemento con foco, para colocar la ventana del
    // IME. false si quien tiene el foco no es un campo de texto.
    bool CaretRect(Rect& caretDip) const;

private:
    void AdoptRoot(std::unique_ptr<Element> root);
    void AdoptLayer(std::unique_ptr<Element> layer, const LayerOptions& options);
    void LayoutScrim();

    struct Layer {
        std::unique_ptr<Element> element;
        LayerOptions options;
    };

    Gfx::Device* m_device = nullptr;
    Motion::Animator* m_animator = nullptr;
    Ui::Text* m_text = nullptr;
    HWND m_hwnd = nullptr;
    winrt::Windows::UI::Composition::Compositor m_compositor{nullptr};
    winrt::Windows::System::DispatcherQueue m_queue{nullptr};

    winrt::Windows::UI::Composition::ContainerVisual m_contentRoot{nullptr};
    winrt::Windows::UI::Composition::ContainerVisual m_overlayRoot{nullptr};
    // El velo de las hojas modales. Uno solo y siempre presente, a opacidad cero cuando no
    // hace falta: crearlo y destruirlo con cada hoja es un parpadeo garantizado.
    winrt::Windows::UI::Composition::SpriteVisual m_scrim{nullptr};
    winrt::Windows::UI::Composition::CompositionColorBrush m_scrimBrush{nullptr};

    std::unique_ptr<Element> m_root;
    std::vector<Layer> m_layers;

    Painter m_painter;
    Router m_router;
    Theme::Tokens m_tokens;
    float m_scale = 1.0f;
    float m_width = 0.0f;
    float m_height = 0.0f;
};

}  // namespace Ui
