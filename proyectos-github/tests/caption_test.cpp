// La geometría de la barra de título. Es lo que no se ve mal en pantalla hasta que ya te
// ha dejado una ventana que no se puede estirar por una esquina, o un botón de cerrar
// que falla por dos píxeles cuando la ventana está maximizada.

#include <doctest/doctest.h>

#include "shell/Caption.h"
#include "shell/Dpi.h"

using Caption::Zone;

namespace {
constexpr float kWidth = 1280.0f;
constexpr float kHeight = 800.0f;
}  // namespace

TEST_CASE("los tres botones van pegados a la derecha y en el orden de Windows") {
    const Caption::Layout layout = Caption::Compute(kWidth, kHeight, false);

    CHECK(layout.close.x + layout.close.width == doctest::Approx(kWidth));
    CHECK(layout.maximize.x == doctest::Approx(layout.close.x - Caption::kButtonWidth));
    CHECK(layout.minimize.x == doctest::Approx(layout.maximize.x - Caption::kButtonWidth));

    CHECK(layout.minimize.x < layout.maximize.x);
    CHECK(layout.maximize.x < layout.close.x);
}

TEST_CASE("cada botón responde en su sitio y la franja restante arrastra") {
    const Caption::Layout layout = Caption::Compute(kWidth, kHeight, false);
    const float middle = Caption::kBarHeight * 0.5f;

    CHECK(Caption::HitTest(layout, layout.close.x + 20.0f, middle) == Zone::Close);
    CHECK(Caption::HitTest(layout, layout.maximize.x + 20.0f, middle) == Zone::Maximize);
    CHECK(Caption::HitTest(layout, layout.minimize.x + 20.0f, middle) == Zone::Minimize);

    CHECK(Caption::HitTest(layout, 400.0f, middle) == Zone::Caption);
    CHECK(Caption::HitTest(layout, 400.0f, Caption::kBarHeight + 1.0f) == Zone::Client);
}

TEST_CASE("las cuatro esquinas ganan a los cuatro lados") {
    const Caption::Layout layout = Caption::Compute(kWidth, kHeight, false);

    CHECK(Caption::HitTest(layout, 1.0f, 1.0f) == Zone::TopLeft);
    CHECK(Caption::HitTest(layout, kWidth - 1.0f, 1.0f) == Zone::TopRight);
    CHECK(Caption::HitTest(layout, 1.0f, kHeight - 1.0f) == Zone::BottomLeft);
    CHECK(Caption::HitTest(layout, kWidth - 1.0f, kHeight - 1.0f) == Zone::BottomRight);

    CHECK(Caption::HitTest(layout, kWidth * 0.5f, 1.0f) == Zone::Top);
    CHECK(Caption::HitTest(layout, kWidth * 0.5f, kHeight - 1.0f) == Zone::Bottom);
    CHECK(Caption::HitTest(layout, 1.0f, kHeight * 0.5f) == Zone::Left);
    CHECK(Caption::HitTest(layout, kWidth - 1.0f, kHeight * 0.5f) == Zone::Right);
}

TEST_CASE("el borde superior gana al botón de cerrar, como en cualquier ventana") {
    const Caption::Layout layout = Caption::Compute(kWidth, kHeight, false);

    // Sobre el botón de cerrar pero en la primera fila: estira, no cierra.
    CHECK(Caption::HitTest(layout, kWidth - 20.0f, 1.0f) == Zone::Top);
    // Y en las últimas columnas, además, es la esquina.
    CHECK(Caption::HitTest(layout, kWidth - 2.0f, 1.0f) == Zone::TopRight);
    // Pasado el borde ya es el botón.
    CHECK(Caption::HitTest(layout, kWidth - 20.0f, Caption::kResizeBorder + 1.0f) == Zone::Close);
}

TEST_CASE("maximizada no hay bordes que estirar y cerrar llega hasta el borde") {
    const Caption::Layout layout = Caption::Compute(kWidth, kHeight, true);

    // Esta es la razón de ser de la prueba: maximizada, el cursor topa con el borde de la
    // pantalla en la fila 0. Si el borde de redimensionado siguiera activo ahí, cerrar
    // sería inalcanzable con el gesto de tirar el ratón a la esquina.
    CHECK(Caption::HitTest(layout, kWidth - 1.0f, 0.0f) == Zone::Close);
    CHECK(Caption::HitTest(layout, 0.0f, 0.0f) == Zone::Caption);
    CHECK(Caption::HitTest(layout, kWidth * 0.5f, kHeight - 1.0f) == Zone::Client);
}

TEST_CASE("la franja mide lo mismo en DIP a cualquier escala") {
    CHECK(Dpi::ToPixels(Caption::kBarHeight, Dpi::ScaleFor(96)) == 48);
    CHECK(Dpi::ToPixels(Caption::kBarHeight, Dpi::ScaleFor(120)) == 60);
    CHECK(Dpi::ToPixels(Caption::kBarHeight, Dpi::ScaleFor(144)) == 72);

    CHECK(Dpi::ToPixels(Caption::kResizeBorder, Dpi::ScaleFor(120)) == 10);
    CHECK(Dpi::ToPixels(Caption::kResizeBorder, Dpi::ScaleFor(144)) == 12);
    CHECK(Dpi::ToPixels(Caption::kResizeBorder, Dpi::ScaleFor(168)) == 14);

    // Las superficies van hacia arriba: quedarse corto medio píxel recorta glifos.
    CHECK(Dpi::SurfaceSide(100.5f, 1.0f) == 101);
    CHECK(Dpi::SurfaceSide(100.0f, 1.25f) == 125);
}

TEST_CASE("una ventana estrecha no solapa los botones con el arrastre") {
    // Tres botones son 138 DIP. Con 200 de ancho todavía queda franja que arrastrar.
    const Caption::Layout layout = Caption::Compute(200.0f, 400.0f, false);
    CHECK(Caption::HitTest(layout, 20.0f, Caption::kBarHeight * 0.5f) == Zone::Caption);
    CHECK(Caption::HitTest(layout, 199.0f, Caption::kBarHeight * 0.5f) == Zone::Right);
    CHECK(Caption::HitTest(layout, 190.0f, Caption::kBarHeight * 0.5f) == Zone::Close);
}
