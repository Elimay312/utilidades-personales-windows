# Actualizar README.md Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Actualizar la tabla de proyectos del README.md raíz para reflejar el estado actual y verificado de las utilidades (Brújula terminando sus 8 fases, Rayo 1.0.0, HUD terminado e Isla con arquitectura de dos islas).

**Architecture:** Modificación de la tabla en Markdown en `README.md` asegurando consistencia de enlaces relativos y formato tabular.

**Tech Stack:** Markdown, Git, PowerShell.

## Global Constraints

- No alterar las secciones posteriores de arquitectura y seguridad.
- Mantener el orden alfabético de la tabla y los enlaces relativos existentes.
- Respetar la voz y el estilo conciso del repositorio.

---

### Task 1: Actualizar la tabla de proyectos en README.md

**Files:**
- Modify: `README.md:12-23`

**Interfaces:**
- Consumes: `docs/superpowers/specs/2026-09-22-actualizar-readme-design.md`
- Produces: `README.md` actualizado

- [ ] **Step 1: Aplicar los cambios en README.md**

Modificar las líneas correspondientes a `calendario`, `hud`, `isla`, `proyectos-github` y `rayo-file-manager` en `README.md`:

```markdown
| Proyecto | Qué es | Estado |
|---|---|---|
| [`calendario/`](calendario/README.md) | **Agenda**: un calendario en C++ que sale con un atajo en el monitor del ratón; escribes `mañana 5pm dentista` y lo crea, sincronizado con Google Calendar y Tasks. | 1.0.0, con instalador. |
| [`dock/`](dock/README.md) | Un dock estilo macOS: magnificación, efecto genio, uno por pantalla, miniaturas de ventanas. | En uso diario. Falta pulir rendimiento. |
| [`hud/`](hud/README.md) | El aviso de volumen, rehecho: cápsula de cristal abajo y centrada, que se transforma en vez de ir y venir. Las teclas son suyas, así que el recuadro gris de Windows no sale. | Terminado. Funciona en tres pantallas y sigue al dispositivo de salida. |
| [`isla/`](isla/README.md) | Una isla dinámica en el borde superior de la pantalla en la que estés trabajando: qué suena, de quién, cuánto queda, y poder pausarlo. Más pomodoro, batería y volumen —con el número y por qué altavoces sale—, y los recordatorios de Agenda en su propia burbuja. | Funcionando (dos islas: principal y avisos). Falta probarla en otros equipos. |
| [`lanzador/`](lanzador/README.md) | `Alt+Espacio` y escribes: aplicaciones, ficheros (vía Everything), prefijos web, sitios del sistema y cuentas, ordenados por lo que más abres. | Funcionando. Falta usarlo unos días. |
| [`proyectos-github/`](proyectos-github/README.md) | **Brújula**: los ~120 repositorios de GitHub ordenados por en cuál conviene trabajar, con el siguiente paso de cada uno guardado para no tener que reconstruirlo. | Terminado (8 de 8 fases). Sincronización en segundo plano, inspector, revisión semanal y empaquetador. |
| [`quicklook/`](quicklook/README.md) | Vista previa con la barra espaciadora: seleccionas un archivo en el Explorador, pulsas espacio, lo ves. Imágenes, PDF, vídeo y audio. | Funcionando. Falta usarlo unos días. |
| [`rayo-file-manager/`](rayo-file-manager/CLAUDE.md) | **Rayo**: un gestor de archivos en C++ con vista previa, vigilancia de carpetas y orden natural. | 1.0.0, con instalador (8 de 8 fases). Su documentación es `CLAUDE.md`. |
| [`renombrar/`](renombrar/README.md) | Renombrado masivo con vista previa: ves la tabla antes → después de todo el lote, y solo entonces se aplica. Con deshacer. | Funcionando. Falta usarlo unos días en la oficina. |
```

- [ ] **Step 2: Verificar diff y validez de enlaces**

Comprobar con `git diff README.md` que los cambios corresponden exactamente a lo especificado y que los enlaces relativos apuntan a archivos existentes.

- [ ] **Step 3: Commit del cambio**

Ejecutar:
```bash
git add README.md
git commit -m "docs: actualizar estado de los proyectos en README"
```
