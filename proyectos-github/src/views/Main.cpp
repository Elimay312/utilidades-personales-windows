#include "views/Main.h"

#include <Windows.h>

#include <algorithm>
#include <optional>

#include "ui/Controls.h"
#include "ui/Host.h"
#include "views/RepoList.h"
#include "views/Sidebar.h"

namespace Views {

namespace {

using Ui::Rect;

// El inspector, METIDO del borde derecho y del de abajo, no pegado a ellos. Así el viaje
// desde una tarjeta es una forma que crece y ya está, y no una que además tiene que acabar
// con dos esquinas cuadradas contra el marco de la ventana.
constexpr float kInspectorWidth = 380.0f;
constexpr float kInspectorGap = Metrics::kSpace2;
// Por debajo de esto la columna de tarjetas deja de ser una lista y pasa a ser una columna
// de elipsis, así que quien cede ancho en una ventana estrecha es el inspector.
constexpr float kMinContentWidth = 320.0f;
// Lo que se espera antes de esconder el inspector al cerrarlo: lo que tarda el muelle
// estándar en asentarse, con un poco de margen. Sale de la tabla y no de un número escrito a
// mano, así que afinar el muelle en la fase 8 lo arrastra solo. Al disparar se comprueba que
// sigue cerrado, por si se reabrió entre medias.
int HideDelayMs() {
    return static_cast<int>(Motion::SettleMs(Motion::SpringFor(Motion::Kind::Standard))) + 80;
}

// Cuánto se aleja la lista cuando entra la revisión semanal. Poco: lo que tiene que decir
// es "esto sigue estando, detrás", y una lista que se va al treinta por ciento se lee como
// otra pantalla que llegó por un lado.
constexpr float kRecedeScale = 0.94f;

// Qué tecla produce la barra en la distribución de teclado de ahora mismo. En un teclado
// estadounidense es VK_OEM_2 a secas y en uno español es Mayús+7, así que preguntarlo es lo
// único que hace que el atajo "/" de CLAUDE.md exista en los dos. Con la tecla escrita a
// mano, el atajo funcionaría en la máquina de quien lo escribió y en ninguna otra.
bool IsSlash(const Input::Key& key) {
    const SHORT mapped = VkKeyScanW(L'/');
    if (mapped == -1) return false;
    if (key.virtualKey != (mapped & 0xFF)) return false;
    if (Input::Has(key.modifiers, Input::Modifiers::Control)) return false;
    if (Input::Has(key.modifiers, Input::Modifiers::Alt)) return false;
    const bool needsShift = ((mapped >> 8) & 1) != 0;
    return Input::Has(key.modifiers, Input::Modifiers::Shift) == needsShift;
}

bool IsNavigation(const Input::Key& key) {
    switch (key.virtualKey) {
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_HOME:
    case VK_END:
    case VK_RETURN:
    case 'J':
    case 'K':
        return true;
    default:
        return false;
    }
}

}  // namespace

bool Main::OnAttach() {
    // El respaldo de Windows 10, donde no hay Mica. En Windows 11 se queda a alfa cero y no
    // cuesta nada: crearlo siempre evita tener dos árboles distintos según el sistema.
    if (!CreateMaterial(0.0f)) return false;

    m_sidebar = Add<Sidebar>();
    m_content = Add<RepoList>();
    // Después de la lista y antes de la barra de título: por encima de las tarjetas y por
    // debajo de los botones de la ventana.
    m_inspector = Add<Inspector>();
    m_inspector->SetVisible(false);
    // La revisión tapa la ventana entera, así que va por encima de la lista y del panel —y
    // por debajo de la barra de título, que es lo único que no puede desaparecer: sin ella
    // no habría por dónde cerrar la ventana.
    m_review = Add<Review>();
    m_review->OnFinished([this] { EndReview(); });
    // El último, y por eso el de arriba: los botones de la ventana no pueden quedar debajo
    // de nada.
    m_chrome = Add<Chrome>();
    // Salvo esta, que va todavía más arriba: mientras se arrastra hasta la barra lateral
    // cruza por encima de la columna, del inspector y del propio título, y pasar por debajo
    // de cualquiera de ellos se vería como que la tarjeta se ha caído dentro.
    m_drag = Add<DragCard>();

    m_sidebar->OnLens([this](App::Lens lens) { ChooseLens(lens, true); });
    m_sidebar->OnSync([this] {
        if (m_syncRequested) m_syncRequested();
    });
    m_sidebar->OnSignOut([this] {
        if (m_signOutRequested) m_signOutRequested();
    });
    m_sidebar->OnSettings([this](float x, float y) {
        if (m_settings) m_settings(x, y);
    });

    m_content->OnQueryChanged([this](const std::wstring& query) {
        if (m_state == nullptr) return;
        m_state->SetQuery(query);
        m_content->Refresh(true);
    });
    m_content->OnLensRequested([this](App::Lens lens) { ChooseLens(lens, false); });
    m_content->OnSyncRequested([this] {
        if (m_syncRequested) m_syncRequested();
    });

    // Clic y Enter abren. Mover la selección con las flechas NO abre, pero si ya está
    // abierto lo reengancha al repositorio nuevo: es la conducta de Mail, y evita una
    // animación de ida y vuelta por cada pulsación de una flecha.
    m_content->OnClicked([this](int slot) { OpenInspector(slot); });
    m_content->OnActivated([this](int slot) { OpenInspector(slot); });
    m_content->OnSelectionChanged([this](int slot) {
        if (!m_inspectorOpen) return;
        if (slot < 0) {
            CloseInspector();
            return;
        }
        if (m_inspectorRequest) m_inspectorRequest(slot);
    });

    m_content->OnCardDragBegin(
        [this](int slot, float x, float y) { return BeginCardDrag(slot, x, y); });
    m_content->OnCardDragMove([this](float x, float y) { MoveCardDrag(x, y); });
    m_content->OnCardDrop([this](const Ui::List::Drop& drop) { FinishCardDrag(drop); });
    m_content->OnCardMenu([this](int slot, float x, float y) {
        if (m_cardMenu) m_cardMenu(slot, x, y);
    });

    m_inspector->OnClose([this] { CloseInspector(); });
    return true;
}

// ------------------------------------------------------------- La tarjeta levantada --

bool Main::BeginCardDrag(int slot, float x, float y) {
    if (m_state == nullptr || m_drag == nullptr || m_content == nullptr) return false;
    const App::Entry* entry = m_state->At(slot);
    if (entry == nullptr) return false;

    const Rect from = m_content->CardRect(slot);
    // Sin sitio de salida no hay arrastre. No debería pasar —se está tocando— pero una
    // tarjeta que sale de la nada tampoco sabría volver a ninguna parte.
    if (from.Empty()) return false;

    m_drag->Lift(*entry, m_content->Layout(), from);
    m_lifted = true;
    m_dragHome = from;
    m_grabX = x - from.x;
    m_grabY = y - from.y;
    return true;
}

void Main::MoveCardDrag(float x, float y) {
    if (!m_lifted || m_drag == nullptr) return;
    // Por donde se agarró: sin esto, la tarjeta pega un salto para centrarse bajo el puntero
    // en el primer movimiento y parece que se ha soltado sola.
    m_drag->MoveTo(x - m_grabX, y - m_grabY);
    if (m_sidebar) m_sidebar->SetDropTarget(m_sidebar->DropLensAt(x, y));
}

void Main::FinishCardDrag(const Ui::List::Drop& drop) {
    if (m_sidebar) m_sidebar->SetDropTarget(std::nullopt);
    if (!m_lifted) return;

    // 1. Sobre un grupo de la barra lateral: cambia de prioridad. Quien contesta es App, que
    //    es quien sabe del límite de Enfoque, y es también quien baja la tarjeta.
    if (m_sidebar && m_priorityRequested) {
        if (const std::optional<App::Lens> lens = m_sidebar->DropLensAt(drop.x, drop.y)) {
            if (const std::optional<Model::Priority> priority = App::PriorityOf(*lens)) {
                m_priorityRequested(drop.from, *priority);
                return;
            }
        }
    }

    // 2. Entre otras dos de la misma lista: se reordena y cae en su celda nueva.
    if (drop.inside && drop.to >= 0 && drop.to != drop.from && m_reorder && m_content) {
        m_reorder(drop.from, drop.to);
        DropCardAt(m_content->CardRect(drop.to));
        return;
    }

    // 3. Y en cualquier otro sitio —el aire, el inspector, fuera de la ventana— vuelve de
    //    donde salió. Es el criterio de aceptación de la fase: soltar fuera no deja nunca
    //    una tarjeta flotando.
    DropCardAt(m_dragHome);
}

void Main::DropCardAt(const Rect& target) {
    if (!m_lifted || m_drag == nullptr) return;
    m_lifted = false;
    const Rect landing = target.Empty() ? m_dragHome : target;
    if (landing.Empty()) {
        m_drag->HideNow();
        return;
    }
    m_drag->FlyTo(landing);
}

void Main::LiftCard(int slot) {
    // Durante la revisión no se levanta nada. Los caminos que cambian una prioridad son los
    // mismos —App::ApplyPriority llama a esto sin saber quién se lo pidió— y la tarjeta de
    // la lista saldría volando por encima de la pantalla de revisión, que es lo único que
    // se está mirando. La que vuela ahí es la de la pila, y la anima ella.
    if (Reviewing()) return;
    // Ya hay una en el aire: viene de un arrastre, y esa manda.
    if (m_lifted || m_state == nullptr || m_drag == nullptr || m_content == nullptr) return;
    const App::Entry* entry = m_state->At(slot);
    if (entry == nullptr) return;
    const Rect from = m_content->CardRect(slot);
    // La tarjeta no se ve —se eligió con el teclado y quedó fuera de la ventanilla, o la
    // búsqueda la dejó fuera—. Sin punto de partida no hay viaje, y el cambio se ve igual:
    // la lista lo anima al recargarse.
    if (from.Empty()) return;

    m_drag->Lift(*entry, m_content->Layout(), from);
    m_lifted = true;
    m_dragHome = from;
    m_grabX = 0.0f;
    m_grabY = 0.0f;
}

void Main::DropCardInto(Model::Priority priority) {
    if (!m_lifted || m_sidebar == nullptr) return;
    DropCardAt(m_sidebar->RectOf(App::LensOf(priority)));
}

void Main::CancelDrop() { DropCardAt(m_dragHome); }

void Main::RefuseDrop() {
    if (!m_lifted || m_drag == nullptr) return;
    m_lifted = false;
    m_drag->Refuse();
}

int Main::SelectedSlot() const { return m_content ? m_content->Selected() : -1; }

void Main::Reveal(int slot) {
    if (m_content == nullptr || m_state == nullptr || m_state->At(slot) == nullptr) return;
    m_content->SelectSlot(slot);
    OpenInspector(slot);
}

void Main::ShowAll() {
    if (m_content == nullptr) return;
    if (m_state && !m_state->Query().empty()) m_content->ClearSearch();
    ChooseLens(App::Lens::All, false);
}

void Main::Bind(App::State* state) {
    m_state = state;
    if (m_content) m_content->Bind(state);
    if (state && m_sidebar) m_sidebar->Select(state->CurrentLens());
}

void Main::SetMica(bool hasMica) {
    if (m_hasMica == hasMica) return;
    m_hasMica = hasMica;
    if (Attached()) OnTheme(Tokens(), 0.0f);
}

Rect Main::InspectorFrame() const {
    const float width = Frame().width;
    const float height = Frame().height;
    const float left = std::min(Sidebar::kWidth, width);
    // El inspector cede ancho antes que la lista: una columna de tarjetas por debajo de
    // kMinContentWidth es una columna de elipsis.
    const float panel =
        std::clamp(width - left - kMinContentWidth - kInspectorGap * 2.0f, 200.0f,
                   kInspectorWidth);
    const float top = Caption::kBarHeight + Metrics::kSpace1;
    return Rect{std::max(width - panel - kInspectorGap, left), top, panel,
                std::max(height - top - kInspectorGap, 1.0f)};
}

Rect Main::SelectedCardRect() const {
    if (m_content == nullptr) return Rect{};
    return m_content->SelectedCardRect();
}

void Main::LayoutColumns() {
    const float width = Frame().width;
    const float height = Frame().height;
    const float left = std::min(Sidebar::kWidth, width);

    if (m_sidebar) m_sidebar->SetFrame(Rect{0.0f, 0.0f, left, height});
    if (m_content) {
        const float reserved =
            m_inspectorOpen ? InspectorFrame().width + kInspectorGap * 2.0f : 0.0f;
        m_content->SetFrame(
            Rect{left, 0.0f, std::max(width - left - reserved, 1.0f), height});
    }
    if (m_chrome) m_chrome->SetFrame(Rect{0.0f, 0.0f, width, Caption::kBarHeight});
    // La ventana entera, esté corriendo o no: así al empezar ya tiene su tamaño y la
    // primera tarjeta entra desde donde tiene que entrar y no desde una esquina.
    if (m_review) m_review->SetFrame(Rect{0.0f, 0.0f, width, height});
}

// ------------------------------------------------------------- La revisión semanal --

void Main::BeginReview(std::vector<Review::Card> cards) {
    if (m_review == nullptr || cards.empty() || m_review->Running()) return;

    // El inspector se cierra ANTES: la revisión va a tapar la ventana entera, y un panel
    // abierto debajo se quedaría abierto al volver, con la columna estrecha y sin que nadie
    // recuerde haberlo dejado así.
    CloseInspector();
    // Y el foco se suelta: con el campo de búsqueda enfocado, el enrutador le daría las
    // teclas a él antes que a nadie y la revisión no vería ni un uno.
    if (Attached()) HostRef().Input().Focus(nullptr, false);

    Recede(true);
    m_review->Begin(std::move(cards));
}

void Main::EndReview() {
    Recede(false);
    if (m_content) m_content->FocusList();
}

void Main::Recede(bool away) {
    if (!Attached()) return;
    const float fade = HostRef().Animator().FadeMs(Motion::Kind::Expressive);
    Ui::Element* behind[] = {m_sidebar, m_content, m_inspector};
    for (Ui::Element* one : behind) {
        if (one == nullptr) continue;
        one->ScaleTo(away ? kRecedeScale : 1.0f, Motion::Kind::Expressive);
        one->SetOpacity(away ? 0.0f : 1.0f, fade);
    }
}

void Main::OnArrange() {
    LayoutColumns();
    // Al recolocar por un cambio de tamaño el inspector se pone donde le toca y ya está:
    // esto no es una transición, es la realidad nueva. Lo que viaja con muelle es abrir y
    // cerrar, y eso lo hacen OpenInspector y CloseInspector sin pasar por aquí.
    if (m_inspector && m_inspectorOpen) {
        m_inspector->SnapTo(InspectorFrame(), Metrics::RadiusOf(Metrics::Radius::Panel));
    }
}

// ---------------------------------------------------------------------- El inspector --

void Main::OpenInspector(int slot) {
    if (m_state == nullptr || m_inspector == nullptr) return;
    if (m_state->At(slot) == nullptr) return;

    if (m_inspectorOpen) {
        // Ya está abierto: no se vuelve a morfear, solo se reengancha.
        if (m_inspectorRequest) m_inspectorRequest(slot);
        return;
    }

    const Rect from = SelectedCardRect();
    m_inspectorOpen = true;
    m_inspector->SetVisible(true);
    m_inspector->SetOpacity(1.0f, 0.0f);

    // El contenido se pide ANTES de colocar nada: el alto de la lista de novedades depende
    // de cuántos botones lleve el panel, así que maquetar con el contenido viejo sería
    // maquetar dos veces.
    if (m_inspectorRequest) m_inspectorRequest(slot);

    if (from.Empty()) {
        // La tarjeta no se ve —se buscó y se fue de la lista, o la selección llegó por
        // teclado sin haberla traído a la vista—. Sin punto de partida no hay viaje: entra
        // donde le toca con un fundido corto.
        m_inspector->SnapTo(InspectorFrame(), Metrics::RadiusOf(Metrics::Radius::Panel));
        m_inspector->SetOpacity(0.0f, 0.0f);
        m_inspector->SetContentOpacity(1.0f, 0.0f);
        m_inspector->SetOpacity(1.0f, HostRef().Animator().FadeMs(Motion::Kind::Standard));
    } else {
        // Encima de la tarjeta y con SU radio: el material es del color de la tarjeta, así
        // que en el primer fotograma son la misma cosa. Lo que se cruza es el contenido.
        m_inspector->SnapTo(from, Metrics::RadiusOf(Metrics::Radius::Card));
        m_inspector->SetContentOpacity(0.0f, 0.0f);
        m_inspector->MorphTo(InspectorFrame(), Metrics::RadiusOf(Metrics::Radius::Panel),
                             Motion::Kind::Standard);
        m_inspector->SetContentOpacity(1.0f, HostRef().Animator().FadeMs(Motion::Kind::Standard));
    }

    // Y la columna se estrecha: el ancho de celda cambia de golpe y las posiciones se
    // deslizan, que es la decisión de la fase 4 sobre lista y cuadrícula aplicada aquí. El
    // aviso va ANTES de recolocar, porque quien coloca es la propia recolocación.
    if (m_content) m_content->ReflowCells();
    LayoutColumns();
}

void Main::CloseInspector() {
    if (!m_inspectorOpen || m_inspector == nullptr) return;
    m_inspectorOpen = false;

    // Primero la columna: las tarjetas vuelven a su sitio mientras el panel encoge, y no
    // después, que se leería como dos animaciones seguidas en vez de una.
    if (m_content) m_content->ReflowCells();
    LayoutColumns();

    const Rect back = SelectedCardRect();
    m_inspector->SetContentOpacity(0.0f, HostRef().Animator().FadeMs(Motion::Kind::Standard));
    if (back.Empty()) {
        m_inspector->SetOpacity(0.0f, HostRef().Animator().FadeMs(Motion::Kind::Standard));
    } else {
        m_inspector->MorphTo(back, Metrics::RadiusOf(Metrics::Radius::Card),
                             Motion::Kind::Standard);
    }

    if (!m_hideInspector && Attached()) {
        if (const auto queue = HostRef().Queue()) {
            m_hideInspector = queue.CreateTimer();
            m_hideInspector.Interval(std::chrono::milliseconds(HideDelayMs()));
            m_hideInspector.IsRepeating(false);
            m_hideInspector.Tick([this](auto&&, auto&&) {
                // Se comprueba otra vez: entre el muelle y el temporizador puede haberse
                // vuelto a abrir, y esconderlo entonces lo dejaría invisible y ocupando sitio.
                if (!m_inspectorOpen && m_inspector) m_inspector->SetVisible(false);
            });
        }
    }
    if (m_hideInspector) {
        m_hideInspector.Stop();
        m_hideInspector.Start();
    } else {
        m_inspector->SetVisible(false);
    }

    if (m_content) m_content->FocusList();
    if (m_inspectorClosed) m_inspectorClosed();
}

void Main::OnTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    if (Gfx::Material* material = MaterialOf()) {
        // Sin Mica, el mismo color de tarjeta pero OPACO: translúcido sobre nada solo
        // enseñaría el escritorio.
        Theme::Color base = tokens.cardSurface;
        base.a = m_hasMica ? 0 : 255;
        material->SetColor(base, HostRef().Animator(), crossfadeMs);
    }
    Ui::Element::OnTheme(tokens, crossfadeMs);
}

