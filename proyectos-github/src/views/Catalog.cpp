#include "views/Catalog.h"

#include <algorithm>
#include <string>
#include <vector>

#include "compositor/Paint.h"
#include "shell/Caption.h"
#include "ui/Controls.h"
#include "ui/Field.h"
#include "ui/Host.h"
#include "ui/List.h"
#include "ui/Overlays.h"
#include "ui/Sidebar.h"
#include "ui/Text.h"

namespace Views {

namespace {

using Ui::Rect;

constexpr float kGutter = Metrics::kSpace4;
constexpr float kPad = Metrics::kSpace4;
constexpr float kRow = Metrics::kControlHeight;
constexpr float kGap = Metrics::kSpace2;
constexpr float kListHeight = 168.0f;
constexpr float kSidebarItemHeight = Metrics::kRowHeight + 2.0f;

// Quinientos, que es el número del criterio de aceptación.
constexpr int kDemoItems = 500;

// Glifos de Segoe Fluent Icons.
constexpr wchar_t kGlyphTarget[] = {0xE7C1, 0};    // Flag
constexpr wchar_t kGlyphClock[] = {0xE823, 0};     // History
constexpr wchar_t kGlyphInbox[] = {0xE8A5, 0};     // Document
constexpr wchar_t kGlyphArchive[] = {0xE7B8, 0};   // Archive
constexpr wchar_t kGlyphSearch[] = {0xE721, 0};    // Search
constexpr wchar_t kGlyphMore[] = {0xE712, 0};      // More
constexpr wchar_t kGlyphRefresh[] = {0xE72C, 0};   // Refresh

D2D1_RECT_F ToBox(const Rect& rect) {
    return D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom());
}

// Un nombre de repositorio de mentira. Sin dominios: auditar.ps1 marca cualquier literal
// que acabe en .com, .dev, .io y compañía, y el texto de ejemplo de una pantalla de
// pruebas no es motivo para relajar la regla 4.
std::wstring DemoName(int index) {
    static const wchar_t* kStems[] = {L"brujula",   L"isla",      L"rayo",      L"dock",
                                      L"pizarra",   L"sendero",   L"faro",      L"cantera"};
    std::wstring name = kStems[static_cast<std::size_t>(index) % 8];
    name += L"-";
    name += std::to_wstring(index + 1);
    return name;
}

std::wstring DemoStep(int index) {
    static const wchar_t* kSteps[] = {
        L"Conectar el lector de carpetas a la columna central",
        L"Medir los destellos al redimensionar",
        L"Terminar la fase y anotar las decisiones",
        L"Revisar las rutas largas en Windows 10",
    };
    return kSteps[static_cast<std::size_t>(index) % 4];
}

}  // namespace

// Una columna del catálogo. Lo único que la hace clara u oscura es Substitute: los tokens
// que recibe de arriba los cambia por los suyos y todo su subárbol se pinta con ellos sin
// enterarse de nada.
class Catalog::Column : public Ui::Element {
public:
    Column(Theme::Appearance appearance, Catalog* catalog)
        : m_appearance(appearance), m_catalog(catalog) {}

    const wchar_t* Title() const {
        return m_appearance == Theme::Appearance::Light ? L"Claro" : L"Oscuro";
    }

    Ui::List* List() const { return m_list; }

protected:
    Theme::Tokens Substitute(const Theme::Tokens& tokens) const override {
        // El acento se hereda del sistema: es el mismo en los dos temas y es el que de
        // verdad va a tener el usuario.
        return Theme::TokensFor(m_appearance, tokens.accent);
    }

    bool OnAttach() override;
    void OnArrange() override;
    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override;
    void OnPaint(const Ui::Paint& paint, const Rect& box) override;

private:
    void ShowMenu(float x, float y);

    Theme::Appearance m_appearance = Theme::Appearance::Light;
    Catalog* m_catalog = nullptr;

    // Tipografía
    Ui::Label* m_title = nullptr;
    Ui::Label* m_body = nullptr;
    Ui::Label* m_clipped = nullptr;
    Ui::Label* m_numbers[3] = {nullptr, nullptr, nullptr};

    // Controles
    Ui::Button* m_primary = nullptr;
    Ui::Button* m_secondary = nullptr;
    Ui::Button* m_plain = nullptr;
    Ui::IconButton* m_icon = nullptr;

