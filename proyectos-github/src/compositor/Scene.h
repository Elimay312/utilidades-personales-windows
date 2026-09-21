#pragma once

// El árbol de composición colgado de la ventana: cola de despacho, compositor, destino
// y las dos raíces.
//
// Raíz: tamaño relativo al de la ventana, así que redimensionar no obliga a escribir
// nada. Contenido: hijo de la raíz con la escala del monitor puesta, de modo que TODO el
// layout de la aplicación se escribe en DIP y un cambio de DPI es una escritura de
// propiedad en vez de rehacer el árbol. Los hermanos lo rehacen entero porque tienen los
// píxeles horneados en los visuals; aquí no hace falta.

#include <Windows.h>

#include "compositor/Winrt.h"

namespace Gfx {

class Scene {
public:
    bool Create(HWND hwnd);
    void Close();

    const winrt::Windows::UI::Composition::Compositor& Compositor() const { return m_compositor; }
    const winrt::Windows::UI::Composition::ContainerVisual& Content() const { return m_content; }
    bool Ready() const { return m_target != nullptr; }

    // widthDip/heightDip es el cliente ya en unidades lógicas.
    void Layout(float widthDip, float heightDip, float scale);

private:
    winrt::Windows::System::DispatcherQueueController m_queue{nullptr};
    winrt::Windows::UI::Composition::Compositor m_compositor{nullptr};
    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget m_target{nullptr};
    winrt::Windows::UI::Composition::ContainerVisual m_root{nullptr};
    winrt::Windows::UI::Composition::ContainerVisual m_content{nullptr};
};

}  // namespace Gfx
