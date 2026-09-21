#pragma once

// El esquema y sus migraciones.
//
// La versión vive en PRAGMA user_version, que es un entero dentro de la cabecera del propio
// archivo y —esto es lo importante— es transaccional: si la migración se cae por la mitad,
// el número no sube. Con una tabla de versiones escrita a mano habría que acordarse de
// meterla en la misma transacción; con user_version no hay nada que recordar.
//
// Las migraciones van hacia delante y nunca hacia atrás. Una base de una versión MÁS NUEVA
// que la del programa no se toca y devuelve error: pasa cuando alguien abre una compilación
// vieja después de una nueva, y convertirla hacia atrás a ciegas sería perder los datos que
// la versión nueva escribió.

#include "store/Db.h"

namespace Store {

inline constexpr int kSchemaVersion = 2;

// Lleva la base hasta kSchemaVersion. Idempotente: llamarla con la base ya al día no hace
// nada y no es un error.
Model::Outcome Migrate(Db& db);

}  // namespace Store
