#include "compositor/Surface.h"

#include <windows.ui.composition.interop.h>

#include <algorithm>
#include <cmath>

#include "compositor/Device.h"
#include "shell/Dpi.h"

namespace Gfx {

namespace {
namespace abi = ABI::Windows::UI::Composition;

// Ni cero ni negativo: Composition rechaza una superficie de lado 0, y una ventana
// arrastrada hasta el mínimo puede pedirlo.
int AtLeastOne(int value) { return std::max(value, 1); }
}  // namespace

bool Surface::Resize(Device& device, float widthDip, float heightDip, float scale) {
    if (!device.Ready()) return false;

    const int widthPx = AtLeastOne(Dpi::SurfaceSide(widthDip, scale));
    const int heightPx = AtLeastOne(Dpi::SurfaceSide(heightDip, scale));

    // Nada que hacer: la textura que ya hay mide lo mismo. Salir aquí no es una
    // optimización, es lo que conserva lo pintado. Recolocar el árbol vuelve a escribir los
    // mismos marcos una y otra vez —cada WM_SIZE, cada Relayout— y pedirle a Composition
    // que redimensione a lo mismo devuelve un hueco del atlas VACÍO: la pantalla entera se
    // quedaría en blanco hasta que algo cambiara de contenido.
    if (m_surface && widthPx == m_widthPx && heightPx == m_heightPx && scale == m_scale) {
        m_widthDip = widthDip;
        m_heightDip = heightDip;
        return true;
    }

    m_widthDip = widthDip;
    m_heightDip = heightDip;
    m_scale = scale;
    m_widthPx = widthPx;
    m_heightPx = heightPx;

    if (m_surface) {
        // Resize sobre la que ya hay, en vez de crear otra: es la diferencia entre
        // redimensionar la ventana y ver subir el consumo de memoria mientras lo haces.
        const auto interop = m_surface.as<abi::ICompositionDrawingSurfaceInterop>();
        const SIZE size{widthPx, heightPx};
        if (SUCCEEDED(interop->Resize(size))) return true;
        Close();
    }

    m_surface = device.Graphics().CreateDrawingSurface2(
        winrt::Windows::Graphics::SizeInt32{widthPx, heightPx},
        winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
    return m_surface != nullptr;
}

bool Surface::Draw(const Painter& painter) {
    if (!m_surface) return false;

    const auto interop = m_surface.as<abi::ICompositionDrawingSurfaceInterop>();

    winrt::com_ptr<ID2D1DeviceContext> dc;
    POINT offset{};
    const HRESULT hr =
        interop->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), dc.put_void(), &offset);
    if (FAILED(hr)) {
        // Dispositivo perdido: quien llama rehará el Device y volverá a pintar.
        return false;
    }

    // El orden importa. Primero el DPI, porque cambia qué es un "1" para todo lo demás.
    const float dpi = static_cast<float>(Dpi::kDefault) * m_scale;
    dc->SetDpi(dpi, dpi);

    // ClearType no existe sobre una superficie con alfa premultiplicado, así que el texto
    // va con antialias en gris. No es una elección: es lo que hay cuando el fondo es la
    // Mica y no un color opaco nuestro.
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    // El hueco del atlas puede venir con los píxeles del inquilino anterior.
    dc->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    // Y ahora el desplazamiento, en DIP porque el contexto ya está en DIP.
    dc->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x) / m_scale,
                                                   static_cast<float>(offset.y) / m_scale));

    painter(Canvas{dc.get(), m_widthDip, m_heightDip});

    return SUCCEEDED(interop->EndDraw());
}

void Surface::Close() {
    if (m_surface) {
        m_surface.Close();
        m_surface = nullptr;
    }
    // Y se olvida el tamaño reservado, o la siguiente llamada a Resize creería que la
    // textura que acaba de soltarse sigue valiendo y no pediría ninguna.
    m_widthPx = 0;
    m_heightPx = 0;
}

}  // namespace Gfx
