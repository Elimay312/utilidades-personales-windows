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

- **Cuando el puesto completo está montado** —tres pantallas— el usuario trabaja en el
  **monitor 1** y orquesta desde el **2**: no abras, muevas ni captures ventanas ahí, y
  manda todo al **monitor 3**, la app, las ventanas de prueba y las capturas.
- **Cuando no está montado** —de viaje, una sola pantalla— `\\.\DISPLAY3` no existe y el
  monitor 3 deja de ser una opción. Entonces **se usa la pantalla que haya**, con
  `--monitor=N` o `AGENDA_DEV_MONITOR=N` apuntando a una que exista. Lo que no cambia es
  la razón de la regla: no dejar ventanas encima de lo que el usuario está mirando. Cierra
  lo que abras y **dilo en el resumen**, para que se sepa que esa comprobación se hizo en
  la pantalla del usuario y no en la de desarrollo.
- Comprueba cuál hay antes de lanzar nada, en vez de dar el 3 por hecho. Una fase no se
  queda sin verificar por no tener el monitor de siempre: el criterio de aceptación se
  prueba con la aplicación delante, en la pantalla que haya.
- Resuelve el monitor por nombre de dispositivo `\\.\DISPLAYN`, usando `EnumDisplayMonitors` con `GetMonitorInfoW` y `MONITORINFOEXW::szDevice`. No uses el índice de enumeración, porque no coincide con el número que muestra Windows.
- La app acepta `--monitor=N` y la variable de entorno `AGENDA_DEV_MONITOR`. En builds Debug el valor por defecto es 3. Si el monitor no existe, la app registra una advertencia y **no se abre**. No hay fallback silencioso a otro monitor.
- Para verificar el **diseño** no hagas capturas del escritorio: usa `Agenda.exe --render-snapshot=<vista> --out=shot.png`, que renderiza la vista fuera de pantalla a PNG (con Direct2D sobre un bitmap WIC), y revisa ese PNG.
- Para verificar el **escalado y el movimiento**, la captura no sirve —sale siempre a 96 ppp y al tamaño base—, así que ahí sí se captura el rectángulo de la ventana de Agenda, y solo ese, con la aplicación abierta en una pantalla escalada.
- El flujo OAuth abre el navegador por defecto, y Windows decide en qué monitor. Antes de lanzarlo, **avisa al usuario y espera su confirmación**.

## Stack fijado (no cambiar sin preguntar)