    Ui::Pill* m_pills[3] = {nullptr, nullptr, nullptr};
    Ui::Dot* m_dots[3] = {nullptr, nullptr, nullptr};
    Ui::Label* m_dotLabels[3] = {nullptr, nullptr, nullptr};

    Ui::Field* m_field = nullptr;
    Ui::SidebarGroup* m_sidebar = nullptr;
    Ui::List* m_list = nullptr;

    // La muestra quieta de lo que flota: es lo que pone el menú y su sombra en claro y en
    // oscuro a la vez.
    Ui::Panel* m_floating = nullptr;
    Ui::Label* m_floatingLabels[2] = {nullptr, nullptr};

    Ui::Button* m_openMenu = nullptr;
    Ui::Button* m_openToast = nullptr;
    Ui::Button* m_openSheet = nullptr;
    Ui::Button* m_shuffle = nullptr;
};

bool Catalog::Column::OnAttach() {
    if (!CreateMaterial(Metrics::RadiusOf(Metrics::Radius::Panel))) return false;
    if (!CreateLayer()) return false;

    // --- Tipografía ---------------------------------------------------------------
    m_title = Add<Ui::Label>(L"Brújula", Ui::Style::Title, Ui::Weight::Semibold);
    m_body = Add<Ui::Label>(L"Revisión año niño — 26 / 20 / 15 / 13 / 11", Ui::Style::Body);
    m_clipped = Add<Ui::Label>(
        L"Un siguiente paso larguísimo que no cabe de ninguna manera y se recorta",
        Ui::Style::Caption);
    m_clipped->UseSecondary();

    // Números tabulares: tres filas que tienen que quedar en columna. Con cifras de
    // anchura variable, el 1 es más estrecho y los puntos no cuadran.
    const wchar_t* kFechas[] = {L"2026-09-21   1 111", L"2026-11-08   88 004",
                                L"2026-12-30   910 777"};
    for (int i = 0; i < 3; ++i) {
        m_numbers[i] = Add<Ui::Label>(kFechas[i], Ui::Style::Caption);
        m_numbers[i]->SetFigures(Ui::Figures::Tabular);
        m_numbers[i]->UseSecondary();
    }

    // --- Botones -------------------------------------------------------------------
    m_primary = Add<Ui::Button>(L"Sincronizar", Ui::ButtonKind::Primary);
    m_secondary = Add<Ui::Button>(L"Secundario", Ui::ButtonKind::Secondary);
    m_plain = Add<Ui::Button>(L"Sin fondo", Ui::ButtonKind::Plain);
    m_icon = Add<Ui::IconButton>(kGlyphRefresh, Ui::ButtonKind::Secondary);

    // --- Píldoras y puntos ------------------------------------------------------------
    const Ui::Priority kPriorities[] = {Ui::Priority::Focus, Ui::Priority::Secondary,
                                        Ui::Priority::Someday};
    const Ui::Activity kActivities[] = {Ui::Activity::Active, Ui::Activity::Paused,
                                        Ui::Activity::Dormant};
    for (int i = 0; i < 3; ++i) {
        m_pills[i] = Add<Ui::Pill>(kPriorities[i]);
        m_dots[i] = Add<Ui::Dot>(kActivities[i]);
        m_dotLabels[i] = Add<Ui::Label>(Ui::NameOf(kActivities[i]), Ui::Style::Footnote);
        m_dotLabels[i]->UseSecondary();
    }

    // --- Campo de texto -----------------------------------------------------------------
    m_field = Add<Ui::Field>(L"Escribe «Revisión año niño»");

    // --- Barra lateral --------------------------------------------------------------------
    m_sidebar = Add<Ui::SidebarGroup>();
    m_sidebar->AddItem(kGlyphTarget, L"Enfoque", 3);
    m_sidebar->AddItem(kGlyphInbox, L"Secundario", 12);
    m_sidebar->AddItem(kGlyphClock, L"Necesita decisión", 7);
    m_sidebar->AddItem(kGlyphArchive, L"Archivado", 104);

    // --- Lista de 500 ------------------------------------------------------------------------
    m_list = Add<Ui::List>();
    m_list->SetRowHeight(Metrics::kRowHeight);
    m_list->SetRowPainter([](const Ui::Paint& paint, const Rect& box,
                             const Ui::List::RowState& state) {
        if (state.selected || state.hovered) {
            winrt::com_ptr<ID2D1SolidColorBrush> veil;
            paint.dc->CreateSolidColorBrush(
                Gfx::ToD2D(state.selected ? paint.tokens->selectionRow
                                          : paint.tokens->controlHover),
                veil.put());
            const float radius = Metrics::RadiusOf(Metrics::Radius::Control);
            paint.dc->FillRoundedRectangle(
                D2D1::RoundedRect(ToBox(box.Inset(2.0f)), radius, radius), veil.get());
        }

        const Ui::Activity activity = static_cast<Ui::Activity>(state.index % 3);
        winrt::com_ptr<ID2D1SolidColorBrush> dot;
        paint.dc->CreateSolidColorBrush(Gfx::ToD2D(Ui::ColorOf(activity, *paint.tokens)),
                                        dot.put());
        paint.dc->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(box.x + Metrics::kSpace2, box.y + box.height * 0.5f),
                          4.0f, 4.0f),
            dot.get());

        winrt::com_ptr<ID2D1SolidColorBrush> ink;
        winrt::com_ptr<ID2D1SolidColorBrush> dim;
        paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->textPrimary), ink.put());
        paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->textSecondary), dim.put());

        const float left = box.x + Metrics::kSpace4;
        paint.text->DrawLine(paint.dc, DemoName(state.index), Ui::Style::Body,
                             Ui::Weight::Semibold,
                             ToBox(Rect{left, box.y, 110.0f, box.height}), ink.get());
        paint.text->DrawLine(paint.dc, DemoStep(state.index), Ui::Style::Caption,
                             Ui::Weight::Regular,
                             ToBox(Rect{left + 116.0f, box.y,
                                        std::max(box.width - left - 190.0f, 1.0f), box.height}),
                             dim.get());

        Ui::Run days;
        const std::wstring text = std::to_wstring(state.index % 90) + L" d";
        days.text = text;
        days.style = Ui::Style::Caption;
        days.align = Ui::Align::Trailing;
        days.figures = Ui::Figures::Tabular;  // una columna de números que se desplaza
        paint.text->Draw(paint.dc, days,
                         ToBox(Rect{box.Right() - 52.0f, box.y, 44.0f, box.height}), dim.get());
    });
    m_list->SetCount(kDemoItems, true);
    m_list->SetSelected(0);
    if (m_catalog) {
        Catalog* catalog = m_catalog;
        m_list->OnRecycled([catalog] { catalog->RefreshStats(); });
    }

    // --- Muestra quieta de lo que flota -------------------------------------------------------
    m_floating = Add<Ui::Panel>(Ui::Panel::Surface::Menu, Metrics::Radius::Card,
                                Metrics::kElevationMenu);
    m_floatingLabels[0] = m_floating->Add<Ui::Label>(L"Abrir en GitHub", Ui::Style::Body);
    m_floatingLabels[1] = m_floating->Add<Ui::Label>(L"Añadir novedad", Ui::Style::Body);

    // --- Lo interactivo -------------------------------------------------------------------------
    m_openMenu = Add<Ui::Button>(L"Menú", Ui::ButtonKind::Secondary);
    m_openMenu->OnActivate([this] {
        const Rect rect = m_openMenu->WindowRect();
        ShowMenu(rect.x, rect.Bottom() + Metrics::kSpace1);
    });

    m_openToast = Add<Ui::Button>(L"Aviso", Ui::ButtonKind::Secondary);
    m_openToast->OnActivate([this] {
        Ui::Host::LayerOptions options;
        options.lightDismiss = false;
        options.modal = false;
        HostRef().PushLayer<Ui::Toast>(options, L"No se pudo sincronizar: sin conexión",
                                       2600);
    });

    m_openSheet = Add<Ui::Button>(L"Hoja", Ui::ButtonKind::Secondary);
    m_openSheet->OnActivate([this] {
        Ui::Host::LayerOptions options;
        options.modal = true;
        options.lightDismiss = true;
        options.scrim = true;
        HostRef().PushLayer<Ui::Sheet>(options, L"Límite de Enfoque",
                                       L"Ya hay cinco repos en Enfoque. Elige cuál baja.");
    });

    m_shuffle = Add<Ui::Button>(L"Reordenar", Ui::ButtonKind::Plain);
    m_shuffle->OnActivate([this] {
        // Baraja invirtiendo las diez primeras: las filas que se ven se deslizan a su
        // sitio nuevo en vez de saltar, que es lo que pedirá la fase 4 al sincronizar.
        std::vector<int> order(kDemoItems);
        for (int i = 0; i < kDemoItems; ++i) order[static_cast<std::size_t>(i)] = i;
        std::reverse(order.begin(), order.begin() + 10);
        m_list->Reorder(std::move(order));
    });
    return true;
}

