#pragma once

// El único sitio donde nacen animaciones (regla 3 de arquitectura). Traduce los muelles
// de MotionSpec.h a animaciones de Composition, y decide qué hacer cuando Windows tiene
// desactivado "Mostrar animaciones".
//
// Los muelles son SpringXNaturalMotionAnimation a propósito: retoman el valor Y LA
// VELOCIDAD actuales del destino, así que interrumpir una animación es volver a llamar
// con otro valor final. Sin Stop, sin reinicio, sin salto. Con una animación de
// fotogramas clave, interrumpir a mitad la haría reempezar desde su valor inicial, que es
// justo el salto que el criterio de aceptación de esta fase prohíbe.
//
// Nada de esto corre en nuestro hilo: lo ejecuta el proceso de DWM. Las animaciones
// siguen yendo finas aunque el hilo de UI esté ocupado.

#include "compositor/Winrt.h"

#include "compositor/MotionSpec.h"

namespace Motion {

// Los nombres largos de C++/WinRT una sola vez. Las animaciones se escriben en muchos
// sitios y `winrt::Windows::Foundation::Numerics::float3` en cada llamada tapa lo que la
// llamada hace.
using Vec2 = winrt::Windows::Foundation::Numerics::float2;
using Vec3 = winrt::Windows::Foundation::Numerics::float3;

class Animator {
public:
    void Attach(const winrt::Windows::UI::Composition::Compositor& compositor);

    // Relee SPI_GETCLIENTAREAANIMATION. Al arrancar y en cada WM_SETTINGCHANGE.
    void RefreshSystemPreference();
    bool Enabled() const { return m_systemAnimations; }

    // Cuánto dura el fundido que acompaña a cada muelle.
    float FadeMs(Kind kind) const;

    void Offset(const winrt::Windows::UI::Composition::Visual& visual,
                const winrt::Windows::Foundation::Numerics::float3& value, Kind kind) const;

    void Scale(const winrt::Windows::UI::Composition::Visual& visual,
               const winrt::Windows::Foundation::Numerics::float3& value, Kind kind) const;

    void Opacity(const winrt::Windows::UI::Composition::Visual& visual, float value,
                 float durationMs) const;

    // El color de un material. Cambiar de tema es esto en casi todas partes: una brocha
    // de color que se va al color nuevo, fundido de verdad y en la GPU, sin repintar.
    void Color(const winrt::Windows::UI::Composition::CompositionColorBrush& brush,
               const winrt::Windows::UI::Color& value, float durationMs) const;

    // El tamaño del visual y el de su geometría, con la MISMA animación. Tienen que ir
    // exactamente juntos: si la geometría se queda atrás, el material queda recortado a
    // la forma vieja y se ve como un mordisco en el borde mientras dura la transición.
    void SizeTogether(const winrt::Windows::UI::Composition::Visual& visual,
                      const winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry&
                          geometry,
                      const winrt::Windows::Foundation::Numerics::float2& value, Kind kind) const;

    void CornerRadius(
        const winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry& geometry,
        const winrt::Windows::Foundation::Numerics::float2& value, Kind kind) const;

    // Fija el centro de escalado al centro del visual y lo deja atado: una expresión es
    // dueña de la propiedad entera, y aquí eso es lo que queremos, porque nadie más
    // escribe CenterPoint y así sigue siendo correcto aunque el tamaño esté animándose.
    // Tamaño de un visual suelto, sin geometría que llevar a la par.
    void Size(const winrt::Windows::UI::Composition::Visual& visual,
              const winrt::Windows::Foundation::Numerics::float2& value, Kind kind) const;

    // Tamaño de una geometría sola. Lo usa Gfx::Material cuando su visual se dimensiona
    // solo con el padre y lo único que hay que animar es la forma.
    void GeometrySize(
        const winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry& geometry,
        const winrt::Windows::Foundation::Numerics::float2& value, Kind kind) const;

    // Con retardo, para escalonar. El muelle admite DelayTime sin dejar de ser
    // interrumpible: lo que no admite son fotogramas clave.
    void OffsetDelayed(const winrt::Windows::UI::Composition::Visual& visual,
                       const winrt::Windows::Foundation::Numerics::float3& value, Kind kind,
                       float delayMs) const;

    void OpacityDelayed(const winrt::Windows::UI::Composition::Visual& visual, float value,
                        float durationMs, float delayMs) const;

    // El cursor del campo de texto, parpadeando en bucle en la GPU. Sin temporizador en el
    // hilo de UI: el bucle de mensajes se queda dormido en GetMessageW y así sigue.
    void Blink(const winrt::Windows::UI::Composition::Visual& caret, float periodMs) const;
    // Lo para y lo deja encendido. Se llama en cada pulsación: un cursor que parpadea
    // mientras se escribe se lee como un fallo de dibujo.
    void Solid(const winrt::Windows::UI::Composition::Visual& caret) const;

    // Ata el desplazamiento del contenido al InteractionTracker. La posición del tracker
    // crece hacia abajo y el contenido se mueve hacia arriba, de ahí el signo.
    void BindScroll(
        const winrt::Windows::UI::Composition::Visual& content,
        const winrt::Windows::UI::Composition::Interactions::InteractionTracker& tracker) const;

    void ShadowColor(const winrt::Windows::UI::Composition::DropShadow& shadow,
                     const winrt::Windows::UI::Color& value, float durationMs) const;

    void BindCenterPoint(const winrt::Windows::UI::Composition::Visual& visual) const;

    const winrt::Windows::UI::Composition::Compositor& Compositor() const { return m_compositor; }

private:
    winrt::Windows::UI::Composition::CompositionEasingFunction Ease() const;

    winrt::Windows::UI::Composition::Compositor m_compositor{nullptr};
    bool m_systemAnimations = true;
};

}  // namespace Motion
