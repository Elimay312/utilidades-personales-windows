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
#include "ui/Text.h"
#include "views/Demo.h"

namespace App {

class Application {
public:
    bool Init(HINSTANCE instance);
    int Run();
    void Shutdown();

private:
    void ApplyTheme(float crossfadeMs);

    Shell::Window m_window;
    Shell::ThemeWatcher m_theme;
    Gfx::Scene m_scene;
    Gfx::Device m_device;
    Motion::Animator m_animator;
    Ui::Text m_text;
    Views::Demo m_demo;
};

}  // namespace App
