// PROYECTO.md. El archivo vive en la raíz de un repositorio de trabajo y lo edita también
// gente —y la web de GitHub—, así que el caso que hay que acertar no es el bonito.
//
// Lo que se vigila aquí es una sola cosa dicha de muchas maneras: **que nada se pierda**. Un
// parser que se rompe con un archivo raro da un error y se ve; uno que lo lee a medias y lo
// vuelve a escribir le borra a alguien un párrafo de su propio repositorio dentro de un
// commit que dice "actualizar PROYECTO.md", y eso no se ve nunca.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "projectfile/Proyecto.h"

namespace {

Model::Instant At(const char* iso) { return Model::ParseIso8601(iso).value(); }

const Model::Instant kHoy = At("2026-09-21T00:00:00Z");

// El ejemplo literal de CLAUDE.md.
constexpr const wchar_t* kEjemplo =
    L"---\n"
    L"prioridad: enfoque\n"
    L"estado: activo\n"
    L"siguiente_paso: Conectar el lector de carpetas a la columna central\n"
    L"actualizado: 2026-09-21\n"
    L"---\n"
    L"\n"
    L"## Novedades\n"
    L"\n"
    L"- 2026-09-20 — Terminada la fase 2, falta probar con rutas largas.\n";

bool Contiene(const std::wstring& texto, const wchar_t* trozo) {
    return texto.find(trozo) != std::wstring::npos;
}

Model::Local Suyo() {
    Model::Local local;
    local.repoId = "R_1";
    local.priority = Model::Priority::Focus;
    local.state = Model::State::Blocked;
    local.nextStep = L"Medir el pase 2";
    return local;
}

Model::Novedad Nota(const char* day, const wchar_t* text) {
    Model::Novedad novedad;
    novedad.repoId = "R_1";
    novedad.day = day;
    novedad.text = text;
    return novedad;
}

}  // namespace

// ------------------------------------------------------------------------- El caso bueno --

TEST_CASE("el ejemplo de CLAUDE.md se lee entero") {
    const Proyecto::File file = Proyecto::Parse(kEjemplo);

    CHECK(file.hadFrontmatter);
    CHECK(file.priority == Model::Priority::Focus);
    CHECK(file.state == Model::State::Active);
    CHECK(file.nextStep == std::wstring(L"Conectar el lector de carpetas a la columna central"));
    REQUIRE(file.updated.has_value());
    CHECK(Model::FormatDay(*file.updated) == "2026-09-21");

    CHECK(file.hadNovedades);
    REQUIRE(file.novedades.size() == 1);
    CHECK(file.novedades[0].day == "2026-09-20");
    CHECK(file.novedades[0].text ==
          std::wstring(L"Terminada la fase 2, falta probar con rutas largas."));
    CHECK(file.before.empty());
    CHECK(file.after.empty());
}

TEST_CASE("y vuelve a salir tal cual") {
    // Ida y vuelta exacta sobre el formato que documenta CLAUDE.md. Si esto se mueve, lo que
    // se mueve es el formato del proyecto, no una prueba.
    CHECK(Proyecto::Render(Proyecto::Parse(kEjemplo)) == std::wstring(kEjemplo));
}

// ---------------------------------------------------------------- Archivos que no cumplen --

TEST_CASE("sin frontmatter no se pierde una línea") {
    const wchar_t* crudo = L"# Notas del proyecto\n\nEsto lo escribió alguien a mano.\n";
    const Proyecto::File file = Proyecto::Parse(crudo);

    CHECK_FALSE(file.hadFrontmatter);
    CHECK_FALSE(file.hadNovedades);
    CHECK(file.before == std::wstring(L"# Notas del proyecto\n\nEsto lo escribió alguien a mano."));
    CHECK(Proyecto::Render(file) == std::wstring(crudo));
}

