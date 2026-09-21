#pragma once

// La columna central: el título de la vista, la búsqueda, el interruptor de disposición, la
// lista de tarjetas y el estado vacío.
//
// No es dueña de los datos. Recibe un puntero al App::State —que vive en la aplicación y
// dura más que ella— y lo lee; cuando el usuario escribe o elige, avisa hacia arriba y
// espera a que le digan que vuelva a mirar. Es la regla 2 de arquitectura: la vista lee
// estado y emite comandos, y no decide nada que se pueda equivocar en silencio.

#include <functional>
#include <string>

#include "app/State.h"
#include "ui/Element.h"
#include "views/Card.h"

namespace Ui {
class Button;
class Field;
class IconButton;
class Label;
class List;
class Slate;
}  // namespace Ui

namespace Views {

class RepoList : public Ui::Element {
public:
    void Bind(const App::State* state) { m_state = state; }

    // Relee el estado y actualiza la lista. 'animate' hace que lo que sigue estando se
    // deslice y lo que entra aparezca; al recolocar por un cambio de tamaño, no.
    void Refresh(bool animate);

    // Recoloca las celdas con animación después de que la columna cambie de ancho. Al
    // abrirse el inspector el ancho cambia de GOLPE —animarlo reasignaría la textura de cada
    // celda en cada fotograma— y lo que se desliza es su posición, que es exactamente la
    // decisión que la fase 4 ya escribió para el paso de lista a cuadrícula.
    void ReflowCells();

    void FocusSearch();
    void ClearSearch();
    bool SearchFocused() const;
    void ToggleLayout();

    // La lista se lleva el foco al abrir y al volver de la búsqueda, para que las flechas
    // funcionen sin haber pulsado antes en ninguna parte.
    void FocusList();
    int Selected() const;
    // Dónde está la tarjeta elegida en coordenadas de ventana, o vacío si no se ve. Es de
    // donde sale la transición compartida hacia el inspector.
    Ui::Rect SelectedCardRect() const;
    void SelectSlot(int slot);
    // Las teclas de navegación cuando el foco está en el campo de búsqueda: bajar desde la
    // búsqueda tiene que entrar en la lista, no quedarse a medias.
    bool Navigate(const Input::Key& e);

    void OnQueryChanged(std::function<void(const std::wstring&)> handler) {
        m_queryChanged = std::move(handler);
    }
    void OnLensRequested(std::function<void(App::Lens)> handler) {
        m_lensRequested = std::move(handler);
    }
    void OnSyncRequested(std::function<void()> handler) { m_syncRequested = std::move(handler); }
    void OnSelectionChanged(std::function<void(int)> handler) {
        m_selectionChanged = std::move(handler);
    }
    // Enter o doble clic sobre una tarjeta. La fase 4 lo dejó sin enganchar a propósito:
    // «lo que se abre —el inspector— es de la fase 5».
    void OnActivated(std::function<void(int)> handler) { m_activated = std::move(handler); }
    // Un clic, aunque esa tarjeta ya estuviera elegida. Ver Ui::List::OnClicked.
    void OnClicked(std::function<void(int)> handler) { m_clicked = std::move(handler); }

protected:
    bool OnAttach() override;
    void OnArrange() override;

private:
    // Qué se enseña cuando no hay nada. Una frase amable y UNA acción, que es lo que pide
    // CLAUDE.md: un estado vacío sin salida es una pantalla que solo dice que no.
    struct EmptyText {
        const wchar_t* title = L"";
        const wchar_t* body = L"";
        const wchar_t* action = nullptr;  // sin texto, sin botón
        App::Lens lens = App::Lens::All;  // adónde lleva el botón
        bool sync = false;                // …o sincroniza, en vez de cambiar de vista
        bool clear = false;               // …o quita la búsqueda
    };

    void ApplyLayout(bool animate);
    void PaintCardRow(const Ui::Paint& paint, const Ui::Rect& box, int index, bool hovered,
                      bool selected);
    void UpdateHeader();
    void UpdateEmpty();
    EmptyText TextForEmpty() const;
    int ColumnsFor(float widthDip) const;

    const App::State* m_state = nullptr;

    Ui::Slate* m_header = nullptr;
    Ui::Label* m_title = nullptr;
    Ui::Label* m_count = nullptr;
    Ui::Field* m_search = nullptr;
    Ui::IconButton* m_toggle = nullptr;
    Ui::List* m_list = nullptr;
    Ui::Slate* m_empty = nullptr;
    Ui::Label* m_emptyTitle = nullptr;
    Ui::Label* m_emptyBody = nullptr;
    Ui::Button* m_emptyAction = nullptr;
    EmptyText m_emptyText;

    std::function<void(const std::wstring&)> m_queryChanged;
    std::function<void(App::Lens)> m_lensRequested;
    std::function<void()> m_syncRequested;
    std::function<void(int)> m_selectionChanged;
    std::function<void(int)> m_activated;
    std::function<void(int)> m_clicked;

    CardLayout m_layout = CardLayout::List;
    // La próxima recolocación es una transición y no un cambio de tamaño. Lo pide
    // ReflowCells, y se consume en la siguiente pasada de OnArrange.
    bool m_animateArrange = false;
};

}  // namespace Views
