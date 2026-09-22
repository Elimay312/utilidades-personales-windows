#pragma once

// Qué pidió quien nos arrancó: la línea de órdenes y el esquema de URL.
//
// Está en brujula_core y es puro porque **esto es una frontera de confianza**. El argumento
// de la línea de órdenes lo escribe el usuario, pero una `brujula://` la puede disparar
// cualquier página web que alguien abra sin mirar: el navegador pregunta y hay gente que
// dice que sí. Lo que llega por ahí es texto de un desconocido, y el sitio para decidir qué
// se acepta es una función con pruebas, no un `if` dentro de wWinMain.
//
// Lo que sale de aquí es SOLO un nombre de repositorio, y se usa para una cosa: buscarlo en
// la caché. No abre archivos, no compone rutas y no llega a ninguna petición de red — si un
// día lo hiciera, esta cabecera es la que habría que volver a leer.

#include <string>

namespace App {

// El nombre que se pidió abrir, o vacío si no se pidió ninguno.
struct Launch {
    std::wstring repo;
};

// `brujula.exe --repo NOMBRE` o `brujula.exe brujula://repo/NOMBRE`. El segundo es como
// llega cuando el esquema lo lanza el shell, que pasa la URL entera como un argumento.
Launch ParseArguments(int count, const wchar_t* const* argv);

// brujula://repo/NOMBRE -> NOMBRE, ya sin porcentajes. Vacío si no es una URL nuestra o si
// lo que trae no tiene forma de nombre de repositorio.
std::wstring RepoFromUrl(const std::wstring& url);

// Qué se acepta como nombre: letras, cifras y los cuatro signos que GitHub admite en un
// nombre de repositorio, más la barra que separa al dueño. Sin espacios, sin controles y
// con un largo máximo.
//
// Es una lista blanca y no una negra a propósito: una lista negra hay que acertarla entera
// y esta solo hay que acertarla una vez.
bool LooksLikeRepoName(const std::wstring& name);

}  // namespace App
