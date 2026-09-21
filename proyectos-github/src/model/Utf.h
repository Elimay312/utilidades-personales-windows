#pragma once

// El borde entre UTF-8 y UTF-16, escrito a mano y probado.
//
// CLAUDE.md dice: cadenas internas en std::wstring, UTF-8 solo en el borde con JSON y
// SQLite. Este archivo ES ese borde, y es el único sitio donde se convierte.
//
// Va a mano y no con MultiByteToWideChar por la regla que decide qué entra en
// brujula_core: *¿se equivocaría esto en silencio, sin que se vea en pantalla hasta que ya
// ha decidido mal?* Una descripción con una eñe mal convertida no revienta nada: sale rara
// en una tarjeta, y solo en la de un repositorio, y solo si alguien mira. Con esto en el
// núcleo, las tildes, la eñe, los emoji y los bytes rotos tienen prueba.
//
// Nada lanza y nada se queda a medias: lo que no es válido sale como U+FFFD y se sigue.
// Una respuesta de GitHub con un byte roto tiene que dejar los otros 108 repositorios
// dentro, no tirar la sincronización entera.

#include <string>
#include <string_view>

namespace Model {

std::wstring ToWide(std::string_view utf8);
std::string ToUtf8(std::wstring_view wide);

}  // namespace Model
