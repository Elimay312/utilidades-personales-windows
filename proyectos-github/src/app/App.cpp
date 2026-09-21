#include "app/App.h"

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

Model::Instant Now() {
    return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
}

// "hace 2 minutos". En horario local no: la cuenta es de diferencias, y una diferencia no
// tiene huso. Lo único que se redondea es hacia abajo, que es como lo lee la gente.
std::wstring Ago(Model::Instant when, Model::Instant now) {
    const long long seconds = Model::ToEpoch(now) - Model::ToEpoch(when);
    if (seconds < 0) return L"hace un momento";
    if (seconds < 60) return L"hace un momento";
    if (seconds < 3600) {
        const long long minutes = seconds / 60;
        return L"hace " + std::to_wstring(minutes) + (minutes == 1 ? L" minuto" : L" minutos");
    }
    if (seconds < 86400) {
        const long long hours = seconds / 3600;
        return L"hace " + std::to_wstring(hours) + (hours == 1 ? L" hora" : L" horas");
    }
    const long long days = seconds / 86400;
    return L"hace " + std::to_wstring(days) + (days == 1 ? L" día" : L" días");
}

const wchar_t* NameOf(Github::Stage stage) {
    switch (stage) {
        case Github::Stage::Idle:       return L"Listo";
        case Github::Stage::Connecting: return L"Conectando con GitHub…";
        case Github::Stage::Metadata:   return L"Trayendo la lista de repositorios…";
        case Github::Stage::Detail:     return L"Trayendo el detalle de los que cambiaron…";
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

    if (!m_demo.Create(m_scene, m_device, m_animator, m_text)) return false;
    // El kit se monta DESPUÉS de la demo: sus dos raíces entran por encima, y así el
    // catálogo y las capas flotantes quedan sobre la vista de la fase 1.
    if (!m_host.Create(m_scene, m_device, m_animator, m_text, m_window.Handle())) return false;
    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);

    // La demo deja de ser lo que se ve. No se borra porque sigue dibujando los botones de la
    // ventana y porque es la única prueba viva de la transición compartida de la fase 1.
    m_demo.ShowContent(false);

    // La caché. El trabajador la abre y la migra; esta conexión es la de lectura del hilo de
    // UI, y las dos conviven porque la base está en WAL.
    //
    // Si esto falla, la aplicación arranca igual y lo dice en el panel: quedarse sin ventana
    // por un problema de disco sería peor que quedarse sin datos.
    if (Model::Result<std::string> path = Store::DatabasePath(); path.IsOk()) {
        const std::string file = path.Take();
        m_sync.Create(m_window.Handle(), Shell::Window::kSyncMessage, file);
        m_db.Open(file);
    }

    InstallStatus();

    m_window.callbacks.onLayout = [this](float width, float height, float scale) {
        m_scene.Layout(width, height, scale);
        m_demo.Layout(width, height, scale, m_window.CaptionLayout(), m_window.HasMica());
        // Sin cruce: redimensionar o cambiar de monitor no es una transición entre dos
        // estados, es el mismo estado en otro tamaño. Cruzarlo se vería como un rastro.
        m_demo.Repaint(m_theme.Tokens(), 0.0f);
        m_host.Layout(width, height, scale);
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
        m_demo.SetCaptionState(m_window.Hovered(), m_window.Pressed());
    };

    m_window.callbacks.onSync = [this] { OnSyncMessage(); };

    WireInput();

    // El primer fotograma completo, con la ventana todavía escondida.
    m_scene.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    m_demo.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale(),
                  m_window.CaptionLayout(), m_window.HasMica());
    ApplyTheme(0.0f);
    // Lo que ya había en la caché, pintado antes de que se vea la ventana. La regla 1 de
    // arquitectura en una línea: la UI no espera a la red.
    RefreshStatus(m_sync.Snapshot());
    m_host.FlushNow();
    return true;
}

void Application::InstallStatus() {
    m_status = m_host.SetRoot<Views::Status>();
    m_status->OnSyncNow([this] { m_sync.Start(); });
    m_status->OnSignOut([this] {
        m_sync.SignOut();
        Toast(L"Sesión cerrada. Los repositorios y las notas siguen aquí.");
    });
    m_host.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);
}

void Application::WireInput() {
    // Desde la fase 3 el kit tiene siempre una raíz, así que toda la entrada va al kit. La
    // demo ya no recibe nada: lo que queda de ella son los botones de la ventana, y esos los
    // reparte Shell::Window por el hit-test del marco.
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
        // Ctrl+R, de la tabla de atajos de CLAUDE.md. Es el único de la tabla que esta fase
        // puede cumplir: los demás necesitan la lista, que es la fase 4.
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

    RefreshStatus(progress);
}

void Application::RefreshStatus(const Github::Progress& progress) {
    if (m_status == nullptr) return;

    Views::Status::Info info;
    info.account = progress.account;
    info.stage = NameOf(progress.stage);
    info.running = progress.running;
    info.toEnrich = progress.toEnrich;
    info.done = progress.enriched;
    info.gone = progress.gone;
    info.contentsForbidden = progress.contentsForbidden;

    if (progress.error.has_value() && progress.error->kind != Model::Fail::Cancelled) {
        info.problem = progress.error->detail;
    }
    if (progress.lastSync.has_value()) info.lastSync = Ago(*progress.lastSync, Now());

    // La cuenta sale de SQLite y no del progreso: así lo que se enseña es lo que de verdad
    // hay guardado, incluso en el primer arranque, cuando no se ha sincronizado nada.
    if (m_db.IsOpen()) {
        Store::Repos repos(m_db);
        if (Model::Result<Store::Stats> counts = repos.Counts(); counts.IsOk()) {
            info.repos = counts.Value().total;
            info.enriched = counts.Value().enriched;
        }
    }

    m_status->Show(info);
    m_host.FlushNow();
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
        InstallStatus();
        RefreshStatus(m_sync.Snapshot());
        m_host.FlushNow();
        return;
    }
    m_status = nullptr;
    m_catalog = m_host.SetRoot<Views::Catalog>();
    m_host.Layout(m_window.WidthDip(), m_window.HeightDip(), m_window.Scale());
    m_host.ApplyTheme(m_theme.Tokens(), 0.0f);
    m_host.FlushNow();
}
#endif

void Application::ApplyTheme(float crossfadeMs) {
    // El marco lo pinta DWM, así que hay que decírselo aparte. Sin esto, una aplicación
    // entera en oscuro lleva una raya clara de borde.
    Shell::Backdrop::SetDarkFrame(m_window.Handle(),
                                  m_theme.Appearance() == Theme::Appearance::Dark);
    m_demo.Repaint(m_theme.Tokens(), crossfadeMs);
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
    m_demo.Close();
    m_theme.Close();
    m_scene.Close();
    m_db.Close();
}

}  // namespace App
