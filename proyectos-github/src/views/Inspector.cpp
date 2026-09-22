#include "views/Inspector.h"

#include <Windows.h>

#include <algorithm>

#include "app/State.h"
#include "compositor/Paint.h"
#include "model/Utf.h"
#include "ui/Controls.h"
#include "ui/Field.h"
#include "ui/Host.h"
#include "ui/List.h"
#include "ui/Overlays.h"
#include "ui/Text.h"

namespace Views {
namespace {

using Ui::Rect;

constexpr float kPad = Metrics::kSpace3;
constexpr float kLine = 18.0f;
constexpr float kTitleLine = 28.0f;
constexpr float kIcon = 28.0f;
constexpr float kNovedadHeight = 48.0f;
constexpr float kMinList = 72.0f;
constexpr int kMaxCommits = 5;
constexpr int kMaxMarkdownButtons = 3;

// Segoe Fluent Icons, por número: en el editor son un hueco en blanco y cualquiera los
// borraría sin verlos.
constexpr wchar_t kGlyphClose[] = {0xE711, 0};  // Cancel
constexpr wchar_t kGlyphAdd[] = {0xE710, 0};    // Add

D2D1_RECT_F ToBox(const Rect& rect) {
    return D2D1::RectF(rect.x, rect.y, rect.Right(), rect.Bottom());
}

void Line(const Ui::Paint& paint, std::wstring_view text, const Rect& box, Ui::Style style,
          Ui::Weight weight, Theme::Color color, bool wrap) {
    if (text.empty() || box.width <= 0.0f) return;
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    paint.dc->CreateSolidColorBrush(Gfx::ToD2D(color), brush.put());

    Ui::Run run;
    run.text = text;
    run.style = style;
    run.weight = weight;
    run.wrap = wrap;
    run.figures = Ui::Figures::Tabular;
    paint.text->Draw(paint.dc, run, ToBox(box), brush.get());
}

std::wstring Plural(int count, const wchar_t* one, const wchar_t* many) {
    return std::to_wstring(count) + L" " + (count == 1 ? one : many);
}

// «3 issues · 1 PR abierto». Cero no se escribe: un renglón que dice que no hay nada de dos
// cosas ocupa lo mismo que uno que dice algo.
std::wstring CountsLine(int issues, int prs) {
    std::wstring text;
    if (issues > 0) text = Plural(issues, L"issue abierta", L"issues abiertas");
    if (prs > 0) {
        if (!text.empty()) text += L" · ";
        text += Plural(prs, L"PR abierto", L"PR abiertos");
    }
    return text.empty() ? std::wstring(L"Sin issues ni PR abiertos") : text;
}

std::wstring CommitLine(const Model::Commit& commit) {
    // El oid recortado a siete, que es como lo escribe git y como se busca en GitHub.
    std::wstring text = commit.oid.substr(0, std::min<std::size_t>(commit.oid.size(), 7));
    text += L" · ";
    text += commit.title;
    return text;
}

}  // namespace

// --------------------------------------------------------------------------- Montaje --

bool Inspector::OnAttach() {
    if (!CreateMaterial(Metrics::RadiusOf(Metrics::Radius::Panel))) return false;
    // Un canto fino: sobre la Mica, y sin sombra, es lo único que separa el panel del fondo.
    MaterialOf()->CreateStroke(1.0f);
    // Durante el viaje desde la tarjeta el contenido ya está maquetado para el tamaño final,
    // así que sin recortar asomaría por fuera de la forma pequeña. Ver Element::MorphTo.
    SetClipsChildren(true);

    // La pizarra va PRIMERA y por eso queda debajo de todo lo demás. Es donde se pinta el
    // texto que no recibe entrada, en una sola textura para el panel entero.
    m_body = Add<Ui::Slate>();

    m_name = m_body->Add<Ui::Label>(L"", Ui::Style::Heading, Ui::Weight::Semibold);
    m_where = m_body->Add<Ui::Label>(L"", Ui::Style::Caption);
    m_where->UseSecondary();
    m_where->SetFigures(Ui::Figures::Tabular);

    m_priorityTitle = m_body->Add<Ui::Label>(L"Prioridad", Ui::Style::Footnote);
    m_priorityTitle->UseSecondary();
    m_stateTitle = m_body->Add<Ui::Label>(L"Estado", Ui::Style::Footnote);
    m_stateTitle->UseSecondary();

    m_stepTitle = m_body->Add<Ui::Label>(L"Siguiente paso", Ui::Style::Footnote);
    m_stepTitle->UseSecondary();
    m_novTitle = m_body->Add<Ui::Label>(L"Novedades", Ui::Style::Footnote);
    m_novTitle->UseSecondary();
    m_commitsTitle = m_body->Add<Ui::Label>(L"Últimos commits", Ui::Style::Footnote);
    m_commitsTitle->UseSecondary();
    m_counts = m_body->Add<Ui::Label>(L"", Ui::Style::Caption);
    m_counts->UseSecondary();
    m_proyTitle = m_body->Add<Ui::Label>(L"PROYECTO.md", Ui::Style::Footnote);
    m_proyTitle->UseSecondary();
    m_proyState = m_body->Add<Ui::Label>(L"", Ui::Style::Caption);
    m_proyState->UseSecondary();

    for (Ui::Label*& label : m_commitLines) {
        label = m_body->Add<Ui::Label>(L"", Ui::Style::Caption);
        label->SetFigures(Ui::Figures::Tabular);
        label->UseSecondary();
    }

    for (Ui::Rule*& rule : m_rules) rule = Add<Ui::Rule>();

    m_closeButton = Add<Ui::IconButton>(kGlyphClose, Ui::ButtonKind::Plain);
    m_closeButton->OnActivate([this] {
        if (m_close) m_close();
    });

    m_priorityButton = Add<Ui::Button>(L"", Ui::ButtonKind::Secondary);
    m_priorityButton->OnActivate([this] { ShowPriorityMenu(); });
    m_stateButton = Add<Ui::Button>(L"", Ui::ButtonKind::Secondary);
    m_stateButton->OnActivate([this] { ShowStateMenu(); });

    m_step = Add<Ui::Field>(L"¿Qué es lo siguiente?");
    // Enter SUELTA el foco y no guarda: guardar es del OnBlur, y así hay un solo camino.
    // Con dos, uno de los dos se olvida —y el que se olvida siempre es el de perder el foco,
    // que es la mitad de las veces que alguien termina de escribir—. De paso, después de
    // Enter las teclas vuelven a ser atajos en vez de letras.
    m_step->OnSubmit([this] { ReleaseFocus(); });
    m_step->OnBlur([this] { CommitNextStep(); });

    m_addButton = Add<Ui::IconButton>(kGlyphAdd, Ui::ButtonKind::Plain);
    m_addButton->OnActivate([this] { BeginNovedad(); });

    m_newNovedad = Add<Ui::Field>(L"Qué ha pasado hoy");
    m_newNovedad->SetVisible(false);
    m_newNovedad->OnSubmit([this] { ReleaseFocus(); });
    m_newNovedad->OnBlur([this] { CommitNovedad(); });

    m_novedades = Add<Ui::List>();
    m_novedades->SetRowHeight(kNovedadHeight);
    m_novedades->SetPadding(0.0f, 0.0f);
    m_novedades->SetRowPainter(
        [this](const Ui::Paint& paint, const Rect& box, const Ui::List::RowState& state) {
            PaintNovedad(paint, box, state.index, state.hovered, state.selected);
        });

    m_importButton = Add<Ui::Button>(L"Importar a Brújula", Ui::ButtonKind::Secondary);
    m_importButton->OnActivate([this] {
        if (m_import) m_import();
    });
    m_repoModeButton = Add<Ui::Button>(L"", Ui::ButtonKind::Plain);
    m_repoModeButton->OnActivate([this] {
        if (m_repoMode) m_repoMode(!m_content.local.repoMode);
    });

    for (int i = 0; i < kMaxMarkdownButtons; ++i) {
        m_markdown[i] = Add<Ui::Button>(L"", Ui::ButtonKind::Plain);
        m_markdown[i]->SetVisible(false);
        m_markdown[i]->OnActivate([this, i] {
            const int visible = VisibleMarkdown();
            const bool isMore = static_cast<int>(m_content.rootMarkdown.size()) > visible &&
                                i == visible - 1;
            if (isMore) {
                ShowMoreMarkdown();
                return;
            }
            if (i < static_cast<int>(m_content.rootMarkdown.size()) && m_readFile) {
                m_readFile(m_content.rootMarkdown[static_cast<std::size_t>(i)]);
            }
        });
    }

    m_openGitHubButton = Add<Ui::Button>(L"Abrir en GitHub", Ui::ButtonKind::Secondary);
    m_openGitHubButton->OnActivate([this] {
        if (m_openGitHub) m_openGitHub();
    });
    m_folderButton = Add<Ui::Button>(L"", Ui::ButtonKind::Secondary);
    m_folderButton->OnActivate([this] {
        if (m_openFolder) m_openFolder();
    });
    return true;
}

void Inspector::OnTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    if (Gfx::Material* material = MaterialOf()) {
        material->SetColor(tokens.cardSurface, HostRef().Animator(), crossfadeMs);
        material->SetStroke(tokens.controlStroke, HostRef().Animator(), crossfadeMs);
    }
    Ui::Element::OnTheme(tokens, crossfadeMs);
}

