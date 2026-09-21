#include "compositor/Scene.h"

#include <DispatcherQueue.h>
#include <windows.ui.composition.interop.h>

namespace wuc = winrt::Windows::UI::Composition;

namespace Gfx {

namespace {
namespace abi = ABI::Windows::UI::Composition::Desktop;
}  // namespace

bool Scene::Create(HWND hwnd) {
    // Windows.UI.Composition exige una cola de despacho en el hilo antes de poder crear
    // el Compositor. Con DQTYPE_THREAD_CURRENT el apartamento tiene que ser
    // DQTAT_COM_NONE: la propia cabecera dice que apartmentType solo cuenta cuando el
    // hilo es dedicado, y pedir ASTA sobre un hilo que ya es STA falla.
    DispatcherQueueOptions options{};
    options.dwSize = sizeof(DispatcherQueueOptions);
    options.threadType = DQTYPE_THREAD_CURRENT;
    options.apartmentType = DQTAT_COM_NONE;

    if (FAILED(CreateDispatcherQueueController(
            options, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(
                         winrt::put_abi(m_queue))))) {
        return false;
    }

    m_compositor = wuc::Compositor();

    const auto interop = m_compositor.as<abi::ICompositorDesktopInterop>();
    // isTopmost = false: la ventana es normal y su orden lo decide Windows.
    if (FAILED(interop->CreateDesktopWindowTarget(
            hwnd, false,
            reinterpret_cast<abi::IDesktopWindowTarget**>(winrt::put_abi(m_target))))) {
        return false;
    }

    m_root = m_compositor.CreateContainerVisual();
    m_root.RelativeSizeAdjustment({1.0f, 1.0f});
    m_target.Root(m_root);

    m_content = m_compositor.CreateContainerVisual();
    m_root.Children().InsertAtTop(m_content);
    return true;
}

void Scene::Layout(float widthDip, float heightDip, float scale) {
    if (!m_content) return;
    // El tamaño en DIP y la escala aparte. Los hijos que se midan en relativo al
    // contenido siguen viendo unidades lógicas, que es lo que queremos.
    m_content.Size({widthDip, heightDip});
    m_content.Scale({scale, scale, 1.0f});
}

void Scene::Close() {
    m_content = nullptr;
    m_root = nullptr;
    if (m_target) {
        m_target.Close();
        m_target = nullptr;
    }
    m_compositor = nullptr;
    if (m_queue) {
        m_queue = nullptr;
    }
}

}  // namespace Gfx
