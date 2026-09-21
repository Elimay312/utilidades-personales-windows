// Asserts sobre la deteccion y el recorte del texto: es la unica logica de la fase 4 que no
// se ve en pantalla hasta que ya ha decidido mal (un binario pintado como texto, un
// caracter partido por el corte de 64 KB). Se ejecuta con build\rayo_preview_check.exe y no
// imprime nada si todo va bien.
#undef NDEBUG

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#include "preview/Preview.h"

namespace {

std::optional<std::string> Decode(const std::string& bytes) {
    return DecodeTextPreview(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
}

size_t CountLines(const std::string& text) {
    size_t lines = text.empty() ? 0 : 1;
    for (const char c : text)
        if (c == '\n') ++lines;
    return lines;
}

void TestPlainText() {
    const auto text = Decode("hola\nmundo\n");
    assert(text && *text == "hola\nmundo\n");

    // Los CR se tiran: no se dibujan pero cuentan al medir el ancho de cada linea.
    const auto crlf = Decode("hola\r\nmundo\r\n");
    assert(crlf && *crlf == "hola\nmundo\n");

    assert(!Decode(""));
}

void TestBom() {
    const auto utf8 = Decode(std::string("\xEF\xBB\xBF", 3) + "\xC3\xA1rbol");
    assert(utf8 && *utf8 == "\xC3\xA1rbol");  // el BOM no llega a la pantalla

    // "hi" en UTF-16, en los dos ordenes de bytes.
    const auto little = Decode(std::string("\xFF\xFE" "h\0i\0", 6));
    assert(little && *little == "hi");
    const auto big = Decode(std::string("\xFE\xFF" "\0h\0i", 6));
    assert(big && *big == "hi");

    // Un BOM solo no es un archivo de texto vacio que merezca la pena pintar, pero tampoco
    // es binario: lo importante es que no reviente.
    const auto onlyBom = Decode(std::string("\xFF\xFE", 2));
    assert(onlyBom && onlyBom->empty());
}

void TestBinary() {
    // Un NUL en los primeros 4 KB delata un binario aunque el resto sea legible.
    assert(!Decode(std::string("MZ\x90\x00 programa", 12)));

    // UTF-8 invalido: 0xC3 exige una continuacion y le sigue un '('.
    assert(!Decode("A\xC3(B"));

    // Un NUL mas alla de los 4 KB no se mira: 64 KB de un binario que empieza en ASCII
    // acabarian pintados, y es un caso que no compensa pagar en cada archivo.
    std::string late(5000, 'a');
    late.push_back('\0');
    assert(Decode(late));
}

void TestTruncatedUtf8() {
    // El corte de 64 KB parte un caracter de dos bytes por la mitad: fuera la cola, y lo que
    // queda tiene que seguir siendo UTF-8 valido (si no, se tomaria por binario).
    const auto cut = Decode(std::string("hola ") + "\xC3");
    assert(cut && *cut == "hola ");

    const auto cutThree = Decode(std::string("hola ") + "\xE2\x82");  // euro a medias
    assert(cutThree && *cutThree == "hola ");

    // Una secuencia entera no se toca.
    const auto whole = Decode(std::string("hola ") + "\xE2\x82\xAC");
    assert(whole && *whole == "hola \xE2\x82\xAC");
}

void TestClipping() {
    std::string many;
    for (int i = 0; i < 1000; ++i) many += "linea\n";
    const auto lines = Decode(many);
    assert(lines && CountLines(*lines) == 200);

    // Un JSON minificado es una sola linea larguisima: se corta y se avisa.
    const std::string huge(5000, 'x');
    const auto clipped = Decode(huge);
    assert(clipped && clipped->size() == 2000 + 4);
    assert(clipped->compare(2000, 4, " ...") == 0);

    // Cortar por columnas cuenta caracteres, no bytes: un acentuado ocupa dos bytes y no
    // puede quedar partido.
    std::string accents;
    for (int i = 0; i < 3000; ++i) accents += "\xC3\xA1";
    const auto accented = Decode(accents);
    assert(accented && accented->size() == 2000 * 2 + 4);
}

}  // namespace

int main() {
    TestPlainText();
    TestBom();
    TestBinary();
    TestTruncatedUtf8();
    TestClipping();
    return 0;
}
