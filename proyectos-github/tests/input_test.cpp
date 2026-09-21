// El contador de clics. Windows solo cuenta hasta dos, así que el triple clic que
// selecciona la línea entera es nuestro, y son dos umbrales —tiempo y distancia— de los
// que se equivocan sin hacer ruido: el fallo es "a veces me selecciona la línea entera
// y no sé por qué", que es de los que no se reproducen a mano.

#include <doctest/doctest.h>

#include "shell/Input.h"

using Input::Has;
using Input::Modifiers;

TEST_CASE("los modificadores se combinan y se consultan por separado") {
    constexpr Modifiers ambos = Modifiers::Control | Modifiers::Shift;
    CHECK(Has(ambos, Modifiers::Control));
    CHECK(Has(ambos, Modifiers::Shift));
    CHECK_FALSE(Has(ambos, Modifiers::Alt));
    CHECK_FALSE(Has(Modifiers::None, Modifiers::Control));
}

TEST_CASE("tres clics seguidos en el mismo sitio cuentan uno, dos y tres") {
    Input::Clicks clicks;
    clicks.Configure(500, 4.0f);

    CHECK(clicks.Count(1000, 50.0f, 50.0f) == 1);
    CHECK(clicks.Count(1200, 50.0f, 50.0f) == 2);
    CHECK(clicks.Count(1400, 50.0f, 50.0f) == 3);
    // El cuarto vuelve a uno en vez de quedarse en tres, que es lo que hacen los
    // navegadores: después de seleccionar la línea entera, el siguiente clic tiene que
    // poder volver a poner el cursor sin esperar medio segundo.
    CHECK(clicks.Count(1600, 50.0f, 50.0f) == 1);
}

TEST_CASE("pasado el intervalo vuelve a empezar") {
    Input::Clicks clicks;
    clicks.Configure(500, 4.0f);

    CHECK(clicks.Count(1000, 50.0f, 50.0f) == 1);
    // 501 ms: justo fuera.
    CHECK(clicks.Count(1501, 50.0f, 50.0f) == 1);
}

TEST_CASE("dos clics rápidos pero lejos son dos clics, no un doble") {
    Input::Clicks clicks;
    clicks.Configure(500, 4.0f);

    CHECK(clicks.Count(1000, 50.0f, 50.0f) == 1);
    // Cinco DIP: fuera del margen de 4. Sin este umbral, teclear deprisa y luego pinchar
    // en otro sitio seleccionaría una palabra que nadie pidió.
    CHECK(clicks.Count(1100, 55.0f, 50.0f) == 1);

    // Justo dentro del margen sí cuenta.
    clicks.Reset();
    CHECK(clicks.Count(2000, 50.0f, 50.0f) == 1);
    CHECK(clicks.Count(2100, 53.0f, 52.0f) == 2);
}

TEST_CASE("el contador se puede reiniciar y no arrastra el clic anterior") {
    Input::Clicks clicks;
    CHECK(clicks.Count(1000, 10.0f, 10.0f) == 1);
    CHECK(clicks.Count(1100, 10.0f, 10.0f) == 2);
    clicks.Reset();
    // Tras perder el foco o cerrarse un menú hay que reiniciar, o el primer clic de
    // vuelta llega convertido en doble.
    CHECK(clicks.Count(1200, 10.0f, 10.0f) == 1);
}

TEST_CASE("un reloj que va hacia atrás no cuenta como clic rápido") {
    Input::Clicks clicks;
    clicks.Configure(500, 4.0f);
    CHECK(clicks.Count(5000, 10.0f, 10.0f) == 1);
    // GetMessageTime da un entero con signo que da la vuelta cada 25 días. La resta sin
    // comprobar el orden daría un número enorme sin signo y "a tiempo" saldría falso...
    // o verdadero, según. Aquí se fija que hacia atrás siempre reinicia.
    CHECK(clicks.Count(4000, 10.0f, 10.0f) == 1);
}