TEST_CASE("un cercado que no se cierra no es frontmatter") {
    // Consumir hasta el final del archivo se comería el documento entero. Lo que de verdad
    // hay delante suele ser una raya horizontal de Markdown.
    const wchar_t* crudo = L"---\n\nUn párrafo suelto y nada más.\n";
    const Proyecto::File file = Proyecto::Parse(crudo);

    CHECK_FALSE(file.hadFrontmatter);
    CHECK_FALSE(file.priority.has_value());
    CHECK(Contiene(file.before, L"Un párrafo suelto"));
    CHECK(Contiene(file.before, L"---"));
}

TEST_CASE("las claves que no entendemos vuelven a escribirse") {
    const wchar_t* crudo =
        L"---\n"
        L"prioridad: secundario\n"
        L"responsable: Elimay\n"
        L"etiquetas: [trabajo, invierno]\n"
        L"una línea sin dos puntos\n"
        L"---\n";
    const Proyecto::File file = Proyecto::Parse(crudo);

    REQUIRE(file.unknown.size() == 3);
    CHECK(file.unknown[0] == std::wstring(L"responsable: Elimay"));
    CHECK(file.unknown[1] == std::wstring(L"etiquetas: [trabajo, invierno]"));
    CHECK(file.unknown[2] == std::wstring(L"una línea sin dos puntos"));

    const std::wstring vuelta = Proyecto::Render(file);
    CHECK(Contiene(vuelta, L"responsable: Elimay"));
    CHECK(Contiene(vuelta, L"etiquetas: [trabajo, invierno]"));
    CHECK(Contiene(vuelta, L"una línea sin dos puntos"));
}

TEST_CASE("un valor que no conocemos tampoco se borra") {
    // El optional sale vacío, y sin el valor en crudo la línea desaparecería al escribir. Es
    // la misma pérdida silenciosa que las claves desconocidas, pero disfrazada de campo
    // conocido.
    const Proyecto::File file = Proyecto::Parse(L"---\nprioridad: urgentísimo\nestado: raro\n---\n");

    CHECK_FALSE(file.priority.has_value());
    CHECK_FALSE(file.state.has_value());
    CHECK(Contiene(Proyecto::Render(file), L"prioridad: urgentísimo"));
    CHECK(Contiene(Proyecto::Render(file), L"estado: raro"));
}

TEST_CASE("se parte por el primer dos puntos y no por los demás") {
    const Proyecto::File file =
        Proyecto::Parse(L"---\nsiguiente_paso: Conectar A: B con C: D\n---\n");
    CHECK(file.nextStep == std::wstring(L"Conectar A: B con C: D"));
}

TEST_CASE("las comillas de alrededor se quitan") {
    CHECK(Proyecto::Parse(L"---\nsiguiente_paso: \"Medir el pase 2\"\n---\n").nextStep ==
          std::wstring(L"Medir el pase 2"));
    CHECK(Proyecto::Parse(L"---\nsiguiente_paso: 'Medir el pase 2'\n---\n").nextStep ==
          std::wstring(L"Medir el pase 2"));
    // Una comilla suelta dentro no es un par y se queda donde está.
    CHECK(Proyecto::Parse(L"---\nsiguiente_paso: Medir el \"pase\" 2\n---\n").nextStep ==
          std::wstring(L"Medir el \"pase\" 2"));
}

TEST_CASE("el archivo en CRLF se devuelve en CRLF") {
    // Reescribir con LF un archivo que venía en CRLF es un diff de TODAS las líneas, y un
    // commit así no hay quien lo revise.
    const Proyecto::File file = Proyecto::Parse(L"---\r\nprioridad: enfoque\r\n---\r\n");
    CHECK(file.crlf);
    const std::wstring vuelta = Proyecto::Render(file);
    CHECK(vuelta == std::wstring(L"---\r\nprioridad: enfoque\r\n---\r\n"));
    CHECK(vuelta.find(L"\n\r") == std::wstring::npos);
}

