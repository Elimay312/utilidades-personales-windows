// La rejilla y los radios. Parece de perogrullo probar constantes, pero la fase 1 ya
// había empezado a repartirlas —Demo.cpp tiene seis locales y tres fuera de la rejilla—
// y el modo en que esto se estropea es que alguien meta un 10 entre el 8 y el 12 para
// que "le cuadre" una tarjeta. Aquí se entera; en pantalla, no.

#include <doctest/doctest.h>

#include <iterator>

#include "ui/Metrics.h"

using Metrics::Radius;

TEST_CASE("los espacios son la rejilla de 4 y van de menos a más") {
    constexpr float kEscala[] = {Metrics::kSpace1, Metrics::kSpace2, Metrics::kSpace3,
                                 Metrics::kSpace4, Metrics::kSpace5};

    for (const float space : kEscala) {
        // Múltiplo de 4: media rejilla es medio píxel a 200 %, y ahí se ve.
        CHECK(static_cast<int>(space) % 4 == 0);
        CHECK(space > 0.0f);
    }

    for (std::size_t i = 1; i < std::size(kEscala); ++i) {
        CHECK(kEscala[i] > kEscala[i - 1]);
    }

    // Los de CLAUDE.md, tal cual.
    CHECK(Metrics::kSpace1 == 8.0f);
    CHECK(Metrics::kSpace5 == 32.0f);
}

TEST_CASE("los cuatro radios son los de la tabla") {
    CHECK(Metrics::RadiusOf(Radius::Control) == 6.0f);
    CHECK(Metrics::RadiusOf(Radius::Card) == 10.0f);
    CHECK(Metrics::RadiusOf(Radius::Panel) == 14.0f);
    CHECK(Metrics::RadiusOf(Radius::Sheet) == 20.0f);

    // Y crecen en ese orden: un control no puede ser más redondo que el panel que lo
    // contiene sin que se vea raro.
    CHECK(Metrics::RadiusOf(Radius::Control) < Metrics::RadiusOf(Radius::Card));
    CHECK(Metrics::RadiusOf(Radius::Card) < Metrics::RadiusOf(Radius::Panel));
    CHECK(Metrics::RadiusOf(Radius::Panel) < Metrics::RadiusOf(Radius::Sheet));
}

TEST_CASE("las alturas derivadas siguen en la rejilla") {
    CHECK(static_cast<int>(Metrics::kControlHeight) % 4 == 0);
    CHECK(static_cast<int>(Metrics::kRowHeight) % 4 == 0);
    CHECK(static_cast<int>(Metrics::kIconSize) % 4 == 0);
    // Un control tiene que caber un cuerpo de 15 con aire: 32 lo hace, 24 no.
    CHECK(Metrics::kControlHeight >= 15.0f + Metrics::kSpace1 * 2.0f - 1.0f);
}

TEST_CASE("el pelo mide un píxel físico a cualquier escala") {
    CHECK(Metrics::Hairline(1.0f) == doctest::Approx(1.0f));
    CHECK(Metrics::Hairline(1.5f) == doctest::Approx(0.6667f).epsilon(0.01));
    CHECK(Metrics::Hairline(2.0f) == doctest::Approx(0.5f));
    // Una escala de cero no llega nunca, pero dividir por ella sería un infinito que se
    // propaga hasta un tamaño de superficie.
    CHECK(Metrics::Hairline(0.0f) == doctest::Approx(1.0f));
}

TEST_CASE("el rectángulo es medio abierto, y por eso dos pegados no se pisan") {
    constexpr Ui::Rect arriba{0.0f, 0.0f, 100.0f, 40.0f};
    constexpr Ui::Rect abajo{0.0f, 40.0f, 100.0f, 40.0f};

    // El punto de la junta pertenece a uno solo. Si los dos lo contuvieran, el hit-test
    // devolvería el que esté más arriba en el árbol y no el que se ve.
    CHECK(abajo.Contains(10.0f, 40.0f));
    CHECK_FALSE(arriba.Contains(10.0f, 40.0f));

    CHECK(arriba.Contains(0.0f, 0.0f));          // la esquina de arriba sí
    CHECK_FALSE(arriba.Contains(100.0f, 0.0f));  // la de la derecha no
}

TEST_CASE("encoger un rectángulo le quita el doble de ancho") {
    constexpr Ui::Rect caja{10.0f, 20.0f, 100.0f, 50.0f};
    constexpr Ui::Rect dentro = caja.Inset(8.0f);

    CHECK(dentro.x == 18.0f);
    CHECK(dentro.y == 28.0f);
    CHECK(dentro.width == 84.0f);   // 100 - 8 - 8
    CHECK(dentro.height == 34.0f);  // 50 - 8 - 8

    // Encogerlo más de la cuenta lo deja vacío, no del revés.
    CHECK(caja.Inset(40.0f).Empty());
}

TEST_CASE("un rectángulo estrujado nunca pide una superficie de lado cero") {
    // Composition rechaza una superficie de lado 0 y la ventana se puede arrastrar hasta
    // el mínimo. Es el mismo cuidado que ya tiene Gfx::Surface, un paso antes.
    constexpr Ui::Rect nada{0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(nada.AtLeast(1.0f).width == 1.0f);
    CHECK(nada.AtLeast(1.0f).height == 1.0f);
    // Y no encoge lo que ya era mayor.
    constexpr Ui::Rect grande{0.0f, 0.0f, 80.0f, 20.0f};
    CHECK(grande.AtLeast(1.0f).width == 80.0f);
}
