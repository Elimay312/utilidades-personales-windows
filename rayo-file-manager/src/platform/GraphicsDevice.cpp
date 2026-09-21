#include "platform/GraphicsDevice.h"

#ifdef _DEBUG
// Ojo con ID3D11InfoQueue::GetMessage: la macro GetMessage de Windows.h ya estaba activa
// cuando se declaro, asi que el metodo se llama de verdad GetMessageW. Llamarlo por la
// macro (GetMessage) es lo unico que cuadra con la declaracion.
#include <d3d11sdklayers.h>

#include <string>
#include <vector>

#include "core/Diag.h"
#endif

using Microsoft::WRL::ComPtr;

namespace {

#ifdef _DEBUG
// ReportLiveDeviceObjects escribe por OutputDebugString, que sin depurador enganchado no
// lo ve nadie. La cola de mensajes de la capa de depuracion si se puede leer, y de ahi al
// log de la app.
void ReportLive(ID3D11Debug* debug, ID3D11InfoQueue* queue) {
    if (!debug || !queue) {
        // Capa de depuracion no instalada. Se activa (como administrador) con:
        // dism /online /add-capability /capabilityname:Tools.Graphics.DirectX~~~~0.0.1.0
        Diag::Log("d3d: sin capa de depuracion (falta la caracteristica Graphics Tools)");
        return;
    }
    debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);

    // El unico objeto vivo que se espera es el propio dispositivo, que sostienen estas dos
    // interfaces de depuracion. Cualquier otro es una fuga nuestra.
    const UINT64 count = queue->GetNumStoredMessages();
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T length = 0;
        if (FAILED(queue->GetMessage(i, nullptr, &length)) || length == 0) continue;
        // vector y no string: el buffer se lee como un D3D11_MESSAGE y hace falta que
        // este alineado como cualquier cosa que salga de new.
        std::vector<unsigned char> storage(length);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
        if (FAILED(queue->GetMessage(i, message, &length)) || !message->pDescription) continue;
        Diag::Log("d3d: " + std::string(message->pDescription));
    }
}
#endif

}  // namespace

bool GraphicsDevice::Create(HWND hwnd) {
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, &m_device, nullptr,
                                   &m_context);
#ifdef _DEBUG
    // La capa de depuracion es una caracteristica opcional de Windows (Graphics Tools):
    // si no esta instalada, sin ella.
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                               ARRAYSIZE(levels), D3D11_SDK_VERSION, &m_device, nullptr,
                               &m_context);
    }
#endif
    if (hr == DXGI_ERROR_UNSUPPORTED)  // maquina sin GPU util: WARP
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                               ARRAYSIZE(levels), D3D11_SDK_VERSION, &m_device, nullptr,
                               &m_context);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(m_device.As(&dxgiDevice))) return false;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_NONE;  // sin estirado: repintamos dentro del WM_SIZE
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    if (FAILED(factory->CreateSwapChainForHwnd(m_device.Get(), hwnd, &sd, nullptr, nullptr,
                                               &m_swapChain)))
        return false;

    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    return CreateRenderTarget();
}

void GraphicsDevice::Destroy() {
#ifdef _DEBUG
    ComPtr<ID3D11Debug> debug;
    ComPtr<ID3D11InfoQueue> queue;
    if (m_device) {
        m_device.As(&debug);
        m_device.As(&queue);
    }
#endif

    m_rtv.Reset();
    m_swapChain.Reset();
    // El contexto retiene lo ultimo que se le ato (el render target, las texturas de la
    // vista previa): sin esto saldrian como objetos vivos en el informe.
    if (m_context) {
        m_context->ClearState();
        m_context->Flush();
    }
    m_context.Reset();

#ifdef _DEBUG
    // La cuenta que deja el ultimo Release del dispositivo no necesita ninguna
    // caracteristica opcional de Windows: cero es que no queda nadie agarrado a el. Las
    // dos interfaces de depuracion, si existen, cuentan.
    if (ID3D11Device* device = m_device.Detach()) {
        const ULONG left = device->Release();
        Diag::Log("d3d: refcount del dispositivo al cerrar " + std::to_string(left) +
                  (debug || queue ? " (2 = las interfaces de depuracion)" : " (0 = limpio)"));
    }
    ReportLive(debug.Get(), queue.Get());
#else
    m_device.Reset();
#endif
}

bool GraphicsDevice::CreateRenderTarget() {
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return false;
    return SUCCEEDED(m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtv));
}

void GraphicsDevice::Resize(UINT width, UINT height) {
    if (!m_swapChain || width == 0 || height == 0) return;

    // FLIP_DISCARD exige soltar toda referencia a los back buffers antes de redimensionar.
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_rtv.Reset();
    if (SUCCEEDED(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)))
        CreateRenderTarget();
}

void GraphicsDevice::Clear(const float rgba[4]) {
    if (!m_rtv) return;
    ID3D11RenderTargetView* rtv = m_rtv.Get();
    m_context->OMSetRenderTargets(1, &rtv, nullptr);
    m_context->ClearRenderTargetView(rtv, rgba);
}

void GraphicsDevice::Present() {
    if (m_swapChain) m_swapChain->Present(1, 0);
}
