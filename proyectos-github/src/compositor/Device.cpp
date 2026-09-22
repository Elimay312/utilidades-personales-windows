#include "compositor/Device.h"

#include <windows.ui.composition.interop.h>

namespace Gfx {

namespace {
namespace abi = ABI::Windows::UI::Composition;

double NowMs() {
    LARGE_INTEGER freq{}, now{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    if (freq.QuadPart == 0) return 0.0;
    return static_cast<double>(now.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
}
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

void Device::BeginCreate() {
    if (m_pending.valid()) return;
    // Sin apartamento COM propio a propósito: ni DXGI, ni D2D ni DirectWrite se activan
    // por el registro de COM, así que no hace falta inicializarlo aquí — y hacerlo metería
    // este hilo en el MTA sin que nadie se lo haya pedido.
    m_pending = std::async(std::launch::async, [this] {
        return CreateWriteFactory() && CreateRenderingDevice();
    });
}

bool Device::CreateWriteFactory() {
    if (m_write) return true;
    // La 6 y no la 1: IDWriteFactory6::CreateTextFormat es la que acepta ejes de fuente
    // variable, que es como se sacan el "Display" y el "Text" de Segoe UI Variable de una
    // sola familia.
    winrt::com_ptr<IUnknown> unknown;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory6),
                                   reinterpret_cast<IUnknown**>(unknown.put())))) {
        return false;
    }
    m_write = unknown.try_as<IDWriteFactory6>();
    return m_write != nullptr;
}

bool Device::FinishCreate(const winrt::Windows::UI::Composition::Compositor& compositor) {
    m_compositor = compositor;
    // Con hilo, el get() es la sincronización: a partir de ahí los punteros que dejó
    // escritos se leen desde este sin candado. Sin hilo —nadie llamó a BeginCreate— se hace
    // aquí mismo lo que el hilo habría hecho, y así sigue habiendo UNA sola función que
    // crea: dos caminos son dos sitios donde olvidar el enganche al compositor.
    const double antes = NowMs();
    const bool listo = m_pending.valid() ? m_pending.get()
                                         : (CreateWriteFactory() && CreateRenderingDevice());
    m_waitedMs = NowMs() - antes;
    return listo && AttachGraphicsDevice();
}

bool Device::Recreate() {
    if (!m_compositor) return false;
    if (!CreateRenderingDevice()) return false;
    return AttachGraphicsDevice();
}

bool Device::AttachGraphicsDevice() {
    m_graphics = nullptr;
    if (!m_compositor || !m_d2d) return false;
    const auto interop = m_compositor.as<abi::ICompositorInterop>();
    return SUCCEEDED(interop->CreateGraphicsDevice(
        m_d2d.get(),
        reinterpret_cast<abi::ICompositionGraphicsDevice**>(winrt::put_abi(m_graphics))));
}

}  // namespace Gfx
