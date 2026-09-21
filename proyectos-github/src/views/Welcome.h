#pragma once

// La hoja de bienvenida: explica qué permisos hacen falta, los pide, y recoge la credencial.
//
// Sale cuando no hay ninguna: ni en el Administrador de credenciales ni en GitHub CLI. Es
// modal y sin cierre al clic fuera, porque sin credencial no hay nada detrás que mirar.
//
// Los permisos se explican ANTES de pedirlos (SEGURIDAD.md, regla 6). Y son dos, no uno:
// Metadata: read no basta para leer PROYECTO.md —ese campo necesita Contents: read— así que
// la hoja pide los dos y dice para qué es cada uno. Contents: WRITE sigue siendo solo del
// modo repo, que es la fase 5.
//
// Se monta sobre Ui::Panel y no heredando de Ui::Sheet: Sheet trae su propio botón de cerrar
// y una altura fija de doscientos, y pelearse con eso saldría más largo que poner el panel.
//
// El campo es secreto: Ui::Field::SetSecret apaga el historial de deshacer, que si no se
// queda con una copia de lo que se pegó.

#include <functional>
#include <string>

#include "ui/Element.h"

namespace Ui {
class Button;
class Field;
class Label;
class Panel;
}  // namespace Ui

namespace Views {

class Welcome : public Ui::Element {
public:
    Welcome() = default;

    // El texto pegado. Quien escucha comprueba y decide; la hoja solo recoge.
    void OnConnect(std::function<void(const std::wstring&)> handler) {
        m_connect = std::move(handler);
    }
    void OnDismiss(std::function<void()> handler) { m_dismiss = std::move(handler); }

    // Un aviso dentro de la propia hoja. Dentro y no como aviso flotante, porque el sitio
    // donde se arregla es este.
    void ShowProblem(const std::wstring& message);

    // Se lleva toda la entrada y no deja pasar un clic al contenido de detrás.
    bool ClipsInput() const override { return true; }
    bool HitTest(float lx, float ly) const override;

protected:
    bool OnAttach() override;
    void OnArrange() override;

private:
    void Accept();

    Ui::Panel* m_panel = nullptr;
    Ui::Label* m_title = nullptr;
    Ui::Label* m_intro = nullptr;
    Ui::Label* m_metadata = nullptr;
    Ui::Label* m_contents = nullptr;
    Ui::Label* m_write = nullptr;
    Ui::Label* m_problem = nullptr;
    Ui::Field* m_field = nullptr;
    Ui::Button* m_open = nullptr;
    Ui::Button* m_accept = nullptr;
    Ui::Button* m_later = nullptr;

    Ui::Rect m_panelRect;
    std::function<void(const std::wstring&)> m_connect;
    std::function<void()> m_dismiss;
};

}  // namespace Views