void Catalog::Column::ShowMenu(float x, float y) {
    std::vector<Ui::Menu::Entry> entries;
    entries.push_back({L"Abrir en GitHub", nullptr});
    entries.push_back({L"Abrir la carpeta local", nullptr});
    entries.push_back({L"Editar el siguiente paso", nullptr});
    entries.push_back({L"Archivar", nullptr});

    Ui::Host::LayerOptions options;
    options.lightDismiss = true;
    options.modal = true;
    HostRef().PushLayer<Ui::Menu>(options, std::move(entries), x, y);
}

void Catalog::Column::OnArrange() {
    const Rect frame = Frame();
    const float inner = std::max(frame.width - kPad * 2.0f, 1.0f);
    float y = Metrics::kSpace4;

    const auto place = [&](Ui::Element* element, float height) {
        if (element) element->SetFrame(Rect{kPad, y, inner, height});
        y += height;
    };

    place(m_title, 34.0f);
    y += 2.0f;
    place(m_body, 22.0f);
    place(m_clipped, 20.0f);
    y += Metrics::kSpace1;
    for (Ui::Label* number : m_numbers) place(number, 18.0f);

    y += Metrics::kSpace2;

    // Botones, en fila.
    {
        float x = kPad;
        const auto row = [&](Ui::Element* element, float width) {
            if (element) element->SetFrame(Rect{x, y, width, kRow});
            x += width + Metrics::kSpace1;
        };
        row(m_primary, m_primary ? m_primary->PreferredWidth() : 0.0f);
        row(m_secondary, m_secondary ? m_secondary->PreferredWidth() : 0.0f);
        row(m_plain, m_plain ? m_plain->PreferredWidth() : 0.0f);
        row(m_icon, kRow);
        y += kRow + kGap;
    }

    // Píldoras y puntos, en fila.
    {
        float x = kPad;
        for (Ui::Pill* pill : m_pills) {
            if (!pill) continue;
            const float width = std::max(pill->PreferredWidth(), 56.0f);
            pill->SetFrame(Rect{x, y, width, 22.0f});
            x += width + Metrics::kSpace1;
        }
        y += 22.0f + Metrics::kSpace1;

        x = kPad;
        for (int i = 0; i < 3; ++i) {
            if (m_dots[i]) m_dots[i]->SetFrame(Rect{x, y, 10.0f, 16.0f});
            const float width = m_dotLabels[i] ? m_dotLabels[i]->PreferredWidth() + 4.0f : 60.0f;
            if (m_dotLabels[i]) m_dotLabels[i]->SetFrame(Rect{x + 12.0f, y, width, 16.0f});
            x += 12.0f + width + Metrics::kSpace2;
        }
        y += 16.0f + kGap;
    }

    place(m_field, kRow);
    y += kGap;

    if (m_sidebar) {
        const float height = kSidebarItemHeight * 4.0f;
        m_sidebar->SetFrame(Rect{kPad, y, inner, height});
        y += height + kGap;
    }

    // La lista se queda con lo que sobre. Así el bloque de abajo siempre cabe, y al
    // encoger la ventana lo que se estrecha es la lista y no lo que se sale por el borde.
    if (m_list) {
        constexpr float kBottomBlock = 64.0f + Metrics::kSpace4;
        const float available = frame.height - y - kBottomBlock;
        const float height = std::clamp(available, 72.0f, kListHeight);
        m_list->SetFrame(Rect{kPad, y, inner, height});
        y += height + kGap;
    }

    // La muestra quieta del panel flotante, al lado de los botones que abren los de
    // verdad.
    if (m_floating) {
        const float width = 168.0f;
        const float height = 64.0f;
        m_floating->SetFrame(Rect{kPad, y, width, height});
        for (int i = 0; i < 2; ++i) {
            if (m_floatingLabels[i]) {
                m_floatingLabels[i]->SetFrame(
                    Rect{Metrics::kSpace2, 6.0f + static_cast<float>(i) * 26.0f,
                         width - Metrics::kSpace2 * 2.0f, 26.0f});
            }
        }

        float x = kPad + width + Metrics::kSpace2;
        const auto row = [&](Ui::Element* element, float width2) {
            if (element) element->SetFrame(Rect{x, y, width2, kRow});
            x += width2 + Metrics::kSpace1;
        };
        // Medidos y no a ojo: «Reordenar» no cabía en el ancho que le puse a mano y
        // salía recortado con elipsis.
        row(m_openMenu, m_openMenu->PreferredWidth());
        row(m_openToast, m_openToast->PreferredWidth());
        row(m_openSheet, m_openSheet->PreferredWidth());
        row(m_shuffle, m_shuffle->PreferredWidth());
    }
}

