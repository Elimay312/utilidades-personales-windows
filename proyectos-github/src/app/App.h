#pragma once

// Monta las piezas y las conecta. Es el único sitio que conoce a todas, y a propósito:
// la ventana no sabe qué es Composition, el compositor no sabe qué es una tarjeta y la
// vista no sabe qué es un WM_.
//
// Desde la fase 3 también es dueño de los servicios de vida larga —la caché y la
// sincronización— y el único que ve a la vez el hilo de trabajo y la pantalla. La fase 4
// añade el estado de la aplicación: App::State vive aquí, se carga desde SQLite ANTES de
// que la ventana se vea, y la vista lo lee.

#include <Windows.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "app/State.h"
#include "compositor/Device.h"
#include "compositor/Motion.h"
#include "compositor/Scene.h"
#include "github/Sync.h"
#include "shell/Notify.h"
#include "shell/ThemeWatcher.h"
#include "shell/Window.h"
#include "store/Db.h"
#include "ui/Host.h"
#include "ui/Text.h"
#include "views/Main.h"
#include "views/Welcome.h"

#if BRUJULA_CATALOGO
#include "views/Catalog.h"
#endif

namespace Ui {
class Sheet;
}

namespace App {

class Application {
public:
    // El sello del WM_COPYDATA con el que una segunda instancia nos pasa qué abrir. Vive
    // aquí porque lo usan main.cpp (al mandarlo) y Shell::Window (al filtrarlo).
    static constexpr ULONG_PTR kOpenRepoCopyData = Shell::Window::kOpenRepoCopyData;

    // Qué repositorio se pidió abrir al arrancar: --repo NOMBRE o brujula://repo/NOMBRE.
    // Se guarda y no se abre todavía porque cuando esto se llama no hay ni ventana.
    void SetPendingRepo(std::wstring name) { m_pendingRepo = std::move(name); }

    bool Init(HINSTANCE instance);
    int Run();
    void Shutdown();

private:
    void ApplyTheme(float crossfadeMs);
    void WireInput();
    void WireView();

    void InstallMain();
    // --- El inspector. App es quien sabe de SQLite y de hilos; la vista solo pide. --------
    void WireInspector();
    void BindInspector(const std::string& repoId);
    void RebindInspector();
    // El camino de TODA edición, y en este orden: primero SQLite, después el estado y la
    // pantalla. El criterio de aceptación de la fase —editar el siguiente paso y cerrar la
    // aplicación conserva el cambio— no puede depender de que haya red ni de que el modo
    // repo esté encendido, así que lo primero que pasa siempre es que se guarda.
    void SaveLocal(Model::Local local);
    void SetPriority(Model::Priority priority);
    // --- Priorizar: el único camino ------------------------------------------------------
    //
    // Todo lo que cambia una prioridad pasa por aquí: el menú del inspector, las teclas 1-4,
    // arrastrar hasta la barra lateral, el menú contextual y la paleta. Es lo que hace que
    // "no se puede tener más en Enfoque que el límite" sea cierto y no cinco veces cierto:
    // con la comprobación copiada en cada sitio, el sexto entra por el camino que se olvidó.
    // Devuelve si se pudo: false es "Enfoque está lleno", y el único que lo mira es la
    // revisión semanal, que necesita saber si la tarjeta sale volando o se queda temblando.
    // Los otros cuatro caminos ya lo ven en la pantalla y no preguntan.
    bool ApplyPriority(const std::string& repoId, Model::Priority priority);
    // Enfoque está lleno: la hoja que pregunta cuál baja a Secundario.
    void AskWhoLeavesFocus(const std::string& candidateId,
                           const std::vector<std::string>& demote);
    // Soltar una tarjeta entre otras dos: escribe el orden de TODA la vista de una tacada.
    void ReorderVisible(int from, int to);
    // El menú contextual de una tarjeta, con todo lo que se puede hacer con ella.
    void ShowCardMenu(int slot, float x, float y);
    void ShowPalette();

    // --- La revisión semanal --------------------------------------------------------------
    //
    // App monta la pila y contesta a lo que la vista pregunta; la vista no toca SQLite ni
    // sabe qué es un desajuste. Quiénes entran y en qué orden lo decide App::ReviewQueue,
    // que es puro y tiene prueba: una pila a la que le falte alguien no da un error, da una
    // revisión más corta — y una revisión más corta se parece mucho a una terminada.
    void StartReview();
    void WireReview();
    // «Esta pregunta, no ahora». Guarda hasta cuándo y, sobre todo, QUÉ se aplazó.
    void Snooze(const std::string& repoId);
    // De un repositorio a una tarjeta de la pila: aquí se redacta todo lo que la vista
    // enseña, incluido por qué está ahí.
    Views::Review::Card ReviewCardOf(const Entry& entry);