// -------------------------------------------------------------------------- Contenido --

void Inspector::Bind(const Content& content) {
    const bool sameRepo = m_content.repoId == content.repoId;
    m_content = content;
    // Cambiar de repositorio cierra lo que se estuviera escribiendo: la novedad a medias era
    // del otro, y dejarla abierta la guardaría en el que no es.
    if (!sameRepo && m_newNovedad) {
        m_newNovedad->SetText(std::wstring());
        m_newNovedad->SetVisible(false);
    }
    Rebuild();
}

void Inspector::Rebuild() {
    if (!Attached()) return;

    m_name->SetText(m_content.name);

    std::wstring where = m_content.nameWithOwner;
    where += L" · ";
    where += Ui::NameOf(m_content.activity);
    if (m_content.daysSincePush.has_value()) {
        where += L" · " + App::AgoDays(*m_content.daysSincePush);
    }
    if (m_content.gone) where += L" · ya no está en la cuenta";
    m_where->SetText(where);

    m_priorityButton->SetLabel(Ui::NameOf(m_content.local.priority));
    m_stateButton->SetLabel(Ui::NameOf(m_content.local.state));
    // SetText no dispara OnChanged —el campo avisa de lo que escribe el usuario, no de lo que
    // le escriben—, así que esto no se guarda a sí mismo en bucle.
    m_step->SetText(m_content.local.nextStep);

    m_novTitle->SetText(m_content.novedades.empty()
                            ? std::wstring(L"Novedades")
                            : L"Novedades · " + std::to_wstring(m_content.novedades.size()));

    std::vector<std::uint64_t> keys;
    keys.reserve(m_content.novedades.size());
    for (const Model::Novedad& novedad : m_content.novedades) {
        keys.push_back(static_cast<std::uint64_t>(novedad.id));
    }
    m_novedades->Update(std::move(keys), true);

    for (int i = 0; i < kMaxCommits; ++i) {
        const bool has = i < static_cast<int>(m_content.commits.size());
        m_commitLines[i]->SetText(
            has ? CommitLine(m_content.commits[static_cast<std::size_t>(i)]) : std::wstring());
        m_commitLines[i]->SetVisible(has);
    }
    m_counts->SetText(CountsLine(m_content.openIssues, m_content.openPrs));

    m_proyState->SetText(m_content.repoModeText);
    m_importButton->SetVisible(m_content.hasProyecto);
    m_repoModeButton->SetLabel(m_content.local.repoMode ? L"Dejar de escribir en el repo"
                                                        : L"Escribir en el repositorio");

    const int visible = VisibleMarkdown();
    for (int i = 0; i < kMaxMarkdownButtons; ++i) {
        const bool show = i < visible;
        m_markdown[i]->SetVisible(show);
        if (!show) continue;
        const int total = static_cast<int>(m_content.rootMarkdown.size());
        if (total > visible && i == visible - 1) {
            m_markdown[i]->SetLabel(L"Otros " + std::to_wstring(total - visible + 1) +
                                    L" archivos .md…");
        } else {
            m_markdown[i]->SetLabel(m_content.rootMarkdown[static_cast<std::size_t>(i)]);
        }
    }

    m_folderButton->SetLabel(m_content.folder.empty() ? L"Elegir carpeta…" : L"Abrir carpeta");
    // Recoloca lo de DENTRO y no el panel entero: el alto de la lista depende de cuántos
    // botones se enseñan, así que no basta con repintar — pero reescribir el marco propio
    // aquí plantaría el panel en su destino a mitad de la transición compartida, y esto se
    // llama en cada clic sobre una tarjeta, también mientras el panel todavía está creciendo.
    RelayoutContent();
}

