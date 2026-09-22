#include "app/App.h"

#include <shellapi.h>

#include <algorithm>
#include <utility>

#include "github/Auth.h"
#include "model/Time.h"
#include "model/Utf.h"
#include "shell/Backdrop.h"
#include "shell/Files.h"
#include "store/Backup.h"
#include "store/Paths.h"
#include "projectfile/Proyecto.h"
#include "store/Repos.h"
#include "ui/Controls.h"
#include "ui/Overlays.h"
#include "views/Palette.h"

namespace App {

namespace {

constexpr float kInitialWidthDip = 1180.0f;
constexpr float kInitialHeightDip = 760.0f;

// La clave de la vista elegida en la tabla de ajustes. Vuelve a abrir donde se dejó, que es
// lo que espera cualquiera que use la aplicación dos días seguidos.
constexpr char kLensSetting[] = "vista";
// Dónde clona el usuario sus repositorios, y si los nuevos nacen con el modo repo puesto.
constexpr char kReposRootSetting[] = "carpeta_repos";
constexpr char kRepoModeSetting[] = "modo_repo_omision";
// El recordatorio de la revisión: "día:hora" con el día de Windows (0 = domingo). Vacío o
// ausente es "no recordar", que es como nace.
constexpr char kReminderSetting[] = "recordatorio";
// Y el día en que sonó por última vez, en YYYY-MM-DD. Va a la tabla y no a una variable
// porque su trabajo es sobrevivir a cerrar la aplicación: sin él, abrirla dos veces el
// viernes por la tarde da dos avisos del mismo recordatorio.
constexpr char kReminderFiredSetting[] = "recordatorio_ultimo";

// Cada cuánto se mira el reloj. Cinco minutos y no el segundo exacto, a propósito: un
// temporizador de una semana se lo come una suspensión del equipo sin avisar, y para un
// recordatorio semanal llegar hasta cinco minutos tarde no significa nada. Solo corre
// mientras hay un recordatorio puesto.
//
// ponytail: comprobación cada 5 min; si algún día hiciera falta al minuto, el reloj es el
// mismo y lo único que cambia es este número.
constexpr int kReminderCheckMs = 5 * 60 * 1000;

const wchar_t* kWeekdays[] = {L"domingo", L"lunes",   L"martes", L"miércoles",
                              L"jueves",  L"viernes", L"sábado"};
// Las horas que se ofrecen. Seis y no veinticuatro: un menú con las veinticuatro es una
// lista por la que hay que desplazarse para elegir algo que da igual a la media hora.
constexpr int kReminderHours[] = {9, 12, 15, 17, 19, 21};

Model::Instant Now() {
    return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
}

// Existe y es una carpeta. Con GetFileAttributesW y no con <filesystem>: es una llamada,
// no lanza, y no arrastra medio encabezado estándar por una pregunta de sí o no.
bool Exists(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// Qué se dice del modo repo en el inspector. Se redacta AQUÍ y no en la vista: la vista no
// traduce estados, que es la misma regla que ya cumple el indicador de sincronización.
std::wstring RepoModeText(const Model::Local& local) {
    if (!local.repoMode) {
        return L"Solo aquí. No se escribe nada en el repositorio.";
    }
    if (!local.repoConfirmed) {
        return L"Falta confirmar el primer commit en este repositorio.";
    }
    if (local.pushPending) {
        return L"Hay cambios sin subir. Se reintenta en la próxima sincronización.";
    }
    return L"Se escribe en PROYECTO.md al guardar.";
}

// Qué cambió entre dos Local, en una frase. Vacío si no cambió nada de lo que el usuario
// escribe — y entonces no hay nada que deshacer, que es lo que distingue "guardar el mismo
// siguiente paso otra vez" de un cambio de verdad.
//
// Se mira aquí y no en cada sitio que guarda: con la frase escrita en cada llamante, el que
// se olvide de pasarla deja un Ctrl+Z que deshace algo sin decir qué.
std::wstring Changed(const Model::Local& before, const Model::Local& after) {
    if (before.priority != after.priority) return L"la prioridad";
    if (before.state != after.state) return L"el estado";
    if (before.nextStep != after.nextStep) return L"el siguiente paso";
    if (before.repoMode != after.repoMode) return L"el modo repo";
    if (before.folder != after.folder) return L"la carpeta";
    return std::wstring();
}

const wchar_t* NameOf(Github::Stage stage) {
    switch (stage) {
        case Github::Stage::Idle:       return L"Listo";
        case Github::Stage::Connecting: return L"Conectando…";
        case Github::Stage::Metadata:   return L"Trayendo repositorios…";
        case Github::Stage::Detail:     return L"Trayendo el detalle…";
        case Github::Stage::Done:       return L"Al día";
        case Github::Stage::Failed:     return L"No se pudo terminar";
    }
    return L"";
}

}  // namespace

bool Application::Init(HINSTANCE instance) {
    // STA antes que nada: el compositor se cuelga del apartamento de este hilo.
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // La ventana nace oculta. Todo lo que sigue ocurre antes de que se vea un píxel.
    if (!m_window.Create(instance, L"Brújula", kInitialWidthDip, kInitialHeightDip)) return false;
    if (!m_scene.Create(m_window.Handle())) return false;
    if (!m_device.Create(m_scene.Compositor())) return false;
    if (!m_text.Create(m_device.Write())) return false;

    m_animator.Attach(m_scene.Compositor());
    m_theme.Create(m_window.Handle(), Shell::Window::kThemeMessage);

    if (!m_host.Create(m_scene, m_device, m_animator, m_text, m_window.Handle())) return false;
    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);

    // La caché. El trabajador la abre y la migra; esta conexión es la de lectura del hilo de
    // UI, y las dos conviven porque la base está en WAL.
    //
    // Si esto falla, la aplicación arranca igual y se queda con el estado vacío: quedarse
    // sin ventana por un problema de disco sería peor que quedarse sin datos.
    if (Model::Result<std::string> path = Store::DatabasePath(); path.IsOk()) {
        const std::string file = path.Take();
        m_sync.Create(m_window.Handle(), Shell::Window::kSyncMessage, file);
        m_db.Open(file);
    }

    // Y lo primero que se hace con ella es leerla entera. La regla 1 de arquitectura en una
    // línea: la interfaz no espera a la red, así que lo que se pinta en el primer fotograma
    // ya está aquí.
    LoadFromCache();
    InstallMain();

    m_window.callbacks.onLayout = [this](float width, float height, float scale) {
        m_scene.Layout(width, height, scale);
        m_host.Layout(width, height, scale);
        PushCaption();
        // El repintado que deja el layout no puede esperar al siguiente mensaje: entre
        // medias se vería un fotograma con las texturas del tamaño viejo.
        m_host.FlushNow();
    };

    m_window.callbacks.onTheme = [this] {
        m_theme.Refresh();
        ApplyTheme(Motion::kThemeCrossfadeMs);
    };

    m_window.callbacks.onMotionSetting = [this] { m_animator.RefreshSystemPreference(); };

    m_window.callbacks.onCaptionState = [this] {
        PushCaption();
        m_host.FlushNow();
    };

    m_window.callbacks.onSync = [this] { OnSyncMessage(); };

    WireInput();

    // El primer fotograma completo, con la ventana todavía escondida.
    m_scene.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    ApplyTheme(0.0f);
    RefreshChrome(m_sync.Snapshot());
    // Y el reloj del recordatorio, que solo se pone en marcha si hay uno puesto. Va al
    // final porque lo primero que hace es mirar la hora, y para dar un aviso hace falta que
    // la pantalla ya exista.
    ArmReminder();
    m_host.FlushNow();
    return true;
}

// ------------------------------------------------------------------------ Los datos --

void Application::LoadFromCache() {
    if (!m_db.IsOpen()) return;

    Store::Repos repos(m_db);
    Model::Result<std::vector<Model::Repo>> all = repos.All();
    if (!all) return;

    std::vector<Model::Local> locals;
    if (Model::Result<std::vector<Model::Local>> stored = repos.AllLocal(); stored.IsOk()) {
        locals = stored.Take();
    }

    m_state.Load(all.Take(), locals, Now());

    // La cuenta NO se lee aquí: ya viene en el progreso de la sincronización, que la saca de
    // la misma tabla al crearse. Leerla otra vez obligaría a repetir el nombre de la clave
    // en dos archivos, y dos nombres de una clave son una clave que un día deja de leerse
    // sin dar ningún error.
    //
    // La vista elegida, solo la primera vez. Después es del usuario: releerla en cada
    // sincronización devolvería la lista a donde estaba hace media hora mientras se mira.
    if (!m_lensLoaded) {
        m_lensLoaded = true;
        if (Model::Result<std::string> lens = repos.Setting(kLensSetting); lens.IsOk()) {
            m_state.SetLens(LensFromSlug(lens.Value(), Lens::All));
        }
    }

    // Estos dos sí se releen cada vez, y no cuesta nada: son dos filas, y así una copia
    // importada que traiga otra carpeta se nota sin reiniciar.
    if (Model::Result<std::string> root = repos.Setting(kReposRootSetting); root.IsOk()) {
        m_reposRoot = Model::ToWide(root.Value());
    }
    if (Model::Result<std::string> mode = repos.Setting(kRepoModeSetting); mode.IsOk()) {
        m_repoModeDefault = !mode.Value().empty();
    }
    LoadReminder();
}

void Application::SaveLens(Lens lens) {
    if (!m_db.IsOpen()) return;
    Store::Repos repos(m_db);
    // Si esto falla no pasa nada que haya que contar: la próxima vez se abre en Todos.
    (void)repos.SetSetting(kLensSetting, SlugOf(lens));
}

void Application::InstallMain() {
    m_main = m_host.SetRoot<Views::Main>();
    m_main->SetMica(m_window.HasMica());
    m_main->Bind(&m_state);
    WireView();

    m_host.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);
    PushCaption();
    // Sin animar: lo que ya estaba en la caché no "entra", ya estaba.
    m_main->Reload(false);
}

