#include "ui/visuals.h"

#include <DispatcherQueue.h>
#include <windows.ui.composition.interop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>

#include <chrono>

#include "core/log.h"

namespace wuc = winrt::Windows::UI::Composition;

namespace dock {
namespace {

// Windows.UI.Composition exige una DispatcherQueue en el hilo antes del Compositor, y el
// controller hay que conservarlo vivo o el compositor se queda sin cola. Uno por proceso,
// creado en el hilo de UI la primera vez que un dock lo pide.
wuc::Compositor SharedCompositor() {
  static winrt::Windows::System::DispatcherQueueController controller{nullptr};
  static wuc::Compositor compositor{nullptr};
  if (!compositor) {
    // Con DQTYPE_THREAD_CURRENT la documentación exige DQTAT_COM_NONE.
    DispatcherQueueOptions options{sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE};
    CreateDispatcherQueueController(
        options, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller)));
    compositor = wuc::Compositor();
  }
  return compositor;
}

wuc::CompositionBrush AcrylicBrush(const wuc::Compositor& compositor) {
  // El material NO se le pide a DWM (DWMWA_SYSTEMBACKDROP_TYPE): la ventana ocupa todo el
  // ancho del monitor y un backdrop de DWM pintaría el rectángulo entero. HostBackdrop
  // muestrea el escritorio ya desenfocado y se aplica solo donde se quiere.
  try {
    return compositor.CreateHostBackdropBrush();
  } catch (const winrt::hresult_error& e) {
    LogError(L"[acrilico] no disponible ({:#010x}); color sólido", static_cast<unsigned>(e.code().value));
    return compositor.CreateColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(200, 32, 32, 40));
  }
}

}  // namespace

Visuals::Visuals(HWND hwnd) : compositor_(SharedCompositor()) {
  compositor_.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>()->CreateDesktopWindowTarget(
      hwnd, TRUE, reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(winrt::put_abi(target_)));
  root_ = compositor_.CreateContainerVisual();
  root_.RelativeSizeAdjustment({1, 1});
  target_.Root(root_);

  // La barra: acrílico debajo y un tinte blanco translúcido encima. Sin el tinte, sobre un
  // fondo oscuro el desenfoque queda casi negro y los iconos no se leen.
  bar_ = compositor_.CreateContainerVisual();
  corners_ = compositor_.CreateRoundedRectangleGeometry();
  // Esquinas recortadas en el compositor, con antialias. SetWindowRgn recortaría sin
  // suavizar.
  bar_.Clip(compositor_.CreateGeometricClip(corners_));

  auto material = compositor_.CreateSpriteVisual();
  material.RelativeSizeAdjustment({1, 1});
  material.Brush(AcrylicBrush(compositor_));
  bar_.Children().InsertAtBottom(material);

  auto tint = compositor_.CreateSpriteVisual();
  tint.RelativeSizeAdjustment({1, 1});
  tint.Brush(compositor_.CreateColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(48, 255, 255, 255)));
  bar_.Children().InsertAtTop(tint);

  root_.Children().InsertAtBottom(bar_);
}

void Visuals::LayoutBar(float windowWidth, float windowHeight, float barWidth, float barHeight) {
  barHeight_ = barHeight;
  bar_.Size({barWidth, barHeight});
  bar_.Offset({(windowWidth - barWidth) / 2, windowHeight - barHeight, 0});
  corners_.Size({barWidth, barHeight});
  corners_.CornerRadius({barHeight * 0.28f, barHeight * 0.28f});
}

void Visuals::Slide(bool hidden, bool instant) {
  const float target = hidden ? barHeight_ : 0.0f;
  if (instant) {
    root_.StopAnimation(L"Offset.Y");
    root_.Offset({0, target, 0});
    return;
  }
  // Amortiguado crítico (1,0) y periodo de 70 ms: llega al 99,9% en ~100 ms, sin rebote.
  auto spring = compositor_.CreateSpringScalarAnimation();
  spring.DampingRatio(1.0f);
  spring.Period(std::chrono::milliseconds(70));
  spring.FinalValue(target);
  root_.StartAnimation(L"Offset.Y", spring);
}

}  // namespace dock
