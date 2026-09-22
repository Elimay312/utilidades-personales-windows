#pragma once

// El dispositivo que comparten Direct2D y Composition.
//
// Tiene que ser UNO: DWM muestrea nuestras superficies, así que si Direct2D dibujara en
// un adaptador y el compositor leyera de otro habría una copia por frame entre los dos.
// De aquí salen todas las superficies de la aplicación.

#include <future>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite_3.h>
#include "compositor/Winrt.h"

namespace Gfx {

class Device {
public:
    // Devuelve false si no hay manera de crear el dispositivo. No lanza: los errores son
    // valores y quien llama decide si eso es fatal.
    // **Arranca la creación en un hilo y vuelve enseguida.** No es una optimización de
    // adorno: D3D11CreateDevice es 166-272 ms medidos de un arranque de 280, contra 1-7 ms
    // de todo lo demás del dispositivo junto —DirectWrite, D2D y el enganche al
    // compositor—. Lo que tarda es cargar el controlador de la tarjeta, y para eso no hace
    // falta ni ventana ni compositor, así que puede ir corriendo mientras el hilo de UI
    // crea la ventana, la escena y lee la caché.
    //
    // Llamar dos veces sin FinishCreate en medio no hace nada la segunda vez.
    void BeginCreate();

    // Espera a que el hilo termine y engancha el dispositivo ya creado al compositor.
    // Si nadie llamó a BeginCreate, lo hace todo aquí mismo: son las dos mitades de la
    // misma llamada y el orden entre ellas es lo único que cambia.
    bool FinishCreate(const winrt::Windows::UI::Composition::Compositor& compositor);

    // El dispositivo se pierde al actualizar el controlador de la GPU o al hibernar.
    // Composition avisa con RenderingDeviceReplaced; esto lo rehace todo desde cero.
    bool Recreate();

    const winrt::Windows::UI::Composition::CompositionGraphicsDevice& Graphics() const {
        return m_graphics;
    }
    IDWriteFactory6* Write() const { return m_write.get(); }

    // Cuánto tuvo que esperar el hilo de UI en FinishCreate. Es la medida de si el hilo de
    // BeginCreate sirvió de algo, y se puede volver a mirar: App la guarda en la tabla de
    // ajustes junto al total del arranque, como la fase 3 hizo con los dos pases de la
    // sincronización. En esta máquina el dispositivo tarda ~172 ms, así que la diferencia
    // entre eso y este número es exactamente el trabajo que se le quitó al camino crítico.
    double WaitedMs() const { return m_waitedMs; }
    bool Ready() const { return m_graphics != nullptr; }

private:
    bool CreateRenderingDevice();
    bool CreateWriteFactory();
    bool AttachGraphicsDevice();

    winrt::Windows::UI::Composition::Compositor m_compositor{nullptr};
    winrt::Windows::UI::Composition::CompositionGraphicsDevice m_graphics{nullptr};
    // El hilo que está creando el dispositivo, si lo hay. Un future y no un thread suelto
    // porque lo que hace falta es esperar A UN RESULTADO: un hilo que falla al crear el
    // dispositivo tiene que poder decirlo, y con un thread habría que inventarse dónde
    // guardar ese booleano.
    std::future<bool> m_pending;
    double m_waitedMs = 0.0;
    winrt::com_ptr<ID3D11Device> m_d3d;
    winrt::com_ptr<ID2D1Device> m_d2d;
    // La fábrica de DirectWrite sobrevive a la pérdida del dispositivo: no depende de la
    // GPU, y rehacerla tiraría la caché de formatos por nada.
    winrt::com_ptr<IDWriteFactory6> m_write;
};

}  // namespace Gfx