void Application::PushCaption() {
    const Caption::Layout& layout = m_window.CaptionLayout();
    const Caption::Zone hovered = m_window.Hovered();
    const Caption::Zone pressed = m_window.Pressed();
    if (m_main) m_main->SetCaption(layout, hovered, pressed);
#if BRUJULA_CATALOGO
    if (m_catalog) m_catalog->SetCaption(layout, hovered, pressed);
#endif
}

// ------------------------------------------------------------------------ El inspector --

void Application::WireInspector() {
    Views::Inspector* panel = m_main->Panel();
    if (panel == nullptr) return;

    m_main->OnInspectorRequest([this](int slot) {
        const Entry* entry = m_state.At(slot);
        if (entry) BindInspector(entry->repo.id);
    });
    m_main->OnInspectorClosed([this] { m_inspectorRepo.clear(); });

    panel->OnPriority([this](Model::Priority priority) { SetPriority(priority); });
    panel->OnState([this](Model::State state) { SetProjectState(state); });
    panel->OnNextStep([this](const std::wstring& text) { SetNextStep(text); });
    panel->OnAddNovedad([this](const std::wstring& text) { AddNovedad(text); });
    panel->OnDeleteNovedad([this](std::int64_t id) { DeleteNovedad(id); });
    panel->OnRepoMode([this](bool wanted) { SetRepoMode(wanted); });
    panel->OnReadRootFile([this](const std::wstring& name) {
        if (m_inspectorRepo.empty()) return;
        Github::Job job;
        job.kind = Github::JobKind::ReadFile;
        job.repoId = m_inspectorRepo;
        job.path = name;
        m_sync.Enqueue(std::move(job));
        Toast(L"Trayendo " + name + L"…");
    });
    panel->OnImport([this] { ImportProyecto(); });
    panel->OnOpenFolder([this] { OpenFolder(); });
    panel->OnOpenGitHub([this] {
        const Entry* entry = m_state.EntryOf(m_inspectorRepo);
        if (entry == nullptr || entry->repo.url.empty()) return;
        ShellExecuteW(nullptr, L"open", entry->repo.url.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
    });
}

void Application::BindInspector(const std::string& repoId) {
    if (m_main == nullptr) return;
    Views::Inspector* panel = m_main->Panel();
    const Entry* entry = m_state.EntryOf(repoId);
    if (panel == nullptr || entry == nullptr) return;

    Views::Inspector::Content content;
    content.repoId = entry->repo.id;
    content.name = entry->repo.name;
    content.nameWithOwner = entry->repo.nameWithOwner;
    content.description = entry->repo.description;
    content.url = entry->repo.url;
    content.activity = entry->activity;
    content.daysSincePush = entry->daysSincePush;
    content.openIssues = entry->repo.openIssues;
    content.openPrs = entry->repo.openPrs;
    content.gone = entry->repo.goneAt.has_value();
    content.hasProyecto = !entry->repo.proyectoText.empty();
    content.local = entry->local;
    content.folder = ResolveFolder(*entry);
    content.repoModeText = RepoModeText(entry->local);

    // Los commits, las novedades y los .md de la raíz se leen AQUÍ y de SQLite, que es una
    // consulta por índice y unos microsegundos. No viven en App::State porque son listas: con
    // 109 repositorios serían quinientas filas más cargadas al arrancar para pintar una lista
    // que no las enseña.
    if (m_db.IsOpen()) {
        Store::Repos repos(m_db);
        if (Model::Result<std::vector<Model::Commit>> commits = repos.CommitsOf(entry->repo.id);
            commits.IsOk()) {
            content.commits = commits.Take();
        }
        if (Model::Result<std::vector<Model::Novedad>> novedades =
                repos.NovedadesOf(entry->repo.id);
            novedades.IsOk()) {
            content.novedades = novedades.Take();
        }
        if (Model::Result<std::vector<std::wstring>> md = repos.RootMarkdownOf(entry->repo.id);
            md.IsOk()) {
            content.rootMarkdown = md.Take();
        }
    }

    m_inspectorRepo = entry->repo.id;
    panel->Bind(content);
}

void Application::RebindInspector() {
    if (m_inspectorRepo.empty()) return;
    // Por identificador y no por posición: al cambiar la prioridad, el repositorio puede
    // haberse salido de la vista que se está mirando, y el panel tiene que seguir enseñando
    // lo que el usuario acaba de tocar en vez de cerrarse en su cara.
    BindInspector(m_inspectorRepo);
}

void Application::SaveLocal(Model::Local local) {
    // Un repositorio al que nunca se le ha escrito nada nace con el interruptor puesto si el
    // ajuste lo dice. Con el interruptor y NADA MÁS: repoConfirmed sigue en falso, así que
    // no se escribe una sola letra hasta que alguien conteste la hoja de ese repositorio.
    if (m_repoModeDefault && !local.repoMode && !local.repoConfirmed) {
        const Entry* before = m_state.EntryOf(local.repoId);
        if (before != nullptr && before->local.updatedAt == Model::Instant{}) {
            local.repoMode = true;
        }
    }
    local.updatedAt = Now();
    if (m_db.IsOpen()) {
        Store::Repos repos(m_db);
        if (Model::Outcome saved = repos.SaveLocal(local); !saved) {
            Toast(std::wstring(Model::NameOf(saved.Err().kind)) + L". " + saved.Err().detail);
            return;
        }
    }
    m_state.ApplyLocal(local, Now());
    if (m_main) m_main->Reload(true);
    RebindInspector();

    // Y solo AHORA, con el cambio ya guardado, se piensa en GitHub. Al revés —encolar antes
    // de escribir— el criterio de aceptación de la fase dependería de que hubiera red.
    if (!local.repoMode || !local.repoConfirmed || !m_db.IsOpen()) return;
    Store::Repos repos(m_db);
    (void)repos.SetPushPending(local.repoId, true);

    Github::Job job;
    job.kind = Github::JobKind::PushProyecto;
    job.repoId = local.repoId;
    m_sync.Enqueue(std::move(job));
}

void Application::SetRepoMode(bool wanted) {
    const Entry* entry = m_state.EntryOf(m_inspectorRepo);
    if (entry == nullptr) return;

    // Apagarlo no pregunta nada: dejar de escribir nunca necesita permiso.
    if (!wanted) {
        Model::Local local = entry->local;
        local.repoMode = false;
        SaveLocal(std::move(local));
        return;
    }
    // Encenderlo en uno ya confirmado tampoco: la pregunta es UNA vez por repositorio, no
    // una vez por interruptor (SEGURIDAD.md, regla 5).
    if (entry->local.repoConfirmed) {
        Model::Local local = entry->local;
        local.repoMode = true;
        SaveLocal(std::move(local));
        return;
    }
    ConfirmRepoMode(*entry);
}

Ui::Sheet* Application::Ask(std::wstring title, std::wstring body, std::wstring accept,
                            std::wstring cancel) {
    Ui::Sheet* sheet = m_host.PushLayer<Ui::Sheet>(
        Ui::Host::LayerOptions{/*modal*/ true, /*lightDismiss*/ false, /*scrim*/ true},
        std::move(title), std::move(body));
    sheet->SetActions(std::move(accept), std::move(cancel));
    m_host.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);
    m_host.FlushNow();
    return sheet;
}

void Application::ConfirmRepoMode(const Entry& entry) {
    const std::string repoId = entry.repo.id;
    const std::wstring where = entry.repo.nameWithOwner;

    Ui::Sheet* sheet = Ask(
        L"¿Escribir en " + where + L"?",
        L"A partir de ahora, Brújula guardará también la prioridad, el estado, el siguiente "
        L"paso y las novedades en un archivo PROYECTO.md en la raíz de ese repositorio, con "
        L"un commit «chore: actualizar PROYECTO.md» en su rama principal.\n"
        L"\n"
        L"No toca ningún otro archivo, no crea ramas y no borra nada. Hace falta que la "
        L"credencial tenga permiso de escritura sobre el contenido. Se pregunta una vez por "
        L"repositorio y se puede apagar cuando quieras.",
        L"Escribir en el repositorio", L"Ahora no");
    sheet->OnAccept([this, repoId] {
        const Entry* fresh = m_state.EntryOf(repoId);
        if (fresh == nullptr) return;
        Model::Local local = fresh->local;
        local.repoMode = true;
        // Las dos a la vez, y esta es la ÚNICA línea de todo el programa que enciende la
        // confirmación. Es lo que hace cierto que no hay un interruptor global.
        local.repoConfirmed = true;
        SaveLocal(std::move(local));
    });
}

