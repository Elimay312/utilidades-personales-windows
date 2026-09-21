#pragma once

// El dispositivo que comparten Direct2D y Composition.
//
// Tiene que ser UNO: DWM muestrea nuestras superficies, así que si Direct2D dibujara en
// un adaptador y el compositor leyera de otro habría una copia por frame entre los dos.
// De aquí salen todas las superficies de la aplicación.

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite_3.h>
#include "compositor/Winrt.h"

namespace Gfx {

class Device {
public:
    // Devuelve false si no hay manera de crear el dispositivo. No lanza: los errores son
    // valores y quien llama decide si eso es fatal.
    bool Create(const winrt::Windows::UI::Composition::Compositor& compositor);

    // El dispositivo se pierde al actualizar el controlador de la GPU o al hibernar.
    // Composition avisa con RenderingDeviceReplaced; esto lo rehace todo desde cero.
    bool Recreate();

    const winrt::Windows::UI::Composition::CompositionGraphicsDevice& Graphics() const {
        return m_graphics;
    }
    IDWriteFactory6* Write() const { return m_write.get(); }
    bool Ready() const { return m_graphics != nullptr; }

private:
    bool CreateRenderingDevice();

    winrt::Windows::UI::Composition::Compositor m_compositor{nullptr};
    winrt::Windows::UI::Composition::CompositionGraphicsDevice m_graphics{nullptr};
    winrt::com_ptr<ID3D11Device> m_d3d;
    winrt::com_ptr<ID2D1Device> m_d2d;
    // La fábrica de DirectWrite sobrevive a la pérdida del dispositivo: no depende de la
    // GPU, y rehacerla tiraría la caché de formatos por nada.
    winrt::com_ptr<IDWriteFactory6> m_write;
};

}  // namespace Gfx
