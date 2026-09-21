#include "views/Sidebar.h"

#include <algorithm>
#include <iterator>

#include "shell/Caption.h"
#include "ui/Controls.h"
#include "ui/Host.h"
#include "ui/Sidebar.h"

namespace Views {

namespace {

using Ui::Rect;

constexpr float kPad = Metrics::kSpace2;
constexpr float kTop = Caption::kBarHeight + Metrics::kSpace1;
constexpr float kFooterHeight = 112.0f;

// Segoe Fluent Icons, por número: en el editor son un hueco en blanco y cualquiera los
// borraría sin verlos.
constexpr wchar_t kGlyphFlag[] = {0xE7C1, 0};       // Flag          — Enfoque
constexpr wchar_t kGlyphDocument[] = {0xE8A5, 0};   // Document      — Secundario
constexpr wchar_t kGlyphHistory[] = {0xE823, 0};    // History       — Algún día
constexpr wchar_t kGlyphArchive[] = {0xE7B8, 0};    // Archive       — Archivado
constexpr wchar_t kGlyphFolder[] = {0xE8B7, 0};     // Folder        — Sin clasificar
constexpr wchar_t kGlyphAll[] = {0xE71D, 0};        // AllApps       — Todos
constexpr wchar_t kGlyphWarning[] = {0xE7BA, 0};    // Warning       — Necesita decisión
constexpr wchar_t kGlyphStopwatch[] = {0xE916, 0};  // Stopwatch     — Dormidos
constexpr wchar_t kGlyphCalendar[] = {0xE787, 0};   // Calendar      — Esta semana
constexpr wchar_t kGlyphMore[] = {0xE712, 0};       // More          — los ajustes

const wchar_t* GlyphOf(App::Lens lens) {
    switch (lens) {
    case App::Lens::Focus:         return kGlyphFlag;
    case App::Lens::Secondary:     return kGlyphDocument;
    case App::Lens::Someday:       return kGlyphHistory;
    case App::Lens::Archived:      return kGlyphArchive;
    case App::Lens::Unsorted:      return kGlyphFolder;
    case App::Lens::All:           return kGlyphAll;
    case App::Lens::NeedsDecision: return kGlyphWarning;
    case App::Lens::Dormant:       return kGlyphStopwatch;
    case App::Lens::ThisWeek:      return kGlyphCalendar;
    }
    return kGlyphAll;
}

}  // namespace

bool Sidebar::OnAttach() {
    // El velo translúcido de la fase 1. Radio cero: es una columna pegada al borde de la
    // ventana, y redondearla dejaría ver la Mica por las esquinas.
    if (!CreateMaterial(0.0f)) return false;

    m_priority = Add<Ui::SidebarGroup>();
    m_priority->SetHeader(L"Prioridad");
    for (const App::Lens lens : App::kPriorityLenses) {
        m_priority->AddItem(GlyphOf(lens), App::NameOf(lens), 0);
    }
    m_priority->OnSelect([this](int index) {
        if (index >= 0 && index < static_cast<int>(std::size(App::kPriorityLenses))) {
            Choose(App::kPriorityLenses[static_cast<std::size_t>(index)]);
        }
    });

    m_smart = Add<Ui::SidebarGroup>();
    m_smart->SetHeader(L"Vistas");
    for (const App::Lens lens : App::kSmartLenses) {
        m_smart->AddItem(GlyphOf(lens), App::NameOf(lens), 0);
    }
    m_smart->OnSelect([this](int index) {
        if (index >= 0 && index < static_cast<int>(std::size(App::kSmartLenses))) {
            Choose(App::kSmartLenses[static_cast<std::size_t>(index)]);
        }
    });

    m_footerRule = Add<Ui::Rule>();
    m_footer = Add<Ui::Slate>();
    m_account = m_footer->Add<Ui::Label>(L"", Ui::Style::Caption);
    m_account->UseSecondary();

    m_syncButton = Add<Ui::Button>(L"Sincronizar", Ui::ButtonKind::Secondary);
    m_syncButton->OnActivate([this] {
        if (m_sync) m_sync();
    });

    m_signOutButton = Add<Ui::Button>(L"Cerrar sesión", Ui::ButtonKind::Plain);
    m_signOutButton->SetEnabled(false);
    m_signOutButton->OnActivate([this] {
        if (m_signOut) m_signOut();
    });

    // Los ajustes, al lado de la cuenta. Un icono y no un botón con texto: lo que hay
    // dentro son cuatro cosas que se usan una vez al año, y un botón ancho al lado de
    // «Sincronizar» pesaría lo mismo que él sin merecerlo.
    m_settingsButton = Add<Ui::IconButton>(kGlyphMore, Ui::ButtonKind::Plain);
    m_settingsButton->OnActivate([this] {
        if (m_settings == nullptr || !Attached()) return;
        const Rect anchor = m_settingsButton->WindowRect();
        m_settings(anchor.x, anchor.Bottom() + 4.0f);
    });

    // El separador de la derecha va el ÚLTIMO: los hijos se insertan arriba, así que el
    // último añadido es el que se pinta encima, y una línea que quede debajo de la columna
    // no se ve.
    m_separator = Add<Ui::Rule>();
    return true;
}

void Sidebar::OnArrange() {
    const float width = Frame().width;
    const float inner = std::max(width - kPad * 2.0f, 1.0f);
    const float hairline = Metrics::Hairline(Attached() ? HostRef().Scale() : 1.0f);

    if (m_separator) m_separator->SetFrame(Rect{width - hairline, 0.0f, hairline, Frame().height});

    float y = kTop;
    if (m_priority) {
        m_priority->SetFrame(Rect{kPad, y, inner, m_priority->PreferredHeight()});
        y += m_priority->PreferredHeight() + Metrics::kSpace4;
    }
    if (m_smart) m_smart->SetFrame(Rect{kPad, y, inner, m_smart->PreferredHeight()});

    // El pie va anclado abajo: lo de la cuenta no se mueve al cambiar de vista, y no puede
    // depender de cuántos grupos haya por encima.
    const float footerTop = std::max(Frame().height - kFooterHeight, y + Metrics::kSpace4);
    if (m_footerRule) m_footerRule->SetFrame(Rect{kPad, footerTop, inner, hairline});
    constexpr float kSettingsSize = 24.0f;
    if (m_footer) {
        const float accountWidth = std::max(inner - kSettingsSize - Metrics::kSpace1, 1.0f);
        m_footer->SetFrame(Rect{kPad, footerTop + Metrics::kSpace2, accountWidth, 20.0f});
        if (m_account) m_account->SetFrame(Rect{Metrics::kSpace1, 0.0f, accountWidth, 20.0f});
    }
    if (m_settingsButton) {
        m_settingsButton->SetFrame(Rect{width - kPad - kSettingsSize, footerTop + 10.0f,
                                        kSettingsSize, kSettingsSize});
    }
    if (m_syncButton) {
        m_syncButton->SetFrame(Rect{kPad, footerTop + 40.0f, inner, Metrics::kControlHeight});
    }
    if (m_signOutButton) {
        m_signOutButton->SetFrame(
            Rect{kPad, footerTop + 40.0f + Metrics::kControlHeight + Metrics::kSpace1, inner,
                 Metrics::kControlHeight});
    }
}

void Sidebar::OnTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    if (Gfx::Material* material = MaterialOf()) {
        material->SetColor(tokens.sidebarVeil, HostRef().Animator(), crossfadeMs);
    }
    Ui::Element::OnTheme(tokens, crossfadeMs);
}

