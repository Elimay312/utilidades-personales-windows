#pragma once

// Codificar en base64. Solo codificar.
//
// Hace falta en un sitio y solo en uno: la API de contenidos de GitHub recibe el archivo en
// base64 dentro de un JSON. Leerlo se hace por GraphQL, que lo devuelve en texto ya, así que
// no hay nada que descodificar y un descodificador sería código sin llamador esperando a
// pudrirse.
//
// Está en el núcleo por la regla que decide qué entra aquí: ¿se equivocaría en silencio? Un
// relleno mal puesto no revienta nada. Sube un PROYECTO.md con la última línea cortada,
// GitHub lo acepta tan contento, y el fallo aparece meses después al abrir el archivo en la
// web — que es justo cuando ya no se sabe qué lo escribió.

#include <string>
#include <string_view>

namespace Model {

// Sobre BYTES, no sobre caracteres. Lo que se codifica es el UTF-8 que va a viajar, así que
// quien llame convierte antes con Model::ToUtf8 y aquí ya no hay texto, hay octetos.
std::string ToBase64(std::string_view bytes);

}  // namespace Model
