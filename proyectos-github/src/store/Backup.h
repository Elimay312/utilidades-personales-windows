#pragma once

// Exportar e importar la base local a un archivo JSON.
//
// **Solo lo del usuario.** Prioridad, estado, siguiente paso, carpeta y novedades. Los
// repositorios no se exportan: vienen de GitHub y vuelven solos con la primera
// sincronización, y meterlos en el archivo lo convertiría en una copia de datos de trabajo
// de la empresa que después acaba en una carpeta compartida.
//
// **Y el modo repo NO se importa**, aunque se exporte. Un archivo que encendiera el modo
// repo y su confirmación en ciento nueve repositorios de golpe sería exactamente el
// interruptor global que la regla 5 de SEGURIDAD.md dice que no existe. Se exporta para que
// la copia diga la verdad de cómo estaba la cosa; al restaurar hay que volver a confirmar
// repositorio por repositorio, y eso es lo correcto.
//
// Cada fila lleva el nombre completo Y el identificador, y al importar se empareja primero
// por nombre. Es la misma decisión de Repos::UpsertMetadata y por el mismo motivo: GitHub
// está retirando sus identificadores globales viejos, y una copia hecha antes del cambio
// tiene que poder restaurarse después.

#include <string>
#include <string_view>

#include "store/Db.h"

namespace Store {

// La versión del formato. Sube cuando cambie la forma del archivo, y un archivo de una
// versión MÁS NUEVA se rechaza en vez de leerse a medias.
inline constexpr int kBackupVersion = 1;

enum class ImportMode {
    // Gana lo más nuevo, comparando 'actualizado'. Es lo que quiere quien lleva dos equipos.
    Merge,
    // Manda el archivo. Es lo que quiere quien acaba de perder la caché.
    Replace,
};

struct ImportReport {
    int matched = 0;    // filas que encontraron su repositorio en esta caché
    int skipped = 0;    // filas cuyo repositorio no está aquí: se sincroniza y se repite
    int kept = 0;       // filas que se dejaron como estaban por ser más nuevas
    int novedades = 0;  // novedades añadidas
};

Model::Result<std::string> ExportJson(Db& db, Model::Instant now);

// Nunca borra. Las novedades se unen por fecha y texto, y una fila que el archivo no trae se
// queda donde estaba: importar una copia vieja no puede vaciar lo que se escribió después.
Model::Result<ImportReport> ImportJson(Db& db, std::string_view json, ImportMode mode);

}  // namespace Store