void Application::ImportProyecto() {
    const Entry* entry = m_state.EntryOf(m_inspectorRepo);
    if (entry == nullptr || entry->repo.proyectoText.empty()) return;

    const Proyecto::File file = Proyecto::Parse(entry->repo.proyectoText);
    const Proyecto::Import traido = Proyecto::Adopt(file, entry->local);

    std::wstring resumen;
    if (traido.local.priority != entry->local.priority) {
        resumen += L"Prioridad: " + std::wstring(Ui::NameOf(traido.local.priority)) + L"\n";
    }
    if (traido.local.state != entry->local.state) {
        resumen += L"Estado: " + std::wstring(Ui::NameOf(traido.local.state)) + L"\n";
    }
    if (traido.local.nextStep != entry->local.nextStep) {
        resumen += L"Siguiente paso: " + traido.local.nextStep + L"\n";
    }
    if (!traido.novedades.empty()) {
        resumen += std::to_wstring(traido.novedades.size()) +
                   (traido.novedades.size() == 1 ? L" novedad" : L" novedades") + L"\n";
    }
    if (resumen.empty()) {
        Toast(L"Ese PROYECTO.md ya dice lo mismo que hay aquí.");
        return;
    }

    const std::string repoId = entry->repo.id;
    Ui::Sheet* sheet =
        Ask(L"Importar el PROYECTO.md",
            L"Esto es lo que dice el archivo del repositorio y lo que pasaría a decir "
            L"Brújula:\n\n" +
                resumen + L"\nLas novedades se unen con las que ya hay: ninguna se borra.",
            L"Importar", L"Ahora no");
    sheet->OnAccept([this, repoId] {
        const Entry* fresh = m_state.EntryOf(repoId);
        if (fresh == nullptr || !m_db.IsOpen()) return;

        const Proyecto::Import adopted =
            Proyecto::Adopt(Proyecto::Parse(fresh->repo.proyectoText), fresh->local);
        Store::Repos repos(m_db);
        const Model::Result<std::vector<Model::Novedad>> have =
            repos.NovedadesOf(fresh->repo.id);
        for (const Model::Novedad& novedad : adopted.novedades) {
            // Unión por fecha y texto, igual que en Proyecto::Merge y por lo mismo: importar
            // dos veces el mismo archivo no puede duplicar las notas.
            bool already = false;
            if (have.IsOk()) {
                for (const Model::Novedad& mine : have.Value()) {
                    if (mine.day == novedad.day && mine.text == novedad.text) {
                        already = true;
                        break;
                    }
                }
            }
            if (!already) (void)repos.AddNovedad(novedad);
        }
        SaveLocal(adopted.local);
    });
}

void Application::DrainJobs() {
    for (const Github::JobResult& done : m_sync.TakeFinished()) {
        if (done.kind == Github::JobKind::PushProyecto) {
            // Un commit que no hizo falta no se anuncia: decir «no ha cambiado nada» cada
            // vez que se guarda dos veces seguidas sería un aviso por cada pulsación.
            if (done.ok && done.unchanged) continue;
            Toast(done.detail);
            continue;
        }
        // ReadFile es de la copia a novedades: lo atiende quien la pidió.
        OnFileArrived(done);
    }
}

void Application::OnFileArrived(const Github::JobResult& done) {
    if (!done.ok) {
        Toast(done.detail);
        return;
    }

    const std::vector<std::wstring> propuestas = Proyecto::AsNovedades(done.text);
    if (propuestas.empty()) {
        Toast(done.path + L" no tiene nada que copiar.");
        return;
    }

    // Una vista previa de verdad: las tres primeras, recortadas, y cuántas hay. Copiar a
    // ciegas lo que trae un archivo ajeno sería meter en las notas de alguien lo que ese
    // alguien no ha leído.
    std::wstring preview;
    for (std::size_t i = 0; i < propuestas.size() && i < 3; ++i) {
        std::wstring line = propuestas[i];
        if (line.size() > 90) line = line.substr(0, 88) + L"…";
        preview += L"· " + line + L"\n";
    }
    if (propuestas.size() > 3) {
        preview += L"…y " + std::to_wstring(propuestas.size() - 3) + L" más\n";
    }

    const std::string repoId = done.repoId;
    Ui::Sheet* sheet = Ask(
        L"Copiar " + done.path + L" a las novedades",
        L"Se añadirían " + std::to_wstring(propuestas.size()) +
            (propuestas.size() == 1 ? L" novedad con la fecha de hoy:\n\n"
                                    : L" novedades con la fecha de hoy:\n\n") +
            preview + L"\nEl archivo del repositorio no se toca.",
        L"Copiar", L"Ahora no");

    sheet->OnAccept([this, repoId, propuestas] {
        const Entry* entry = m_state.EntryOf(repoId);
        if (entry == nullptr || !m_db.IsOpen()) return;

        Store::Repos repos(m_db);
        const std::string day = Model::FormatDay(Now());
        for (const std::wstring& text : propuestas) {
            Model::Novedad novedad;
            novedad.repoId = repoId;
            novedad.day = day;
            novedad.text = text;
            novedad.createdAt = Now();
            (void)repos.AddNovedad(novedad);
        }
        // Por el camino de siempre: sella la fecha y, si toca, encola la subida.
        SaveLocal(entry->local);
    });
}

void Application::SetPriority(Model::Priority priority) {
    ApplyPriority(m_inspectorRepo, priority);
}

bool Application::ApplyPriority(const std::string& repoId, Model::Priority priority) {
    const Entry* entry = m_state.EntryOf(repoId);
    if (entry == nullptr) {
        // El repositorio ya no está —una sincronización lo quitó mientras se arrastraba—. La
        // tarjeta que hubiera en el aire vuelve a su sitio igual.
        if (m_main) m_main->CancelDrop();
        return false;
    }

    // Se levanta antes de decidir nada: si viene de un arrastre ya está levantada y esto no
    // hace nada, y si viene del teclado o de la paleta es lo que hace que el cambio se VEA
    // irse a su grupo en vez de aparecer ya hecho.
    if (m_main) m_main->LiftCard(m_state.SlotOfId(repoId));

    if (entry->local.priority == priority) {
        // Ya estaba ahí. Se deja caer en su grupo igual: quien acaba de arrastrarla hasta
        // allí tiene que ver que llegó, no que se le queda la tarjeta en la mano.
        if (m_main) m_main->DropCardInto(priority);
        return true;
    }

    if (priority == Model::Priority::Focus) {
        std::vector<Model::FocusEntry> inFocus;
        for (const Entry& other : m_state.Entries()) {
            if (other.local.priority != Model::Priority::Focus) continue;
            inFocus.push_back(Model::FocusEntry{other.repo.id, other.repo.pushedAt});
        }
        const Model::FocusPlan plan =
            Model::PlanFocus(inFocus, entry->repo.id, Now(), m_state.Thresholds());
        // El límite no se puede saltar (CLAUDE.md), así que esto no tiene una rama que lo
        // ignore: la tarjeta se para en seco, tiembla, y la hoja pregunta cuál baja.
        if (!plan.fits) {
            if (m_main) m_main->RefuseDrop();
            AskWhoLeavesFocus(repoId, plan.demote);
            return false;
        }
    }

    Model::Local local = entry->local;
    local.priority = priority;
    SaveLocal(std::move(local));
    if (m_main) m_main->DropCardInto(priority);
    return true;
}

