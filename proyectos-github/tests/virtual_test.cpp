// La aritmética de la lista virtualizada. Está aquí porque es lo que decide mal sin que
// se vea: un rango mal calculado no da error ni parpadea, simplemente crea quinientas
// superficies en vez de veinticinco y tira el criterio de los 60 fps. El día que eso
// pase, esta prueba lo dice; la pantalla, no.

#include <doctest/doctest.h>

#include "ui/Virtual.h"

namespace {

// Los números del catálogo: 500 elementos de 36 DIP en una ventanilla de 400.
constexpr int kItems = 500;
constexpr float kRow = 36.0f;
constexpr float kViewport = 400.0f;

}  // namespace

TEST_CASE("arriba del todo se materializa la primera pantalla y poco más") {
    const Ui::Slice slice = Ui::Visible(0.0f, kViewport, kRow, kItems, 2);

    CHECK(slice.first == 0);
    // 400 / 36 son 11 filas y pico, o sea 12 visibles, más 2 de margen por abajo. Por
    // arriba no hay margen que dar porque estamos en el tope.
    CHECK(slice.count == 14);
    CHECK(slice.offset == doctest::Approx(0.0f));

    // Y esto es lo que hace que la fase cumpla: veinticinco superficies como mucho, no
    // quinientas. Si este número se dispara, el reciclado dejó de funcionar.
    CHECK(slice.count < 25);
}

TEST_CASE("con 500 elementos nunca se materializan más que una pantalla y su margen") {
    // Barriendo la lista entera de cien en cien DIP: en ningún punto puede crecer.
    for (float scroll = 0.0f; scroll <= Ui::MaxScroll(kItems, kRow, kViewport); scroll += 100.0f) {
        const Ui::Slice slice = Ui::Visible(scroll, kViewport, kRow, kItems, 2);
        // Doce visibles como mucho (400/36 son 11 y pico), más dos de margen por cada
        // lado, más el propio primero: diecisiete.
        CHECK(slice.count <= 17);
        CHECK(slice.first >= 0);
        CHECK(slice.first + slice.count <= kItems);
    }
}

TEST_CASE("el primero de la tanda asoma por arriba, y por eso el desplazamiento es negativo") {
    // Media fila bajada: la fila 0 está medio salida.
    const Ui::Slice slice = Ui::Visible(18.0f, kViewport, kRow, kItems, 0);
    CHECK(slice.first == 0);
    CHECK(slice.offset == doctest::Approx(-18.0f));

    // Una fila y media: ahora la primera viva es la 1 y asoma media.
    const Ui::Slice media = Ui::Visible(54.0f, kViewport, kRow, kItems, 0);
    CHECK(media.first == 1);
    CHECK(media.offset == doctest::Approx(-18.0f));
}

TEST_CASE("justo en la junta entre dos filas empieza la de abajo") {
    // Con suelo y no con redondeo: a 1,9 alturas la primera que se ve es la 1 asomando,
    // no la 2. Redondeando, la 1 desaparecería antes de tiempo y se vería un hueco.
    const Ui::Slice justo = Ui::Visible(kRow, kViewport, kRow, kItems, 0);
    CHECK(justo.first == 1);
    CHECK(justo.offset == doctest::Approx(0.0f));

    const Ui::Slice casi = Ui::Visible(kRow * 1.9f, kViewport, kRow, kItems, 0);
    CHECK(casi.first == 1);
}

TEST_CASE("el margen se pide por los dos lados pero no se sale de la lista") {
    const Ui::Slice medio = Ui::Visible(3600.0f, kViewport, kRow, kItems, 3);
    CHECK(medio.first == 100 - 3);

    // Al principio no hay nada que dar por arriba.
    const Ui::Slice principio = Ui::Visible(0.0f, kViewport, kRow, kItems, 3);
    CHECK(principio.first == 0);

    // Y al final no hay nada que dar por abajo.
    const float max = Ui::MaxScroll(kItems, kRow, kViewport);
    const Ui::Slice fin = Ui::Visible(max, kViewport, kRow, kItems, 3);
    CHECK(fin.first + fin.count == kItems);
}