| Área | Decisión |
|---|---|
| Lenguaje | C++20, MSVC (Visual Studio 2022), solo x64 |
| Build | CMake ≥ 3.28 con presets y vcpkg en modo manifest |
| Ventanas | Win32 puro (`RegisterClassExW` y `CreateWindowExW`) |
| Render | Direct2D 1.1, DirectWrite y DirectComposition |
| Fondo | Acrylic o Mica mediante `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)`, esquinas con `DWMWA_WINDOW_CORNER_PREFERENCE`; fallback a color sólido en Windows 10 |
| Animación | 160 ms y ease-out para todo lo que entra: el panel al abrirse, el mes al deslizarse, la tarjeta nueva al subir a la lista y la línea que tacha una tarea terminada. Un solo vocabulario de movimiento, no uno por cosa que se mueve. Transiciones simples (abrir, cerrar, fundidos) con animaciones de DirectComposition (`IDCompositionAnimation`, tramos cúbicos que interpola la GPU). El motor de springs está en `src/ui/spring.h` (Euler semi-implícito, cuatro subpasos por fotograma) y corre sobre `FrameClock` (`src/ui/vsync.h`), un hilo que espera a `DwmFlush` y avisa una vez por fotograma compuesto. Solo lo usa la expansión popup↔app. Los estados de hover y foco y el deslizamiento del mes viven dentro del contenido Direct2D, que DirectComposition no puede animar por sí solo, así que los interpola un temporizador de 16 ms que solo corre mientras algo se mueve |
| HTTP | WinHTTP (nativo, sin dependencias) |
| JSON | nlohmann-json (vcpkg) |
| Almacenamiento | SQLite3 (vcpkg; donde no hay vcpkg, la amalgamación por FetchContent con su hash fijado), en `%LOCALAPPDATA%\Agenda\agenda.db`, en modo WAL |
| Secretos | Refresh token cifrado con `CryptProtectData` (DPAPI) |
| Tests | Catch2 v3 (vcpkg) |
| DPI | Per-Monitor v2 en el manifiesto; todo el layout en DIPs. El popup lee el DPI del **monitor** (`MonitorDpi` en `layout.h`), no el de la ventana: Windows no le manda `WM_DPICHANGED` cuando cambia la escala de su propio monitor (medido en la fase 7) |
| Notificaciones | Toast de Windows por WinRT con WRL (`runtimeobject`, del SDK), escenario *reminder*; necesita el acceso del menú Inicio con el AUMID `Agenda.Desktop` (`src/core/aumid.h`) que crea el instalador. Si la [Isla](../isla/README.md) corre, el recordatorio va antes a su buzón (`\\.\pipe\IslaDinamica.avisos`, protocolo en `isla/SEGURIDAD.md` §3.7, cliente en `src/app/isla.*`) y el toast queda de respaldo |
| Accesibilidad | UI Automation (`uiautomationcore`) con un proveedor genérico sobre una lista plana de nodos (`src/ui/accessibility.*`) |
| Instalación | Instalador nativo propio (`Instalar-Agenda.exe`, `src/installer/`), por usuario y sin administrador; ni MSIX ni WiX (decisión de la fase 7) |

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
- **Colores (tema claro, derivado del oscuro):** panel `#F4F4F7` al 85 %, superficie `#FFFFFF`, texto primario `#1B1C21`, secundario `#6C6D75`, borde negro al 10 %. El acento baja a `#2F6FE0`, porque el número del día va en blanco sobre el círculo y `#4A8BF5` no da contraste suficiente sobre un panel claro. Por defecto la app sigue el tema del sistema (`AppsUseLightTheme`, solo lectura); desde la fase 7 la configuración puede fijar oscuro o claro.
- **Alto contraste:** con un tema de contraste de Windows, todos los colores salen de `GetSysColor` (`HighContrastTheme`), nada es translúcido y lo que se distinguía por un tinte lleva borde. Manda sobre cualquier preferencia.
- **Formas:** radio de 14 px en el panel, 8 px en las tarjetas de evento, cápsula completa en el input y círculo en el día de hoy.
- **Espaciado:** rejilla de 4 px y 16 px de padding interno.
- **Tamaño del panel:** 340×420 DIP es el tamaño en el que está escrito el diseño, no un tamaño fijo. En ejecución el alto es el 42 % del área de trabajo del monitor, recortado entre 380 y 560 DIP, y el ancho sale de la proporción 340:420. **Todo escala uno a uno**: letras, círculos, tarjetas y espacios. Un panel más grande tiene que significar contenido más grande, nunca el mismo contenido flotando en más panel vacío. Las medidas se calculan una vez en `MakeLayout` y las leen igual el dibujo y la detección de clics.
- **Tarjeta de evento:** barra de color de 3 px a la izquierda, hora en texto secundario y título en primario.
- **Movimiento:**
  - Apertura: fade de 0 a 1 y desplazamiento de 8 px hacia arriba en 160 ms (ease-out).
  - Cierre: 120 ms. Antes de fundirse se quitan el material y el borde de DWM y el panel se pinta opaco: DWM los dibuja fuera del visual que se funde y, puestos, quedaban como un fantasma gris.
  - Expansión del popup a la app: spring (rigidez ~300, amortiguación ~30) que anima tamaño, posición y radio a la vez. Medido a 125 %: llega al tamaño final en ~320 ms y se asienta en ~520 ms.