void Catalog::Column::OnTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    // Opaco a propósito: una columna clara sobre la Mica oscura no se leería. Es la única
    // superficie de la aplicación que tapa el material, y solo porque su trabajo es
    // enseñar los colores sin que la Mica los tiña.
    Theme::Color base = tokens.cardSurface;
    base.a = 255;
    if (Gfx::Material* material = MaterialOf()) {
        material->SetColor(base, HostRef().Animator(), crossfadeMs);
    }
    Ui::Element::OnTheme(tokens, crossfadeMs);
}

void Catalog::Column::OnPaint(const Ui::Paint& paint, const Rect& box) {
    winrt::com_ptr<ID2D1SolidColorBrush> secondary;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->textSecondary), secondary.put());
    Ui::Run label;
    label.text = Title();
    label.style = Ui::Style::Footnote;
    label.weight = Ui::Weight::Semibold;
    label.align = Ui::Align::Trailing;
    paint.text->Draw(paint.dc, label,
                     ToBox(Rect{box.x + kPad, box.y + 6.0f,
                                std::max(box.width - kPad * 2.0f, 1.0f), 14.0f}),
                     secondary.get());
}

// ============================================================================ Catalog ==

Catalog::Catalog() = default;

bool Catalog::OnAttach() {
    if (!CreateLayer()) return false;
    m_light = Add<Column>(Theme::Appearance::Light, this);
    m_dark = Add<Column>(Theme::Appearance::Dark, this);
    return true;
}