TEST_CASE("el BOM no estorba") {
    const std::wstring crudo = std::wstring(1, static_cast<wchar_t>(0xFEFF)) +
                               L"---\nprioridad: enfoque\n---\n";
    CHECK(Proyecto::Parse(crudo).priority == Model::Priority::Focus);
}

TEST_CASE("un archivo vacío no inventa nada") {
    const Proyecto::File file = Proyecto::Parse(L"");
    CHECK_FALSE(file.hadFrontmatter);
    CHECK(Proyecto::Render(file).empty());
}

// -------------------------------------------------------------------------- Las novedades --

TEST_CASE("los renglones se leen con cualquiera de los separadores") {
    const Proyecto::File file = Proyecto::Parse(
        L"## Novedades\n"
        L"\n"
        L"- 2026-09-20 — con raya larga\n"
        L"- 2026-09-19 – con raya media\n"
        L"- 2026-09-18 - con guion\n"
        L"- 2026-09-17: con dos puntos\n"
        L"- 2026-09-16 sin nada en medio\n"
        L"* 2026-09-15 — con asterisco\n");

    REQUIRE(file.novedades.size() == 6);
    CHECK(file.novedades[0].text == std::wstring(L"con raya larga"));
    CHECK(file.novedades[1].text == std::wstring(L"con raya media"));
    CHECK(file.novedades[2].text == std::wstring(L"con guion"));
    CHECK(file.novedades[3].text == std::wstring(L"con dos puntos"));
    CHECK(file.novedades[4].text == std::wstring(L"sin nada en medio"));
    CHECK(file.novedades[5].day == "2026-09-15");
}

TEST_CASE("un renglón sin fecha sigue siendo una nota") {
    // Tirarlo por no traer fecha sería borrar el dato para proteger el formato.
    const Proyecto::File file = Proyecto::Parse(L"## Novedades\n\n- Falta probar con rutas largas\n");
    REQUIRE(file.novedades.size() == 1);
    CHECK(file.novedades[0].day.empty());
    CHECK(file.novedades[0].text == std::wstring(L"Falta probar con rutas largas"));
    CHECK(Contiene(Proyecto::Render(file), L"- Falta probar con rutas largas"));
}

TEST_CASE("una raya horizontal no es una novedad") {
    const Proyecto::File file = Proyecto::Parse(
        L"## Novedades\n\n- 2026-09-20 — algo\n\n---\n\n## Otra sección\n\nTexto.\n");

    REQUIRE(file.novedades.size() == 1);
    CHECK(Contiene(file.after, L"---"));
    CHECK(Contiene(file.after, L"## Otra sección"));
    CHECK(Contiene(file.after, L"Texto."));
}

TEST_CASE("el encabezado se reconoce en cualquier caja y con cualquier nivel") {
    CHECK(Proyecto::Parse(L"# NOVEDADES\n- 2026-09-20 — x\n").novedades.size() == 1);
    CHECK(Proyecto::Parse(L"### novedades\n- 2026-09-20 — x\n").novedades.size() == 1);
    // Y un encabezado que no es ese no abre la sección.
    const Proyecto::File otro = Proyecto::Parse(L"## Notas\n\n- 2026-09-20 — x\n");
    CHECK_FALSE(otro.hadNovedades);
    CHECK(Contiene(otro.before, L"- 2026-09-20 — x"));
}

TEST_CASE("lo de antes y lo de después de la lista se conservan") {
    const wchar_t* crudo =
        L"---\n"
        L"prioridad: enfoque\n"
        L"---\n"
        L"\n"
        L"Este repositorio es el que mueve la ventana.\n"
        L"\n"
        L"## Novedades\n"
        L"\n"
        L"- 2026-09-20 — algo\n"
        L"\n"
        L"## Cómo se compila\n"
        L"\n"
        L"    cmake --build build\n";
    const Proyecto::File file = Proyecto::Parse(crudo);

    CHECK(file.before == std::wstring(L"Este repositorio es el que mueve la ventana."));
    CHECK(Contiene(file.after, L"## Cómo se compila"));
    CHECK(Contiene(file.after, L"cmake --build build"));
    CHECK(Proyecto::Render(file) == std::wstring(crudo));
}

