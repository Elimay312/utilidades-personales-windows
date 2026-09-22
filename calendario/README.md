# Agenda

Calendario nativo para Windows 11, escrito en C++ y Win32 puro. Se abre con un atajo global
en un popup compacto sobre la barra de tareas, entiende lenguaje natural (`mañana 5pm
dentista`) y, al hacer clic en el calendario, se expande con animación a una app completa con
vistas de día, semana y mes. Sincroniza con Google Calendar y Google Tasks.

Las decisiones de producto, el stack y el sistema de diseño están en [CLAUDE.md](CLAUDE.md).

## Estado

**En desarrollo, fase 0.** Todavía no hay interfaz: solo el esqueleto que compila, el logging,
la lectura de configuración y la resolución del monitor de trabajo.

## Requisitos

- Windows 10 1809 o superior, o Windows 11 (x64).
- Visual Studio 2022 con la carga de trabajo *Desarrollo para el escritorio con C++* (MSVC
  v143 y el SDK de Windows 10/11).
- CMake 3.28 o superior.
- [vcpkg](https://vcpkg.io) con la variable de entorno `VCPKG_ROOT` apuntando a su carpeta.
  Si no está instalado, CMake descarga nlohmann-json y Catch2 con `FetchContent` y el build
  funciona igual; solo hace falta Git y conexión.

## Compilación

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Lo mismo con `release`. Los binarios quedan en `build\debug\Agenda.exe` y
`build\release\Agenda.exe`.

### Ejecución

```
build\debug\Agenda.exe --monitor=3
```

- `--monitor=N` fija el monitor por su número de Windows, es decir el dispositivo
  `\\.\DISPLAYN`. El orden de enumeración **no** es ese número.
- `AGENDA_DEV_MONITOR=N` hace lo mismo, y `--monitor` tiene prioridad.
- En builds Debug el valor por defecto es 3, que es el monitor de desarrollo. En Release, sin
  argumento ni variable, se usa el monitor primario.
- Si el monitor indicado no está conectado, se registra el error y el proceso termina con
  código **2**. No hay fallback silencioso a otro monitor. Si ya hay otra instancia
  ejecutándose, termina con código **1**.

Por ahora la app solo escribe en el log el monitor resuelto y su rectángulo de trabajo, y sale.

### Datos en disco

- Log: `%LOCALAPPDATA%\Agenda\logs\agenda-AAAAMMDD.log` (UTF-8, un archivo por día).
- Configuración: `config.json` y, opcionalmente, `config.local.json`, primero junto al
  ejecutable y después en `%LOCALAPPDATA%\Agenda`. Se fusionan en ese orden, así que el
  archivo local manda. Los dos pueden faltar. **`config.local.json` guarda los secretos y
  nunca se sube al repositorio.**

## Estructura del proyecto

```
src/
  app/      entrada (wWinMain), monitores y argumentos
  ui/       ventanas, render D2D, componentes, animación (vacío hasta la fase 1)
  nlp/      parser de lenguaje natural, sin dependencias de interfaz
  data/     SQLite, modelos y repositorios
  sync/     OAuth y clientes de Google Calendar y Tasks
  core/     logging, configuración, rutas y utilidades
tests/      pruebas con Catch2
assets/     manifiesto (DPI Per-Monitor v2) e iconos
docs/       capturas y decisiones de arquitectura
```

## Hoja de ruta

| Fase | Qué entrega | Estado |
|---|---|---|
| 0 | Esqueleto que compila, documentación, logging, configuración y regla de monitores | Hecha |
| 1 | Ventana popup con fondo acrylic, atajo global e icono en la bandeja | Pendiente |
| 2 | Sistema de diseño y vista de mes compacta con datos de ejemplo | Pendiente |
| 3 | Parser de lenguaje natural con vista previa en vivo | Pendiente |
| 4 | Almacenamiento en SQLite: eventos, tareas y caché local | Pendiente |
| 5 | Sincronización con Google Calendar y Google Tasks (OAuth) | Pendiente |
| 6 | Expansión animada a la app completa con vistas de día, semana y mes | Pendiente |
| 7 | Pulido, rendimiento, empaquetado y arranque con Windows | Pendiente |

El detalle de cada fase vive en el plan de fases del proyecto; las fases 3 a 7 pueden ajustarse
sobre la marcha.