TEST_CASE("una lista que cabe entera no se desplaza ni un poco") {
    // Diez filas de 36 son 360, y la ventanilla mide 400.
    CHECK(Ui::MaxScroll(10, kRow, kViewport) == doctest::Approx(0.0f));
    CHECK(Ui::ClampScroll(50.0f, Ui::MaxScroll(10, kRow, kViewport)) == doctest::Approx(0.0f));

    const Ui::Slice slice = Ui::Visible(0.0f, kViewport, kRow, 10, 2);
    CHECK(slice.first == 0);
    CHECK(slice.count == 10);
}

TEST_CASE("una lista vacía no pide ninguna fila") {
    const Ui::Slice slice = Ui::Visible(0.0f, kViewport, kRow, 0, 2);
    CHECK(slice.count == 0);
    CHECK(Ui::ContentHeight(0, kRow) == doctest::Approx(0.0f));
    CHECK(Ui::MaxScroll(0, kRow, kViewport) == doctest::Approx(0.0f));
}

TEST_CASE("una ventanilla de alto cero tampoco") {
    // Pasa de verdad: la ventana arrastrada hasta el mínimo, o el panel antes del primer
    // layout. Sin esto sería una división entre cero.
    const Ui::Slice slice = Ui::Visible(0.0f, 0.0f, kRow, kItems, 2);
    CHECK(slice.count == 0);
}

TEST_CASE("una fila de alto cero no cuelga la aplicación") {
    const Ui::Slice slice = Ui::Visible(0.0f, kViewport, 0.0f, kItems, 0);
    CHECK(slice.count > 0);
    CHECK(slice.count <= kItems);
}

TEST_CASE("el desplazamiento se sujeta a los dos topes") {
    const float max = Ui::MaxScroll(kItems, kRow, kViewport);
    CHECK(max == doctest::Approx(500.0f * 36.0f - 400.0f));
    CHECK(Ui::ClampScroll(-100.0f, max) == doctest::Approx(0.0f));
    CHECK(Ui::ClampScroll(max + 500.0f, max) == doctest::Approx(max));
    CHECK(Ui::ClampScroll(1000.0f, max) == doctest::Approx(1000.0f));
}

TEST_CASE("llevar a la vista una fila que ya se ve no mueve nada") {
    // Desplazar cuando no hace falta es lo que hace que bajar con las flechas dé tirones
    // en vez de deslizarse.
    CHECK(Ui::ScrollToShow(5, kRow, 0.0f, kViewport, 0.0f) == doctest::Approx(0.0f));
    // Y es idempotente: aplicarlo dos veces da lo mismo.
    const float una = Ui::ScrollToShow(50, kRow, 0.0f, kViewport, 0.0f);
    CHECK(Ui::ScrollToShow(50, kRow, una, kViewport, 0.0f) == doctest::Approx(una));
}

TEST_CASE("llevar a la vista hace lo mínimo, y por el lado que toca") {
    // Por abajo se alinea abajo.
    const float abajo = Ui::ScrollToShow(12, kRow, 0.0f, kViewport, 0.0f);
    CHECK(abajo == doctest::Approx(13.0f * kRow - kViewport));

    // Por arriba, arriba.
    const float arriba = Ui::ScrollToShow(2, kRow, 500.0f, kViewport, 0.0f);
    CHECK(arriba == doctest::Approx(2.0f * kRow));

    // Y nunca por debajo de cero, ni con margen.
    CHECK(Ui::ScrollToShow(0, kRow, 500.0f, kViewport, 20.0f) == doctest::Approx(0.0f));
}

