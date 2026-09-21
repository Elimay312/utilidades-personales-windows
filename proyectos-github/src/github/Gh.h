#pragma once

// Pedirle la credencial a GitHub CLI.
//
// Es lo primero que se intenta, y si sale bien NO se guarda nada: el mejor sitio para una
// credencial es el de otro que ya la gestiona bien (SEGURIDAD.md). Solo se guarda en el
// Administrador de credenciales la que el usuario pega a mano, porque esa no tiene dueño.
//
// Se lanza con CreateProcessW y una tubería. No con system() ni con _wpopen: la regla 7 de
// auditar.ps1 los prohíbe, y con motivo — los dos construyen una línea de órdenes y se la
// dan a un intérprete.
//
// Cuesta ~570 ms medidos, así que esto se llama desde el hilo de trabajo y una sola vez por
// arranque. Nunca desde el hilo que pinta.

#include <string>

#include "github/Auth.h"
#include "model/Result.h"

namespace Github {
namespace Gh {

// Vacío si GitHub CLI no está instalado.
bool Find(std::wstring& pathOut);

// Un Secret vacío significa "gh está pero no tiene sesión iniciada", que no es un error:
// es la señal de enseñar la hoja de bienvenida.
Model::Result<Secret> AskForCredential();

}  // namespace Gh
}  // namespace Github
