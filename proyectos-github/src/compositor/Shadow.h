#pragma once

// La sombra suave de los elementos flotantes: menú, aviso y hoja modal. Y solo esos tres.
//
// La fase 1 la dejó sin resolver, con dos caminos probados que no funcionaron y uno sin
// probar. El que se ha elegido no es ninguno de los tres: es LayerVisual::Shadow con
// SourcePolicy en InheritFromVisualContent, que aplana el subárbol y saca la sombra de su
// alfa. Tiene dos ventajas sobre enmascarar a mano:
//
//   - La regla de la fase 1 sigue intacta. El material sigue siendo un ShapeVisual con su
//     geometría y su brocha, y el contenido sigue siendo un Gfx::Layer. No hace falta una
//     superficie con el rectángulo dibujado ni un nine-grid con sus márgenes, que además
//     se miden en píxeles de la superficie y habría que reescalar a cada DPI.
//   - Las esquinas suavizadas que ya da el rasterizador de formas son también el borde de
//     la sombra, gratis.
//
// A cambio, dos cosas que hay que saber:
//
//   - Un LayerVisual aplana su subárbol en una superficie fuera de pantalla cada vez que
//     cambia. Para un menú no se nota; para una lista de 500 filas sería el final del
//     criterio de los 60 fps. POR ESO ESTO ES SOLO PARA LO QUE FLOTA.
//   - Las sombras no las recorta el clip implícito del tamaño, pero SÍ las recorta un
//     Visual.Clip explícito. Gfx::Morph pone uno en su raíz, así que nada que lleve
//     sombra puede colgar de un Morph. Es la razón de que el Host tenga una capa de
//     overlay aparte y no anidada.

#include "compositor/Winrt.h"
#include "shell/Theme.h"
#include "ui/Metrics.h"

namespace Motion {
class Animator;
}

namespace Gfx {

class Shadow {
public:
    // Create decide si hay sombra preguntando por las interfaces —ILayerVisual2 y
    // IDropShadow2—: en un Windows anterior a 1709 no están, y entonces devuelve un
    // contenedor sin sombra. Quien llama no tiene que preguntar nada; si le importa,
    // HasShadow() se lo dice después.
    bool Create(const winrt::Windows::UI::Composition::Compositor& compositor,
                const winrt::Windows::UI::Composition::ContainerVisual& parent,
                Metrics::Elevation elevation);

    // Aquí dentro van el material y el contenido del elemento.
    const winrt::Windows::UI::Composition::ContainerVisual& Content() const { return m_content; }

    void SetSize(float widthDip, float heightDip);
    void SetColor(Theme::Color color, const Motion::Animator& animator, float durationMs);
    bool HasShadow() const { return m_shadow != nullptr; }

    void Close();

private:
    // Content() devuelve esto cuando no hay sombra que poner, y así quien llama no tiene
    // que preguntar: mete sus visuales donde le digan y lo demás le da igual.
    winrt::Windows::UI::Composition::ContainerVisual m_content{nullptr};
    winrt::Windows::UI::Composition::LayerVisual m_layer{nullptr};
    winrt::Windows::UI::Composition::DropShadow m_shadow{nullptr};
};

}  // namespace Gfx
