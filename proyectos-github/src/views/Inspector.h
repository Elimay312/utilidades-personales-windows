#pragma once

// El inspector: el panel de la derecha donde se edita lo que esta aplicación existe para
// guardar — la prioridad, el estado, el siguiente paso y las novedades — y donde se lee lo
// que GitHub cuenta de ese repositorio.
//
// **No toca SQLite ni la red.** Recibe un Content ya montado y emite comandos hacia arriba;
// quien sabe de bases de datos y de hilos es App. Es la regla 2 de arquitectura, igual que
// Views::RepoList, y aquí importa más que en ninguna otra vista: lo que se escribe desde
// aquí es lo único del programa que no se puede volver a descargar.
//
// **Y no se queda con punteros al estado.** Content se COPIA. App::State se reconstruye
// entero después de cada sincronización —dos consultas y unos milisegundos, que fue la
// decisión de la fase 4— así que un puntero a una App::Entry guardado aquí apuntaría a
// memoria liberada en cuanto llegara el hilo de trabajo.
//
// **El panel no tiene superficie propia, y es a propósito.** Element::MorphTo solo puede
// animar la forma de un elemento sin Gfx::Layer; con textura habría que reasignarla en cada
// fotograma. Así que el texto estático vive en UNA pizarra que ocupa todo el panel, y
// encima van los elementos que reciben entrada. Es la regla del orden en z de ui/Element.h
// usada del derecho: la pizarra se añade primero y queda debajo.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "model/Types.h"
#include "ui/Element.h"

namespace Ui {
class Button;
class Field;
class IconButton;
class Label;
class List;
class Rule;
class Slate;
}  // namespace Ui

namespace Views {

class Inspector : public Ui::Element {
public:
    // Todo lo que el panel enseña, ya resuelto por quien sabe: nada de esto se deduce aquí.
    struct Content {
        std::string repoId;
        std::wstring name;
        std::wstring nameWithOwner;
        std::wstring description;
        std::wstring url;
        Model::Activity activity = Model::Activity::Dormant;
        std::optional<std::int64_t> daysSincePush;
        int openIssues = 0;
        int openPrs = 0;
        bool gone = false;
        bool hasProyecto = false;

        Model::Local local;
        std::vector<Model::Commit> commits;
        std::vector<Model::Novedad> novedades;
        std::vector<std::wstring> rootMarkdown;

        // La carpeta que abriría el botón, ya resuelta. Vacía: el botón invita a elegirla.
        std::wstring folder;
        // El estado del modo repo, redactado arriba. Esta vista no traduce estados.
        std::wstring repoModeText;
    };

    void Bind(const Content& content);
    const std::string& RepoId() const { return m_content.repoId; }

    // --- Teclado, desde Views::Main -------------------------------------------------
    void FocusNextStep();
    void BeginNovedad();
    // Hay un campo de texto con el foco. Con esto, la «n» de «pantalla» no abre una novedad.
    bool Editing() const;
    // Esc: primero cancela la edición que haya, y solo si no hay ninguna cierra el panel.
    // Devuelve true si se comió la tecla.
    bool CancelEditing();

    void OnClose(std::function<void()> handler) { m_close = std::move(handler); }
    void OnPriority(std::function<void(Model::Priority)> handler) {
        m_priorityChanged = std::move(handler);
    }
    void OnState(std::function<void(Model::State)> handler) {
        m_stateChanged = std::move(handler);
    }
    void OnNextStep(std::function<void(const std::wstring&)> handler) {
        m_nextStep = std::move(handler);
    }
    void OnAddNovedad(std::function<void(const std::wstring&)> handler) {
        m_addNovedad = std::move(handler);
    }
    void OnDeleteNovedad(std::function<void(std::int64_t)> handler) {
        m_deleteNovedad = std::move(handler);
    }
    void OnRepoMode(std::function<void(bool)> handler) { m_repoMode = std::move(handler); }
    void OnImport(std::function<void()> handler) { m_import = std::move(handler); }
    void OnReadRootFile(std::function<void(const std::wstring&)> handler) {
        m_readFile = std::move(handler);
    }
    void OnOpenFolder(std::function<void()> handler) { m_openFolder = std::move(handler); }
    void OnOpenGitHub(std::function<void()> handler) { m_openGitHub = std::move(handler); }

    // Se lleva toda la entrada que caiga dentro: es un panel, no un hueco por el que se vea
    // la lista de detrás.
    bool ClipsInput() const override { return true; }
    bool OnKey(const Input::Key& e) override;

protected:
    bool OnAttach() override;
    void OnArrange() override;
    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override;

private:
    void Rebuild();
    void PaintNovedad(const Ui::Paint& paint, const Ui::Rect& box, int index, bool hovered,
                      bool selected);
    void ShowPriorityMenu();
    void ShowStateMenu();
    void ShowMoreMarkdown();
    void ReleaseFocus();
    void CommitNextStep();
    void CommitNovedad();
    // Cuántos .md de la raíz caben con su propio botón. El resto va a un menú.
    int VisibleMarkdown() const;

    Content m_content;

    // Una sola pizarra para TODO el texto que no recibe entrada. Ver la cabecera.
    Ui::Slate* m_body = nullptr;
    Ui::Label* m_name = nullptr;
    Ui::Label* m_where = nullptr;
    Ui::Label* m_stepTitle = nullptr;
    Ui::Label* m_novTitle = nullptr;
    Ui::Label* m_priorityTitle = nullptr;
    Ui::Label* m_stateTitle = nullptr;
    Ui::Label* m_commitsTitle = nullptr;
    // Cinco etiquetas y no una con saltos de línea dentro: Ui::Text recorta con elipsis
    // midiendo el texto ENTERO, así que cinco renglones en una sola se recortarían por un
    // punto cualquiera del bloque en vez de uno por renglón.
    Ui::Label* m_commitLines[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    Ui::Label* m_counts = nullptr;
    Ui::Label* m_proyTitle = nullptr;
    Ui::Label* m_proyState = nullptr;

    Ui::IconButton* m_closeButton = nullptr;
    Ui::Button* m_priorityButton = nullptr;
    Ui::Button* m_stateButton = nullptr;
    Ui::Field* m_step = nullptr;
    Ui::IconButton* m_addButton = nullptr;
    Ui::Field* m_newNovedad = nullptr;
    Ui::List* m_novedades = nullptr;
    Ui::Button* m_importButton = nullptr;
    Ui::Button* m_repoModeButton = nullptr;
    Ui::Button* m_markdown[3] = {nullptr, nullptr, nullptr};
    Ui::Button* m_openGitHubButton = nullptr;
    Ui::Button* m_folderButton = nullptr;
    Ui::Rule* m_rules[4] = {nullptr, nullptr, nullptr, nullptr};

    std::function<void()> m_close;
    std::function<void(Model::Priority)> m_priorityChanged;
    std::function<void(Model::State)> m_stateChanged;
    std::function<void(const std::wstring&)> m_nextStep;
    std::function<void(const std::wstring&)> m_addNovedad;
    std::function<void(std::int64_t)> m_deleteNovedad;
    std::function<void(bool)> m_repoMode;
    std::function<void()> m_import;
    std::function<void(const std::wstring&)> m_readFile;
    std::function<void()> m_openFolder;
    std::function<void()> m_openGitHub;

    // Con los commits escondidos porque el panel es muy bajo. Se decide al colocar.
    bool m_tight = false;
};

}  // namespace Views
