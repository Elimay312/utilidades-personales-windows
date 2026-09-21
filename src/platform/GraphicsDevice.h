#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

// Dispositivo D3D11 + swap chain FLIP_DISCARD para una sola ventana.
class GraphicsDevice {
public:
    bool Create(HWND hwnd);
    void Destroy();

    void Resize(UINT width, UINT height);
    void Clear(const float rgba[4]);
    void Present();

    ID3D11Device* Device() const { return m_device.Get(); }
    ID3D11DeviceContext* Context() const { return m_context.Get(); }

private:
    bool CreateRenderTarget();

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
};
