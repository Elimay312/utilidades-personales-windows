#pragma once

// La vista principal de la fase 4: barra de título, barra lateral y columna de tarjetas.
//
// Es la raíz del kit y sustituye a las dos vistas provisionales que había —Views::Demo de
// la fase 1 y Views::Status de la fase 3—, que ya decían en su cabecera que esta fase las
// tiraba.
//
// No habla con la red ni con SQLite. Recibe un App::State ya cargado, lo lee y avisa hacia
// arriba de lo que el usuario quiere; quien sabe de hilos y de bases de datos es App, que
// sigue siendo el único sitio que conoce a la vez el trabajador y la pantalla.

#include <functional>
#include <string>

#include "app/State.h"
#include "shell/Caption.h"
#include "ui/Element.h"
#include "views/Chrome.h"

namespace Views {

class RepoList;
class Sidebar;

class Main : public Ui::Element {
public:
    // El estado vive en App y dura más que esta vista. Aquí solo se lee y se le cambian la
    // vista elegida y la búsqueda, que son las dos cosas que el usuario decide desde aquí.
    void Bind(App::State* state);
    // En Windows 10 no hay Mica y el fondo lo ponemos nosotros.
    void SetMica(bool hasMica);

    void SetCaption(const Caption::Layout& layout, Caption::Zone hovered, Caption::Zone pressed);
    void SetSync(const Chrome::Sync& sync);
    void SetAccount(const std::wstring& account);
    void SetSyncing(bool running);

    // Vuelve a leer el estado entero: contadores de la barra lateral y lista.
    void Reload(bool animate);

    void OnSyncRequested(std::function<void()> handler) { m_syncRequested = std::move(handler); }
    void OnSignOutRequested(std::function<void()> handler) {
        m_signOutRequested = std::move(handler);
    }
    // Para guardar la vista elegida en los ajustes: volver a abrir donde se dejó.
    void OnLensChanged(std::function<void(App::Lens)> handler) {
        m_lensChanged = std::move(handler);
    }
    // Ctrl+O, de la tabla de atajos de CLAUDE.md. Llega con la posición elegida dentro de lo
    // visible; quien sabe abrir un navegador es App, no una vista.
    void OnOpenInGitHub(std::function<void(int)> handler) { m_openInGitHub = std::move(handler); }

    bool OnKey(const Input::Key& e) override;

protected:
    bool OnAttach() override;
    void OnArrange() override;
    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override;

private:
    void ChooseLens(App::Lens lens, bool fromSidebar);

    App::State* m_state = nullptr;
    Chrome* m_chrome = nullptr;
    Sidebar* m_sidebar = nullptr;
    RepoList* m_content = nullptr;

    std::function<void()> m_syncRequested;
    std::function<void()> m_signOutRequested;
    std::function<void(App::Lens)> m_lensChanged;
    std::function<void(int)> m_openInGitHub;

    bool m_hasMica = true;
};

}  // namespace Views