TEST_CASE("tres deltas de panel táctil suman lo mismo que una muesca de rueda") {
    // Este es el caso del panel táctil de precisión: manda deltas de 40 en vez de uno de
    // 120. Truncando cada uno por separado se perderían los restos y el desplazamiento
    // suave sería un tercio más corto que el de la rueda.
    Ui::Wheel suelto;
    suelto.SetLinesPerNotch(3);
    float total = 0.0f;
    total += suelto.Take(40, kRow, kViewport);
    total += suelto.Take(40, kRow, kViewport);
    total += suelto.Take(40, kRow, kViewport);

    Ui::Wheel entera;
    entera.SetLinesPerNotch(3);
    const float muesca = entera.Take(120, kRow, kViewport);

    CHECK(total == doctest::Approx(muesca));
    CHECK(muesca == doctest::Approx(3.0f * kRow));
}

TEST_CASE("deltas diminutos acaban moviendo algo") {
    Ui::Wheel wheel;
    wheel.SetLinesPerNotch(3);
    // Ocho unidades son 7,2 DIP con filas de 36: menos de una fila, pero no cero. Sin
    // guardar el resto, un panel táctil que manda ocho a ochenta hercios no movería la
    // lista en absoluto.
    float total = 0.0f;
    for (int i = 0; i < 20; ++i) total += wheel.Take(8, kRow, kViewport);
    CHECK(total > 100.0f);
}

TEST_CASE("una rueda configurada por página deja una fila de solape") {
    Ui::Wheel wheel;
    wheel.SetLinesPerNotch(-1);  // así codifica Windows «una pantalla»
    CHECK(wheel.Take(120, kRow, kViewport) == doctest::Approx(kViewport - kRow));
}

TEST_CASE("cero líneas por muesca no deja la rueda muerta") {
    // Llega de verdad en algunas configuraciones, y significa «no desplazar». Tratarlo
    // como el valor de fábrica es preferible a una rueda que no hace nada y parece rota.
    Ui::Wheel wheel;
    wheel.SetLinesPerNotch(0);
    CHECK(wheel.LinesPerNotch() == 3);
    CHECK(wheel.Take(120, kRow, kViewport) > 0.0f);
}

TEST_CASE("la rueda hacia arriba devuelve el signo contrario") {
    Ui::Wheel wheel;
    wheel.SetLinesPerNotch(3);
    CHECK(wheel.Take(-120, kRow, kViewport) == doctest::Approx(-3.0f * kRow));
}

TEST_CASE("el tirón elástico cede cada vez menos y nunca se desboca") {
    // Tirar el doble no estira el doble: es lo que hace que el final de la lista se
    // sienta como un tope blando y no como un muro ni como un agujero.
    const float poco = Ui::RubberBand(50.0f, kViewport);
    const float mucho = Ui::RubberBand(100.0f, kViewport);
    CHECK(mucho > poco);
    CHECK(mucho < poco * 2.0f);

    // Y por mucho que se tire, tiende a 0,55 de la ventanilla.
    CHECK(Ui::RubberBand(100000.0f, kViewport) < kViewport * 0.56f);

    // Simétrico, y sin tirón no hay desplazamiento.
    CHECK(Ui::RubberBand(-50.0f, kViewport) == doctest::Approx(-poco));
    CHECK(Ui::RubberBand(0.0f, kViewport) == doctest::Approx(0.0f));
}

TEST_CASE("el escalonado de entrada tiene techo") {
    CHECK(Ui::StaggerMs(0) == doctest::Approx(0.0f));
    CHECK(Ui::StaggerMs(1) == doctest::Approx(20.0f));  // los 20 ms de CLAUDE.md
    CHECK(Ui::StaggerMs(5) == doctest::Approx(100.0f));
    // Sin techo, el elemento 499 empezaría a aparecer diez segundos después del primero.
    CHECK(Ui::StaggerMs(499) == doctest::Approx(200.0f));
}
