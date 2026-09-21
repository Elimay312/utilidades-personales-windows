#pragma once

// Una superficie de dibujo reutilizable, con las dos trampas de Composition metidas
// dentro para que ningún llamante tenga que acordarse de ellas:
//
//  1. BeginDraw puede devolver un HUECO dentro de un atlas compartido, no una textura
//     propia. Hay que dibujar en el desplazamiento que indica o todo acaba encima del
//     inquilino anterior. Aquí se aplica como transformación, así que quien pinta
//     siempre cree empezar en (0,0).
//  2. El contexto nace con el DPI del escritorio. Se fija al de la ventana para que el
//     dibujo se escriba en DIP y salga nítido a cualquier escala.
//
// Y la lección que el lanzador midió: NO se pide una superficie nueva en cada repintado.
// Cincuenta repintados dejaban ~105 MB porque el compositor no las devuelve a la vez.
// Se crea una y se redimensiona.

#include <Windows.h>

#include <d2d1_1.h>
#include <functional>
#include "compositor/Winrt.h"

namespace Gfx {

class Device;

class Surface {
public:
    // Lo que recibe quien dibuja: el contexto ya colocado y en DIP, y el tamaño útil.
    struct Canvas {
        ID2D1DeviceContext* dc = nullptr;
        float width = 0.0f;
        float height = 0.0f;
    };

    using Painter = std::function<void(const Canvas&)>;

    // widthDip/heightDip es el tamaño lógico; scale, la escala de la ventana. La textura
    // sale del tamaño físico, que es lo que hace que el texto no se emborrone.
    bool Resize(Device& device, float widthDip, float heightDip, float scale);

    // Redibuja entero. Devuelve false si el dispositivo se perdió y hay que rehacerlo.
    bool Draw(const Painter& painter);

    const winrt::Windows::UI::Composition::CompositionDrawingSurface& Handle() const {
        return m_surface;
    }
    bool Ready() const { return m_surface != nullptr; }

    // Suelta la textura. Hay que llamarlo: una CompositionDrawingSurface es memoria de
    // Direct2D y no se va sola cuando se suelta el visual que la usaba.
    void Close();

private:
    winrt::Windows::UI::Composition::CompositionDrawingSurface m_surface{nullptr};
    float m_widthDip = 0.0f;
    float m_heightDip = 0.0f;
    float m_scale = 1.0f;
};

}  // namespace Gfx
