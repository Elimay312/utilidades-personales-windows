# CLAUDE.md — Agenda (calendario rápido para Windows)

> Nombre en clave: **Agenda**. Si el usuario cambia el nombre, actualiza este archivo, README y CMake.

## Qué es

Un calendario nativo para Windows escrito en C++ que se abre con un atajo global, al estilo de Fantastical:

1. El usuario pulsa el atajo y aparece un **popup compacto** (abajo a la derecha, sobre la barra de tareas) con el mes, los eventos del día y un campo de texto con foco.
2. Escribe en lenguaje natural (`mañana 5pm dentista`) y ve una **vista previa en vivo** de lo que entendió. Con Enter se crea el evento o la tarea.
3. Si hace **clic en el calendario**, el popup se **expande con animación** a una app completa con vistas de día, semana y mes y una línea de tiempo detallada.
4. Todo se sincroniza con **Google Calendar** (eventos) y **Google Tasks** (tareas).

## Prioridades (en este orden)

1. **Diseño.** Debe sentirse parte de Windows 11 y verse pulido: tipografía, espaciado, animaciones y estados hover y foco cuidados. Ante la duda entre algo más bonito y algo 2 ms más rápido, elige lo bonito.
2. **Sensación de inmediatez.** El popup aparece en menos de 100 ms desde el atajo, porque se precrea oculto y los datos se leen de la caché local. No hay spinners en el popup.
3. **Rendimiento bruto.** C++ bien escrito ya es rápido. No hagas micro-optimizaciones que empeoren la legibilidad.

## Regla de monitores (OBLIGATORIA)

- El usuario trabaja en el **monitor 1** y orquesta desde el **monitor 2**. **NUNCA** abras, muevas ni captures ventanas en los monitores 1 o 2.
- **Todo** lo que se ejecute durante el desarrollo va al **monitor 3**: la app, las ventanas de prueba y las capturas.
- Resuelve el monitor por nombre de dispositivo `\\.\DISPLAY3`, usando `EnumDisplayMonitors` con `GetMonitorInfoW` y `MONITORINFOEXW::szDevice`. No uses el índice de enumeración, porque no coincide con el número que muestra Windows.
- La app acepta `--monitor=N` y la variable de entorno `AGENDA_DEV_MONITOR`. En builds Debug el valor por defecto es 3. Si el monitor no existe, la app registra una advertencia y **no se abre**. No hay fallback silencioso a otro monitor.
- Para verificar el diseño **no** hagas capturas del escritorio. Usa `Agenda.exe --render-snapshot=<vista> --out=shot.png`, que renderiza la vista fuera de pantalla a PNG (con Direct2D sobre un bitmap WIC). Revisa ese PNG.
- El flujo OAuth abre el navegador por defecto, y Windows decide en qué monitor. Antes de lanzarlo, **avisa al usuario y espera su confirmación**.

## Stack fijado (no cambiar sin preguntar)

| Área | Decisión |
|---|---|
| Lenguaje | C++20, MSVC (Visual Studio 2022), solo x64 |
| Build | CMake ≥ 3.28 con presets y vcpkg en modo manifest |
| Ventanas | Win32 puro (`RegisterClassExW` y `CreateWindowExW`) |
| Render | Direct2D 1.1, DirectWrite y DirectComposition |
| Fondo | Acrylic o Mica mediante `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)`, esquinas con `DWMWA_WINDOW_CORNER_PREFERENCE`; fallback a color sólido en Windows 10 |
| Animación | Transiciones simples (abrir, cerrar, fundidos) con animaciones de DirectComposition (`IDCompositionAnimation`, tramos cúbicos que interpola la GPU). El motor propio de springs sobre temporizador vsync llega en la fase 6, que es la que lo necesita para la expansión popup→app. Los estados de hover y foco y el deslizamiento del mes viven dentro del contenido Direct2D, que DirectComposition no puede animar por sí solo, así que los interpola un temporizador de 16 ms que solo corre mientras algo se mueve |
| HTTP | WinHTTP (nativo, sin dependencias) |
| JSON | nlohmann-json (vcpkg) |
| Almacenamiento | SQLite3 (vcpkg), en `%LOCALAPPDATA%\Agenda\agenda.db` |
| Secretos | Refresh token cifrado con `CryptProtectData` (DPAPI) |
| Tests | Catch2 v3 (vcpkg) |
| DPI | Per-Monitor v2 en el manifiesto; todo el layout en DIPs |

