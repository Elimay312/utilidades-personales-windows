#pragma once

// La pantalla de revisión del kit. Se abre con F12 y solo existe en Debug: es una
// herramienta para juzgar materiales y movimiento, no una función del producto, así que
// en Release ni se compila ni deja su texto de ejemplo dentro del binario.
//
// Dos columnas. Cada componente se instancia dos veces: la izquierda con los tokens de
// claro y la derecha con los de oscuro, sea cual sea el tema de Windows. Sale gratis
// porque Element::ApplyTheme recorre un SUBÁRBOL y Ui::Paint lleva los tokens por
// puntero: una columna solo tiene que sobrescribir Substitute. Con una captura se
// demuestra el criterio de aceptación de la fase.
//
// El menú, el aviso y la hoja se enseñan de dos maneras: una muestra quieta dentro de
// cada columna —que es lo que los pone en claro y en oscuro a la vez, y de paso prueba
// la sombra en los dos fondos— y los de verdad, que se abren con los botones y viven en
// la capa flotante del Host con el tema del sistema.

#include "shell/Caption.h"
#include "ui/Element.h"
#include "views/Chrome.h"

namespace Views {

class Catalog : public Ui::Element {
public:
    Catalog();

    // Los botones de la ventana también aquí. Antes los dibujaba Views::Demo, que vivía
    // fuera del kit y se veía pasara lo que pasara; ahora son de Views::Chrome, y una raíz
    // que no lo incluya deja la ventana sin minimizar, maximizar ni cerrar a la vista
    // —siguen funcionando, porque el hit-test del marco no depende de lo que se dibuje, y
    // por eso el fallo no se nota hasta que se busca el aspa y no está—.
    void SetCaption(const Caption::Layout& layout, Caption::Zone hovered, Caption::Zone pressed);

protected:
    bool OnAttach() override;
    void OnArrange() override;
    void OnPaint(const Ui::Paint& paint, const Ui::Rect& box) override;

private:
    class Column;

    void RefreshStats();

    Chrome* m_chrome = nullptr;
    Column* m_light = nullptr;
    Column* m_dark = nullptr;
    // Lo que costó el último reciclado de la lista de 500. Es la medida del criterio de
    // los 60 fps, y se enseña en la barra de arriba.
    float m_recycleMs = 0.0f;
    int m_liveRows = 0;
};

}  // namespace Views
