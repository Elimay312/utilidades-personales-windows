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
    std::wstring m_account;
    // La vista guardada se lee una sola vez, al arrancar. Ver LoadFromCache.
    bool m_lensLoaded = false;

#if BRUJULA_CATALOGO
    Views::Catalog* m_catalog = nullptr;
#endif
};

}  // namespace App