    // --- El recordatorio ------------------------------------------------------------------
    void ShowReminderMenu(float x, float y);
    void SetReminder(int weekday, int hour);
    void LoadReminder();
    // Arranca o para el reloj según haya recordatorio puesto. Se llama al cargar y al
    // cambiarlo: un reloj despertando al hilo cada cinco minutos para nada sería tener un
    // tic, que es justo lo que esta aplicación no tiene.
    void ArmReminder();
    void CheckReminder();
    std::wstring ReminderText() const;
    // Elegir un repositorio y abrirle el inspector, venga de donde venga. Cambia de vista si
    // hace falta: buscar algo en la paleta y que no aparezca porque estaba filtrado sería
    // encontrarlo y perderlo en el mismo gesto.
    void RevealRepo(const std::string& repoId);
    // Por NOMBRE y no por identificador: es lo que traen la URL y la línea de órdenes, y
    // nadie va a escribir un node id de GraphQL en un lanzador. Si no está en la caché lo
    // dice con un aviso, que es más útil que no hacer nada: casi siempre significa que ese
    // repositorio todavía no se ha sincronizado.
    void OpenRepoByName(const std::wstring& name);
    // Deja registrado brujula:// en HKCU apuntando a ESTE ejecutable. Ver App.cpp.
    void RegisterUrlScheme();
    void SetProjectState(Model::State state);
    void SetNextStep(const std::wstring& text);
    void AddNovedad(const std::wstring& text);
    void DeleteNovedad(std::int64_t id);
    // Por identificador de repositorio y no por "el del inspector": deshacer una novedad
    // borrada tiene que funcionar aunque el panel se haya cerrado o esté en otro.
    void DeleteNovedadOf(const std::string& repoId, std::int64_t id);
    void OpenFolder();
    void OpenFolderOf(const std::string& repoId);
    // El interruptor del modo repo. Encenderlo en un repositorio que nunca se ha confirmado
    // abre la hoja; apagarlo no pregunta nada.
    void SetRepoMode(bool wanted);
    void ConfirmRepoMode(const Entry& entry);
    // Una hoja modal con dos salidas, ya colocada y con el tema puesto. Las tres preguntas
    // de esta fase se montan igual, y montarlas tres veces sería tres sitios donde olvidarse
    // del ApplyTheme y quedarse con una hoja en el tema de antes.
    Ui::Sheet* Ask(std::wstring title, std::wstring body, std::wstring accept,
                   std::wstring cancel);
    // El botón «Importar»: lo que dice el PROYECTO.md del repositorio pasa a ser lo de aquí.
    // Pregunta antes, porque pisa lo que el usuario tenga escrito.
    void ImportProyecto();
    // Los trabajos que terminaron en el hilo de GitHub: un commit, o el texto de un .md.
    void DrainJobs();
    // Llegó el texto de un .md de la raíz: se enseña lo que se copiaría antes de copiarlo.
    void OnFileArrived(const Github::JobResult& done);

    // --- Los ajustes del pie de la barra lateral -----------------------------------------
    void ShowSettings(float x, float y);
    // La hoja de «Acerca de»: quién es esto, qué versión y cuánto tardó en abrirse.
    void ShowAbout();
    void ChooseReposRoot();
    void ExportBackup();
    void ImportBackup();
    void ToggleRepoModeDefault();
    void ChooseFolderFor(const std::string& repoId);
    // La carpeta que abriría el botón: la del repositorio si se eligió, y si no la que sale
    // de la raíz configurada. Vacía si no hay ninguna que exista.
    std::wstring ResolveFolder(const Entry& entry) const;
    // Lee la caché entera y la mete en el estado. Es lo que corre antes de enseñar la
    // ventana, y por eso son dos consultas y no ciento diez.
    void LoadFromCache();
    // Llega del hilo de sincronización. Como el aviso del tema, no trae carga: se relee.
    void OnSyncMessage();
    void RefreshChrome(const Github::Progress& progress);
    void ShowWelcome();
    void Toast(const std::wstring& message);
    void OpenInGitHub(int slot);
    void SaveLens(Lens lens);

