#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace dock {

// dock.json es JSON con comentarios y comas finales, escrito a mano. nlohmann 3.12 ignora
// comentarios pero no comas finales (no tiene ignore_trailing_commas), así que se limpian
// las dos cosas aquí, respetando lo que va dentro de las cadenas.
std::string StripJsonc(std::string_view text);

// Parsea sin excepciones: un dock.json roto devuelve un json "discarded" y el que llama se
// queda con los valores por defecto.
nlohmann::json ParseJsonc(std::string_view text);

// Las claves se buscan sin distinguir mayúsculas, como hacía el dock de C#: "IconSize" y
// "iconSize" son la misma. nullptr si no está.
const nlohmann::json* Find(const nlohmann::json& object, std::string_view key);

}  // namespace dock