void Application::AskWhoLeavesFocus(const std::string& candidateId,
                                    const std::vector<std::string>& demote) {
    const Entry* candidate = m_state.EntryOf(candidateId);
    if (candidate == nullptr || demote.empty()) return;

    // En el orden que trae Model::PlanFocus: el que lleva más tiempo sin un push, primero. No
    // se marca ninguno como "el recomendado" — la lista ya lo dice por sí sola, y decirlo
    // además con palabras sería empujar a alguien a bajar su trabajo por una fecha.
    std::vector<std::wstring> options;
    options.reserve(demote.size());
    for (const std::string& id : demote) {
        const Entry* other = m_state.EntryOf(id);
        if (other == nullptr) continue;
        options.push_back(other->repo.name + L" · " +
                          (other->daysSincePush.has_value() ? AgoDays(*other->daysSincePush)
                                                            : L"sin actividad"));
    }
    if (options.empty()) return;

    const std::wstring limit = std::to_wstring(m_state.Thresholds().focusLimit);
    Ui::Sheet* sheet = Ask(L"Enfoque está lleno",
                           L"Caben " + limit + L" y ya están los " + limit + L". Para que " +
                               candidate->repo.name +
                               L" entre, uno tiene que bajar a Secundario:",
                           L"Ahora no", std::wstring());

    const std::vector<std::string> leaving = demote;
    // Ojo al orden: Ask() ya repartió el tema, y estos botones nacen después. Sin volver a
    // repartirlo salen sin color de fondo —Ui::Button lo pone en OnTheme y no al crearse—,
    // que es un botón invisible con su texto encima.
    sheet->SetOptions(std::move(options), [this, candidateId, leaving](int index) {
        if (index < 0 || index >= static_cast<int>(leaving.size())) return;
        const Entry* out = m_state.EntryOf(leaving[static_cast<std::size_t>(index)]);
        const Entry* in = m_state.EntryOf(candidateId);
        if (out == nullptr || in == nullptr) return;

        Model::Local down = out->local;
        Model::Local up = in->local;
        const Model::Local beforeDown = down;
        const Model::Local beforeUp = up;
        const std::wstring said =
            out->repo.name + L" baja a Secundario y " + in->repo.name + L" entra en Enfoque.";
        down.priority = Model::Priority::Secondary;
        up.priority = Model::Priority::Focus;

        // Los dos como UN cambio: con dos entradas en la pila, el primer Ctrl+Z dejaría seis
        // en Enfoque durante un rato, que es justo el estado que no puede existir.
        m_quietUndo = true;
        SaveLocal(std::move(down));
        SaveLocal(std::move(up));
        m_quietUndo = false;
        PushUndo(L"el relevo en Enfoque", [this, beforeDown, beforeUp] {
            SaveLocal(beforeDown);
            SaveLocal(beforeUp);
        });
        Toast(said);
        // Si esto venía de la revisión semanal, la tarjeta se quedó temblando esperando
        // una respuesta. Ya la hay: que salga hacia Enfoque y entre la siguiente.
        if (m_main && m_main->Weekly()) {
            m_main->Weekly()->Accepted(candidateId, Model::Priority::Focus);
        }
    });

    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);
    m_host.FlushNow();
}

void Application::SetProjectState(Model::State state) {
    const Entry* entry = m_state.EntryOf(m_inspectorRepo);
    if (entry == nullptr || entry->local.state == state) return;
    Model::Local local = entry->local;
    local.state = state;
    SaveLocal(std::move(local));
}

void Application::SetNextStep(const std::wstring& text) {
    const Entry* entry = m_state.EntryOf(m_inspectorRepo);
    if (entry == nullptr || entry->local.nextStep == text) return;
    Model::Local local = entry->local;
    local.nextStep = text;
    SaveLocal(std::move(local));
}

void Application::AddNovedad(const std::wstring& text) {
    const Entry* entry = m_state.EntryOf(m_inspectorRepo);
    if (entry == nullptr || text.empty() || !m_db.IsOpen()) return;

    Model::Novedad novedad;
    novedad.repoId = entry->repo.id;
    novedad.day = Model::FormatDay(Now());
    novedad.text = text;
    novedad.createdAt = Now();

    Store::Repos repos(m_db);
    if (Model::Outcome added = repos.AddNovedad(novedad); !added) {
        Toast(std::wstring(Model::NameOf(added.Err().kind)) + L". " + added.Err().detail);
        return;
    }
    // El número de la fila recién escrita, que es lo único con lo que se puede volver atrás:
    // dos novedades del mismo día con el mismo texto son dos filas distintas, y buscarla por
    // el contenido borraría la que no es.
    const std::int64_t addedId = m_db.LastInsertId();
    const std::string repoId = entry->repo.id;
    const std::wstring name = entry->repo.name;

    // Y por el camino de siempre: una novedad también es un cambio del repositorio, así que
    // pasa por SaveLocal para que se selle la fecha y, cuando toque, se encole la subida.
    SaveLocal(entry->local);
    PushUndo(L"la novedad de " + name,
             [this, repoId, addedId] { DeleteNovedadOf(repoId, addedId); });
}

void Application::DeleteNovedad(std::int64_t id) { DeleteNovedadOf(m_inspectorRepo, id); }

void Application::DeleteNovedadOf(const std::string& repoId, std::int64_t id) {
    const Entry* entry = m_state.EntryOf(repoId);
    if (entry == nullptr || !m_db.IsOpen()) return;

    Store::Repos repos(m_db);
    // Lo que se va a borrar, ANTES de borrarlo: es lo único que permite devolverlo. Una
    // novedad borrada por error no se puede volver a descargar de ninguna parte.
    Model::Novedad gone;
    if (Model::Result<std::vector<Model::Novedad>> all = repos.NovedadesOf(repoId); all.IsOk()) {
        for (const Model::Novedad& one : all.Value()) {
            if (one.id == id) gone = one;
        }
    }

    if (Model::Outcome removed = repos.DeleteNovedad(id); !removed) {
        Toast(std::wstring(Model::NameOf(removed.Err().kind)) + L". " + removed.Err().detail);
        return;
    }
    SaveLocal(entry->local);
    if (gone.id != 0) {
        PushUndo(L"borrar una novedad de " + entry->repo.name, [this, gone] {
            if (!m_db.IsOpen()) return;
            Store::Repos again(m_db);
            // Vuelve con otro número de fila y con su fecha original. El número no lo mira
            // nadie más que la propia lista del inspector.
            (void)again.AddNovedad(gone);
            const Entry* back = m_state.EntryOf(gone.repoId);
            if (back != nullptr) SaveLocal(back->local);
        });
    }
}

std::wstring Application::ResolveFolder(const Entry& entry) const {
    // La del repositorio manda sobre la raíz: quien elige una carpeta a mano lo hace porque
    // ese repositorio no está donde están los demás.
    if (!entry.local.folder.empty() && Exists(entry.local.folder)) return entry.local.folder;
    if (m_reposRoot.empty()) return std::wstring();

    std::wstring guess = m_reposRoot;
    if (guess.back() != L'\\' && guess.back() != L'/') guess.push_back(L'\\');
    guess += entry.repo.name;
    return Exists(guess) ? guess : std::wstring();
}

void Application::OpenFolder() { OpenFolderOf(m_inspectorRepo); }

void Application::OpenFolderOf(const std::string& repoId) {
    const Entry* entry = m_state.EntryOf(repoId);
    if (entry == nullptr) return;
    const std::wstring folder = ResolveFolder(*entry);
    // Sin carpeta que abrir, el botón sirve para elegirla. Un botón que solo sabe decir que
    // no es un botón que sobra.
    if (folder.empty()) {
        ChooseFolderFor(entry->repo.id);
        return;
    }
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// --------------------------------------------------------------------------- Ajustes --

void Application::ShowSettings(float x, float y) {
    std::vector<Ui::Menu::Entry> entries;
    entries.push_back({m_reposRoot.empty() ? L"Elegir la carpeta de repositorios…"
                                           : L"Cambiar la carpeta de repositorios…",
                       [this] { ChooseReposRoot(); }});
    entries.push_back({m_repoModeDefault ? L"Modo repo por omisión: sí"
                                         : L"Modo repo por omisión: no",
                       [this] { ToggleRepoModeDefault(); }});
    entries.push_back({ReminderText(), [this, x, y] { ShowReminderMenu(x, y); }});
    entries.push_back({L"Exportar una copia de seguridad…", [this] { ExportBackup(); }});
    entries.push_back({L"Importar una copia…", [this] { ImportBackup(); }});

    m_host.PushLayer<Ui::Menu>(Ui::Host::LayerOptions{false, true, false}, std::move(entries),
                               x, y);
    m_host.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);
    m_host.FlushNow();
}

void Application::ChooseReposRoot() {
    const std::wstring folder =
        Shell::PickFolder(m_window.Handle(), L"¿Dónde clonas tus repositorios?");
    // Vacío es "me he arrepentido", no un error: el cuadro se cerró y no pasa nada.
    if (folder.empty()) return;

    m_reposRoot = folder;
    if (m_db.IsOpen()) {
        Store::Repos repos(m_db);
        (void)repos.SetSetting(kReposRootSetting, Model::ToUtf8(folder));
    }
    RebindInspector();
    Toast(L"Los repositorios se buscarán dentro de " + folder);
}

void Application::ToggleRepoModeDefault() {
    m_repoModeDefault = !m_repoModeDefault;
    if (m_db.IsOpen()) {
        Store::Repos repos(m_db);
        (void)repos.SetSetting(kRepoModeSetting, m_repoModeDefault ? "1" : "");
    }
    // Se dice lo que hace Y lo que NO hace. Un ajuste llamado «por omisión» que la gente
    // crea que enciende ciento nueve escrituras de golpe es peor que no tenerlo.
    Toast(m_repoModeDefault
              ? L"Los repositorios nuevos nacerán con el modo repo puesto. El primer commit "
                L"de cada uno se sigue confirmando aparte."
              : L"Los repositorios nuevos se quedan solo en local.");
}

