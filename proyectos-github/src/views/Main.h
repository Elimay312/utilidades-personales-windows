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
#include "compositor/Winrt.h"
#include "model/Types.h"
#include "shell/Caption.h"
#include "ui/Element.h"
#include "ui/List.h"
#include "views/Card.h"
#include "views/Chrome.h"
#include "views/Inspector.h"

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

    // --- La tarjeta levantada ------------------------------------------------------------
    //
    // Arrastrar y las teclas 1-4 terminan en el mismo sitio a propósito: la tarjeta se
    // levanta, vuela hasta su grupo y se apaga allí. Quien decide si el viaje se puede hacer
    // es App —es quien conoce el límite de Enfoque y quien escribe en SQLite—, así que esto
    // son tres verbos que App llama por su nombre y ninguna decisión.
    //
    // LiftCard no hace nada si ya hay una levantada (viene de un arrastre) o si esa tarjeta
    // no se ve. Las otras tres no hacen nada si no hay ninguna levantada. Entre las cuatro,
    // no queda un camino por el que una tarjeta se quede flotando.
    void LiftCard(int slot);
    void DropCardInto(Model::Priority priority);
    void CancelDrop();
    void RefuseDrop();

    int SelectedSlot() const;
    // Elegir una tarjeta y abrirle el inspector. Lo pide la paleta de comandos.
    void Reveal(int slot);
    // Todos y sin búsqueda: donde se ve cualquier repositorio, se haya clasificado o no.
    void ShowAll();

    // --- El inspector ------------------------------------------------------------------
    // Quien monta su contenido es App, que es quien sabe de SQLite. La vista solo dice
    // «hace falta el de esta posición» y lo abre o lo cierra.
    Inspector* Panel() const { return m_inspector; }
    bool InspectorOpen() const { return m_inspectorOpen; }
    void CloseInspector();
    void OnInspectorRequest(std::function<void(int)> handler) {
        m_inspectorRequest = std::move(handler);
    }
    void OnInspectorClosed(std::function<void()> handler) {
        m_inspectorClosed = std::move(handler);
    }

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
    // El menú de ajustes del pie de la barra lateral, con el punto donde abrirlo.
    void OnSettings(std::function<void(float, float)> handler) {
        m_settings = std::move(handler);
    }
    // Una tarjeta quiere cambiar de grupo: arrastrada hasta la barra lateral, o con las
    // teclas 1-4. Llega con la posición dentro de lo visible, como el resto.
    void OnPriority(std::function<void(int, Model::Priority)> handler) {
        m_priorityRequested = std::move(handler);
    }
    // Soltada entre otras dos de la misma lista.
    void OnReorder(std::function<void(int, int)> handler) { m_reorder = std::move(handler); }
    // Botón derecho sobre una tarjeta, con el punto donde abrir el menú.
    void OnCardMenu(std::function<void(int, float, float)> handler) {
        m_cardMenu = std::move(handler);
    }
    void OnUndo(std::function<void()> handler) { m_undo = std::move(handler); }
    // Ctrl+K. Se pide desde aquí y no desde App —donde viven Ctrl+R y F12— a propósito: ahí
    // arriba las teclas se miran ANTES que el enrutador, y entonces Ctrl+K con la paleta ya
    // abierta abriría una segunda encima de la primera.
    void OnPalette(std::function<void()> handler) { m_palette = std::move(handler); }

    bool OnKey(const Input::Key& e) override;

protected:
    bool OnAttach() override;
    void OnArrange() override;
    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override;

private:
    void ChooseLens(App::Lens lens, bool fromSidebar);
    void OpenInspector(int slot);
    bool BeginCardDrag(int slot, float x, float y);
    void MoveCardDrag(float x, float y);
    void FinishCardDrag(const Ui::List::Drop& drop);
    // El final de todos los caminos: la tarjeta cae en 'target' y se apaga allí.
    void DropCardAt(const Ui::Rect& target);
    // Coloca la barra lateral, la columna y la barra de título. Aparte de OnArrange porque
    // abrir el inspector las recoloca SIN recolocar el propio inspector: ese viaja con un
    // muelle, y un SetFrame a mitad de camino lo dejaría clavado en su destino.
    void LayoutColumns();
    Ui::Rect InspectorFrame() const;
    // Dónde está la tarjeta elegida, o un rectángulo vacío si no se ve. Es el otro extremo
    // de la transición compartida.
    Ui::Rect SelectedCardRect() const;

    App::State* m_state = nullptr;
    Chrome* m_chrome = nullptr;
    Sidebar* m_sidebar = nullptr;
    RepoList* m_content = nullptr;
    Inspector* m_inspector = nullptr;
    // La única tarjeta que puede estar en el aire. Se crea al arrancar y vive escondida.
    DragCard* m_drag = nullptr;

    std::function<void()> m_syncRequested;
    std::function<void()> m_signOutRequested;
    std::function<void(App::Lens)> m_lensChanged;
    std::function<void(int)> m_openInGitHub;
    std::function<void(float, float)> m_settings;
    std::function<void(int)> m_inspectorRequest;
    std::function<void()> m_inspectorClosed;
    std::function<void(int, Model::Priority)> m_priorityRequested;
    std::function<void(int, int)> m_reorder;
    std::function<void(int, float, float)> m_cardMenu;
    std::function<void()> m_undo;
    std::function<void()> m_palette;

    // Se esconde cuando el muelle de vuelta ha terminado, no antes: mientras viaja hay que
    // seguir viéndolo. Sin temporizador habría que despertar al hilo en cada fotograma para
    // preguntar si ya llegó, que es justo lo que este proyecto no hace.
    winrt::Windows::System::DispatcherQueueTimer m_hideInspector{nullptr};

    bool m_hasMica = true;
    bool m_inspectorOpen = false;
    // Hay una tarjeta levantada. No es lo mismo que "el ratón está arrastrando": las teclas
    // 1-4 también levantan una, y entonces no hay puntero de por medio.
    bool m_lifted = false;
    Ui::Rect m_dragHome;
    // Por dónde se agarró, para que la tarjeta no salte bajo el puntero al levantarla.
    float m_grabX = 0.0f;
    float m_grabY = 0.0f;
};

}  // namespace Views