// ------------------------------------------------------------------------- Los mandos --

void Main::ChooseLens(App::Lens lens, bool fromSidebar) {
    if (m_state == nullptr) return;
    if (!fromSidebar && m_sidebar) m_sidebar->Select(lens);

    m_state->SetLens(lens);
    if (m_content) {
        // Cambiar de vista NO borra la búsqueda: quien escribe "fase" y va saltando de
        // grupo está buscando dónde cayó algo, y borrarle el filtro en cada salto le
        // obligaría a escribirlo otra vez en cada uno.
        m_content->Refresh(true);
        m_content->FocusList();
    }
    if (m_lensChanged) m_lensChanged(lens);
}

void Main::Reload(bool animate) {
    if (m_state == nullptr) return;
    if (m_sidebar) m_sidebar->SetCounts(*m_state);
    if (m_content) m_content->Refresh(animate);
}

void Main::SetCaption(const Caption::Layout& layout, Caption::Zone hovered,
                      Caption::Zone pressed) {
    if (m_chrome) m_chrome->SetCaption(layout, hovered, pressed);
}

void Main::SetSync(const Chrome::Sync& sync) {
    if (m_chrome) m_chrome->SetSync(sync);
}

void Main::SetAccount(const std::wstring& account) {
    if (m_sidebar) m_sidebar->SetAccount(account);
}