int Inspector::VisibleMarkdown() const {
    const int total = static_cast<int>(m_content.rootMarkdown.size());
    if (total <= kMaxMarkdownButtons) return total;
    // Con más de los que caben, el último botón deja de ser un archivo y pasa a ser la
    // puerta al resto. Así el alto del panel no depende de cuántos .md tenga un repositorio.
    return kMaxMarkdownButtons;
}

void Inspector::PaintNovedad(const Ui::Paint& paint, const Rect& box, int index, bool hovered,
                             bool selected) {
    if (index < 0 || index >= static_cast<int>(m_content.novedades.size())) return;
    const Model::Novedad& novedad = m_content.novedades[static_cast<std::size_t>(index)];

    if (selected || hovered) {
        winrt::com_ptr<ID2D1SolidColorBrush> veil;
        paint.dc->CreateSolidColorBrush(
            Gfx::ToD2D(selected ? paint.tokens->selectionRow : paint.tokens->controlHover),
            veil.put());
        const float radius = Metrics::RadiusOf(Metrics::Radius::Control);
        paint.dc->FillRoundedRectangle(D2D1::RoundedRect(ToBox(box), radius, radius),
                                       veil.get());
    }

    const float left = box.x + 6.0f;
    const float width = std::max(box.width - 12.0f, 1.0f);
    const std::wstring day = novedad.day.empty() ? std::wstring(L"sin fecha")
                                                 : Model::ToWide(novedad.day);
    Line(paint, day, Rect{left, box.y + 4.0f, width, 14.0f}, Ui::Style::Footnote,
         Ui::Weight::Regular, paint.tokens->textSecondary, false);
    Line(paint, novedad.text, Rect{left, box.y + 19.0f, width, 26.0f}, Ui::Style::Caption,
         Ui::Weight::Regular, paint.tokens->textPrimary, true);
}

