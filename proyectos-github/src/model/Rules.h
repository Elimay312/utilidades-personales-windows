#pragma once

// Las tres reglas de CLAUDE.md, como funciones puras.
//
// Están aquí y no dentro de una vista por la regla 2 de arquitectura, pero el motivo de
// fondo es el de siempre: son decisiones que se equivocan en silencio. Un repositorio en el
// grupo equivocado parece un repositorio bien colocado; un límite de Enfoque que se puede
// saltar no avisa de que se saltó; un desajuste que no se detecta es, literalmente, nada en
// pantalla. Ninguna de las tres da un error cuando falla.
//
// "Ahora" entra por parámetro y no sale de system_clock::now(). Es lo único que permite
// probar los bordes de 14 y 90 días, que es donde estas funciones se equivocan.

#include <optional>
#include <string>
#include <vector>

#include "model/Types.h"

namespace Model {

struct Thresholds {
    // Los de CLAUDE.md. Configurables, y por eso son datos y no números escritos dentro.
    int activeDays = 14;
    int dormantDays = 90;
    int focusLimit = 5;
    int focusDormantDays = 14;
};

// activo si hubo push en los últimos 14 días; en pausa entre 14 y 90; dormido MÁS de 90.
//
// Los bordes son asimétricos y no es un descuido: 14 días justos ya es "en pausa" —los
// últimos 14 días no incluyen el día 14— y 90 justos todavía es "en pausa", porque dormido
// es "más de 90". Está copiado de CLAUDE.md tal cual y tiene prueba en los dos bordes.
//
// Sin pushedAt —un repositorio al que no se ha empujado nunca— sale dormido: no hay
// actividad que medir, y esconderlo en "activo" sería esconderlo de la vista que existe
// para encontrarlo.
Activity Classify(std::optional<Instant> pushedAt, Instant now, const Thresholds& limits);

// Lo que hace falta saber de un repositorio que ya está en Enfoque para poder ordenar a
// quién se baja.
struct FocusEntry {
    std::string id;
    std::optional<Instant> pushedAt;
};

struct FocusPlan {
    // false: no cabe y hay que elegir. El límite no se puede saltar (CLAUDE.md), así que
    // quien llama no tiene un camino para ignorar esto.
    bool fits = false;
    // Los que están en Enfoque, ordenados por el que lleva más tiempo sin un push: el
    // primero es el mejor candidato a bajar. Vacío cuando cabe.
    std::vector<std::string> demote;
};

// Qué pasa si 'candidate' entra en Enfoque. Si ya estaba dentro, cabe y no consume hueco:
// volver a marcar como Enfoque algo que ya lo es no puede pedir que se baje a nadie.
FocusPlan PlanFocus(const std::vector<FocusEntry>& inFocus, const std::string& candidate,
                    Instant now, const Thresholds& limits);

// "Necesita decisión". Solo estos dos casos: "Sin clasificar" y "Dormidos" son vistas
// aparte de la barra lateral, no desajustes.
enum class Mismatch {
    None,
    FocusDormant,       // está en Enfoque y lleva más de focusDormantDays sin un push
    ArchivedButActive,  // está archivado y sin embargo le siguen llegando pushes
};

Mismatch Review(const Repo& repo, const Local& local, Instant now, const Thresholds& limits);

}  // namespace Model
