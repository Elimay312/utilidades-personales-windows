#pragma once

#include "platform/GraphicsDevice.h"
#include "platform/Window.h"

// Mensajes que los hilos de trabajo usaran para despertar el bucle (PostMessageW).
constexpr UINT WM_APP_WAKE = WM_APP + 0;

class App {
public:
    ~App();

    bool Init();
    int Run();

private:
    void RequestFrames();
    void RenderFrame();
    void BuildUi();

    Window m_window;
    GraphicsDevice m_gfx;
    int m_pendingFrames = 0;
    bool m_running = true;
};