Cualquier dependencia que no esté en esta tabla requiere **preguntar antes**.

## Sistema de diseño (basado en el mockup de referencia)

- **Tipografía:** Segoe UI Variable, con fallback a Segoe UI. Tamaños: 11 para etiquetas de día, 12 para días, 13 para eventos y 15 semibold para el título del mes.
- **Colores (tema oscuro, el principal):**
  - Fondo del panel: `#1E1F24` al 85 % sobre acrylic.
  - Superficie de tarjeta: `#2A2B31`.
  - Texto primario: `#F2F2F5`.
  - Texto secundario: `#8B8C94`.
  - Acento y hoy: `#4A8BF5`.
  - Evento alternativo: `#F5A623`.
  - Punto de evento en el día: 4 px con el color del calendario.
- **Colores (tema claro, derivado del oscuro):** panel `#F4F4F7` al 85 %, superficie `#FFFFFF`, texto primario `#1B1C21`, secundario `#6C6D75`, borde negro al 10 %. El acento baja a `#2F6FE0`, porque el número del día va en blanco sobre el círculo y `#4A8BF5` no da contraste suficiente sobre un panel claro. La app sigue el tema del sistema (`AppsUseLightTheme`, solo lectura).
- **Formas:** radio de 14 px en el panel, 8 px en las tarjetas de evento, cápsula completa en el input y círculo en el día de hoy.
- **Espaciado:** rejilla de 4 px. El panel mide 340×420 DIP y tiene 16 px de padding interno.
- **Tarjeta de evento:** barra de color de 3 px a la izquierda, hora en texto secundario y título en primario.
- **Movimiento:**
  - Apertura: fade de 0 a 1 y desplazamiento de 8 px hacia arriba en 160 ms (ease-out).
  - Cierre: 120 ms.
  - Expansión del popup a la app: spring (rigidez ~300, amortiguación ~30) que anima tamaño, posición y radio a la vez.
  - Respeta la preferencia de "reducir animaciones" de Windows (`SPI_GETCLIENTAREAANIMATION`).
- **Semana:** empieza en lunes. Iniciales en español: L M X J V S D. El locale por defecto es es-CO.
- Cada vista nueva debe verificarse con `--render-snapshot` antes de darla por terminada.

## Modelo de datos: evento o tarea

- **Evento:** tiene inicio y fin y ocupa tiempo. Se sincroniza con Google Calendar API v3.
- **Tarea:** tiene fecha límite opcional y hora opcional, y se completa con un check. Se sincroniza con Google Tasks API v1.
- Reglas del parser:
  - Si el texto trae una hora, se crea un **evento** de 60 min por defecto.
  - Si no trae hora, se crea una **tarea**.
  - El prefijo `t:` o `!` fuerza que sea tarea. El prefijo `e:` fuerza que sea evento.
- La caché local es la fuente de verdad para la interfaz. La sincronización ocurre en segundo plano con `syncToken` en eventos y `updatedMin` en tareas. En conflictos gana el cambio más reciente, y cada conflicto se registra en el log.

## Parser de lenguaje natural

- Se implementa como módulo propio en `src/nlp/`. No depende de la interfaz y se prueba exhaustivamente con Catch2.
- Idiomas: español (principal) e inglés.
- Resultado: `ParsedInput { kind, title, start, end, allDay, durationMin, recurrence?, spans[] }`. Los `spans` sirven para resaltar en el input qué tokens se reconocieron.
- Casos que deben pasar como mínimo:
  - `mañana 5pm dentista`
  - `hoy 17:00 dentista`
  - `dentista el viernes a las 3 de la tarde por 2h`
  - `pasado mañana 9 reunión con Ana`
  - `el 25 almuerzo`
  - `comprar leche` (tarea sin fecha)
  - `t: pagar luz el lunes`
  - `gym cada lunes 7am`
