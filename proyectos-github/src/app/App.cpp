#include "app/App.h"

#include <shellapi.h>

#include "github/Auth.h"
#include "model/Time.h"
#include "model/Utf.h"
#include "shell/Backdrop.h"
#include "store/Paths.h"
#include "store/Repos.h"
#include "ui/Overlays.h"

namespace App {

namespace {

constexpr float kInitialWidthDip = 1180.0f;
constexpr float kInitialHeightDip = 760.0f;

// La clave de la vista elegida en la tabla de ajustes. Vuelve a abrir donde se dejó, que es
// lo que espera cualquiera que use la aplicación dos días seguidos.
constexpr char kLensSetting[] = "vista";

Model::Instant Now() {
    return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
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

void Application::WireView() {
    m_main->OnSyncRequested([this] { m_sync.Start(); });
    m_main->OnSignOutRequested([this] {
        m_sync.SignOut();
        m_account.clear();
        m_main->SetAccount(m_account);
        Toast(L"Sesión cerrada. Los repositorios y las notas siguen aquí.");
    });
    m_main->OnLensChanged([this](Lens lens) { SaveLens(lens); });
    m_main->OnOpenInGitHub([this](int slot) { OpenInGitHub(slot); });
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
