#include "views/Status.h"

#include <algorithm>

#include "shell/Caption.h"
#include "ui/Controls.h"
#include "ui/Host.h"
#include "ui/Overlays.h"

namespace Views {
namespace {

using Ui::Rect;

constexpr float kWidth = 420.0f;
constexpr float kPad = Metrics::kSpace4;
constexpr float kGap = Metrics::kSpace2;
constexpr float kLine = 22.0f;

}  // namespace

bool Status::OnAttach() {
    m_panel = Add<Ui::Panel>(Ui::Panel::Surface::Sheet, Metrics::Radius::Panel,
                             Metrics::kElevationMenu);

    // El número, grande y con cifras tabulares: es lo que se mira mientras sube.
    m_count = m_panel->Add<Ui::Label>(L"Sin sincronizar", Ui::Style::Title, Ui::Weight::Semibold);
    m_count->SetFigures(Ui::Figures::Tabular);
    m_count->SetAlign(Ui::Align::Center);

    m_account = m_panel->Add<Ui::Label>(L"", Ui::Style::Body);
    m_account->UseSecondary();
    m_account->SetAlign(Ui::Align::Center);

    m_stage = m_panel->Add<Ui::Label>(L"", Ui::Style::Body);
    m_stage->SetAlign(Ui::Align::Center);

    m_detail = m_panel->Add<Ui::Label>(L"", Ui::Style::Caption);
    m_detail->SetFigures(Ui::Figures::Tabular);
    m_detail->UseSecondary();
    m_detail->SetAlign(Ui::Align::Center);

    m_problem = m_panel->Add<Ui::Label>(L"", Ui::Style::Caption);
    m_problem->SetAlign(Ui::Align::Center);
    m_problem->SetVisible(false);

    m_note = m_panel->Add<Ui::Label>(L"", Ui::Style::Footnote);
    m_note->UseSecondary();
    m_note->SetAlign(Ui::Align::Center);
    m_note->SetVisible(false);

    m_syncButton = m_panel->Add<Ui::Button>(L"Sincronizar", Ui::ButtonKind::Primary);
    m_syncButton->OnActivate([this] {
        if (m_sync) m_sync();
    });

    m_signOutButton = m_panel->Add<Ui::Button>(L"Cerrar sesión", Ui::ButtonKind::Secondary);
    m_signOutButton->OnActivate([this] {
        if (m_signOut) m_signOut();
    });
    return true;
}

void Status::OnArrange() {
    if (m_panel == nullptr) return;

    const float width = std::min(kWidth, std::max(Frame().width - Metrics::kSpace5 * 2.0f, 240.0f));
    const float inner = width - kPad * 2.0f;

    float y = kPad;
    const auto place = [&](Ui::Element* element, float height, float after) {
        if (element == nullptr || !element->Visible()) return;
        element->SetFrame(Rect{kPad, y, inner, height});
        y += height + after;
    };

    place(m_count, 34.0f, 4.0f);
    place(m_account, kLine, Metrics::kSpace3);
    place(m_stage, kLine, 4.0f);
    place(m_detail, kLine, kGap);
    place(m_problem, kLine * 2.0f, kGap);
    place(m_note, kLine, kGap);

    const float buttonsTop = y + Metrics::kSpace1;
    const float height = buttonsTop + Metrics::kControlHeight + kPad;

    // Centrado en el hueco que queda bajo la barra de título, no en la ventana entera: con
    // la barra de por medio, centrar en la ventana deja el panel un poco alto.
    const float top = Caption::kBarHeight;
    const float available = std::max(Frame().height - top, height);
    m_panel->SetFrame(Rect{(Frame().width - width) * 0.5f,
                           top + (available - height) * 0.5f, width, height});

    constexpr float kButton = 130.0f;
    if (m_syncButton) {
        m_syncButton->SetFrame(
            Rect{width * 0.5f - kButton - kGap * 0.5f, buttonsTop, kButton,
                 Metrics::kControlHeight});
    }
    if (m_signOutButton) {
        m_signOutButton->SetFrame(
            Rect{width * 0.5f + kGap * 0.5f, buttonsTop, kButton, Metrics::kControlHeight});
    }
}

void Status::Show(const Info& info) {
    if (m_panel == nullptr) return;

    if (info.repos > 0) {
        std::wstring count = std::to_wstring(info.repos);
        count += info.repos == 1 ? L" repositorio" : L" repositorios";
        m_count->SetText(count);
    } else {
        m_count->SetText(L"Sin sincronizar");
    }

    m_account->SetText(info.account.empty() ? L"Ninguna cuenta conectada" : info.account);
    m_stage->SetText(info.stage);

    // El progreso del segundo pase. Cuando no hay nada que enriquecer se dice, porque ese
    // es justo el criterio de aceptación de lo incremental y conviene verlo.
    std::wstring detail;
    if (info.running && info.toEnrich > 0) {
        detail = std::to_wstring(info.done) + L" / " + std::to_wstring(info.toEnrich) +
                 L" con detalle";
    } else if (!info.lastSync.empty()) {
        detail = L"Sincronizado " + info.lastSync;
        if (info.gone > 0) {
            detail += L" · " + std::to_wstring(info.gone) + L" ya no están en la cuenta";
        }
    }
    m_detail->SetText(detail);

    m_problem->SetText(info.problem);
    m_problem->SetVisible(!info.problem.empty());

    m_note->SetText(info.contentsForbidden
                        ? L"El token no puede leer contenidos: sin PROYECTO.md"
                        : L"");
    m_note->SetVisible(info.contentsForbidden);

    // Mientras sincroniza, los dos botones se apagan. Sincronizar dos veces a la vez no
    // rompe nada —Sync lo ignora— pero un botón que se puede pulsar y no hace nada es peor
    // que uno apagado.
    m_syncButton->SetEnabled(!info.running);
    m_syncButton->SetLabel(info.running ? L"Sincronizando…" : L"Sincronizar");
    m_signOutButton->SetEnabled(!info.running && !info.account.empty());

    Relayout();
}

}  // namespace Views