- **La app expandida (fase 6):**
  - Ocupa el 80 % del área de trabajo, centrada. Tamaño de diseño: 1536×826 DIP.
  - **La barra lateral mide lo mismo que el popup y empieza en su esquina**, así que la cabecera, las iniciales y la rejilla del popup son el mini mes de la app: no se mueven durante la expansión. Debajo, en el hueco de la lista del día, van los calendarios y la bandeja de tareas sin fecha.
  - Todo escala con el `type` del popup: las letras de la app son las del popup (11/12/13/15).
  - Radio de la ventana: 8 DIP (`kRadiusApp`), el de cualquier ventana de Windows 11. Bloques de la línea de tiempo y fichas de día entero: 8 DIP, como las tarjetas, recortado a la mitad de su alto.
  - Línea de tiempo: 48 DIP por hora, margen de horas de 56, ajuste a 15 min.
  - Línea de ahora: `#FF5A5F` en oscuro, `#E0393E` en claro (token `now`). Fuerte en la columna de hoy y al 35 % en las demás.
  - Bloques: el color del calendario mezclado con la superficie (26 % en oscuro, 16 % en claro) y la barra de 3 px a la izquierda.
  - La cápsula viaja primero a la derecha y luego arriba (en línea recta cruzaría la rejilla). La lista del día se va en el primer 30 % del camino y la app entra entre el 30 y el 85 %.
  - Panel de detalle: 320 DIP a la derecha, superficie de tarjeta con el radio del panel (14), campos de 32 DIP con fondo `panelOpaque` y radio 8; foco en acento, error en el rojo de `now`. Entra con los 160 ms de siempre y no con el muelle: es algo que entra, no la ventana que cambia de tamaño.
  - Arrastrar: ajuste a 15 min, 4 DIP de temblor siguen siendo un clic, y los 6 DIP de abajo de un bloque lo estiran. El fantasma del arrastre es el bloque con el contorno de la selección.
  - Respeta la preferencia de "reducir animaciones" de Windows (`SPI_GETCLIENTAREAANIMATION`).
- **Semana:** empieza en lunes. Iniciales en español: L M X J V S D; en inglés, M T W T F S S. El locale por defecto es es-CO.
- **Configuración (fase 7):** ventana normal con barra de título del color de `panelOpaque`, 560 DIP de ancho, tarjetas de 64 DIP con radio 8 sobre el panel, secciones en 15 semibold, controles de 32 DIP a la derecha: el segmentado es la cápsula de las pestañas de la app, el selector es el del panel de detalle, el interruptor mide 44×22 y su bola viaja en 160 ms.
- **Foco de teclado:** anillo de 2 DIP en `textPrimary`, 3 DIP por fuera de lo enfocado, y solo después de usar el teclado (un clic lo quita), como en Windows.
- Cada vista nueva debe verificarse con `--render-snapshot` antes de darla por terminada. El PNG se renderiza siempre a 96 ppp y al tamaño base, así que **no puede pillar errores de DPI ni de escalado**: eso hay que mirarlo con la app delante en un monitor escalado. `--panel=WxH` fuerza un tamaño de panel para poder juzgarlo en cualquier pantalla.

## Modelo de datos: evento o tarea

