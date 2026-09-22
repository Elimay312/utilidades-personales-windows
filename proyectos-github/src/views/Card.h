#pragma once

// La tarjeta de un repositorio, en sus dos disposiciones.
//
// Es una FUNCIÓN y no un elemento, y eso no es una simplificación: las filas de Ui::List
// son superficies recicladas, no árboles. Una tarjeta hecha de seis etiquetas y una píldora
// serían siete visuales por fila y, con veinticinco vivas, ciento setenta y cinco visuales
// que crear y destruir mientras se desplaza. Aquí se dibuja dentro de la superficie que la
// lista ya tiene.
//
// Las dos disposiciones son la misma tarjeta con los mismos datos en otro sitio, y están en
// el mismo archivo a propósito: separarlas es cómo una de las dos deja de enseñar el
// siguiente paso y nadie se entera hasta que abre la otra.

#include "app/State.h"
#include "ui/Element.h"

namespace Views {

enum class CardLayout { List, Grid };

// Los altos de cada disposición, y el ancho mínimo de una celda de la cuadrícula. Los usa
// quien reparte las celdas, que es Views::RepoList.
inline constexpr float kListCardHeight = 60.0f;
inline constexpr float kGridCardHeight = 118.0f;
inline constexpr float kGridCardMinWidth = 240.0f;
inline constexpr float kCardGap = Metrics::kSpace1;

void PaintCard(const Ui::Paint& paint, const Ui::Rect& box, const App::Entry& entry,
               CardLayout layout, bool hovered, bool selected);

// La tarjeta LEVANTADA: la copia que viaja con el puntero mientras se arrastra, y la que
// sale disparada hacia su grupo cuando se cambia la prioridad con el teclado.
//
// Es un elemento y no una celda de la lista porque tiene que salirse de la lista: la
// ventanilla se recorta a sí misma (ui/List.cpp pone un InsetClip), así que una celda
// arrastrada hacia la barra lateral desaparecería justo al cruzar el borde. Cuelga de
// Views::Main, que no recorta nada, y por eso su sombra tampoco sale cortada.
//
// Va DENTRO del árbol y no como capa flotante del Host a propósito: las capas se cierran
// todas juntas cuando la ventana pierde el foco, y una tarjeta levantada que desaparece
// mientras alguien la sujeta dejaría a Views::Main apuntando a memoria liberada.
//
// Una sola, creada al arrancar y escondida. No hay dos tarjetas en el aire a la vez.
class DragCard : public Ui::Element {
public:
    // Aparece encima de 'from' —que es donde está la tarjeta de verdad— y crece a 1,03.
    void Lift(const App::Entry& entry, CardLayout layout, const Ui::Rect& from);
    // La esquina superior izquierda, en coordenadas de ventana. Sin animar: lo que manda
    // aquí es el puntero, y un muelle detrás del ratón se siente como retraso.
    void MoveTo(float xDip, float yDip);
    // Cae en su sitio con el muelle suave y se apaga al llegar.
    void FlyTo(const Ui::Rect& target);
    // El "no" del límite de Enfoque: se para, tiembla y se apaga.
    void Refuse();
    void HideNow();

    // No recibe entrada. Está encima de todo y, sin esto, el puntero no llegaría nunca a la
    // lista de debajo — que es exactamente a quien hay que seguir escuchando mientras dura
    // el arrastre.
    bool HitTest(float, float) const override { return false; }

protected:
    bool OnAttach() override;
    void OnPaint(const Ui::Paint& paint, const Ui::Rect& box) override;
    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override;

private:
    // Copiada, no prestada: App::State se reconstruye entero después de cada
    // sincronización, y una sincronización puede terminar mientras alguien arrastra.
    App::Entry m_entry;
    CardLayout m_layout = CardLayout::List;
};

}  // namespace Views
