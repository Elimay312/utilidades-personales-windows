#pragma once

// La barra de título: los tres botones de la ventana, el nombre de la aplicación y el
// indicador de sincronización.
//
// Lo que queda de Views::Demo, que la fase 4 tira, y ahora escrito con el kit: los botones
// son elementos con material y superficie, y no visuales colocados a mano. Lo que NO cambia
// es de dónde viene su estado: la franja de arriba contesta HTCAPTION y HTMINBUTTON al
// hit-test del marco, así que ahí no llega un solo WM_MOUSEMOVE del área de cliente. El
// hover y el pulsado los reparte Shell::Window desde los mensajes del marco y entran por
// SetCaptionState. Por eso estos botones no son Ui::Button: no tienen de dónde recibir
// entrada.
//
// Y por eso mismo el indicador de sincronización no se puede pulsar, aunque lo pida el
// cuerpo. Sincronizar es Ctrl+R y el botón de la barra lateral; esto solo informa.

#include <functional>
#include <string>

#include "compositor/Winrt.h"
#include "shell/Caption.h"
#include "ui/Element.h"

namespace Views {

class Chrome : public Ui::Element {
public:
    struct Sync {
        bool running = false;
        bool problem = false;
        // Ya redactado por quien sabe de sincronizaciones. Esta vista no traduce estados.
        std::wstring text;
    };

    void SetCaption(const Caption::Layout& layout, Caption::Zone hovered, Caption::Zone pressed);
    void SetSync(const Sync& sync);
    // El nombre de la aplicación, o el de la pantalla que esté puesta. Lo cambia el
    // catálogo de Debug, que es la otra raíz que existe y también necesita los botones de
    // la ventana: dos títulos pintados en el mismo sitio se leen uno encima del otro.
    void SetTitle(std::wstring title);

    // La franja de arriba no recibe entrada del cliente, así que tampoco puede robarla.
    bool HitTest(float, float) const override { return false; }

protected:
    bool OnAttach() override;
    void OnArrange() override;
    void OnPaint(const Ui::Paint& paint, const Ui::Rect& box) override;

private:
    class WindowButton;
    class Beacon;

    // Lo que mide el texto del indicador. El punto va pegado a su izquierda, así que
    // colocarlo obliga a medirlo antes de pintarlo.
    float TextWidth() const;

    WindowButton* m_buttons[3] = {nullptr, nullptr, nullptr};
    Beacon* m_beacon = nullptr;

    std::wstring m_title = L"Brújula";
    Caption::Layout m_caption;
    Caption::Zone m_hovered = Caption::Zone::Client;
    Caption::Zone m_pressed = Caption::Zone::Client;
    Sync m_sync;
};

}  // namespace Views
