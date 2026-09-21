#pragma once

// La barra lateral: grupos de prioridad con contador y vistas inteligentes.
//
// El detalle que la hace lo que es: la selección es UN SOLO material que se desliza de un
// elemento a otro con muelle estándar, no un fondo que se enciende en uno y se apaga en
// el otro. Es la diferencia entre que la selección se mueva y que parpadee, y es también
// por lo que Gfx::Material sabe soltarse de su padre y colocarse donde le digan.
//
// El grupo entero es UNA superficie. Seis filas de texto no son seis texturas: los
// elementos no tienen Gfx::Layer y se pintan en la del grupo. El hover sí repinta, pero
// repinta una superficie pequeña y solo cuando el ratón cambia de fila.

#include <functional>
#include <string>
#include <vector>

#include "ui/Element.h"

namespace Ui {

class SidebarGroup;

class SidebarItem : public Element {
public:
    SidebarItem(std::wstring glyph, std::wstring label, int count);

    void SetCount(int count);
    const std::wstring& Label() const { return m_label; }

    bool Focusable() const override { return true; }

protected:
    void OnPaint(const Paint& paint, const Rect& box) override;
    bool OnPointer(const Input::Pointer& e) override;
    bool OnKey(const Input::Key& e) override;
    void OnStateChanged() override;

private:
    friend class SidebarGroup;
    SidebarGroup* Group() const;

    std::wstring m_glyph;
    std::wstring m_label;
    int m_count = 0;
    bool m_selected = false;
};

class SidebarGroup : public Element {
public:
    SidebarItem* AddItem(std::wstring glyph, std::wstring label, int count);
    void Select(int index, bool animate);
    int Selected() const { return m_selected; }
    void OnSelect(std::function<void(int)> handler) { m_select = std::move(handler); }

    int IndexOf(const SidebarItem* item) const;

protected:
    bool OnAttach() override;
    void OnArrange() override;
    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override;
    bool OnKey(const Input::Key& e) override;

private:
    void MoveSelection(bool animate);

    std::vector<SidebarItem*> m_items;
    std::function<void(int)> m_select;
    int m_selected = 0;
};

}  // namespace Ui
