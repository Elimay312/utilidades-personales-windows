#pragma once

// El panel de estado de la fase 3, y solo de la fase 3.
//
// La vista principal es la fase 4. Esto está aquí para poder mirar de frente los dos
// criterios de aceptación —que los 109 repositorios entran en pocos segundos y que la
// segunda sincronización no vuelve a pedir el detalle— sin tener que abrir SQLite a mano.
// La fase 4 lo tira, igual que la fase 2 tenía que tirar a Views::Demo.
//
// No sabe de red ni de base de datos: recibe un Info ya masticado y enseña lo que pone. Es
// la regla 2 de arquitectura, y de paso es lo que permite que App sea el único sitio que
// conoce a la vez la sincronización y la pantalla.

#include <functional>
#include <string>

#include "ui/Element.h"

namespace Ui {
class Button;
class Label;
class Panel;
}  // namespace Ui

namespace Views {

class Status : public Ui::Element {
public:
    struct Info {
        std::wstring account;       // vacío si no hay cuenta conectada
        std::wstring stage;         // en qué anda ahora mismo
        std::wstring lastSync;      // "hace 2 minutos", o vacío si nunca
        std::wstring problem;       // el último error, si lo hubo
        int repos = 0;
        int enriched = 0;
        int gone = 0;
        int toEnrich = 0;
        int done = 0;
        bool running = false;
        bool contentsForbidden = false;
    };

    Status() = default;

    void Show(const Info& info);

    void OnSyncNow(std::function<void()> handler) { m_sync = std::move(handler); }
    void OnSignOut(std::function<void()> handler) { m_signOut = std::move(handler); }

protected:
    bool OnAttach() override;
    void OnArrange() override;

private:
    Ui::Panel* m_panel = nullptr;
    Ui::Label* m_count = nullptr;
    Ui::Label* m_account = nullptr;
    Ui::Label* m_stage = nullptr;
    Ui::Label* m_detail = nullptr;
    Ui::Label* m_problem = nullptr;
    Ui::Label* m_note = nullptr;
    Ui::Button* m_syncButton = nullptr;
    Ui::Button* m_signOutButton = nullptr;

    std::function<void()> m_sync;
    std::function<void()> m_signOut;
};

}  // namespace Views