- Ambigüedad de horas: "a las 3" sin am/pm se interpreta entre 8:00 y 20:00. Si la hora ya pasó hoy y no se indicó fecha, se usa la próxima ocurrencia.

## Estructura de carpetas

```
src/
  app/        entrada, bucle de mensajes, hotkey, bandeja, monitores
  ui/         ventanas, render D2D, componentes, animación, vistas (popup, expanded)
  nlp/        parser (sin dependencias de UI)
  data/       SQLite, modelos, repositorios
  sync/       OAuth, cliente Google Calendar/Tasks, cola de sincronización
  core/       fechas, logging, config, utilidades
tests/        Catch2
assets/       iconos, manifiesto
docs/         decisiones de arquitectura (ADR) si hacen falta
```

## Convenciones

- El código, los identificadores y los comentarios van en **inglés**. README, CHANGELOG y los textos de la interfaz van en **español**.
- Se usa RAII para todo recurso Win32 y COM: `wil` o `Microsoft::WRL::ComPtr`. Nunca uses `new` o `delete` directos.
- No lances excepciones a través de callbacks Win32. Los errores se manejan con `std::expected` o HRESULT comprobado.
- Nada bloquea el hilo de la interfaz. La red y SQLite pesado se ejecutan en un hilo de trabajo y se comunican con la interfaz mediante `PostMessage`.
- Cada commit lleva un mensaje en formato Conventional Commits (`feat:`, `fix:`, `docs:`...).

## Documentación viva (OBLIGATORIO)

- **CHANGELOG.md**: sigue el formato *Keep a Changelog* con SemVer. Cada fase termina añadiendo su entrada bajo `[Unreleased]`, o cerrando una versión (`0.N.0`) cuando el usuario apruebe la fase.
- **README.md**: incluye qué es, capturas (generadas con `--render-snapshot` en `docs/img/`), requisitos, cómo compilar, cómo configurar Google (credenciales), atajos y sintaxis del lenguaje natural. Actualízalo cuando algo de eso cambie.
- Si una decisión se desvía de este archivo, actualiza CLAUDE.md en el mismo cambio y dilo en el resumen.

## Seguridad y límites

- **NUNCA** hagas commit de `client_secret`, tokens ni `config.local.json`. Van en `.gitignore`. El usuario crea el proyecto en Google Cloud; tú solo documentas los pasos.
- **Detente y pregunta antes de:**
  - borrar archivos;
  - añadir dependencias;
  - escribir en el registro (por ejemplo, arranque con Windows);
  - cambiar el esquema de SQLite después de la fase 4;
  - lanzar el navegador para OAuth;
  - hacer cualquier cosa fuera de la carpeta del repo.
- Solo haz lo que pide la fase actual. No añadas funcionalidades, abstracciones ni refactors no solicitados.

## Protocolo de trabajo por fases (aplica a TODA sesión)

- Cada sesión trabaja **una sola fase**, la que indique el usuario. No adelantes trabajo de fases siguientes.
- Tras cada paso relevante escribe: ✅ [qué se completó].
- Si un build o test falla 3 veces seguidas por la misma causa, detente y explica el problema en vez de seguir intentando.
- Al terminar una fase:
  1. Compila en Debug y Release sin warnings nuevos (/W4).
  2. Ejecuta `ctest`.
  3. Actualiza CHANGELOG.md en `[Unreleased]` y README.md si aplica.
  4. Haz commit con Conventional Commits.
  5. Da un resumen de 10 líneas máximo con lo hecho, lo pendiente y los riesgos, y **ESPERA la aprobación** del usuario.

## Comandos

```
cmake --preset debug && cmake --build --preset debug
ctest --preset debug
build\debug\Agenda.exe --monitor=3
build\debug\Agenda.exe --render-snapshot=popup --out=docs\img\popup.png
```
