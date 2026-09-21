#include "app/App.h"

#include <shellapi.h>

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
    const Entry* entry = m_state.EntryOf(m_inspectorRepo);
    if (entry == nullptr || entry->local.priority == priority) return;

    if (priority == Model::Priority::Focus) {
        std::vector<Model::FocusEntry> inFocus;
        for (const Entry& other : m_state.Entries()) {
            if (other.local.priority != Model::Priority::Focus) continue;
            inFocus.push_back(Model::FocusEntry{other.repo.id, other.repo.pushedAt});
        }
        const Model::FocusPlan plan =
            Model::PlanFocus(inFocus, entry->repo.id, Now(), m_state.Thresholds());
        // El límite no se puede saltar (CLAUDE.md). La hoja para elegir a quién se baja es de
        // la fase 6; lo que esta fase no puede hacer es dejar pasar el sexto.
        if (!plan.fits) {
            Toast(L"Enfoque está lleno: caben " +
                  std::to_wstring(m_state.Thresholds().focusLimit) +
                  L". Baja uno a Secundario antes de subir otro.");
            return;
        }
    }

    Model::Local local = entry->local;
    local.priority = priority;
    SaveLocal(std::move(local));
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
    // Y por el camino de siempre: una novedad también es un cambio del repositorio, así que
    // pasa por SaveLocal para que se selle la fecha y, cuando toque, se encole la subida.
    SaveLocal(entry->local);
}

void Application::DeleteNovedad(std::int64_t id) {
    const Entry* entry = m_state.EntryOf(m_inspectorRepo);
    if (entry == nullptr || !m_db.IsOpen()) return;

    Store::Repos repos(m_db);
    if (Model::Outcome removed = repos.DeleteNovedad(id); !removed) {
        Toast(std::wstring(Model::NameOf(removed.Err().kind)) + L". " + removed.Err().detail);
        return;
    }
    SaveLocal(entry->local);
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

void Application::OpenFolder() {
    const Entry* entry = m_state.EntryOf(m_inspectorRepo);
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
    WireInspector();
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
        // Ctrl+R, de la tabla de atajos de CLAUDE.md. Se queda aquí y no en la vista porque
        // quien sabe sincronizar es App: la vista pide, no hace.
        if (key.virtualKey == 'R' && Input::Has(key.modifiers, Input::Modifiers::Control)) {
            m_sync.Start();
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
