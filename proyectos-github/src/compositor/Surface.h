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
    //
    // RESERVAR LA TEXTURA LA VACÍA. ICompositionDrawingSurfaceInterop::Resize devuelve un
    // hueco del atlas, y el hueco nuevo trae los píxeles de quien estuviera antes; por eso
    // Surface::Draw empieza siempre por un Clear. O sea que un cambio de tamaño obliga a
    // repintar: el que llama no puede quedarse con lo que había. Y por eso mismo aquí se
    // sale sin tocar nada cuando el tamaño FÍSICO no cambia, que es lo que pasa en casi
    // todas las llamadas —recolocar el árbol vuelve a escribir los mismos marcos— y lo que
    // convertiría cada recolocación en un borrado de toda la pantalla.
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
    // El tamaño con el que se reservó de verdad. En píxeles y no en DIP: dos anchos
    // lógicos distintos pueden redondear al mismo píxel, y lo que decide si la textura
    // hay que pedirla otra vez es el píxel.
    int m_widthPx = 0;
    int m_heightPx = 0;
};

}  // namespace Gfx