void Main::SetSyncing(bool running) {
    if (m_sidebar) m_sidebar->SetSyncing(running);
}

bool Main::OnKey(const Input::Key& e) {
    if (!e.down || m_content == nullptr) return false;

    // La revisión es un MODO, no una capa por la que se cuelan los atajos de la lista de
    // detrás: mientras corre se queda con todo el teclado. Lo único que deja pasar son las
    // teclas con Alt, porque Alt+F4 y Alt+Espacio son de Windows y no nuestras — y Ctrl+R,
    // que App mira antes que el enrutador y por tanto antes que esto.
    if (Reviewing()) return m_review->Keys(e);

    // Mientras se escribe, las letras son letras. Sin esta comprobación, la «j» de «bruja»
    // movería la selección de la lista en vez de escribirse, y la «n» de «pantalla» abriría
    // una novedad en mitad de una frase.
    const bool searching = m_content->SearchFocused();
    const bool editing = m_inspectorOpen && m_inspector && m_inspector->Editing();
    const bool typing = searching || editing;

    if (e.virtualKey == 'F' && Input::Has(e.modifiers, Input::Modifiers::Control)) {
        m_content->FocusSearch();
        return true;
    }
    // Ctrl+K funciona también mientras se escribe: es la manera de salir de donde estés, y un
    // atajo que solo vale cuando no estás haciendo nada no sirve para eso.
    if (e.virtualKey == 'K' && Input::Has(e.modifiers, Input::Modifiers::Control)) {
        if (m_palette) m_palette();
        return true;
    }
    if (!typing && IsSlash(e)) {
        m_content->FocusSearch();
        return true;
    }
    if (e.virtualKey == VK_ESCAPE) {
        // Por capas, de dentro afuera: primero la tarjeta en el aire, luego la edición a
        // medias, luego el inspector y luego la búsqueda. Las capas flotantes ya se las
        // llevó el enrutador antes de llegar aquí.
        if (m_content->Dragging()) {
            m_content->CancelDrag();
            return true;
        }
        if (m_inspectorOpen && m_inspector) {
            if (m_inspector->CancelEditing()) return true;
            CloseInspector();
            return true;
        }
        if (m_state && !m_state->Query().empty()) {
            m_content->ClearSearch();
            return true;
        }
        return false;
    }

    // E y N, de la tabla de atajos: editar el siguiente paso y añadir una novedad. Solo con
    // el inspector abierto, que es donde existen las dos cosas.
    if (!typing && m_inspectorOpen && m_inspector && !Input::Has(e.modifiers, Input::Modifiers::Control)) {
        if (e.virtualKey == 'E') {
            m_inspector->FocusNextStep();
            return true;
        }
        if (e.virtualKey == 'N') {
            m_inspector->BeginNovedad();
            return true;
        }
    }
    if (e.virtualKey == 'G' && Input::Has(e.modifiers, Input::Modifiers::Control)) {
        m_content->ToggleLayout();
        return true;
    }
    // Deshacer. Llega aquí solo cuando el foco NO está en un campo de texto: los campos se
    // quedan el Ctrl+Z para su propio historial, que es lo que espera quien está escribiendo
    // una frase y quiere recuperar la palabra de antes, no el cambio de prioridad de hace
    // diez minutos.
    if (e.virtualKey == 'Z' && Input::Has(e.modifiers, Input::Modifiers::Control) &&
        !Input::Has(e.modifiers, Input::Modifiers::Shift)) {
        if (m_undo) m_undo();
        return true;
    }
    if (e.virtualKey == 'O' && Input::Has(e.modifiers, Input::Modifiers::Control)) {
        if (m_openInGitHub && m_content->Selected() >= 0) m_openInGitHub(m_content->Selected());
        return true;
    }

    // 1, 2, 3 y 4: las cuatro prioridades de la tabla de atajos, sobre la tarjeta elegida.
    // «Sin clasificar» no tiene tecla y no es un olvido: es la ausencia de prioridad, y para
    // quitarla está el menú contextual. Van sin Control ni Alt, y nunca mientras se escribe:
    // el "1" de una fecha dentro de una novedad es un uno.
    if (!typing && !Input::Has(e.modifiers, Input::Modifiers::Control) &&
        !Input::Has(e.modifiers, Input::Modifiers::Alt)) {
        const int digit = e.virtualKey - '1';
        if (digit >= 0 && digit <= 3) {
            const int slot = m_content->Selected();
            if (slot >= 0 && m_priorityRequested) {
                m_priorityRequested(slot, static_cast<Model::Priority>(digit));
            }
            return true;
        }
    }

    // Las flechas desde la BÚSQUEDA bajan a la lista; las letras, no. Desde un campo del
    // inspector no bajan: ahí arriba y abajo no son "el siguiente repositorio", son el
    // renglón de al lado, y mover la selección en mitad de una edición la tiraría.
    if (typing) {
        if (editing) return false;
        switch (e.virtualKey) {
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
            return m_content->Navigate(e);
        default:
            return false;
        }
    }

    // Y con el foco en cualquier otro sitio —o en ninguno—, la navegación es de la lista.
    if (IsNavigation(e)) return m_content->Navigate(e);
    return false;
}

}  // namespace Views