- **Evento:** tiene inicio y fin y ocupa tiempo. Se sincroniza con Google Calendar API v3.
- **Tarea:** tiene fecha límite opcional y hora opcional, y se completa con un check. Se sincroniza con Google Tasks API v1.
- **Las horas se guardan como reloj de pared local**, o sea un día (`YYYY-MM-DD`) y un minuto de ese día, nunca como instante UTC. Es lo que ya lleva el código (`nlp::DateTime`), es la única pregunta que hace la interfaz —«¿qué hay el día D?»— y es lo que guarda Google Calendar, que manda `dateTime` con su `timeZone`. Guardar además el UTC serían dos conversiones por consulta y dos ideas de qué día es hoy dentro de la misma caché. `updated_at` sí es un instante UTC, porque es metadato de conflicto y no una hora de la agenda.
- **La lista del día se lee como la forma del día:** primero lo de día entero, después todo lo que tiene hora, y al final las tareas sin hora. El final es lo que importa: el popup enseña dos tarjetas, y con las tareas sin hora delante, tres pendientes echarían del panel la reunión de hoy.
- **Una tarea sin fecha aparece en el día de hoy.** No se le inventa una fecha —se guarda con `due_day` nulo— pero se enseña ahí, porque algo que se crea y no se ve en ninguna parte es peor que no haberlo creado.
- **Una regla de repetición se despliega** (fase 6) con `OccursOn` en `src/core/recurrence.h`: FREQ DAILY/WEEKLY/MONTHLY/YEARLY con INTERVAL, BYDAY de días sueltos, COUNT y UNTIL. Desde la fase 8.2 también BYDAY con ordinal dentro del mes (`1TU`, `-1FR`), BYMONTHDAY (negativo cuenta desde el final; con BYDAY a la vez es la intersección) y BYMONTH. Lo que no entiende (BYSETPOS, ordinales anuales sin BYMONTH) se queda en su primer día. Desde la fase 8.1 la columna `recurrence` guarda la lista entera de Google, una línea por línea, y `OccursOn` salta los días de sus EXDATE.
- **Una ocurrencia que Google aparta de su serie** (movida, editada o cancelada en la web) es una fila propia con `series_id` (el id **en Google** de la serie, no nuestro uid, porque es lo que llega y la serie puede venir en otra página) y `original_day`. Una cancelada se guarda como lápida (`deleted_at`) y no se borra; al final de cada pasada se barren las lápidas cuya serie ya no está (al borrar una serie, Google manda después sus ocurrencias apartadas como canceladas). Los tres sitios que despliegan una serie (`ItemsForDay`, `DueReminders`, `DotsForRange`) se saltan los días apartados; la serie se identifica como `COALESCE(remote_id, uid sin guiones)`, que es el id con el que sube (`EventIdFor`).
- Reglas del parser:
  - Si el texto trae una hora, se crea un **evento** de 60 min por defecto.
  - Si no trae hora, se crea una **tarea**.
  - El prefijo `t:` o `!` fuerza que sea tarea. El prefijo `e:` fuerza que sea evento.
  - El prefijo `?` no se parsea: es una búsqueda (fase 10, `Store::Search`, `Searching` en `popup_view.h`). Ctrl+F lo pone. Los resultados se apilan junto a la tarjeta de la vista previa, sobre un fondo opaco: hacia arriba en el popup (hasta 4, por encima del mes) y hacia abajo en la app (hasta 6). El plegado sin acentos es uno solo, `core/text.h`, y lo usan el parser y la búsqueda.