void Catalog::OnArrange() {
    const Rect frame = Frame();
    const float top = Caption::kBarHeight;
    const float height = std::max(frame.height - top - kGutter, 1.0f);
    const float width = std::max((frame.width - kGutter * 3.0f) * 0.5f, 1.0f);

    if (m_light) m_light->SetFrame(Rect{kGutter, top, width, height});
    if (m_dark) m_dark->SetFrame(Rect{kGutter * 2.0f + width, top, width, height});
}

void Catalog::RefreshStats() {
    if (!m_light || !m_light->List()) return;
    const float ms = m_light->List()->LastRecycleMs();
    const int rows = m_light->List()->LiveRows();
    // Solo si cambió de verdad: esto se llama en cada fotograma mientras la lista se
    // desliza, y repintar la barra de arriba sesenta veces por segundo para enseñar el
    // mismo número sería justo el trabajo que se está midiendo.
    if (rows == m_liveRows && std::abs(ms - m_recycleMs) < 0.01f) return;
    m_recycleMs = ms;
    m_liveRows = rows;
    Invalidate();
}

void Catalog::OnPaint(const Ui::Paint& paint, const Rect& box) {
    winrt::com_ptr<ID2D1SolidColorBrush> primary;
    winrt::com_ptr<ID2D1SolidColorBrush> secondary;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->textPrimary), primary.put());
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(paint.tokens->textSecondary), secondary.put());

    paint.text->DrawLine(paint.dc, L"Catálogo de componentes", Ui::Style::Body,
                         Ui::Weight::Semibold,
                         ToBox(Rect{box.x + kGutter, box.y, 320.0f, Caption::kBarHeight}),
                         primary.get());

    // La medida del criterio, a la vista. Los botones de la ventana ocupan los 138 DIP de
    // la derecha, así que el texto se queda a este lado.
    wchar_t stats[96];
    swprintf_s(stats, L"reciclado %.2f ms · %d filas vivas · %d elementos", m_recycleMs,
               m_liveRows, 500);
    Ui::Run run;
    run.text = stats;
    run.style = Ui::Style::Footnote;
    run.align = Ui::Align::Trailing;
    run.figures = Ui::Figures::Tabular;
    paint.text->Draw(paint.dc, run,
                     ToBox(Rect{box.x + 340.0f, box.y,
                                std::max(box.width - 340.0f - 150.0f, 1.0f),
                                Caption::kBarHeight}),
                     secondary.get());
}

}  // namespace Views
