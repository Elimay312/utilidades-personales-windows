#pragma once

// La barra lateral: los grupos de prioridad con su contador, las vistas inteligentes y, al
// pie, la cuenta conectada con lo que se puede hacer con ella.
//
// El velo translúcido es un material de color sobre la Mica y no acrílico, que es la
// decisión medida de la fase 1: CreateHostBackdropBrush se pinta negro en una aplicación
// Win32 sin empaquetar. Aquí solo se usa el token que salió de allí.
//
// Los dos grupos son dos Ui::SidebarGroup y la selección es UNA, así que el que no la tiene
// apaga la suya: lo que se ve es una sola píldora que se muda de un grupo al otro.

#include <functional>
#include <string>

#include "app/State.h"
#include "ui/Element.h"

namespace Ui {
class Button;
class Label;
class Rule;
class SidebarGroup;
class Slate;
}  // namespace Ui

namespace Views {

class Sidebar : public Ui::Element {
public:
    // El ancho es fijo, como en las aplicaciones de Mac: una barra lateral que se estira
    // con la ventana deja de ser una columna y pasa a ser media pantalla de huecos.
    static constexpr float kWidth = 264.0f;

    void SetCounts(const App::State& state);
    void Select(App::Lens lens);
    void SetAccount(const std::wstring& account);
    void SetSyncing(bool running);

    void OnLens(std::function<void(App::Lens)> handler) { m_lens = std::move(handler); }
    void OnSync(std::function<void()> handler) { m_sync = std::move(handler); }
    void OnSignOut(std::function<void()> handler) { m_signOut = std::move(handler); }

protected:
    bool OnAttach() override;
    void OnArrange() override;
    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override;

private:
    void Choose(App::Lens lens);

    Ui::SidebarGroup* m_priority = nullptr;
    Ui::SidebarGroup* m_smart = nullptr;
    Ui::Rule* m_separator = nullptr;
    Ui::Rule* m_footerRule = nullptr;
    Ui::Slate* m_footer = nullptr;
    Ui::Label* m_account = nullptr;
    Ui::Button* m_syncButton = nullptr;
    Ui::Button* m_signOutButton = nullptr;

    std::function<void(App::Lens)> m_lens;
    std::function<void()> m_sync;
    std::function<void()> m_signOut;

    App::Lens m_selected = App::Lens::All;
    bool m_hasAccount = false;
    bool m_syncing = false;
};

}  // namespace Views
