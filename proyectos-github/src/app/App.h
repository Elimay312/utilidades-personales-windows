#pragma once

// Monta las piezas y las conecta. Es el único sitio que conoce a todas, y a propósito:
// la ventana no sabe qué es Composition, el compositor no sabe qué es una tarjeta y la
// vista no sabe qué es un WM_.

#include <Windows.h>

#include "compositor/Device.h"
#include "compositor/Motion.h"
#include "compositor/Scene.h"
#include "shell/ThemeWatcher.h"
#include "shell/Window.h"
#include "ui/Host.h"
#include "ui/Text.h"
#include "views/Demo.h"

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
#if BRUJULA_CATALOGO
    // El catálogo es de Ui::Host, que lo destruye; esto es solo el puntero prestado que
    // dice si está abierto.
    Views::Catalog* m_catalog = nullptr;
#endif
};

}  // namespace App