void Application::ExportBackup() {
    if (!m_db.IsOpen()) return;

    Model::Result<std::string> json = Store::ExportJson(m_db, Now());
    if (!json) {
        Toast(std::wstring(Model::NameOf(json.Err().kind)) + L". " + json.Err().detail);
        return;
    }

    const std::wstring path = Shell::PickSaveFile(m_window.Handle(), L"Guardar la copia",
                                                  L"json", L"brujula.json");
    if (path.empty()) return;

    if (Model::Outcome saved = Shell::SaveText(path, json.Value()); !saved) {
        Toast(std::wstring(Model::NameOf(saved.Err().kind)) + L". " + saved.Err().detail);
        return;
    }
    Toast(L"Copia guardada. Lleva tus notas, no tus repositorios.");
}

void Application::ImportBackup() {
    if (!m_db.IsOpen()) return;

    const std::wstring path =
        Shell::PickOpenFile(m_window.Handle(), L"Abrir una copia de Brújula", L"json");
    if (path.empty()) return;

    Model::Result<std::string> text = Shell::LoadText(path);
    if (!text) {
        Toast(std::wstring(Model::NameOf(text.Err().kind)) + L". " + text.Err().detail);
        return;
    }

    // Fusionando: gana lo más nuevo y no se borra nada. Reemplazar existe en store/Backup.h y
    // no se ofrece aquí a propósito — quien importa una copia casi siempre quiere recuperar,
    // no sustituir, y la diferencia entre las dos solo se nota cuando ya es tarde.
    Model::Result<Store::ImportReport> report =
        Store::ImportJson(m_db, text.Value(), Store::ImportMode::Merge);
    if (!report) {
        Toast(std::wstring(Model::NameOf(report.Err().kind)) + L". " + report.Err().detail);
        return;
    }

    LoadFromCache();
    if (m_main) m_main->Reload(true);
    RebindInspector();

    std::wstring said = std::to_wstring(report.Value().matched) + L" repositorios importados";
    if (report.Value().novedades > 0) {
        said += L", " + std::to_wstring(report.Value().novedades) + L" novedades nuevas";
    }
    if (report.Value().kept > 0) {
        said += L", " + std::to_wstring(report.Value().kept) + L" sin tocar por ser más nuevos";
    }
    if (report.Value().skipped > 0) {
        said += L". Quedan " + std::to_wstring(report.Value().skipped) +
                L" que todavía no están en esta caché: sincroniza y vuelve a importar";
    }
    Toast(said);
}

void Application::ChooseFolderFor(const std::string& repoId) {
    const Entry* entry = m_state.EntryOf(repoId);
    if (entry == nullptr) return;

    const std::wstring folder = Shell::PickFolder(
        m_window.Handle(), (L"¿Dónde está " + entry->repo.name + L"?").c_str());
    if (folder.empty()) return;

    Model::Local local = entry->local;
    local.folder = folder;
    SaveLocal(std::move(local));
}

// ------------------------------------------------------- Ordenar, deshacer y menús --

bool Application::WriteOrder(const std::vector<std::pair<std::string, int>>& orders) {
    if (!m_db.IsOpen()) return false;

    // Todo o nada. Son hasta ciento nueve escrituras por un solo gesto, y una tanda a medias
    // dejaría la lista ordenada por un orden que nadie pidió —ni el viejo ni el nuevo—.
    Store::Transaction tx(m_db);
    if (Model::Outcome started = tx.Begin(); !started) {
        Toast(std::wstring(Model::NameOf(started.Err().kind)) + L". " + started.Err().detail);
        return false;
    }
    Store::Repos repos(m_db);
    for (const std::pair<std::string, int>& one : orders) {
        if (Model::Outcome written = repos.SetOrder(one.first, one.second); !written) {
            Toast(std::wstring(Model::NameOf(written.Err().kind)) + L". " +
                  written.Err().detail);
            return false;
        }
    }
    if (Model::Outcome done = tx.Commit(); !done) {
        Toast(std::wstring(Model::NameOf(done.Err().kind)) + L". " + done.Err().detail);
        return false;
    }
    return true;
}

void Application::ReorderVisible(int from, int to) {
    if (m_main == nullptr || !m_db.IsOpen()) return;

    // Con una búsqueda puesta no se ordena. Lo que se ve es un trozo de la vista, así que
    // "entre estas dos" no dice nada de los que el filtro dejó fuera y que están justo ahí
    // en medio: el orden saldría de otra manera en cuanto se borrara la búsqueda.
    if (m_state.Searching()) {
        Toast(L"Para ordenar a mano, quita antes la búsqueda: lo que se ve es solo una parte.");
        if (m_main) m_main->CancelDrop();
        return;
    }

    std::vector<std::string> ids;
    std::vector<std::pair<std::string, int>> before;
    ids.reserve(static_cast<std::size_t>(m_state.VisibleCount()));
    before.reserve(static_cast<std::size_t>(m_state.VisibleCount()));
    for (int slot = 0; slot < m_state.VisibleCount(); ++slot) {
        const Entry* entry = m_state.At(slot);
        if (entry == nullptr) continue;
        ids.push_back(entry->repo.id);
        before.emplace_back(entry->repo.id, entry->local.order);
    }

    const std::vector<std::string> moved = Reordered(ids, from, to);
    if (moved.empty()) return;

    // Se renumera la vista ENTERA, del uno en adelante, y no solo los dos de al lado. Con
    // números sueltos habría que inventarse huecos entre medias y un día no cabría ninguno;
    // así, el orden de la pantalla y el de la cajón son el mismo número.
    std::vector<std::pair<std::string, int>> orders;
    orders.reserve(moved.size());
    for (std::size_t i = 0; i < moved.size(); ++i) {
        orders.emplace_back(moved[i], static_cast<int>(i) + 1);
    }
    if (!WriteOrder(orders)) return;

    PushUndo(L"el orden de " + std::wstring(NameOf(m_state.CurrentLens())), [this, before] {
        if (!WriteOrder(before)) return;
        LoadFromCache();
        if (m_main) m_main->Reload(true);
    });

    LoadFromCache();
    m_main->Reload(true);
}

void Application::PushUndo(std::wstring said, std::function<void()> apply) {
    if (m_quietUndo) return;
    // Cincuenta y a olvidar los de abajo. Es una pila para el error de hace un momento, no un
    // historial del día: lo que de verdad guarda lo escrito es SQLite.
    constexpr std::size_t kDepth = 50;
    m_undo.push_back(Undo{std::move(said), std::move(apply)});
    if (m_undo.size() > kDepth) m_undo.erase(m_undo.begin());
}

void Application::UndoLast() {
    if (m_undo.empty()) {
        Toast(L"No hay nada que deshacer.");
        return;
    }
    const Undo last = std::move(m_undo.back());
    m_undo.pop_back();

    // Callado: la vuelta atrás no se apunta a sí misma. Sin esto, Ctrl+Z dos veces se
    // quedaría meciendo el mismo cambio para siempre.
    m_quietUndo = true;
    if (last.apply) last.apply();
    m_quietUndo = false;
    Toast(L"Deshecho: " + last.said);
}

void Application::ShowCardMenu(int slot, float x, float y) {
    const Entry* entry = m_state.At(slot);
    if (entry == nullptr) return;

    // Todo por VALOR: el menú vive más que esta llamada, y para cuando alguien elija una
    // opción puede haber terminado una sincronización y haberse reconstruido el estado entero.
    const std::string repoId = entry->repo.id;
    const std::wstring url = entry->repo.url;
    const Model::Priority current = entry->local.priority;

    std::vector<Ui::Menu::Entry> entries;
    entries.push_back({L"Abrir", [this, repoId] { RevealRepo(repoId); }});
    for (int i = 0; i < 5; ++i) {
        const Model::Priority priority = static_cast<Model::Priority>(i);
        std::wstring label = priority == Model::Priority::Unsorted
                                 ? std::wstring(L"Quitar la prioridad")
                                 : L"Poner en " + std::wstring(Ui::NameOf(priority));
        // La de ahora se dice con palabras y no con una marca: un menú con una columna de
        // marcas vacías pide leerlo dos veces para encontrar la única que está puesta.
        if (priority == current) label += L" (ahora)";
        entries.push_back({std::move(label),
                           [this, repoId, priority] { ApplyPriority(repoId, priority); }});
    }
    entries.push_back({L"Editar el siguiente paso", [this, repoId] {
                           RevealRepo(repoId);
                           if (m_main && m_main->Panel()) m_main->Panel()->FocusNextStep();
                       }});
    entries.push_back({L"Añadir una novedad", [this, repoId] {
                           RevealRepo(repoId);
                           if (m_main && m_main->Panel()) m_main->Panel()->BeginNovedad();
                       }});
    if (!url.empty()) {
        entries.push_back({L"Abrir en GitHub", [url] {
                               ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr,
                                             SW_SHOWNORMAL);
                           }});
    }
    entries.push_back({L"Abrir la carpeta local", [this, repoId] { OpenFolderOf(repoId); }});

    m_host.PushLayer<Ui::Menu>(Ui::Host::LayerOptions{false, true, false}, std::move(entries),
                               x, y);
    m_host.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);
    m_host.FlushNow();
}

