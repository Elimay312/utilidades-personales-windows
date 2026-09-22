#pragma once

// La revisión semanal (Ctrl+Mayús+R): el modo a pantalla completa donde se decide, una
// tarjeta detrás de otra y solo con el teclado, qué se hace con los repositorios que piden
// una decisión.
//
// **No decide nada.** Pregunta hacia arriba —igual que Views::Inspector y Views::Palette—
// y quien contesta es App, que es quien conoce el límite de Enfoque y quien escribe en
// SQLite. La única respuesta que esta vista entiende es sí o no: con un sí la tarjeta sale
// volando hacia el color de su grupo, con un no tiembla y se queda donde está.
//
// **Y no es una capa flotante del Host, a propósito.** Las capas se cierran TODAS cuando
// la ventana pierde el foco —`onDeactivate` llama a `PopAllLayers`— y una revisión a
// medias que desaparece por mirar el navegador un momento perdería por dónde iba. Es un
// hijo de Views::Main que ocupa la ventana entera, exactamente por el mismo motivo por el
// que Views::DragCard tampoco es una capa.
//
// **Las tarjetas son DOS y se alternan.** La que sale tiene que seguir viéndose mientras
// la siguiente entra, o entre una decisión y la otra hay un hueco en blanco; y con veinte
// repositorios en dos minutos ese hueco es la mitad del tiempo. Cada una es dueña de su
// superficie porque cada una vuela por su lado.

#include <functional>
#include <string>
#include <vector>

#include "model/Types.h"
#include "ui/Element.h"

namespace Ui {
class Field;
class Panel;
}  // namespace Ui

namespace Views {

class Review : public Ui::Element {
public:
    // Un repositorio de la pila, con todo ya redactado. Esta vista no traduce estados ni
    // consulta SQLite: es la misma regla que cumplen el inspector y la paleta, y aquí
    // también es lo que permite que la pila sobreviva a una sincronización que termine a
    // mitad de la revisión y reconstruya el App::State entero por debajo.
    struct Card {
        std::string repoId;
        std::wstring name;
        std::wstring where;      // dueño/nombre
        std::wstring why;        // por qué está en la pila
        std::wstring nextStep;   // vacío: la tarjeta lo dice y E invita a escribirlo
        std::wstring meta;       // "Activo · C++ · hace 3 días"
        std::wstring commit;     // el último commit de la rama principal
        // Las tres últimas, ya con su fecha delante. Tres y no todas: lo que hace falta
        // para recordar dónde se quedó, no el historial.
        std::vector<std::wstring> novedades;
        Model::Priority priority = Model::Priority::Unsorted;
        Model::Activity activity = Model::Activity::Dormant;
    };

    // Arranca la sesión. Con la pila vacía no hace nada: quien llama lo comprueba antes y
    // lo dice con un aviso, que es más útil que una pantalla que solo sabe estar vacía.
    void Begin(std::vector<Card> cards);
    bool Running() const { return m_running; }
    // Termina y devuelve la ventana a la lista. Lo llama Esc y lo llama App al cerrar.
    void Finish();

    // Las teclas de la revisión, que se las pasa Views::Main mientras está corriendo. Van
    // por ahí y no por el foco porque la tarjeta no es un control: no hay nada que enfocar
    // salvo el campo del siguiente paso, y ese sí se lleva las suyas antes que nadie.
    bool Keys(const Input::Key& e);

    // El límite de Enfoque puede decir que no, así que esto devuelve si se pudo. Es el
    // mismo trato que tiene el arrastre con App::ApplyPriority, y por el mismo motivo.
    void OnDecide(std::function<bool(const std::string&, Model::Priority)> handler) {
        m_decide = std::move(handler);
    }
    void OnNextStep(std::function<void(const std::string&, const std::wstring&)> handler) {
        m_nextStep = std::move(handler);
    }
    void OnFinished(std::function<void()> handler) { m_finished = std::move(handler); }
    // La hoja del límite de Enfoque acabó eligiendo a quién bajar: la tarjeta que se había
    // quedado esperando sale ahora.
    void Accepted(const std::string& repoId, Model::Priority priority);

    // Se lleva toda la entrada: detrás no hay una lista con la que se pueda interactuar,
    // hay una lista apagada.
    bool ClipsInput() const override { return true; }

protected:
    bool OnAttach() override;
    void OnArrange() override;
    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override;

private:
    class Board;
    class Bar;
    class CardView;

    // Enseña la tarjeta de m_at en la cara que toque y deja la otra libre.
    void ShowCurrent(bool animate);
    void Decide(Model::Priority priority);
    void Skip();
    void BeginEdit();
    void CommitEdit();
    bool CancelEdit();
    bool Editing() const;
    // La tarjeta de delante sale hacia la diana de 'priority' y entra la siguiente.
    void Advance(Model::Priority priority);
    void UpdateProgress(bool animate);
    void ShowSummary();
    void PaintHeader(const Ui::Paint& paint, const Ui::Rect& box);
    void PaintFooter(const Ui::Paint& paint, const Ui::Rect& box);
    void PaintSummary(const Ui::Paint& paint, const Ui::Rect& box);
    // Dónde cae la tarjeta cuando está delante, en coordenadas de esta vista.
    Ui::Rect CardFrame() const;

    std::vector<Card> m_cards;
    // Cuántas quedan por decidir se lee de m_cards.size() - m_at; cuántas se decidieron de
    // verdad, de m_decided, que no cuenta las saltadas.
    int m_at = 0;
    int m_decided = 0;
    int m_total = 0;
    // Cuántas hay en cada prioridad al terminar, para el resumen. Se cuenta aquí y no se
    // le pregunta al estado: el resumen habla de lo que ACABA de pasar, y el estado
    // contesta lo que hay ahora, que después de una sincronización no es lo mismo.
    int m_tally[5] = {};

    Board* m_header = nullptr;
    Board* m_footer = nullptr;
    Bar* m_track = nullptr;
    Bar* m_fill = nullptr;
    // Las dos caras. m_front es la de delante; la otra es la que acaba de salir volando o
    // la que todavía no ha entrado.
    CardView* m_faces[2] = {nullptr, nullptr};
    int m_front = 0;
    // Los dos fantasmas de debajo, que son lo que convierte una tarjeta en una pila. Son
    // materiales y nada más: no llevan texto porque no se lee nada de ellos.
    Bar* m_stack[2] = {nullptr, nullptr};
    Ui::Field* m_step = nullptr;

    Ui::Panel* m_summary = nullptr;
    Board* m_summaryBoard = nullptr;
    Bar* m_summaryBars[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};

    std::function<bool(const std::string&, Model::Priority)> m_decide;
    std::function<void(const std::string&, const std::wstring&)> m_nextStep;
    std::function<void()> m_finished;

    // Se esconde cuando el fundido de salida ha terminado, no antes: mientras se va hay
    // que seguir viéndolo. Es el mismo temporizador que usa Views::Main para el inspector
    // y por lo mismo: sin él habría que despertar al hilo en cada fotograma para preguntar
    // si ya llegó, que es justo lo que este proyecto no hace.
    winrt::Windows::System::DispatcherQueueTimer m_hide{nullptr};

    bool m_running = false;
    bool m_done = false;  // se acabó la pila: se ve el resumen
};

}  // namespace Views
