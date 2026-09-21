#include "views/Welcome.h"

#include <Windows.h>

#include <ShellApi.h>

#include <algorithm>

#include "ui/Controls.h"
#include "ui/Field.h"
#include "ui/Host.h"
#include "ui/Overlays.h"

namespace Views {
namespace {

using Ui::Rect;

constexpr float kWidth = 460.0f;
constexpr float kPad = Metrics::kSpace4;
constexpr float kLine = 22.0f;
constexpr float kGap = Metrics::kSpace2;

// La página de GitHub donde se crea una credencial de grano fino. El único enlace externo
// de toda la aplicación, y va a github.com, que es el único destino que permite la regla 4
// de SEGURIDAD.md.
constexpr const wchar_t* kNewCredentialUrl =
    L"https://github.com/settings/personal-access-tokens/new";

}  // namespace

bool Welcome::OnAttach() {
    m_panel = Add<Ui::Panel>(Ui::Panel::Surface::Sheet, Metrics::Radius::Sheet,
                             Metrics::kElevationSheet);

    m_title = m_panel->Add<Ui::Label>(L"Conecta tu cuenta de GitHub", Ui::Style::Heading,
                                      Ui::Weight::Semibold);

    m_intro = m_panel->Add<Ui::Label>(
        L"Brújula lee tus repositorios para ayudarte a decidir en qué trabajar. "
        L"Hace falta un token de acceso personal con dos permisos:",
        Ui::Style::Body);
    m_intro->UseSecondary();

    // Los permisos, uno por línea y con el para qué al lado. Enseñarlos antes de pedirlos es
    // la regla 6 de SEGURIDAD.md, y decir para qué sirve cada uno es lo que la hace útil.
    m_metadata = m_panel->Add<Ui::Label>(L"Metadata: lectura — nombres, fechas y actividad",
                                         Ui::Style::Body);
    m_contents = m_panel->Add<Ui::Label>(L"Contents: lectura — leer el PROYECTO.md de cada repo",
                                         Ui::Style::Body);

    m_write = m_panel->Add<Ui::Label>(
        L"Escribir en tus repositorios es otra cosa y se pide aparte: solo si algún día "
        L"activas el modo repo, y confirmando repositorio por repositorio.",
        Ui::Style::Caption);
    m_write->UseSecondary();

    m_open = m_panel->Add<Ui::Button>(L"Crear el token en GitHub", Ui::ButtonKind::Secondary);
    m_open->OnActivate([] {
        // Se abre en el navegador del sistema. Brújula no trae navegador ni lo quiere.
        ShellExecuteW(nullptr, L"open", kNewCredentialUrl, nullptr, nullptr, SW_SHOWNORMAL);
    });

    m_field = m_panel->Add<Ui::Field>(L"Pega aquí el token");
    // Sin historial de deshacer: un campo normal se quedaría con una copia de lo pegado.
    m_field->SetSecret(true);
    m_field->SetMaxLength(255);
    m_field->OnSubmit([this] { Accept(); });

    // Sin color propio, y a propósito: la tabla de CLAUDE.md no tiene un token de error, y
    // el naranja de Enfoque significa otra cosa. Todo lo que hay alrededor es texto
    // secundario, así que dejar esta línea en primario ya la destaca sin inventarse un
    // token para una hoja que la fase 5 va a rehacer.
    m_problem = m_panel->Add<Ui::Label>(L"", Ui::Style::Caption);
    m_problem->SetVisible(false);

    m_accept = m_panel->Add<Ui::Button>(L"Conectar", Ui::ButtonKind::Primary);
    m_accept->OnActivate([this] { Accept(); });

    m_later = m_panel->Add<Ui::Button>(L"Ahora no", Ui::ButtonKind::Plain);
    m_later->OnActivate([this] {
        if (m_dismiss) m_dismiss();
        HostRef().PopLayer(this);
    });
    return true;
}

void Welcome::OnArrange() {
    if (m_panel == nullptr) return;

    const float width = std::min(kWidth, std::max(Frame().width - Metrics::kSpace5 * 2.0f, 260.0f));
    const float inner = width - kPad * 2.0f;

    // La altura sale del contenido y no de un número fijo: el texto de los permisos ocupa
    // tres líneas en una ventana ancha y más en una estrecha, y una hoja con el botón fuera
    // del panel es peor que una hoja alta.
    float y = kPad;
    const auto place = [&](Ui::Element* element, float height, float after) {
        if (element == nullptr || !element->Visible()) return;
        element->SetFrame(Rect{kPad, y, inner, height});
        y += height + after;
    };

    place(m_title, 30.0f, kGap);
    place(m_intro, kLine * 2.0f, kGap);
    place(m_metadata, kLine, 4.0f);
    place(m_contents, kLine, kGap);
    place(m_write, kLine * 2.0f, Metrics::kSpace3);
    place(m_open, Metrics::kControlHeight, Metrics::kSpace3);
    place(m_field, Metrics::kControlHeight, kGap);
    place(m_problem, kLine, kGap);

    const float buttonsTop = y + Metrics::kSpace1;
    const float height = buttonsTop + Metrics::kControlHeight + kPad;

    const float x = (Frame().width - width) * 0.5f;
    const float top = std::max((Frame().height - height) * 0.5f, Metrics::kSpace4);
    m_panelRect = Rect{x, top, width, height};
    m_panel->SetFrame(m_panelRect);

    constexpr float kAcceptWidth = 120.0f;
    constexpr float kLaterWidth = 100.0f;
    if (m_accept) {
        m_accept->SetFrame(Rect{width - kPad - kAcceptWidth, buttonsTop, kAcceptWidth,
                                Metrics::kControlHeight});
    }
    if (m_later) {
        m_later->SetFrame(Rect{width - kPad - kAcceptWidth - kGap - kLaterWidth, buttonsTop,
                               kLaterWidth, Metrics::kControlHeight});
    }

    // Expresivo, que es el muelle que la tabla de CLAUDE.md le pide a una hoja modal.
    m_panel->Appear(Motion::Kind::Expressive);
}

bool Welcome::HitTest(float lx, float ly) const {
    return m_panelRect.Contains(lx, ly);
}

void Welcome::ShowProblem(const std::wstring& message) {
    if (m_problem == nullptr) return;
    m_problem->SetText(message);
    m_problem->SetVisible(!message.empty());
    Relayout();
}

void Welcome::Accept() {
    if (m_field == nullptr) return;

    // Se copia, se entrega y se vacía el campo en el mismo sitio. SetText(L"") además vacía
    // el historial, que con SetSecret ya estaba apagado: las dos cosas por si alguien quita
    // una de ellas más adelante.
    std::wstring pasted = m_field->Text();
    if (pasted.empty()) {
        ShowProblem(L"Pega el token antes de conectar.");
        return;
    }

    if (m_connect) m_connect(pasted);
    SecureZeroMemory(pasted.data(), pasted.size() * sizeof(wchar_t));
    m_field->SetText(std::wstring());
}

}  // namespace Views
