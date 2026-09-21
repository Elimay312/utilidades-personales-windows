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

class Sheet : public Element {
public:
    Sheet(std::wstring title, std::wstring body);

    bool ClipsInput() const override { return true; }
    bool HitTest(float lx, float ly) const override;

    // Para que quien la abra pueda meterle lo suyo.
    Panel* Content() const { return m_panel; }

protected:
    bool OnAttach() override;
    void OnArrange() override;

private:
    std::wstring m_title;
    std::wstring m_body;
    Panel* m_panel = nullptr;
    Label* m_titleLabel = nullptr;
    Label* m_bodyLabel = nullptr;
    Rect m_panelRect;
};

}  // namespace Ui