// ------------------------------------------------------------------------- Idempotencia --

TEST_CASE("escribir dos veces da lo mismo") {
    // La prueba que cubre todo lo anterior a la vez: si una ida y vuelta cambia algo, la
    // segunda escritura de cualquier repositorio en modo repo haría un commit que dice que
    // cambió algo que no cambió nadie.
    const wchar_t* desordenado =
        L"\xFEFF"
        L"---\n"
        L"  estado :   bloqueado  \n"
        L"prioridad: enfoque\n"
        L"responsable: alguien\n"
        L"---\n"
        L"Un párrafo pegado al cercado.\n"
        L"\n"
        L"\n"
        L"## novedades\n"
        L"- 2026-09-18 - tres\n"
        L"\n"
        L"- 2026-09-20 — uno\n"
        L"- sin fecha\n"
        L"Texto que cierra la lista.\n";

    const std::wstring una = Proyecto::Render(Proyecto::Parse(desordenado));
    const std::wstring dos = Proyecto::Render(Proyecto::Parse(una));
    CHECK(una == dos);

    // Y nada de lo que no entendíamos se ha quedado por el camino.
    CHECK(Contiene(una, L"responsable: alguien"));
    CHECK(Contiene(una, L"Un párrafo pegado al cercado."));
    CHECK(Contiene(una, L"Texto que cierra la lista."));
    CHECK(Contiene(una, L"- sin fecha"));
}

// ----------------------------------------------------------------------------- Fusionar --

TEST_CASE("fusionar conserva lo ajeno y pisa lo nuestro") {
    const Proyecto::File remoto = Proyecto::Parse(
        L"---\n"
        L"prioridad: algun-dia\n"
        L"responsable: alguien\n"
        L"---\n"
        L"\n"
        L"Un párrafo que no es nuestro.\n"
        L"\n"
        L"## Novedades\n"
        L"\n"
        L"- 2026-09-19 — escrita desde el portátil\n"
        L"\n"
        L"## Licencia\n"
        L"\n"
        L"MIT.\n");

    const std::vector<Model::Novedad> mias = {Nota("2026-09-20", L"escrita aquí")};
    const Proyecto::File fundido = Proyecto::Merge(remoto, Suyo(), mias, kHoy);
    const std::wstring texto = Proyecto::Render(fundido);

    // Lo nuestro manda…
    CHECK(Contiene(texto, L"prioridad: enfoque"));
    CHECK(Contiene(texto, L"estado: bloqueado"));
    CHECK(Contiene(texto, L"siguiente_paso: Medir el pase 2"));
    CHECK(Contiene(texto, L"actualizado: 2026-09-21"));
    // …y lo suyo sigue ahí.
    CHECK(Contiene(texto, L"responsable: alguien"));
    CHECK(Contiene(texto, L"Un párrafo que no es nuestro."));
    CHECK(Contiene(texto, L"## Licencia"));
    // Las dos novedades, la más nueva primero.
    REQUIRE(fundido.novedades.size() == 2);
    CHECK(fundido.novedades[0].day == "2026-09-20");
    CHECK(fundido.novedades[1].text == std::wstring(L"escrita desde el portátil"));
}

TEST_CASE("fusionar dos veces no duplica novedades") {
    // Es lo que pasa en cada guardado: lo que subimos la vez anterior vuelve dentro del
    // remoto. Sin la unión por fecha y texto, el archivo crecería una copia por commit.
    const std::vector<Model::Novedad> mias = {Nota("2026-09-20", L"escrita aquí")};

    const Proyecto::File una = Proyecto::Merge(Proyecto::Parse(L""), Suyo(), mias, kHoy);
    const Proyecto::File dos =
        Proyecto::Merge(Proyecto::Parse(Proyecto::Render(una)), Suyo(), mias, kHoy);

    CHECK(una.novedades.size() == 1);
    CHECK(dos.novedades.size() == 1);
    CHECK(Proyecto::Render(una) == Proyecto::Render(dos));
}