void Application::ShowPalette() {
    std::vector<Views::Palette::Action> actions;

    // Lo del repositorio elegido va PRIMERO, porque es lo que se busca cuando se abre la
    // paleta con una tarjeta señalada. Sin ninguna elegida, estas acciones no existen: una
    // paleta que ofrece "Poner en Enfoque" sin decir el qué solo puede equivocarse.
    const Entry* chosen = m_main ? m_state.At(m_main->SelectedSlot()) : nullptr;
    if (chosen != nullptr) {
        const std::string repoId = chosen->repo.id;
        const std::wstring name = chosen->repo.name;
        for (int i = 0; i < 5; ++i) {
            const Model::Priority priority = static_cast<Model::Priority>(i);
            std::wstring label = priority == Model::Priority::Unsorted
                                     ? std::wstring(L"Quitar la prioridad")
                                     : L"Poner en " + std::wstring(Ui::NameOf(priority));
            actions.push_back({std::move(label), name, [this, repoId, priority] {
                                   ApplyPriority(repoId, priority);
                               }});
        }
        if (!chosen->repo.url.empty()) {
            const std::wstring url = chosen->repo.url;
            actions.push_back({L"Abrir en GitHub", name, [url] {
                                   ShellExecuteW(nullptr, L"open", url.c_str(), nullptr,
                                                 nullptr, SW_SHOWNORMAL);
                               }});
        }
    }

    actions.push_back({L"Sincronizar ahora", L"Ctrl+R", [this] { m_sync.Start(); }});
    actions.push_back(
        {L"Empezar la revisión semanal", L"Ctrl+Mayús+R", [this] { StartReview(); }});
    actions.push_back({L"Deshacer", L"Ctrl+Z", [this] { UndoLast(); }});

    for (const Entry& entry : m_state.Entries()) {
        const std::string repoId = entry.repo.id;
        actions.push_back({entry.repo.name, entry.repo.nameWithOwner,
                           [this, repoId] { RevealRepo(repoId); }});
    }

    m_host.PushLayer<Views::Palette>(
        Ui::Host::LayerOptions{/*modal*/ true, /*lightDismiss*/ true, /*scrim*/ true},
        std::move(actions));
    m_host.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);
    m_host.FlushNow();
}

// ------------------------------------------------------------- La revisión semanal --

Views::Review::Card Application::ReviewCardOf(const Entry& entry) {
    Views::Review::Card card;
    card.repoId = entry.repo.id;
    card.name = entry.repo.name;
    card.where = entry.repo.nameWithOwner;
    card.nextStep = entry.local.nextStep;
    card.priority = entry.local.priority;
    card.activity = entry.activity;

    // Por qué está en la pila. Se redacta AQUÍ y no en la vista, como el estado del modo
    // repo y el del indicador de sincronización: las vistas no traducen estados.
    switch (entry.mismatch) {
    case Model::Mismatch::FocusDormant:
        card.why = L"EN ENFOQUE Y SIN ACTIVIDAD";
        break;
    case Model::Mismatch::ArchivedButActive:
        card.why = L"ARCHIVADO, PERO LE SIGUEN LLEGANDO PUSHES";
        break;
    case Model::Mismatch::None:
        card.why = L"SIN CLASIFICAR";
        break;
    }

    card.meta = Ui::NameOf(entry.activity);
    if (!entry.repo.language.empty()) card.meta += L" · " + entry.repo.language;
    card.meta += L" · ";
    card.meta += entry.daysSincePush.has_value() ? AgoDays(*entry.daysSincePush)
                                                 : L"sin actividad";
    if (entry.repo.openIssues > 0 || entry.repo.openPrs > 0) {
        card.meta += L" · " + std::to_wstring(entry.repo.openIssues) + L" issues, " +
                     std::to_wstring(entry.repo.openPrs) + L" PR";
    }

    if (!entry.repo.commitTitle.empty()) {
        card.commit = entry.repo.commitTitle;
        if (entry.repo.commitDate.has_value()) {
            card.commit = Model::ToWide(Model::FormatDay(*entry.repo.commitDate)) + L" — " +
                          card.commit;
        }
    }

    // Las tres últimas novedades y no todas: lo que hace falta para recordar dónde se
    // quedó, no el historial. Se leen de SQLite aquí y no viven en App::State por lo mismo
    // que las del inspector — son listas, y con 109 repositorios serían quinientas filas
    // cargadas al arrancar para una pantalla que casi nunca se abre.
    if (m_db.IsOpen()) {
        Store::Repos repos(m_db);
        if (Model::Result<std::vector<Model::Novedad>> all = repos.NovedadesOf(entry.repo.id);
            all.IsOk()) {
            for (const Model::Novedad& one : all.Value()) {
                if (card.novedades.size() >= 3) break;
                card.novedades.push_back(Model::ToWide(one.day) + L" — " + one.text);
            }
        }
    }
    return card;
}

void Application::StartReview() {
    if (m_main == nullptr) return;
    if (m_main->Reviewing()) {
        Toast(L"La revisión ya está en marcha. Esc para salir.");
        return;
    }

    const std::vector<std::string> queue = ReviewQueue(m_state);
    if (queue.empty()) {
        // Y se dice POR QUÉ está vacía. Con los aplazados callados, la barra lateral diría
        // «Necesita decisión 3» y la revisión «no hay nada»: dos verdades que juntas se leen
        // como un fallo.
        const int parked = m_state.CountOf(Lens::Snoozed);
        Toast(parked > 0
                  ? L"Nada que revisar ahora: los " + std::to_wstring(parked) +
                        L" que quedaban están pospuestos. Los ves en «Pospuestos»."
                  : std::wstring(L"No hay nada que revisar: todo está clasificado y ninguno "
                                 L"se ha desajustado."));
        return;
    }

    std::vector<Views::Review::Card> cards;
    cards.reserve(queue.size());
    for (const std::string& id : queue) {
        const Entry* entry = m_state.EntryOf(id);
        if (entry != nullptr) cards.push_back(ReviewCardOf(*entry));
    }
    m_main->BeginReview(std::move(cards));
}

void Application::WireReview() {
    Views::Review* review = m_main ? m_main->Weekly() : nullptr;
    if (review == nullptr) return;

    review->OnDecide([this](const std::string& repoId, Model::Priority priority) {
        // El repositorio pudo irse de la cuenta mientras duraba la revisión. La tarjeta
        // sale igual: quedarse temblando sobre algo que ya no existe no es decir "no".
        if (m_state.EntryOf(repoId) == nullptr) return true;
        return ApplyPriority(repoId, priority);
    });
    review->OnSnooze([this](const std::string& repoId) { Snooze(repoId); });
    review->OnNextStep([this](const std::string& repoId, const std::wstring& text) {
        const Entry* entry = m_state.EntryOf(repoId);
        if (entry == nullptr || entry->local.nextStep == text) return;
        // Por el camino de siempre: primero SQLite y después la pantalla, y con él viene
        // gratis el modo repo y la entrada de deshacer.
        Model::Local local = entry->local;
        local.nextStep = text;
        SaveLocal(std::move(local));
    });
}

void Application::Snooze(const std::string& repoId) {
    const Entry* entry = m_state.EntryOf(repoId);
    if (entry == nullptr) return;

    Model::Local local = entry->local;
    const Model::Local before = local;
    // Se guarda LA PREGUNTA que se está aplazando, no solo hasta cuándo: el desajuste que
    // el repositorio tiene AHORA, con «ninguno» queriendo decir «está sin clasificar». Si
    // mientras dura el plazo aparece otro distinto, la revisión lo vuelve a preguntar.
    local.snoozeFor = entry->mismatch;
    local.snoozeUntil =
        Model::DayNumber(Now()) + static_cast<std::int64_t>(m_state.Thresholds().snoozeDays);
    SaveLocal(local);

    PushUndo(L"posponer " + entry->repo.name, [this, before] { SaveLocal(before); });
    Toast(entry->repo.name + L" vuelve a preguntarse dentro de " +
          std::to_wstring(m_state.Thresholds().snoozeDays) + L" días.");
}

// ------------------------------------------------------------------ El recordatorio --

std::wstring Application::ReminderText() const {
    if (m_reminderDay < 0) return L"Recordar la revisión: nunca";
    return L"Recordar la revisión: " + std::wstring(kWeekdays[m_reminderDay]) + L" a las " +
           std::to_wstring(m_reminderHour) + L":00";
}

