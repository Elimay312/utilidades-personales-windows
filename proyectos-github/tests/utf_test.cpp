// El borde entre UTF-8 y UTF-16. Es el sitio donde un fallo no se ve: una descripción con
// una eñe mal convertida no revienta nada, sale rara en una tarjeta de 109, y solo si
// alguien se fija. Y lo que llega por aquí son nombres y mensajes de commit de
// repositorios de trabajo, así que romperlos es romper el dato.

#include <doctest/doctest.h>

#include "model/Utf.h"

#include <string>

using Model::ToUtf8;
using Model::ToWide;

namespace {
// U+FFFD, el carácter de sustitución, en las dos codificaciones.
constexpr const char* kFffdUtf8 = "\xEF\xBF\xBD";
constexpr wchar_t kFffd = L'�';
}  // namespace

TEST_CASE("las tildes y la eñe van y vuelven") {
    // "año" en UTF-8: la eñe son dos bytes. Escrito con escapes y no con el carácter para
    // que la prueba no dependa de cómo guarde este archivo el editor de turno.
    CHECK(ToWide("a\xC3\xB1o") == L"año");
    CHECK(ToUtf8(L"año") == "a\xC3\xB1o");

    const std::string original = "Revisi\xC3\xB3n a\xC3\xB1o ni\xC3\xB1o";
    CHECK(ToUtf8(ToWide(original)) == original);
}

TEST_CASE("un emoji es un par de suplentes, no un carácter") {
    // U+1F600. En UTF-8 son cuatro bytes; en UTF-16 son DOS unidades, y esa es la parte que
    // se olvida: quien cuente wchar_t creyendo que cuenta caracteres parte el emoji.
    const std::wstring wide = ToWide("\xF0\x9F\x98\x80");
    CHECK(wide.size() == 2);
    CHECK(wide == L"\U0001F600");
    CHECK(ToUtf8(wide) == "\xF0\x9F\x98\x80");
}

TEST_CASE("un byte inválido se sustituye y no se come el resto") {
    // Lo importante no es que salga U+FFFD: es que siga habiendo una "b" detrás. Si el
    // decodificador se atascara o saltara de más, una respuesta con un byte roto se
    // llevaría por delante los otros 108 repositorios.
    const std::wstring wide = ToWide("a\xFF" "b");
    CHECK(wide.size() == 3);
    CHECK(wide[0] == L'a');
    CHECK(wide[1] == kFffd);
    CHECK(wide[2] == L'b');
}

TEST_CASE("una secuencia cortada no lee más allá del final") {
    // 0xC3 anuncia dos bytes y no hay segundo. Leerlo de todos modos sería leer fuera de la
    // cadena, que es el fallo que no da error: da lo que hubiera en esa memoria.
    CHECK(ToWide("a\xC3") == std::wstring(L"a") + kFffd);

    // 0xE2 anuncia tres y solo hay dos. Salen DOS sustituciones y no una, y es una decisión:
    // cada byte que no vale se sustituye por su cuenta. Unicode recomienda agrupar la
    // secuencia rota en una sola sustitución, que quedaría más bonito, pero aquí lo único
    // que se hace con esto es enseñarlo; lo que de verdad importa —no atascarse, no leer de
    // más y no perder lo que viene detrás— se cumple igual y con la mitad de código.
    CHECK(ToWide("\xE2\x82") == std::wstring(2, kFffd));
}

TEST_CASE("las formas sobrelargas se rechazan") {
    // "\xC0\xAF" es una barra codificada en dos bytes. Es válida aritméticamente y no lo es
    // en UTF-8, y aceptarla es la manera clásica de colar una barra por un filtro que mira
    // la cadena antes de convertirla.
    const std::wstring wide = ToWide("\xC0\xAF");
    CHECK(wide.find(L'/') == std::wstring::npos);
    CHECK(wide.size() == 2);
}

TEST_CASE("un suplente codificado en UTF-8 se rechaza") {
    // ED A0 80 es U+D800 escrito como si fuera un carácter normal: es CESU-8, no UTF-8.
    // Dejarlo pasar metería medio par en una wstring, y al escribirla de vuelta saldría
    // UTF-8 que ningún otro programa acepta.
    const std::wstring wide = ToWide("\xED\xA0\x80");
    CHECK(wide.size() == 3);
    for (const wchar_t unit : wide) CHECK(unit == kFffd);
}

TEST_CASE("un suplente suelto al escribir sale sustituido") {
    // Llega así cuando alguien corta una cadena por la mitad de un emoji, por ejemplo al
    // recortar una descripción para que quepa en una tarjeta.
    CHECK(ToUtf8(std::wstring(1, L'\xD83D')) == kFffdUtf8);
    CHECK(ToUtf8(std::wstring(1, L'\xDE00')) == kFffdUtf8);
    // Y el par entero sigue saliendo bien, que es lo que demuestra que no sustituye de más.
    CHECK(ToUtf8(L"\U0001F600") == "\xF0\x9F\x98\x80");
}

TEST_CASE("una cadena vacía no es un caso especial") {
    CHECK(ToWide("").empty());
    CHECK(ToUtf8(L"").empty());
}