TEST_CASE("una novedad sin fecha del archivo se queda al final y no se pierde") {
    const Proyecto::File remoto = Proyecto::Parse(L"## Novedades\n\n- una nota vieja sin fecha\n");
    const Proyecto::File fundido =
        Proyecto::Merge(remoto, Suyo(), {Nota("2026-09-20", L"con fecha")}, kHoy);

    REQUIRE(fundido.novedades.size() == 2);
    CHECK(fundido.novedades[0].day == "2026-09-20");
    CHECK(fundido.novedades[1].day.empty());
    CHECK(fundido.novedades[1].text == std::wstring(L"una nota vieja sin fecha"));
}

// ------------------------------------------------------------------------------ Importar --

TEST_CASE("importar no vacía lo que ya había") {
    Model::Local actual = Suyo();
    // El archivo no trae siguiente paso: el que hay aquí tiene que sobrevivir.
    const Proyecto::File file = Proyecto::Parse(
        L"---\nprioridad: secundario\nactualizado: 2026-09-10\n---\n"
        L"\n## Novedades\n\n- 2026-09-09 — algo pasó\n");

    const Proyecto::Import traido = Proyecto::Adopt(file, actual);
    CHECK(traido.local.priority == Model::Priority::Secondary);
    CHECK(traido.local.state == Model::State::Blocked);  // no venía en el archivo
    CHECK(traido.local.nextStep == std::wstring(L"Medir el pase 2"));
    CHECK(Model::FormatDay(traido.local.updatedAt) == "2026-09-10");

    REQUIRE(traido.novedades.size() == 1);
    CHECK(traido.novedades[0].repoId == "R_1");
    CHECK(traido.novedades[0].day == "2026-09-09");
    // Su fecha es su fecha: importar un archivo de hace tres meses no puede decir que todo
    // se escribió hoy.
    CHECK(Model::FormatDay(traido.novedades[0].createdAt) == "2026-09-09");
}

TEST_CASE("una novedad sin fecha hereda la del archivo") {
    const Proyecto::File file =
        Proyecto::Parse(L"---\nactualizado: 2026-09-10\n---\n\n## Novedades\n\n- sin fecha\n");
    const Proyecto::Import traido = Proyecto::Adopt(file, Suyo());
    REQUIRE(traido.novedades.size() == 1);
    CHECK(traido.novedades[0].day == "2026-09-10");
}

// ------------------------------------------------------------- Copiar otro .md a novedades --

TEST_CASE("las viñetas de otro .md se proponen una a una") {
    const std::vector<std::wstring> propuestas = Proyecto::AsNovedades(
        L"# Cambios\n\n- Arreglado el arranque\n* Subida la versión\n\n---\n\nFin.\n");

    REQUIRE(propuestas.size() == 2);
    CHECK(propuestas[0] == std::wstring(L"Arreglado el arranque"));
    CHECK(propuestas[1] == std::wstring(L"Subida la versión"));
}

TEST_CASE("sin viñetas, el archivo entero es una sola novedad") {
    const std::vector<std::wstring> propuestas =
        Proyecto::AsNovedades(L"Notas sueltas.\n\nY una segunda línea.\n");
    REQUIRE(propuestas.size() == 1);
    CHECK(propuestas[0] == std::wstring(L"Notas sueltas. Y una segunda línea."));
    // Sin saltos de línea dentro: una novedad es un renglón de una lista, y con saltos
    // rompería la pantalla y el propio PROYECTO.md al que acabaría volviendo.
    CHECK(propuestas[0].find(L'\n') == std::wstring::npos);
}

TEST_CASE("un archivo en blanco no propone nada") {
    CHECK(Proyecto::AsNovedades(L"   \n\n").empty());
}