void Application::LoadReminder() {
    m_reminderDay = -1;
    m_reminderHour = 9;
    if (!m_db.IsOpen()) return;

    Store::Repos repos(m_db);
    if (Model::Result<std::string> saved = repos.Setting(kReminderSetting); saved.IsOk()) {
        const std::string& text = saved.Value();
        const std::size_t colon = text.find(':');
        // Tolerante, como el parser de PROYECTO.md y por lo mismo: es texto que acaba en
        // una caché del usuario, y un ajuste ilegible no puede impedir que la aplicación
        // abra. Lo que no se entiende es "no recordar".
        if (colon != std::string::npos) {
            const int day = std::atoi(text.substr(0, colon).c_str());
            const int hour = std::atoi(text.substr(colon + 1).c_str());
            if (day >= 0 && day <= 6 && hour >= 0 && hour <= 23) {
                m_reminderDay = day;
                m_reminderHour = hour;
            }
        }
    }
    if (Model::Result<std::string> last = repos.Setting(kReminderFiredSetting); last.IsOk()) {
        m_reminderFired = last.Value();
    }
}

void Application::SetReminder(int weekday, int hour) {
    m_reminderDay = weekday;
    m_reminderHour = hour;
    if (m_db.IsOpen()) {
        Store::Repos repos(m_db);
        (void)repos.SetSetting(kReminderSetting,
                               weekday < 0 ? std::string()
                                           : std::to_string(weekday) + ":" +
                                                 std::to_string(hour));
    }
    ArmReminder();
    Toast(weekday < 0 ? L"No se recordará la revisión."
                      : L"Se avisará los " + std::wstring(kWeekdays[weekday]) + L" a las " +
                            std::to_wstring(hour) +
                            L":00, siempre que Brújula esté abierta.");
}

void Application::ArmReminder() {
    if (!m_reminder) {
        const auto queue = m_host.Queue();
        if (!queue) return;
        m_reminder = queue.CreateTimer();
        m_reminder.Interval(std::chrono::milliseconds(kReminderCheckMs));
        m_reminder.IsRepeating(true);
        m_reminder.Tick([this](auto&&, auto&&) { CheckReminder(); });
    }
    if (m_reminderDay < 0) {
        m_reminder.Stop();
        return;
    }
    m_reminder.Start();
    // Y se mira YA, sin esperar los cinco minutos: abrir la aplicación un viernes por la
    // tarde tiene que dar el aviso de ese viernes, no el del siguiente.
    CheckReminder();
}

void Application::CheckReminder() {
    if (m_reminderDay < 0) return;

    SYSTEMTIME local{};
    GetLocalTime(&local);
    if (static_cast<int>(local.wDayOfWeek) != m_reminderDay) return;
    // Pasada la hora, no A la hora: con la aplicación cerrada a las cinco y abierta a las
    // siete, el aviso sigue teniendo sentido. Al día siguiente ya no, y de eso se encarga
    // la comprobación del día de arriba.
    if (static_cast<int>(local.wHour) < m_reminderHour) return;

    // El día se escribe del MISMO reloj con el que se decidió disparar, que es el local. Con
    // Model::FormatDay —que da el día UTC, como debe— el aviso de un lunes a las nueve de la
    // noche se marcaba como del martes, y al cruzar la medianoche UTC el marcador dejaba de
    // coincidir y volvía a sonar esa misma noche. Dos avisos del mismo recordatorio.
    const auto two = [](int value) {
        return (value < 10 ? std::string("0") : std::string()) + std::to_string(value);
    };
    const std::string today = std::to_string(local.wYear) + "-" + two(local.wMonth) + "-" +
                              two(local.wDay);
    if (m_reminderFired == today) return;
    m_reminderFired = today;
    if (m_db.IsOpen()) {
        Store::Repos repos(m_db);
        (void)repos.SetSetting(kReminderFiredSetting, today);
    }

    // Y no se avisa de nada si no hay nada que revisar: un recordatorio que dice "no hay
    // nada" es el que hace que la gente apague los recordatorios.
    const std::size_t pending = ReviewQueue(m_state).size();
    if (pending == 0) return;

    const std::wstring body =
        std::to_wstring(pending) +
        (pending == 1 ? L" repositorio espera una decisión." : L" repositorios esperan una "
                                                               L"decisión.");
    if (!m_balloon.Show(m_window.Handle(), L"Revisión semanal", body)) {
        // Sin globo —notificaciones apagadas, o el shell dijo que no— el aviso se da por
        // dentro, que es lo que hace el resto del programa de todas maneras.
        Toast(L"Toca la revisión semanal: " + body + L" Ctrl+Mayús+R.");
    }
}

void Application::ShowReminderMenu(float x, float y) {
    std::vector<Ui::Menu::Entry> entries;
    entries.push_back({m_reminderDay < 0 ? L"No recordar (ahora)" : L"No recordar",
                       [this] { SetReminder(-1, m_reminderHour); }});
    for (int day = 0; day < 7; ++day) {
        std::wstring label = kWeekdays[day];
        if (day == m_reminderDay) label += L" (ahora)";
        entries.push_back({std::move(label), [this, day, x, y] {
                               // Y ahora la hora. Dos menús encadenados y no una hoja con
                               // treinta y un botones: elegir día y hora son dos preguntas,
                               // y juntarlas en una pantalla es una rejilla que hay que leer.
                               std::vector<Ui::Menu::Entry> hours;
                               for (const int hour : kReminderHours) {
                                   std::wstring text = std::to_wstring(hour) + L":00";
                                   if (hour == m_reminderHour) text += L" (ahora)";
                                   hours.push_back({std::move(text), [this, day, hour] {
                                                        SetReminder(day, hour);
                                                    }});
                               }
                               m_host.PushLayer<Ui::Menu>(
                                   Ui::Host::LayerOptions{false, true, false},
                                   std::move(hours), x, y);
                               m_host.Layout(m_window.WidthDip(), m_window.HeightDip(),
                                             m_window.Scale());
                               m_host.ApplyTheme(m_theme.Tokens(), 0.0f);
                               m_host.FlushNow();
                           }});
    }

    m_host.PushLayer<Ui::Menu>(Ui::Host::LayerOptions{false, true, false}, std::move(entries),
                               x, y);
    m_host.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);
    m_host.FlushNow();
}

void Application::RevealRepo(const std::string& repoId) {
    if (m_main == nullptr) return;
    // Si no se ve desde donde estamos —otra vista, o una búsqueda puesta— se va a Todos.
    // Encontrar algo en la paleta y que no aparezca sería encontrarlo y perderlo en el mismo
    // gesto.
    if (m_state.SlotOfId(repoId) < 0) m_main->ShowAll();
    const int slot = m_state.SlotOfId(repoId);
    if (slot >= 0) m_main->Reveal(slot);
}

void Application::WireView() {
    m_main->OnSyncRequested([this] { m_sync.Start(); });
    m_main->OnSettings([this](float x, float y) { ShowSettings(x, y); });
    m_main->OnSignOutRequested([this] {
        m_sync.SignOut();
        m_account.clear();
        m_main->SetAccount(m_account);
        Toast(L"Sesión cerrada. Los repositorios y las notas siguen aquí.");
    });
    m_main->OnLensChanged([this](Lens lens) { SaveLens(lens); });
    m_main->OnOpenInGitHub([this](int slot) { OpenInGitHub(slot); });
    m_main->OnPriority([this](int slot, Model::Priority priority) {
        const Entry* entry = m_state.At(slot);
        if (entry == nullptr) {
            // La tarjeta se fue de la vista entre el gesto y esto. Que no se quede en el aire.
            m_main->CancelDrop();
            return;
        }
        ApplyPriority(entry->repo.id, priority);
    });
    m_main->OnReorder([this](int from, int to) { ReorderVisible(from, to); });
    m_main->OnCardMenu([this](int slot, float x, float y) { ShowCardMenu(slot, x, y); });
    m_main->OnUndo([this] { UndoLast(); });
    m_main->OnPalette([this] { ShowPalette(); });
    WireInspector();
    WireReview();
}