// ------------------------------------------------------------------------ Los mandos --

void Inspector::ShowPriorityMenu() {
    if (!Attached()) return;
    std::vector<Ui::Menu::Entry> entries;
    for (const Model::Priority priority :
         {Model::Priority::Focus, Model::Priority::Secondary, Model::Priority::Someday,
          Model::Priority::Archived, Model::Priority::Unsorted}) {
        entries.push_back({Ui::NameOf(priority), [this, priority] {
                               if (m_priorityChanged) m_priorityChanged(priority);
                           }});
    }
    const Rect anchor = m_priorityButton->WindowRect();
    HostRef().PushLayer<Ui::Menu>(Ui::Host::LayerOptions{false, true, false}, std::move(entries),
                                  anchor.x, anchor.Bottom() + 4.0f);
}

void Inspector::ShowStateMenu() {
    if (!Attached()) return;
    std::vector<Ui::Menu::Entry> entries;
    for (const Model::State state : {Model::State::Active, Model::State::Blocked,
                                     Model::State::Waiting, Model::State::Done}) {
        entries.push_back({Ui::NameOf(state), [this, state] {
                               if (m_stateChanged) m_stateChanged(state);
                           }});
    }
    const Rect anchor = m_stateButton->WindowRect();
    HostRef().PushLayer<Ui::Menu>(Ui::Host::LayerOptions{false, true, false}, std::move(entries),
                                  anchor.x, anchor.Bottom() + 4.0f);
}

void Inspector::ShowMoreMarkdown() {
    if (!Attached()) return;
    std::vector<Ui::Menu::Entry> entries;
    for (const std::wstring& name : m_content.rootMarkdown) {
        entries.push_back({name, [this, name] {
                               if (m_readFile) m_readFile(name);
                           }});
    }
    const Rect anchor = m_markdown[VisibleMarkdown() - 1]->WindowRect();
    HostRef().PushLayer<Ui::Menu>(Ui::Host::LayerOptions{false, true, false}, std::move(entries),
                                  anchor.x, anchor.Bottom() + 4.0f);
}

void Inspector::ReleaseFocus() {
    if (Attached()) HostRef().Input().Focus(nullptr, false);
}

void Inspector::CommitNextStep() {
    if (m_step == nullptr) return;
    if (m_step->Text() == m_content.local.nextStep) return;
    if (m_nextStep) m_nextStep(m_step->Text());
}

void Inspector::CommitNovedad() {
    if (m_newNovedad == nullptr) return;
    const std::wstring text = m_newNovedad->Text();
    m_newNovedad->SetText(std::wstring());
    m_newNovedad->SetVisible(false);
    Relayout();
    if (!text.empty() && m_addNovedad) m_addNovedad(text);
}

void Inspector::FocusNextStep() {
    if (m_step == nullptr || !Attached()) return;
    HostRef().Input().Focus(m_step, true);
}