- La caché local es la fuente de verdad para la interfaz. La sincronización ocurre en segundo plano con `syncToken` en eventos y `updatedMin` en tareas. En conflictos gana el cambio más reciente, y cada conflicto se registra en el log.
- **`calendars.is_primary` significa «aquí cae lo que se crea»**, no «es el primary de Google». Se siembra con el primary en la primera conexión y a partir de ahí la mueve el submenú de la bandeja. Es la desviación que evitó inventar un almacén de ajustes para una elección que se hace una vez.
- **La sincronización corre en su propio hilo, no en la cola del `Store`**, aunque `store.h` diera eso por hecho en la fase 4. Esa cola lleva también las escrituras del popup, y una petición de veinte segundos por delante dejaría una creación sin escribir veinte segundos. Lo que sí pasa por el `Store` es cada escritura en SQLite, con `Store::Run`: una conexión y un escritor. Dos conexiones habrían sido peor, porque en SQLite las transacciones son de la conexión y no del hilo.
- **El esquema se quedó en v1 en la fase 5.** Todo lo que hacía falta ya estaba reservado; el token es lo único que no cabía en una tabla y va a un archivo cifrado con DPAPI.
- **El esquema pasó a v5 en la fase 12.1**, con permiso y sin reconstruir tablas: la tabla `accounts` (id, email, `token_file`) y `calendars.account_id` y `calendars.remote_id`. La cuenta que ya estaba es la 1 y conserva `token.bin`; las siguientes usan `token-<id>.bin` (`GoogleAuth` recibe el nombre). El email es el id del calendario principal, que escribe la pasada. `remote_id` es para un calendario compartido en dos cuentas (fase 12.2): NULL significa que la clave ES el id de Google. `Migrate(db, upTo)` para en una versión anterior, que es como los tests construyen una caché antigua (SQLite no deja borrar una columna con clave foránea).
- **El esquema pasó a v4 en la fase 8.1**, con permiso y solo con columnas: `events.series_id` y `events.original_day`, con su índice. La migración vacía los `syncToken` para bajar las excepciones que ya existían.
- **El esquema pasó a v3 en la fase 7**, con permiso y solo con columnas: `events.reminders` (minutos antes separados por comas; NULL es «los del calendario», que es el `useDefault` de Google; cadena vacía es ninguno) y `calendars.reminders` (sus `defaultReminders`). Solo los recordatorios de tipo notificación. La migración vacía los `syncToken`. En cada pasada los de Google ganan siempre, como el etag, salvo que haya un cambio de recordatorio de Agenda en la cola (`+reminders`).
- **Los recordatorios se editan desde la fase 11**, en el panel de detalle: cinco píldoras (*Auto* = los del calendario, *Ninguno*, 10 min, 1 h, 1 día; `ReminderOf`/`RemindersFor` en `model.h`) y una lista distinta hecha en Google se enseña como personalizada y se conserva. Los de correo se guardan en la misma columna con una `m` delante (`m1440`), que las notificaciones ignoran, para que un cambio hecho aquí los devuelva a Google tal cual. Solo se suben con el bit `kEditReminders`.
- **El esquema pasó a v2 en la fase 6**, con permiso del usuario y solo añadiendo columnas: `calendars.hidden` (el interruptor de la barra lateral; `visible` significa «Google todavía lo lista» y cada pasada lo reescribe), `events.location` y `events.moved_from` (el calendario de origen de un evento que se cambió de calendario, para el `POST .../move` de Google).
- **La cola dice qué cambió.** Una operación de edición de evento es `update`, `update+location`, `update+recurrence` o las dos: el PATCH solo manda la ubicación y la repetición cuando se editaron (la RRULE sin sus EXDATE, reenviada con cada movimiento, devolvería repeticiones borradas en la web).
- **En `pending_ops` la nueva operación entra antes de que salgan las que sustituye.** El id es un rowid sin AUTOINCREMENT y borrar primero reutiliza el número; una pasada con la vieja en vuelo borraría la nueva al terminar.
- **Borrar desde la app es diferido**: se oculta al momento y se borra al irse el aviso de deshacer (o al ocultar la ventana). Si la app se cierra antes, no se borra, que es el lado seguro.
- **Una repetición pregunta «Solo este / Toda la serie»** (fase 8.3) al arrastrarla, al editar un campo del panel y al borrarla, en la cápsula de abajo de «¿Borrar...?» con los dos botones. *Solo este* crea la fila apartada (`Store::DetachOccurrence`, `RemoveOccurrence`) y sube un PATCH o un DELETE al id de la ocurrencia en Google, que se calcula (`InstanceIdFor`: id de la serie, `_`, y el día o el instante UTC de su inicio) en vez de pedirlo. *Toda la serie* se recuerda mientras el panel siga abierto. Calendario y repetición van siempre a la serie. `DayItem::occurrence` dice qué día de la serie es cada tarjeta, y el panel abierto sobre una repetición enseña las fechas de ese día. Un evento que dura varios días sigue sin arrastrarse.
- **La app expandida no se cierra al perder el foco**, a diferencia del popup: deja de estar siempre encima y se queda detrás como cualquier ventana. Sigue siendo `WS_EX_TOOLWINDOW`, sin botón en la barra de tareas; la trae al frente la bandeja, y el atajo la cierra.

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
  - Desde la fase 9: `25 de octubre`, `October 25`, `25/10/2027` (día y mes), `en 2 semanas`, `fin de mes`, `este fin de semana`, `3-5pm`, `a las 3 hasta las 5`, `de 3 a 5 de la tarde`, `por 1h30`, `por una hora y media`, `17h30`.