void Application::OpenInGitHub(int slot) {
    const Entry* entry = m_state.At(slot);
    if (entry == nullptr || entry->repo.url.empty()) return;
    // El navegador del usuario, con la dirección que vino de la API. No se compone a mano:
    // un nombre con caracteres raros haría una dirección rota, y la buena ya la tenemos.
    ShellExecuteW(nullptr, L"open", entry->repo.url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ------------------------------------------------------------------------- La entrada --

void Application::WireInput() {
    m_window.callbacks.onPointer = [this](const Input::Pointer& pointer) {
        m_host.Input().Pointer(pointer);
    };

    m_window.callbacks.onKey = [this](const Input::Key& key) {
        if (!key.down) return false;
#if BRUJULA_CATALOGO
        if (key.virtualKey == VK_F12) {
            ToggleCatalog();
            return true;
        }
#endif
        // Ctrl+R y Ctrl+Mayús+R, de la tabla de atajos de CLAUDE.md. Se quedan aquí y no en
        // la vista porque quien sabe sincronizar y quien sabe montar la pila de la revisión
        // es App: la vista pide, no hace. Y el Mayús se mira PRIMERO: con la comprobación
        // al revés, Ctrl+Mayús+R sincronizaría y la revisión no se abriría nunca.
        if (key.virtualKey == 'R' && Input::Has(key.modifiers, Input::Modifiers::Control)) {
            if (Input::Has(key.modifiers, Input::Modifiers::Shift)) {
                StartReview();
            } else {
                m_sync.Start();
            }
            return true;
        }
        return m_host.Input().Key(key);
    };

    m_window.callbacks.onChar = [this](wchar_t unit) { m_host.Input().Char(unit); };

    m_window.callbacks.onSetCursor = [this](float x, float y) {
        const wchar_t* cursor = m_host.Input().CursorAt(x, y);
        if (!cursor) return false;
        SetCursor(LoadCursorW(nullptr, cursor));
        return true;
    };

    m_window.callbacks.onWindowFocus = [this](bool focused) {
        m_host.Input().WindowFocus(focused);
    };

    // Los menús y los avisos se cierran al perder el foco. La hoja de bienvenida NO: es
    // modal, y cerrarla al cambiar de ventana dejaría la aplicación sin manera de conectar.
    m_window.callbacks.onDeactivate = [this] {
        if (m_welcome == nullptr) m_host.PopAllLayers();
    };

    m_window.callbacks.onContextMenu = [this](float x, float y, bool keyboard) {
        if (keyboard) return;
        Input::Pointer pointer;
        pointer.action = Input::Action::Down;
        pointer.button = Input::Button::Right;
        pointer.x = x;
        pointer.y = y;
        m_host.Input().Pointer(pointer);
    };

    m_window.callbacks.onCaretRect = [this](float& x, float& y, float& height) {
        Ui::Rect caret;
        if (!m_host.CaretRect(caret)) return false;
        x = caret.x;
        y = caret.y;
        height = caret.height;
        return true;
    };

    m_window.callbacks.onFlush = [this] { m_host.FlushNow(); };

    // El globo del recordatorio. Pulsarlo abre la revisión, y para eso hay que traer la
    // ventana delante: puede estar minimizada o detrás, que es justo cuando un recordatorio
    // sirve para algo. Se puede porque el clic viene del shell y nos cede el primer plano.
    m_window.callbacks.onNotify = [this](LPARAM lparam) {
        if (!m_balloon.OnMessage(lparam)) return;
        if (IsIconic(m_window.Handle())) ShowWindow(m_window.Handle(), SW_RESTORE);
        SetForegroundWindow(m_window.Handle());
        StartReview();
    };
}

// ------------------------------------------------------------ El puente con el hilo --

void Application::OnSyncMessage() {
    // Reconocer ANTES de leer: si el trabajador cambia algo entre medias, volverá a publicar
    // y habrá otra vuelta. Al revés se perdería el último cambio.
    m_sync.Acknowledge();
    const Github::Progress progress = m_sync.Snapshot();
    // Los trabajos de un repositorio no viajan dentro del progreso: se sacan aparte, y se
    // sacan ANTES de recargar, porque uno de ellos pudo dejar escrito algo en SQLite.
    DrainJobs();

    if (progress.needsCredential && m_welcome == nullptr) ShowWelcome();

    if (progress.error.has_value() && progress.error->kind != Model::Fail::Cancelled) {
        const std::wstring message = std::wstring(Model::NameOf(progress.error->kind)) + L". " +
                                     progress.error->detail;
        if (message != m_shownError) {
            m_shownError = message;
            Toast(message);
        }
    } else if (!progress.error.has_value()) {
        m_shownError.clear();
    }

    // Y se vuelve a leer la caché entera. Con 109 repositorios son dos consultas y unos
    // pocos milisegundos, y a cambio la lista no puede quedarse a medias: no hay un camino
    // por el que la pantalla y SQLite acaben diciendo cosas distintas. La selección no se
    // guarda ni se restaura aquí: Ui::List la sigue por clave, así que el repositorio
    // elegido sigue elegido aunque cambie de sitio.
    LoadFromCache();
    if (m_main) {
        // Con animación: esto SÍ es una transición: lo que cambió de grupo se desliza a su
        // sitio y lo que llega entra escalonado. La identidad la lleva Ui::List por clave,
        // así que una tarjeta que solo cambia de posición no se repinta como si fuera otra.
        m_main->Reload(true);
    }
    RefreshChrome(progress);
    m_host.FlushNow();
}

void Application::RefreshChrome(const Github::Progress& progress) {
    if (m_main == nullptr) return;

    m_account = progress.account;
    m_main->SetAccount(m_account);

    Views::Chrome::Sync sync;
    sync.running = progress.running;
    sync.problem = progress.error.has_value() &&
                   progress.error->kind != Model::Fail::Cancelled;

    if (progress.running) {
        sync.text = NameOf(progress.stage);
        if (progress.stage == Github::Stage::Detail && progress.toEnrich > 0) {
            sync.text += L" " + std::to_wstring(progress.enriched) + L"/" +
                         std::to_wstring(progress.toEnrich);
        }
    } else if (sync.problem) {
        sync.text = progress.error->detail;
    } else if (progress.lastSync.has_value()) {
        sync.text = L"Al día · " +
                    AgoSeconds(Model::ToEpoch(Now()) - Model::ToEpoch(*progress.lastSync));
    } else {
        sync.text = L"Sin sincronizar";
    }

    if (progress.contentsForbidden) sync.text += L" · sin PROYECTO.md";

    m_main->SetSync(sync);
    m_main->SetSyncing(progress.running);
}

void Application::ShowWelcome() {
    m_welcome = m_host.PushLayer<Views::Welcome>(
        Ui::Host::LayerOptions{/*modal*/ true, /*lightDismiss*/ false, /*scrim*/ true});

    m_welcome->OnConnect([this](const std::wstring& pasted) {
        if (!Github::LooksLikeCredential(pasted)) {
            m_welcome->ShowProblem(L"Eso no parece un token de GitHub. Pégalo entero.");
            return;
        }

        Github::Secret credential;
        credential.Adopt(pasted);
        m_sync.UseCredential(std::move(credential));

        // La hoja se cierra en el siguiente turno de la cola y no aquí: estamos DENTRO de
        // una llamada suya, y destruirla ahora sería tirar el objeto que todavía está
        // ejecutándose. Es la misma cola que ya usa el aviso para desaparecer solo.
        m_host.Queue().TryEnqueue([this] {
            if (m_welcome != nullptr) {
                m_host.PopLayer(m_welcome);
                m_welcome = nullptr;
            }
            m_sync.Start();
        });
    });

    m_welcome->OnDismiss([this] { m_welcome = nullptr; });

    m_host.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);
    m_host.FlushNow();
}

void Application::Toast(const std::wstring& message) {
    // Un aviso discreto dentro de la aplicación, que es la regla 4 de arquitectura. El único
    // MessageBox que hay en todo el proyecto sigue siendo el de main.cpp, y sigue siendo
    // para "no se pudo crear la ventana", que es cuando no hay dónde enseñar un aviso.
    m_host.PushLayer<Ui::Toast>(Ui::Host::LayerOptions{false, false, false}, message, 4500);
    m_host.FlushNow();
}

#if BRUJULA_CATALOGO
void Application::ToggleCatalog() {
    if (m_catalog) {
        m_host.PopAllLayers();
        m_welcome = nullptr;
        m_catalog = nullptr;
        InstallMain();
        RefreshChrome(m_sync.Snapshot());
        m_host.FlushNow();
        return;
    }
    m_main = nullptr;
    m_catalog = m_host.SetRoot<Views::Catalog>();
    m_host.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);
    PushCaption();
    m_host.FlushNow();
}
#endif

void Application::ApplyTheme(float crossfadeMs) {
    // El marco lo pinta DWM, así que hay que decírselo aparte. Sin esto, una aplicación
    // entera en oscuro lleva una raya clara de borde.
    Shell::Backdrop::SetDarkFrame(m_window.Handle(),
                                  m_theme.Appearance() == Theme::Appearance::Dark);
    m_host.ApplyTheme(m_theme.Tokens(), crossfadeMs);
    m_host.FlushNow();
}

int Application::Run() {
    m_window.Show(SW_SHOW);
    // Y solo ahora se empieza a hablar con la red: lo que hay en la caché ya está pintado.
    m_sync.Start();
    return m_window.Run();
}

void Application::Shutdown() {
    // El reloj y el globo, antes que nada: el globo cuelga del HWND, y un icono del área de
    // notificación cuyo dueño ya no existe se queda ahí hasta que alguien pasa el ratón por
    // encima. Es el único rastro que esta aplicación puede dejar fuera de su ventana.
    if (m_reminder) m_reminder.Stop();
    m_balloon.Hide();

    // PRIMERO la sincronización, y Close() no vuelve hasta haber unido sus hilos. Un
    // trabajador que sobreviva a la ventana es el cuelgue más probable de toda la fase: se
    // despertaría con un HWND muerto y una base cerrada debajo.
    m_sync.Close();

    m_host.Close();
    m_theme.Close();
    m_scene.Close();
    m_db.Close();
}

}  // namespace App