    // --- Deshacer -------------------------------------------------------------------------
    //
    // Una pila de "cómo se deshace esto", no de "qué pasó". Deshacer un cambio de prioridad y
    // deshacer una novedad borrada no se parecen en nada salvo en que los dos saben volver
    // atrás, y guardar la vuelta atrás ya hecha es lo que evita tener un tipo de evento por
    // cada cosa que la aplicación sabe cambiar.
    struct Undo {
        std::wstring said;
        std::function<void()> apply;
    };
    void PushUndo(std::wstring said, std::function<void()> apply);
    void UndoLast();
    // Escribe pares (repositorio, orden) de una sentada y en una transacción. Lo usan el
    // arrastre y su deshacer, que son la misma escritura con otros números.
    bool WriteOrder(const std::vector<std::pair<std::string, int>>& orders);
    // Reparte el estado de la barra de título a la raíz que haya puesta. Son dos —la vista
    // principal y el catálogo de Debug— y las dos llevan un Views::Chrome, porque los
    // botones de la ventana se dibujan aquí dentro y una raíz sin ellos deja la ventana sin
    // aspa a la vista.
    void PushCaption();

#if BRUJULA_CATALOGO
    void ToggleCatalog();
#endif

    Shell::Window m_window;
    Shell::ThemeWatcher m_theme;
    Gfx::Scene m_scene;
    Gfx::Device m_device;
    Motion::Animator m_animator;
    Ui::Text m_text;
    Ui::Host m_host;

    // La conexión de LECTURA del hilo de UI. El trabajador tiene la suya; las dos apuntan al
    // mismo archivo y conviven porque la base está en WAL.
    Store::Db m_db;
    Github::Sync m_sync;
    State m_state;

    // Los dos son del Ui::Host, que los destruye; esto son punteros prestados.
    Views::Main* m_main = nullptr;
    Views::Welcome* m_welcome = nullptr;
    // El último error que ya se enseñó como aviso, para no repetirlo en cada mensaje.
    std::wstring m_shownError;
    // A qué repositorio está enganchado el inspector. Se guarda el IDENTIFICADOR y no la
    // posición: la lista se reordena y se filtra debajo, y una posición guardada acabaría
    // apuntando a otro repositorio sin dar ningún error.
    std::string m_inspectorRepo;
    // Dónde clona el usuario sus repositorios. Vacío hasta que lo elija: sin ella, el botón
    // de la carpeta local invita a elegirla en vez de abrir la que no es.
    std::wstring m_reposRoot;
    // Los repositorios nuevos nacen con el interruptor puesto. NO se saltan la confirmación:
    // escribir sigue exigiendo que alguien diga que sí en ESE repositorio (SEGURIDAD.md,
    // regla 5). Lo único que ahorra es tener que encenderlo a mano cada vez.
    bool m_repoModeDefault = false;
    std::wstring m_account;
    // Lo que se pidió abrir al arrancar, hasta que haya ventana donde abrirlo.
    std::wstring m_pendingRepo;
    // La vista guardada se lee una sola vez, al arrancar. Ver LoadFromCache.
    bool m_lensLoaded = false;

    // --- El recordatorio de la revisión ---------------------------------------------------
    // El día es el de Windows (0 = domingo) y -1 es "no recordar". El día en que sonó por
    // última vez se guarda como texto en los ajustes y no en memoria: sin eso, abrir la
    // aplicación dos veces el viernes por la tarde da dos avisos del mismo recordatorio.
    Shell::Balloon m_balloon;
    int m_reminderDay = -1;
    int m_reminderHour = 9;
    std::string m_reminderFired;
    winrt::Windows::System::DispatcherQueueTimer m_reminder{nullptr};

    std::vector<Undo> m_undo;
    // Deshacer no se apunta a sí mismo. Sin esto, la primera vuelta atrás dejaría en la pila
    // cómo deshacerla, y Ctrl+Z dos veces se quedaría meciendo el mismo cambio para siempre.
    // Lo usan también los cambios que van en pareja —bajar uno de Enfoque y subir otro—, que
    // se apuntan como UNA entrada.
    bool m_quietUndo = false;

#if BRUJULA_CATALOGO
    Views::Catalog* m_catalog = nullptr;
#endif
};

}  // namespace App
