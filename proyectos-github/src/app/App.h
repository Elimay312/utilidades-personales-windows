#pragma once

// Monta las piezas y las conecta. Es el único sitio que conoce a todas, y a propósito:
// la ventana no sabe qué es Composition, el compositor no sabe qué es una tarjeta y la
// vista no sabe qué es un WM_.
//
// Desde la fase 3 también es dueño de los servicios de vida larga —la caché y la
// sincronización— y el único que ve a la vez el hilo de trabajo y la pantalla. El estado de
// la aplicación y el despacho de comandos siguen siendo trabajo de la fase 4.

#include <Windows.h>

#include <string>

#include "compositor/Device.h"
#include "compositor/Motion.h"
#include "compositor/Scene.h"
#include "github/Sync.h"
#include "shell/ThemeWatcher.h"
#include "shell/Window.h"
#include "store/Db.h"
#include "ui/Host.h"
#include "ui/Text.h"
#include "views/Demo.h"
#include "views/Status.h"
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

    // La raíz de la fase 3. La fase 4 la sustituye por la vista de verdad.
    void InstallStatus();
    // Llega del hilo de sincronización. Como el aviso del tema, no trae carga: se relee.
    void OnSyncMessage();
    void RefreshStatus(const Github::Progress& progress);
    void ShowWelcome();
    void Toast(const std::wstring& message);

#if BRUJULA_CATALOGO
    void ToggleCatalog();
    bool CatalogOpen() const { return m_catalog != nullptr; }
#else
    static constexpr bool CatalogOpen() { return false; }
#endif

    Shell::Window m_window;
    Shell::ThemeWatcher m_theme;
    Gfx::Scene m_scene;
    Gfx::Device m_device;
    Motion::Animator m_animator;
    Ui::Text m_text;
    Ui::Host m_host;
    Views::Demo m_demo;

    // La conexión de LECTURA del hilo de UI. El trabajador tiene la suya; las dos apuntan al
    // mismo archivo y conviven porque la base está en WAL.
    Store::Db m_db;
    Github::Sync m_sync;

    // Los dos son del Ui::Host, que los destruye; esto son punteros prestados.
    Views::Status* m_status = nullptr;
    Views::Welcome* m_welcome = nullptr;
    // El último error que ya se enseñó como aviso, para no repetirlo en cada mensaje.
    std::wstring m_shownError;

#if BRUJULA_CATALOGO
    Views::Catalog* m_catalog = nullptr;
#endif
};

}  // namespace App
