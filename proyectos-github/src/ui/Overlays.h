#pragma once

// Lo que flota: el panel translúcido con sombra, el menú contextual, el aviso discreto y
// la hoja modal.
//
// Los tres viven en la capa de overlay del Host, que es hermana del contenido y no hija.
// No es una manía de orden: las sombras NO las recorta el clip implícito del tamaño, pero
// SÍ las recorta un Visual.Clip explícito, y Gfx::Morph pone uno en su raíz. Un menú
// colgado dentro de un Morph tendría la sombra cortada por la mitad.
//
// Cada uno es un contenedor del tamaño de la ventana con un panel dentro. El contenedor
// contesta al hit-test solo dentro del panel, y eso es lo que hace que un clic fuera
// cierre el menú en vez de atravesarlo.

#include <functional>
#include <string>
#include <vector>

#include "ui/Controls.h"
#include "ui/Element.h"

namespace Ui {

// El material flotante: sombra suave, superficie translúcida y un canto fino.
class Panel : public Element {
public:
    enum class Surface { Menu, Toast, Sheet };

    Panel(Surface surface, Metrics::Radius radius, Metrics::Elevation elevation);

    // Entra con fundido y una escala corta. Se llama después de colocarlo.
    void Appear(Motion::Kind kind);

protected:
    bool OnAttach() override;
    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override;

private:
    Surface m_surface = Surface::Menu;
    Metrics::Radius m_radius = Metrics::Radius::Panel;
    Metrics::Elevation m_elevation{};
};

// ------------------------------------------------------------------------- Menú --

class Menu : public Element {
public:
    struct Entry {
        std::wstring label;
        std::function<void()> action;
    };

    Menu(std::vector<Entry> entries, float xDip, float yDip);

    bool ClipsInput() const override { return true; }
    bool HitTest(float lx, float ly) const override;

protected:
    bool OnAttach() override;
    void OnArrange() override;
    bool OnKey(const Input::Key& e) override;

private:
    class Item;

    void Highlight(int index);
    void Activate(int index);

    std::vector<Entry> m_entries;
    std::vector<Item*> m_items;
    Panel* m_panel = nullptr;
    Rect m_panelRect;
    float m_x = 0.0f;
    float m_y = 0.0f;
    int m_highlight = -1;
    bool m_placed = false;
};

// ------------------------------------------------------------------------ Aviso --

// El aviso discreto. Nunca un MessageBox: la regla 4 de arquitectura dice que los errores
// son valores y se enseñan dentro de la aplicación.
class Toast : public Element {
public:
    Toast(std::wstring message, int millis);
    ~Toast() override;

    bool ClipsInput() const override { return true; }
    bool HitTest(float lx, float ly) const override;

protected:
    bool OnAttach() override;
    void OnArrange() override;
    bool OnPointer(const Input::Pointer& e) override;

private:
    void Dismiss();

    std::wstring m_message;
    int m_millis = 3000;
    Panel* m_panel = nullptr;
    Label* m_label = nullptr;
    Rect m_panelRect;
    winrt::Windows::System::DispatcherQueueTimer m_timer{nullptr};
};

// ------------------------------------------------------------------------- Hoja --

// Una pregunta con dos salidas. La fase 2 la dejó con un solo botón de «Cerrar» porque el
// catálogo solo tenía que enseñarla; la fase 5 la necesita de verdad, para lo único de la
// aplicación que no se puede deshacer: el primer commit en un repositorio de trabajo.
//
// El alto sale del texto MEDIDO y no de un número fijo. Una hoja que pregunta si se puede
// escribir en el repositorio de alguien no puede tener la explicación recortada.
class Sheet : public Element {
public:
    Sheet(std::wstring title, std::wstring body);

    // Sin esto la hoja solo sabe cerrarse, que es lo que hacía en la fase 2. Con esto son
    // dos botones: el de la derecha acepta y el de su izquierda se va sin hacer nada.
    void SetActions(std::wstring accept, std::wstring cancel);

    // Una pregunta de varias respuestas, en botones apilados entre el texto y las acciones.
    // La estrena el límite de Enfoque, que no pregunta "¿sí o no?" sino "¿cuál de estos
    // cinco baja?", y la alternativa —un menú dentro de una hoja modal— sería una capa
    // flotante encima de otra para elegir entre cinco cosas que caben a la vista.
    //
    // La hoja crece con ellos: el alto ya salía del texto medido, y esto es un sumando más.
    void SetOptions(std::vector<std::wstring> labels, std::function<void(int)> chosen);
    // El de aceptar en rojo de advertencia no existe en la tabla de CLAUDE.md; lo que sí hay
    // es el acento, y un botón primario ya dice cuál es la salida por omisión.
    void OnAccept(std::function<void()> handler) { m_accepted = std::move(handler); }
    void OnCancel(std::function<void()> handler) { m_cancelled = std::move(handler); }

    bool ClipsInput() const override { return true; }
    bool HitTest(float lx, float ly) const override;
    bool OnKey(const Input::Key& e) override;

    // Para que quien la abra pueda meterle lo suyo.
    Panel* Content() const { return m_panel; }

protected:
    bool OnAttach() override;
    void OnArrange() override;

private:
    // Cerrar destruye la hoja, y con ella la lambda desde la que se está llamando. Por eso
    // la acción se copia antes y el cierre se aplaza al siguiente turno de la cola, que es
    // el mismo truco que ya usan Ui::Toast y la hoja de bienvenida.
    void Leave(bool accepted);

    std::wstring m_title;
    std::wstring m_body;
    std::wstring m_acceptText;
    std::wstring m_cancelText;
    Panel* m_panel = nullptr;
    Label* m_titleLabel = nullptr;
    Label* m_bodyLabel = nullptr;
    Button* m_acceptButton = nullptr;
    Button* m_cancelButton = nullptr;
    std::vector<Button*> m_options;
    std::function<void()> m_accepted;
    std::function<void()> m_cancelled;
    std::function<void(int)> m_chosen;
    Rect m_panelRect;
};

}  // namespace Ui