- Ambigüedad de horas: "a las 3" o "4:05" sin am/pm (y sin cero delante) pueden ser de mañana o de tarde. Hoy vale la mitad que todavía no ha pasado. Si las dos siguen por delante, o el día es otro, se toma la que cae entre 8:00 y 20:00 y `ParsedInput::otherMinute` lleva la otra: la vista previa pregunta a. m. / p. m. (clic o ↑↓). Si las dos pasaron y no se indicó fecha, mañana. (Cambiado por el usuario tras la 1.0.0: "4:05" se leía como 24 h y caía de madrugada.)
- Un evento escrito cuando ya pasaron todos sus recordatorios avisa una vez al empezar (`Store::DueReminders`, con `updated_at`).

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

- El código, los identificadores y los comentarios van en **inglés**. README y CHANGELOG van en **español**. Desde la fase 7 la interfaz es **bilingüe**: cada texto se escribe con sus dos versiones juntas donde se dibuja, `T(L"español", L"English")` (`src/core/i18n.h`), y el español es el idioma por defecto.
- Se usa RAII para todo recurso Win32 y COM: `wil` o `Microsoft::WRL::ComPtr`. Nunca uses `new` o `delete` directos.
- No lances excepciones a través de callbacks Win32. Los errores se manejan con HRESULT comprobado o con un valor de retorno que haya que mirar. **`std::expected` es de C++23 y el proyecto es C++20**, así que `src/data/` devuelve `bool` y deja el motivo en `Db::error()` y en el log; si algún día se sube el estándar, ese es el sitio por donde empezar.
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
  - escribir en el registro (por ejemplo, arranque con Windows). Aprobado en la fase 7 y solo eso: el valor `Agenda` de `HKCU\...\Run` (interruptor de la configuración e instalador) y la clave `HKCU\...\Uninstall\Agenda` del instalador;
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
build\debug\Agenda.exe --render-snapshot=popup-creado --out=docs\img\popup-creado.png
build\debug\Agenda.exe --render-snapshot=popup-sin-conexion --out=docs\img\popup-sin-conexion.png
build\debug\Agenda.exe --render-snapshot=app-dia --theme=dark --out=docs\img\app-dia.png
build\debug\Agenda.exe --render-snapshot=app-semana --theme=light --out=docs\img\app-semana-claro.png
build\debug\Agenda.exe --render-snapshot=app-mes --out=docs\img\app-mes.png
build\debug\Agenda.exe --render-snapshot=app-transicion --out=docs\img\app-transicion.png
build\debug\Agenda.exe --render-snapshot=app-detalle --out=docs\img\app-detalle.png
build\debug\Agenda.exe --render-snapshot=app-arrastre --out=docs\img\app-arrastre.png
build\debug\Agenda.exe --render-snapshot=app-borrar --out=docs\img\app-borrar.png
build\debug\Agenda.exe --render-snapshot=app-repeticion --out=docs\img\app-repeticion.png
build\debug\Agenda.exe --render-snapshot=popup-buscar --out=docs\img\popup-buscar.png
build\debug\Agenda.exe --render-snapshot=app-buscar --out=docs\img\app-buscar.png
build\debug\Agenda.exe --render-snapshot=configuracion --theme=light --out=docs\img\configuracion-claro.png
build\debug\Agenda.exe --render-snapshot=popup --theme=contrast --out=docs\img\popup-contraste.png
powershell -NoProfile -ExecutionPolicy Bypass -File empaquetar.ps1   # build\release\Instalar-Agenda.exe
```