void Inspector::BeginNovedad() {
    if (m_newNovedad == nullptr || !Attached()) return;
    m_newNovedad->SetVisible(true);
    Relayout();
    HostRef().Input().Focus(m_newNovedad, true);
}

bool Inspector::Editing() const {
    if (!Attached()) return false;
    const Ui::Element* focused = HostRef().Input().Focused();
    return focused == m_step || focused == m_newNovedad;
}

bool Inspector::CancelEditing() {
    if (!Attached()) return false;
    const Ui::Element* focused = HostRef().Input().Focused();

    if (focused == m_newNovedad) {
        m_newNovedad->SetText(std::wstring());
        m_newNovedad->SetVisible(false);
        Relayout();
        HostRef().Input().Focus(nullptr, false);
        return true;
    }
    if (focused == m_step) {
        // Se descarta lo escrito y vuelve lo que hay guardado. Guardarlo sería lo contrario
        // de lo que hace Esc en cualquier otro sitio.
        m_step->SetText(m_content.local.nextStep);
        HostRef().Input().Focus(nullptr, false);
        return true;
    }
    return false;
}

bool Inspector::OnKey(const Input::Key& e) {
    if (!e.down) return false;

    // Supr borra la novedad elegida. No hay aspa por fila porque una fila de Ui::List no es
    // un árbol de elementos: sería un visual más por cada novedad viva.
    if (e.virtualKey == VK_DELETE && Attached() &&
        HostRef().Input().Focused() == static_cast<Ui::Element*>(m_novedades)) {
        const int slot = m_novedades->Selected();
        if (slot >= 0 && slot < static_cast<int>(m_content.novedades.size()) && m_deleteNovedad) {
            m_deleteNovedad(m_content.novedades[static_cast<std::size_t>(slot)].id);
            return true;
        }
    }
    return false;
}

// --------------------------------------------------------------------- La maquetación --