void Sidebar::Choose(App::Lens lens) {
    if (m_selected == lens) return;
    m_selected = lens;
    // El otro grupo apaga su píldora. La selección de la barra lateral es una sola.
    const bool inPriority =
        std::find(std::begin(App::kPriorityLenses), std::end(App::kPriorityLenses), lens) !=
        std::end(App::kPriorityLenses);
    if (inPriority) {
        if (m_smart) m_smart->Deselect();
    } else if (m_priority) {
        m_priority->Deselect();
    }
    if (m_lens) m_lens(lens);
}

void Sidebar::Select(App::Lens lens) {
    // Antes de OnAttach no hay grupos, y Views::Main llama a Bind() —que llama aquí— en
    // cuanto tiene el estado. Un puntero nulo por aquí sería el fallo más tonto de la fase.
    if (m_priority == nullptr || m_smart == nullptr) return;

    m_selected = lens;
    for (std::size_t i = 0; i < std::size(App::kPriorityLenses); ++i) {
        if (App::kPriorityLenses[i] != lens) continue;
        m_priority->Select(static_cast<int>(i), true);
        m_smart->Deselect();
        return;
    }
    for (std::size_t i = 0; i < std::size(App::kSmartLenses); ++i) {
        if (App::kSmartLenses[i] != lens) continue;
        m_smart->Select(static_cast<int>(i), true);
        m_priority->Deselect();
        return;
    }
}

void Sidebar::SetCounts(const App::State& state) {
    if (m_priority == nullptr || m_smart == nullptr) return;

    for (std::size_t i = 0; i < std::size(App::kPriorityLenses); ++i) {
        if (Ui::SidebarItem* item = m_priority->ItemAt(static_cast<int>(i))) {
            item->SetCount(state.CountOf(App::kPriorityLenses[i]));
        }
    }
    for (std::size_t i = 0; i < std::size(App::kSmartLenses); ++i) {
        if (Ui::SidebarItem* item = m_smart->ItemAt(static_cast<int>(i))) {
            item->SetCount(state.CountOf(App::kSmartLenses[i]));
        }
    }
}

void Sidebar::SetAccount(const std::wstring& account) {
    m_hasAccount = !account.empty();
    if (m_account) {
        m_account->SetText(m_hasAccount ? account : std::wstring(L"Sin cuenta conectada"));
    }
    if (m_signOutButton) m_signOutButton->SetEnabled(m_hasAccount && !m_syncing);
}

void Sidebar::SetSyncing(bool running) {
    if (m_syncing == running) return;
    m_syncing = running;
    if (m_syncButton) {
        // Sincronizar dos veces a la vez no rompe nada —Github::Sync lo ignora— pero un
        // botón que se puede pulsar y no hace nada es peor que uno apagado.
        m_syncButton->SetEnabled(!running);
        m_syncButton->SetLabel(running ? L"Sincronizando…" : L"Sincronizar");
    }
    if (m_signOutButton) m_signOutButton->SetEnabled(m_hasAccount && !running);
}

}  // namespace Views
