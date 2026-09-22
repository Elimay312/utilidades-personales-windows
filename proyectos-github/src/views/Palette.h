#pragma once

// La paleta de comandos (Ctrl+K): escribir para encontrar un repositorio o ejecutar una
// acción, sin tocar el ratón.
//
// **No sabe hacer nada de lo que ofrece.** Recibe las acciones ya montadas por App —que es
// quien sabe sincronizar, abrir un navegador y cambiar una prioridad— y se limita a
// filtrarlas y a llamar a la que se elija. Es la regla 2 de arquitectura, la misma que
// cumplen Views::RepoList y Views::Inspector.
//
// Y el filtro es EL MISMO que el de la búsqueda de la lista: App::Terms sobre un texto
// plegado, y todas las palabras tienen que aparecer. Dos maneras de buscar dentro de la
// misma aplicación serían dos ideas distintas de qué significa encontrar algo, y la
// segunda se descubre el día que una tilde deja de dar resultados en un sitio y sí en el
// otro.

#include <functional>
#include <string>
#include <vector>

#include "ui/Element.h"

namespace Ui {
class Field;
class Label;
class List;
class Panel;
}  // namespace Ui

namespace Views {

class Palette : public Ui::Element {
public:
    struct Action {
        std::wstring label;
        // A la derecha y en tinta secundaria: el atajo, o de qué repositorio se habla.
        // Entra también en la búsqueda, y por eso el dueño de un repositorio se encuentra.
        std::wstring hint;
        std::function<void()> run;
    };

    explicit Palette(std::vector<Action> actions);

    // Se lleva toda la entrada que caiga dentro del panel; lo de fuera cierra.
    bool ClipsInput() const override { return true; }
    bool HitTest(float lx, float ly) const override;
    bool OnKey(const Input::Key& e) override;

protected:
    bool OnAttach() override;
    void OnArrange() override;

private:
    void Filter(const std::wstring& query);
    // Ejecuta la de la posición 'shown' de lo que se ve ahora mismo.
    void Run(int shown);
    void PaintRow(const Ui::Paint& paint, const Ui::Rect& box, int shown, bool hovered,
                  bool selected);

    std::vector<Action> m_actions;
    // Plegado y en paralelo a m_actions. Se calcula una vez al abrir: filtrar en cada
    // pulsación no puede volver a plegar ciento veinte cadenas.
    std::vector<std::wstring> m_haystacks;
    // Índices de m_actions, en el orden en que se enseñan.
    std::vector<int> m_shown;

    Ui::Panel* m_panel = nullptr;
    Ui::Field* m_field = nullptr;
    Ui::List* m_list = nullptr;
    Ui::Label* m_empty = nullptr;
    Ui::Rect m_panelRect;
    bool m_placed = false;
};

}  // namespace Views
