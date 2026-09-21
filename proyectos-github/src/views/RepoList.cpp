#include "views/RepoList.h"

#include <algorithm>

#include "shell/Caption.h"
#include "ui/Controls.h"
#include "ui/Field.h"
#include "ui/Host.h"
#include "ui/List.h"
#include "ui/Text.h"

namespace Views {

namespace {

using Ui::Rect;

constexpr float kPad = Metrics::kSpace4;
constexpr float kTop = Caption::kBarHeight + Metrics::kSpace2;
constexpr float kHeaderHeight = 48.0f;
constexpr float kSearchWidth = 240.0f;
constexpr float kToggleSize = Metrics::kControlHeight;

// Segoe Fluent Icons. Por número y no pegando el carácter: viven en el área de uso privado
// de Unicode, así que en el editor son un hueco en blanco.
constexpr wchar_t kGlyphList[] = {0xEA37, 0};  // List
constexpr wchar_t kGlyphGrid[] = {0xF0E2, 0};  // GridView

std::wstring Plural(int count, const wchar_t* one, const wchar_t* many) {
    return std::to_wstring(count) + L" " + (count == 1 ? one : many);
}

}  // namespace

bool RepoList::OnAttach() {
    m_header = Add<Ui::Slate>();
    m_title = m_header->Add<Ui::Label>(L"", Ui::Style::Heading, Ui::Weight::Semibold);
    m_count = m_header->Add<Ui::Label>(L"", Ui::Style::Caption);
    m_count->UseSecondary();
    m_count->SetFigures(Ui::Figures::Tabular);

    m_search = Add<Ui::Field>(L"Buscar");
    m_search->OnChanged([this](const std::wstring& text) {
        if (m_queryChanged) m_queryChanged(text);
    });
    // Enter en la búsqueda baja a la lista con lo encontrado. Es lo que espera quien escribe
    // dos letras y quiere moverse con las flechas sin tocar el ratón.
    m_search->OnSubmit([this] { FocusList(); });

    m_toggle = Add<Ui::IconButton>(kGlyphGrid, Ui::ButtonKind::Secondary);
    m_toggle->OnActivate([this] { ToggleLayout(); });

    m_list = Add<Ui::List>();
    m_list->SetPadding(kPad, Metrics::kSpace2);
    m_list->SetRowPainter(
        [this](const Ui::Paint& paint, const Rect& box, const Ui::List::RowState& state) {
            PaintCardRow(paint, box, state.index, state.hovered, state.selected);
        });
    m_list->OnSelectionChanged([this](int index) {
        if (m_selectionChanged) m_selectionChanged(index);
    });
    // Los dos, y distintos: Enter y el doble clic ACTIVAN, y un clic suelto es un clic. Sin
    // separarlos, moverse con las flechas —que también cambia la selección— abriría el
    // inspector en cada pulsación.
    m_list->OnActivate([this](int index) {
        if (m_activated) m_activated(index);
    });
    m_list->OnClicked([this](int index) {
        if (m_clicked) m_clicked(index);
    });

    m_empty = Add<Ui::Slate>();
    m_emptyTitle = m_empty->Add<Ui::Label>(L"", Ui::Style::Heading, Ui::Weight::Semibold);
    m_emptyTitle->SetAlign(Ui::Align::Center);
    m_emptyBody = m_empty->Add<Ui::Label>(L"", Ui::Style::Body);
    m_emptyBody->UseSecondary();
    m_emptyBody->SetAlign(Ui::Align::Center);
    // En varias líneas: son las únicas frases de la aplicación escritas para leerse
    // enteras, y una elipsis a mitad de una explicación la convierte en nada.
    m_emptyBody->SetWrap(true);
    m_empty->SetVisible(false);

    m_emptyAction = Add<Ui::Button>(L"", Ui::ButtonKind::Secondary);
    m_emptyAction->SetVisible(false);
    m_emptyAction->OnActivate([this] {
        if (m_emptyText.sync) {
            if (m_syncRequested) m_syncRequested();
            return;
        }
        if (m_emptyText.clear) {
            ClearSearch();
            return;
        }
        if (m_lensRequested) m_lensRequested(m_emptyText.lens);
    });
    return true;
}

int RepoList::ColumnsFor(float widthDip) const {
    if (m_layout == CardLayout::List) return 1;
    const float inner = widthDip - kPad * 2.0f;
    // Cuántas caben con su hueco. El +gap de los dos lados es la cuenta de siempre: n
    // celdas llevan n-1 huecos, así que se le suma uno a los dos términos y se divide.
    const int columns =
        static_cast<int>((inner + kCardGap) / (kGridCardMinWidth + kCardGap));
    return std::clamp(columns, 1, 4);
}

void RepoList::ApplyLayout(bool animate) {
    if (m_list == nullptr) return;
    const float height = m_layout == CardLayout::List ? kListCardHeight : kGridCardHeight;
    m_list->SetLayout(ColumnsFor(Frame().width), height,
                      m_layout == CardLayout::List ? Metrics::kSpace1 : kCardGap, animate);
}

void RepoList::OnArrange() {
    const float width = Frame().width;
    const float right = width - kPad;

    if (m_toggle) m_toggle->SetFrame(Rect{right - kToggleSize, kTop, kToggleSize, kToggleSize});
    if (m_search) {
        m_search->SetFrame(Rect{right - kToggleSize - Metrics::kSpace1 - kSearchWidth, kTop,
                                kSearchWidth, Metrics::kControlHeight});
    }
    if (m_header) {
        const float headerWidth =
            std::max(width - kPad * 2.0f - kSearchWidth - kToggleSize - Metrics::kSpace4, 1.0f);
        m_header->SetFrame(Rect{kPad, kTop - 6.0f, headerWidth, kHeaderHeight});
        if (m_title) m_title->SetFrame(Rect{0.0f, 0.0f, headerWidth, 26.0f});
        if (m_count) m_count->SetFrame(Rect{0.0f, 27.0f, headerWidth, 18.0f});
    }

    const bool slide = m_animateArrange;
    m_animateArrange = false;

    const float listTop = kTop + kHeaderHeight;
    const float listHeight = std::max(Frame().height - listTop, 1.0f);
    if (m_list) {
        if (slide) m_list->AnimateNextArrange();
        m_list->SetFrame(Rect{0.0f, listTop, width, listHeight});
    }

    // El estado vacío, en el tercio de arriba de la lista y no en el centro exacto: un
    // bloque de texto centrado en una ventana alta queda flotando muy abajo.
    const float emptyWidth = std::min(width - kPad * 2.0f, 420.0f);
    const float emptyX = (width - emptyWidth) * 0.5f;
    const float emptyY = listTop + std::max(listHeight * 0.28f, Metrics::kSpace5);
    // Tres renglones de cuerpo caben de sobra en cualquiera de las frases y no dejan hueco
    // cuando sobran: el texto se centra en su caja.
    constexpr float kEmptyHeight = 28.0f + 6.0f + 66.0f;
    if (m_empty) {
        m_empty->SetFrame(Rect{emptyX, emptyY, emptyWidth, kEmptyHeight});
        if (m_emptyTitle) m_emptyTitle->SetFrame(Rect{0.0f, 0.0f, emptyWidth, 28.0f});
        if (m_emptyBody) m_emptyBody->SetFrame(Rect{0.0f, 34.0f, emptyWidth, 66.0f});
    }
    if (m_emptyAction) {
        constexpr float kActionWidth = 190.0f;
        m_emptyAction->SetFrame(Rect{(width - kActionWidth) * 0.5f,
                                     emptyY + kEmptyHeight + Metrics::kSpace2, kActionWidth,
                                     Metrics::kControlHeight});
    }

    // Después de colocar: el número de columnas depende del ancho que acabamos de recibir.
    ApplyLayout(slide);
}

// ----------------------------------------------------------------------- El contenido --

void RepoList::PaintCardRow(const Ui::Paint& paint, const Rect& box, int index, bool hovered,
                            bool selected) {
    if (m_state == nullptr) return;
    const App::Entry* entry = m_state->At(index);
    if (entry == nullptr) return;
    PaintCard(paint, box, *entry, m_layout, hovered, selected);
}

void RepoList::Refresh(bool animate) {
    if (m_state == nullptr || m_list == nullptr) return;
    // Update repinta TODAS las filas vivas, no solo las que se mueven. Hace falta: una
    // sincronización reescribe el "hace X días" de una tarjeta que no ha cambiado de sitio,
    // y una fila que se queda quieta con su dibujo de antes es el peor de los dos fallos
    // posibles, porque parece un dato bueno.
    m_list->Update(m_state->Keys(), animate);
    UpdateHeader();
    UpdateEmpty();
}

void RepoList::UpdateHeader() {
    if (m_state == nullptr || m_title == nullptr) return;

    const App::Lens lens = m_state->CurrentLens();
    m_title->SetText(App::NameOf(lens));

    const int visible = m_state->VisibleCount();
    const int total = m_state->CountOf(lens);
    if (m_state->Searching()) {
        m_count->SetText(std::to_wstring(visible) + L" de " + std::to_wstring(total));
    } else {
        m_count->SetText(Plural(total, L"repositorio", L"repositorios"));
    }
}

RepoList::EmptyText RepoList::TextForEmpty() const {
    EmptyText text;
    if (m_state == nullptr) return text;

    if (m_state->Searching()) {
        text.title = L"Sin resultados";
        if (m_state->CurrentLens() != App::Lens::All) {
            text.body = L"Aquí no hay nada con esas palabras. Puede estar en otro grupo.";
            text.action = L"Buscar en Todos";
            text.lens = App::Lens::All;
        } else {
            text.body = L"Ninguno de tus repositorios lleva esas palabras en el nombre, la "
                        L"descripción o el siguiente paso.";
            text.action = L"Quitar la búsqueda";
            text.clear = true;
        }
        return text;
    }

    // Sin un solo repositorio no es un grupo vacío: es una caché vacía, y lo que hace falta
    // no es cambiar de vista sino traer los datos.
    if (m_state->Entries().empty()) {
        text.title = L"Todavía no hay repositorios";
        text.body = L"Sincroniza para traer los de tu cuenta de GitHub. Se quedan guardados "
                    L"aquí y la próxima vez aparecen al instante.";
        text.action = L"Sincronizar ahora";
        text.sync = true;
        return text;
    }

    switch (m_state->CurrentLens()) {
    case App::Lens::Focus:
        text.title = L"Nada en Enfoque";
        text.body = L"Enfoque es lo que estás haciendo ahora mismo, y caben cinco. Elige los "
                    L"primeros entre los que están sin clasificar.";
        text.action = L"Ver sin clasificar";
        text.lens = App::Lens::Unsorted;
        break;
    case App::Lens::Secondary:
        text.title = L"Nada en Secundario";
        text.body = L"Aquí va lo que no es de ahora pero tampoco se olvida.";
        text.action = L"Ver todos";
        break;
    case App::Lens::Someday:
        text.title = L"Algún día está vacío";
        text.body = L"Es el sitio de lo que te gustaría hacer y no tiene fecha.";
        text.action = L"Ver todos";
        break;
    case App::Lens::Archived:
        text.title = L"No has archivado nada";
        text.body = L"Archivar quita de en medio sin borrar: las notas siguen aquí.";
        text.action = L"Ver todos";
        break;
    case App::Lens::Unsorted:
        text.title = L"No queda nada sin clasificar";
        text.body = L"Todos tus repositorios tienen ya un grupo. Buen momento para mirar "
                    L"cuáles están en Enfoque.";
        text.action = L"Ver Enfoque";
        text.lens = App::Lens::Focus;
        break;
    case App::Lens::All:
        text.title = L"No hay repositorios que enseñar";
        text.body = L"Todos los que había dejaron de aparecer en la cuenta.";
        text.action = L"Sincronizar ahora";
        text.sync = true;
        break;
    case App::Lens::NeedsDecision:
        text.title = L"Nada que decidir";
        text.body = L"Ningún repositorio en Enfoque lleva parado más de dos semanas, y ningún "
                    L"archivado ha recibido pushes.";
        text.action = L"Ver Enfoque";
        text.lens = App::Lens::Focus;
        break;
    case App::Lens::Dormant:
        text.title = L"Ninguno está dormido";
        text.body = L"Todos han recibido algún push en los últimos noventa días.";
        text.action = L"Ver todos";
        break;
    case App::Lens::ThisWeek:
        text.title = L"Esta semana, nada";
        text.body = L"Ningún repositorio ha recibido un push en los últimos siete días.";
        text.action = L"Ver todos";
        break;
    }
    return text;
}

void RepoList::UpdateEmpty() {
    if (m_state == nullptr || m_empty == nullptr) return;

    const bool show = m_state->VisibleCount() == 0;
    m_empty->SetVisible(show);
    if (!show) {
        m_emptyAction->SetVisible(false);
        return;
    }

    m_emptyText = TextForEmpty();
    m_emptyTitle->SetText(m_emptyText.title);
    m_emptyBody->SetText(m_emptyText.body);
    m_emptyAction->SetVisible(m_emptyText.action != nullptr);
    if (m_emptyText.action) m_emptyAction->SetLabel(m_emptyText.action);
    m_empty->Relayout();
}

// ------------------------------------------------------------------------ La búsqueda --

void RepoList::FocusSearch() {
    if (m_search == nullptr || !Attached()) return;
    HostRef().Input().Focus(m_search, true);
}

void RepoList::ClearSearch() {
    if (m_search == nullptr) return;
    // SetText no dispara OnChanged —el campo avisa de lo que escribe el usuario, no de lo
    // que le escriben—, así que el aviso hacia arriba va a mano.
    m_search->SetText(std::wstring());
    if (m_queryChanged) m_queryChanged(std::wstring());
    FocusList();
}

bool RepoList::SearchFocused() const {
    if (m_search == nullptr || !Attached()) return false;
    return HostRef().Input().Focused() == m_search;
}

void RepoList::FocusList() {
    if (m_list == nullptr || !Attached()) return;
    HostRef().Input().Focus(m_list, false);
}

int RepoList::Selected() const { return m_list ? m_list->Selected() : -1; }

Ui::Rect RepoList::SelectedCardRect() const {
    if (m_list == nullptr) return Ui::Rect{};
    return m_list->RowRect(m_list->Selected());
}

void RepoList::SelectSlot(int slot) {
    if (m_list) m_list->SetSelected(slot);
}

bool RepoList::Navigate(const Input::Key& e) { return m_list && m_list->Navigate(e); }

void RepoList::ReflowCells() {
    // Solo levanta la bandera: quien recoloca de verdad es OnArrange, y hacerlo aquí además
    // dejaría las celdas puestas en su sitio nuevo ANTES de que hubiera nada que animar.
    m_animateArrange = true;
}

void RepoList::ToggleLayout() {
    m_layout = m_layout == CardLayout::List ? CardLayout::Grid : CardLayout::List;
    // El icono enseña adónde se va, no dónde se está: es un interruptor, y un interruptor
    // que enseña su estado actual se lee como si ya estuviera pulsado.
    if (m_toggle) m_toggle->SetLabel(m_layout == CardLayout::List ? kGlyphGrid : kGlyphList);
    // SetLayout ya recoloca, repinta y recicla: las que siguen a la vista se deslizan a su
    // celda nueva con el muelle suave y las que entran —en cuadrícula caben muchas más—
    // aparecen escalonadas.
    ApplyLayout(true);
}

}  // namespace Views
