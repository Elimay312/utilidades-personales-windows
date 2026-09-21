#pragma once

// Los componentes sin estado interno: etiqueta, botón, botón de icono, píldora de
// prioridad y punto de actividad.
//
// Van juntos en un archivo porque son cinco piezas de treinta líneas que comparten el
// mismo esqueleto —material detrás, un trozo de texto delante— y repartirlas en cinco
// cabeceras sería más ceremonia que código. El campo de texto, la lista, la barra
// lateral y lo que flota sí tienen archivo propio: esos tienen estado y se leen aparte.
//
// El hover y el pulsado NO repintan. El hover anima el color del material —una propiedad
// en la GPU— y el pulsado anima la escala; la textura se queda como está. Repintar para
// aclarar un fondo sería reconstruir una superficie sesenta veces por segundo mientras el
// ratón cruza una lista.

#include <functional>
#include <string>

#include "ui/Element.h"
#include "ui/Text.h"

namespace Ui {

// Un trozo de texto y nada más: sin material, sin foco y sin superficie propia. Se pinta
// en la de su ancestro, que es lo que hace que una tarjeta con seis etiquetas sea una
// textura y no seis.
class Label : public Element {
public:
    explicit Label(std::wstring text, Style style = Style::Body,
                   Weight weight = Weight::Regular);

    void SetText(std::wstring text);
    const std::wstring& Text() const { return m_text; }
    void SetAlign(Align align) { m_align = align; }
    void SetFigures(Figures figures) { m_figures = figures; }
    // Por omisión, textPrimary. Con esto se elige otro token.
    void SetColor(Theme::Color color);
    void UseSecondary();

    // Lo que mide de verdad, para que quien lo coloca no tenga que adivinar.
    float PreferredWidth();

protected:
    void OnPaint(const Paint& paint, const Rect& box) override;

private:
    std::wstring m_text;
    Style m_style = Style::Body;
    Weight m_weight = Weight::Regular;
    Align m_align = Align::Leading;
    Figures m_figures = Figures::Proportional;
    bool m_custom = false;
    bool m_secondary = false;
    Theme::Color m_color;
};

enum class ButtonKind {
    Primary,    // relleno de acento
    Secondary,  // velo sobre la Mica
    Plain,      // sin fondo hasta que el ratón pasa por encima
};

class Button : public Element {
public:
    explicit Button(std::wstring label, ButtonKind kind = ButtonKind::Secondary);

    void SetLabel(std::wstring label);
    void OnActivate(std::function<void()> handler) { m_activate = std::move(handler); }
    float PreferredWidth();

    bool Focusable() const override { return true; }

protected:
    bool OnAttach() override;
    void OnPaint(const Paint& paint, const Rect& box) override;
    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override;
    void OnStateChanged() override;
    bool OnPointer(const Input::Pointer& e) override;
    bool OnKey(const Input::Key& e) override;

    void Activate();
    // El color del material según hover, pulsado y deshabilitado.
    Theme::Color Fill(const Theme::Tokens& tokens) const;
    Theme::Color Ink(const Theme::Tokens& tokens) const;

    ButtonKind m_kind = ButtonKind::Secondary;
    std::wstring m_label;
    std::function<void()> m_activate;
    bool m_paintedEnabled = true;
};

// El mismo botón con un glifo de Segoe Fluent Icons en vez de una etiqueta.
class IconButton : public Button {
public:
    explicit IconButton(std::wstring glyph, ButtonKind kind = ButtonKind::Plain);

protected:
    void OnPaint(const Paint& paint, const Rect& box) override;
};

// La etiqueta de prioridad: el color de la prioridad sobre un relleno del mismo tono muy
// rebajado. Ni un bloque de color, que gritaría, ni solo texto, que no se distinguiría.
enum class Priority { Focus, Secondary, Someday, Archived, Unsorted };

const wchar_t* NameOf(Priority priority);
Theme::Color ColorOf(Priority priority, const Theme::Tokens& tokens);

class Pill : public Element {
public:
    explicit Pill(Priority priority);
    void SetPriority(Priority priority);
    float PreferredWidth();

protected:
    bool OnAttach() override;
    void OnPaint(const Paint& paint, const Rect& box) override;
    void OnTheme(const Theme::Tokens& tokens, float crossfadeMs) override;

private:
    Priority m_priority = Priority::Unsorted;
};

// El punto de actividad. Un círculo de 8 DIP, sin material: es un FillEllipse, porque una
// geometría de rectángulo redondeado con radio la mitad del lado sale igual pero cuesta
// un visual más por cada fila de la lista.
enum class Activity { Active, Paused, Dormant };

const wchar_t* NameOf(Activity activity);
Theme::Color ColorOf(Activity activity, const Theme::Tokens& tokens);

class Dot : public Element {
public:
    explicit Dot(Activity activity);
    void SetActivity(Activity activity);

protected:
    void OnPaint(const Paint& paint, const Rect& box) override;

private:
    Activity m_activity = Activity::Dormant;
};

}  // namespace Ui
