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

#include <string>

#include "app/State.h"
#include "compositor/Device.h"
#include "compositor/Motion.h"
#include "compositor/Scene.h"
#include "github/Sync.h"
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
    void SetProjectState(Model::State state);
    void SetNextStep(const std::wstring& text);
    void AddNovedad(const std::wstring& text);
    void DeleteNovedad(std::int64_t id);
    void OpenFolder();
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
    // La vista guardada se lee una sola vez, al arrancar. Ver LoadFromCache.
    bool m_lensLoaded = false;

#if BRUJULA_CATALOGO
    Views::Catalog* m_catalog = nullptr;
#endif
};

}  // namespace App