void Inspector::OnArrange() {
    const float width = Frame().width;
    const float height = Frame().height;
    const float inner = std::max(width - kPad * 2.0f, 1.0f);
    if (m_body == nullptr) return;

    m_body->SetFrame(Rect{0.0f, 0.0f, width, height});

    // --- Lo de abajo, que está anclado y se mide primero ------------------------------
    const float footTop = height - kPad - Metrics::kControlHeight;
    const float half = std::max((inner - Metrics::kSpace1) * 0.5f, 1.0f);
    m_openGitHubButton->SetFrame(Rect{kPad, footTop, half, Metrics::kControlHeight});
    m_folderButton->SetFrame(
        Rect{kPad + half + Metrics::kSpace1, footTop, half, Metrics::kControlHeight});

    const int markdownCount = VisibleMarkdown();
    const float markdownHeight =
        markdownCount > 0 ? static_cast<float>(markdownCount) * (kIcon + 4.0f) : 0.0f;
    // PROYECTO.md: título, estado y la fila de botones.
    const float proyectoHeight = kLine + 4.0f + kLine + 6.0f + kIcon;
    const int commitCount = std::min(static_cast<int>(m_content.commits.size()), kMaxCommits);
    // El bloque de commits, con su separador: es lo único que se puede quitar, y por eso se
    // mide aparte.
    const float commitsBlock = Metrics::kSpace2 + 1.0f + Metrics::kSpace2 + kLine + 4.0f +
                               static_cast<float>(std::max(commitCount, 1)) * kLine +
                               Metrics::kSpace1 + kLine;
    const float belowAlways = Metrics::kSpace2 + 1.0f + Metrics::kSpace2 + proyectoHeight +
                              markdownHeight + Metrics::kSpace2 + Metrics::kControlHeight + kPad;

    // --- Lo de arriba, en orden -------------------------------------------------------
    float y = kPad;
    m_name->SetFrame(Rect{kPad, y, std::max(inner - kIcon - 4.0f, 1.0f), kTitleLine});
    m_closeButton->SetFrame(Rect{width - kPad - kIcon, y, kIcon, kIcon});
    y += kTitleLine + 2.0f;
    m_where->SetFrame(Rect{kPad, y, inner, kLine});
    y += kLine + Metrics::kSpace2;

    m_rules[0]->SetFrame(Rect{kPad, y, inner, 1.0f});
    y += 1.0f + Metrics::kSpace2;

    m_priorityTitle->SetFrame(Rect{kPad, y, half, 14.0f});
    m_stateTitle->SetFrame(Rect{kPad + half + Metrics::kSpace1, y, half, 14.0f});
    y += 16.0f;
    m_priorityButton->SetFrame(Rect{kPad, y, half, Metrics::kControlHeight});
    m_stateButton->SetFrame(
        Rect{kPad + half + Metrics::kSpace1, y, half, Metrics::kControlHeight});
    y += Metrics::kControlHeight + Metrics::kSpace2;

    m_stepTitle->SetFrame(Rect{kPad, y, inner, 14.0f});
    y += 16.0f;
    m_step->SetFrame(Rect{kPad, y, inner, Metrics::kControlHeight});
    y += Metrics::kControlHeight + Metrics::kSpace2;

    m_rules[1]->SetFrame(Rect{kPad, y, inner, 1.0f});
    y += 1.0f + Metrics::kSpace2;

    m_novTitle->SetFrame(Rect{kPad, y, std::max(inner - kIcon - 4.0f, 1.0f), 14.0f});
    m_addButton->SetFrame(Rect{width - kPad - kIcon, y - 7.0f, kIcon, kIcon});
    y += 18.0f;
    if (m_newNovedad->Visible()) {
        m_newNovedad->SetFrame(Rect{kPad, y, inner, Metrics::kControlHeight});
        y += Metrics::kControlHeight + 6.0f;
    }

    // Los commits son lo primero que se va cuando el panel no da de sí, y la decisión se
    // toma AQUÍ, antes de colocar nada de abajo. Decidirlo después obligaría a recolocar, y
    // una segunda vuelta cuya condición depende de la primera deja un panel justo en el
    // límite parpadeando entre las dos maquetaciones.
    m_tight = height - y - belowAlways - commitsBlock < kMinList;
    const float listHeight =
        std::max(height - y - belowAlways - (m_tight ? 0.0f : commitsBlock), kMinList);
    m_novedades->SetFrame(Rect{kPad, y, inner, listHeight});
    y += listHeight + Metrics::kSpace2;

    m_rules[2]->SetFrame(Rect{kPad, y, inner, 1.0f});
    y += 1.0f + Metrics::kSpace2;

    m_commitsTitle->SetVisible(!m_tight);
    m_counts->SetVisible(!m_tight);
    if (m_tight) {
        for (Ui::Label* label : m_commitLines) label->SetVisible(false);
    }

    if (!m_tight) {
        m_commitsTitle->SetFrame(Rect{kPad, y, inner, 14.0f});
        y += 18.0f;
        if (commitCount == 0) {
            // Un repositorio sin un solo commit: son justo los olvidados, y un hueco en
            // blanco no dice que estén vacíos, dice que algo no cargó.
            m_commitLines[0]->SetVisible(true);
            m_commitLines[0]->SetText(L"Sin commits todavía");
            m_commitLines[0]->SetFrame(Rect{kPad, y, inner, kLine});
            y += kLine;
        } else {
            for (int i = 0; i < commitCount; ++i) {
                m_commitLines[i]->SetFrame(Rect{kPad, y, inner, kLine});
                y += kLine;
            }
        }
        y += Metrics::kSpace1;
        m_counts->SetFrame(Rect{kPad, y, inner, kLine});
        y += kLine + Metrics::kSpace2;

        m_rules[3]->SetVisible(true);
        m_rules[3]->SetFrame(Rect{kPad, y, inner, 1.0f});
        y += 1.0f + Metrics::kSpace2;
    } else {
        m_rules[3]->SetVisible(false);
    }

    m_proyTitle->SetFrame(Rect{kPad, y, inner, 14.0f});
    y += 18.0f;
    m_proyState->SetFrame(Rect{kPad, y, inner, kLine});
    y += kLine + 6.0f;

    const float importWidth = m_content.hasProyecto ? half : 0.0f;
    if (m_content.hasProyecto) {
        m_importButton->SetFrame(Rect{kPad, y, importWidth, kIcon});
    }
    const float modeX = kPad + (m_content.hasProyecto ? importWidth + Metrics::kSpace1 : 0.0f);
    m_repoModeButton->SetFrame(
        Rect{modeX, y, std::max(width - kPad - modeX, 1.0f), kIcon});
    y += kIcon;

    for (int i = 0; i < markdownCount; ++i) {
        y += 4.0f;
        m_markdown[i]->SetFrame(Rect{kPad, y, inner, kIcon});
        y += kIcon;
    }
}

}  // namespace Views
