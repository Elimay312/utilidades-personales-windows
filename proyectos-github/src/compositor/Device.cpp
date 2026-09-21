#include "compositor/Device.h"

#include <windows.ui.composition.interop.h>

namespace Gfx {

namespace {
namespace abi = ABI::Windows::UI::Composition;
}  // namespace

bool Device::CreateRenderingDevice() {
    m_d3d = nullptr;
    m_d2d = nullptr;

    // HARDWARE y no WARP: la isla y el HUD eligieron WARP porque suben cinco glifos una
    // vez y no vuelven a dibujar. Brújula repinta texto al redimensionar, al cambiar de
    // DPI y al cambiar de tema, así que aquí sí compensa la GPU.
    //
    // BGRA_SUPPORT no es opcional: sin él, D2D1CreateDevice falla.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                   D3D11_SDK_VERSION, m_d3d.put(), nullptr, nullptr);

    // Sin GPU utilizable —escritorio remoto, máquina virtual sin aceleración— WARP lo
    // hace todo por software. Más lento, pero la ventana se abre.
    if (hr == DXGI_ERROR_UNSUPPORTED || hr == E_FAIL) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                               D3D11_SDK_VERSION, m_d3d.put(), nullptr, nullptr);
    }
    if (FAILED(hr)) return false;

    const auto dxgi = m_d3d.try_as<IDXGIDevice>();
    if (!dxgi) return false;

    return SUCCEEDED(D2D1CreateDevice(dxgi.get(), nullptr, m_d2d.put()));
}

bool Device::Create(const winrt::Windows::UI::Composition::Compositor& compositor) {
    m_compositor = compositor;

    if (!m_write) {
        // La 6 y no la 1: IDWriteFactory6::CreateTextFormat es la que acepta ejes de
        // fuente variable, que es como se sacan el "Display" y el "Text" de Segoe UI
        // Variable de una sola familia.
        winrt::com_ptr<IUnknown> unknown;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory6),
                                       reinterpret_cast<IUnknown**>(unknown.put())))) {
            return false;
        }
        m_write = unknown.try_as<IDWriteFactory6>();
        if (!m_write) return false;
    }

    return Recreate();
}

bool Device::Recreate() {
    m_graphics = nullptr;
    if (!m_compositor) return false;
    if (!CreateRenderingDevice()) return false;

    const auto interop = m_compositor.as<abi::ICompositorInterop>();
    return SUCCEEDED(interop->CreateGraphicsDevice(
        m_d2d.get(),
        reinterpret_cast<abi::ICompositionGraphicsDevice**>(winrt::put_abi(m_graphics))));
}

}  // namespace Gfx
