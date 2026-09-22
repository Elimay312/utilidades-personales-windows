# Especificación de Diseño: Actualización del README.md

## 1. Contexto y Objetivo
El repositorio raíz agrupa múltiples utilidades de escritorio independientes para Windows 11. Cada proyecto evoluciona en su propia subcarpeta con su propia documentación y reglas.

El [README.md](../../../README.md) principal contenía estados desactualizados de varios proyectos que completaron hitos mayores recientemente (especialmente Brújula y Rayo, que finalizaron sus 8 fases de desarrollo, así como mejoras en HUD e Isla).

El objetivo es actualizar la tabla de la sección `## Qué hay` para reflejar la realidad del software sin alterar la voz ni la estructura del repositorio.

## 2. Modificaciones en la Tabla de Proyectos

Se actualizarán las siguientes entradas dentro de la tabla de la sección `## Qué hay`:

1. **`calendario/` (Agenda)**:
   - **Qué es**: Reflejar que el atajo abre el popup en el monitor activo del ratón.
   - **Texto**: `**Agenda**: un calendario en C++ que sale con un atajo en el monitor del ratón; escribes \`mañana 5pm dentista\` y lo crea, sincronizado con Google Calendar y Tasks.`
   - **Estado**: `1.0.0, con instalador.`

2. **`hud/`**:
   - **Estado actual en tabla**: `Funcionando en tres pantallas. Falta usarlo unos días.`
   - **Nuevo Estado**: `Terminado. Funciona en tres pantallas y sigue al dispositivo de salida.`

3. **`isla/`**:
   - **Estado actual en tabla**: `Funcionando. Falta probarla en otros equipos.`
   - **Nuevo Estado**: `Funcionando (dos islas: principal y avisos). Falta probarla en otros equipos.`

4. **`proyectos-github/` (Brújula)**:
   - **Estado actual en tabla**: `En construcción: fase 1 de 8. Hay ventana, todavía no hay datos.`
   - **Nuevo Estado**: `Terminado (8 de 8 fases). Sincronización en segundo plano, inspector, revisión semanal y empaquetador.`

5. **`rayo-file-manager/` (Rayo)**:
   - **Estado actual en tabla**: `Funcionando. Le falta README propio; por ahora su documentación es \`CLAUDE.md\`.`
   - **Nuevo Estado**: `1.0.0, con instalador (8 de 8 fases). Su documentación es \`CLAUDE.md\`.`

Las demás filas (`dock/`, `lanzador/`, `quicklook/`, `renombrar/`) y las secciones posteriores permanecen inalteradas.

## 3. Criterios de Aceptación
- La tabla de `README.md` refleja con fidelidad técnica el estado actual de cada componente.
- Los enlaces relativos a carpetas y READMEs de cada proyecto siguen funcionando.
- El formato Markdown se mantiene limpio y sin roturas de sintaxis.
