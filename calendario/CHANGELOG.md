# Changelog

Todos los cambios relevantes de Agenda se documentan aquí.

El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el versionado
sigue [SemVer](https://semver.org/lang/es/).

## [Unreleased]

### Añadido

- Fase 4: **Enter crea, y lo creado sobrevive a cerrar la aplicación.** Todo va a SQLite, en
  `%LOCALAPPDATA%\Agenda\agenda.db`, en modo WAL. El esquema tiene cinco tablas —`calendars`,
  `events`, `tasks`, `sync_state` y `pending_ops`— con migraciones versionadas en
  `PRAGMA user_version`, que es transaccional: si una migración se cae por la mitad, el número
  no sube y el siguiente arranque vuelve a intentarlo desde el mismo sitio. Una caché escrita
  por una versión más nueva no se toca y da error, en vez de convertirse hacia atrás a ciegas.
- Fase 4: `src/data/` como biblioteca estática (`agenda_data`), por el mismo motivo que
  `agenda_nlp`: aquí dentro no entra ni Direct2D ni una ventana, así que los tests la prueban
  directa contra una base en memoria. `db.{h,cpp}` envuelve SQLite en lo justo —un handle que
  se cierra solo, una sentencia que se finaliza sola y la conversión UTF-8 ↔ `wstring` en un
  único sitio—, `schema.{h,cpp}` lleva las migraciones y `store.{h,cpp}` el repositorio.
- Fase 4: **las lecturas van en el hilo de interfaz y las escrituras en uno de trabajo.** El
  popup tiene que estar en pantalla en menos de 100 ms y sin spinners, y las dos consultas que
  lo llenan son un índice y unas pocas filas. Las escrituras se encolan; el trabajador avisa
  con un `WM_APP+2` **sin carga** y la interfaz relee, porque un puntero reservado en un hilo
  habría que liberarlo en el otro.
- Fase 4: **la interfaz es optimista.** El `uid` lo genera la ventana antes de encolar nada,
  así que la tarjeta está en pantalla desde el primer fotograma y nunca hay que esperar al
  `rowid`. Si el trabajador falla, devuelve el `uid`, la fila se quita y se dice por qué. Cada
  escritura y su fila de `pending_ops` van **en la misma transacción**: una creación guardada
  sin su operación sería un evento que jamás llega a Google, sin un solo error por ningún lado.
- Fase 4: **Ctrl+Z deshace durante cinco segundos**, mientras el aviso «Creado · Deshacer» está
  puesto, y **devuelve la frase al campo de texto** —deshacer un error de tecleo solo sirve si
  el error vuelve para corregirlo—. La oferta termina con el aviso, que es la única forma de
  que las dos cosas no puedan discrepar. Nada se ha enviado todavía, así que deshacer borra la
  fila y su operación juntas y no deja lápida.
- Fase 4: **las tareas llevan casilla y se tachan con animación.** El tachado es un rectángulo
  de 1 DIP que crece, y no `IDWriteTextLayout::SetStrikethrough`, que es todo o nada y por eso
  no se puede animar. La casilla se dibuja con dos trazos, como el chevron, para no depender
  de un glifo. La tarea marcada se queda en la lista: tachada, no desaparecida.
- Fase 4: la lista del día mezcla eventos y tareas y los ordena como la forma del día —día
  entero, luego la hora, y las tareas sin hora al final—; los puntos del mes salen de datos
  reales y un evento de varios días los pone en todos ellos. Una tarea **sin fecha** se guarda
  con `due_day` nulo y se enseña en el día de hoy.
- Fase 4: vista de captura nueva, `--render-snapshot=popup-creado`: el panel en el instante
  siguiente a Enter, con el aviso puesto y la tarjeta a medio subir. Ese momento dura ciento
  sesenta milisegundos y no había otra forma de mirarlo con calma.
- Fase 4: `tests/test_store.cpp`, con la mitad del criterio de aceptación que no necesita
  ventana: crear, soltar el archivo como lo suelta cerrar la aplicación, volver a abrirlo y
  encontrarlo. Más las migraciones, la caché de versión superior, el evento de dos días, el
  orden del día, la tarea sin fecha, marcar y desmarcar, y que deshacer se lleve la operación
  encolada.

### Cambiado

- Fase 4: la **regla de monitores** de `CLAUDE.md` daba por hecho el puesto de tres pantallas y
  dejaba una fase sin verificar cuando no está montado. Ahora dice las dos situaciones: con las
  tres, todo al monitor 3 como siempre; con una sola, se usa la que haya y se dice en el
  resumen. La razón de la regla no cambia —no dejar ventanas encima de lo que el usuario mira—
  pero ya no impide probar el criterio de aceptación con la aplicación delante.
- Fase 4: **el tiempo se guarda como reloj de pared local** —un día y un minuto de ese día—
  y no como instante UTC. Es lo que ya llevaba el código, es la única pregunta que hace la
  interfaz y es lo que guarda Google Calendar. `updated_at` es la excepción y sí es UTC,
  porque es metadato de conflicto y no una hora de la agenda.
- Fase 4: `src/ui/sample_data.h` deja de ser lo que pinta la aplicación y pasa a ser lo que
  pinta **solo la captura**. `--render-snapshot` ya congelaba el día y la hora para que el PNG
  cambiara cuando cambia el diseño y no cuando pasa el tiempo; enchufarlo a la base del usuario
  habría hecho que la herramienta con la que se revisa el diseño dependiera de lo que alguien
  escribiera esa mañana.
- Fase 4: el aviso y la vista previa comparten rectángulo y **la lista les cede una tarjeta**
  mientras uno de los dos está puesto. La primera versión dejaba el aviso encima de la tarjeta
  recién creada, que es justo la que había que mirar; se vio en la captura de `popup-creado`.
- Fase 4: una regla de repetición **se guarda y no se despliega**. El RRULE va a su columna
  —es literalmente lo que quiere Google en la fase 5— y el evento sale en su primer día. El
  motor de ocurrencias es de la fase 6, que es la que tiene vistas donde se note.
- Fase 4: `CLAUDE.md` pedía `std::expected`, que es de C++23, y el proyecto es C++20. `src/data/`
  devuelve `bool` y deja el motivo en `Db::error()` y en el log. Anotado allí.

### Corregido

- Fase 4: **algo creado podía seguir sin verse.** Una tarea sin fecha escrita mientras se
  miraba otro día se guardaba en el día de hoy, pero el popup se quedaba donde estaba: el
  aviso decía «Creado» encima de un día donde no había aparecido nada. Ahora el popup salta
  siempre al día donde cayó, y para una tarea sin fecha ese día es hoy. Encontrado usando la
  aplicación, no leyendo el código.
- Fase 4: la vista previa prometía «Dentista» y la tarjeta creaba «dentista». `nlp::Capitalised`
  sale del anónimo y la usan las dos, porque una vista previa que no dice exactamente lo que
  va a pasar deja de merecer que se lea.
- Fase 4: la descarga de la amalgamación de SQLite moría con «SSL certificate verification
  failed» mientras los clones de git funcionaban, así que parecía un problema de red y no lo
  era: el `cmake` del PATH no trae almacén de certificados. `CMakeLists.txt` le pasa el de Git
  para Windows por `CMAKE_TLS_CAINFO`, que FetchContent reenvía al sub-build, y lo normaliza a
  barras hacia delante porque `%ProgramFiles%` trae `C:\Program Files` y ese `\P` aborta el
  script generado.

### Añadido

- Fase 3: parser de lenguaje natural en `src/nlp/`, compilado como biblioteca estática
  (`agenda_nlp`) sin nada de interfaz dentro. Entiende español e inglés a la vez, sin detectar
  idioma: fechas relativas (`hoy`, `mañana`, `pasado mañana`, días de la semana, `próximo
  lunes`, `el 25`, `en 3 días`), horas (`5pm`, `17:00`, `17h`, `a las 5`, `5 de la tarde`,
  `mediodía`), duraciones (`por 2h`, `30 min`, `de 3 a 5`), recurrencia básica traducida a
  RRULE (`cada lunes`, `todos los días`) y los prefijos `t:`, `!` y `e:`. El «ahora» entra
  como parámetro, así que los tests no dependen del reloj de la máquina.
- Fase 3: las reglas de ambigüedad de CLAUDE.md, tal cual. Una hora sin `am`/`pm` cae entre
  las 8:00 y las 20:00; si la hora ya pasó y no se escribió fecha, se usa la del día
  siguiente, pero una fecha explícita se respeta aunque su hora quede atrás. Con hora sale un
  evento de 60 minutos, sin hora una tarea, y el prefijo manda sobre las dos cosas.
- Fase 3: 55 tests de Catch2 para el parser, con los ocho casos que CLAUDE.md nombra y los
  límites que duelen: acentos y su ausencia, mayúsculas, `25:00` y `13pm`, `el 31` en un mes
  de treinta, el 25 de diciembre saltando de año, y «mañana» escrito a las 23:59.
- Fase 3: vista previa en vivo. Los tokens reconocidos se pintan en color de acento dentro de
  la cápsula, y encima aparece una tarjeta que dice qué se va a crear —`📅 Mañana ·
  17:00–18:00 · Dentista`, `☑ Tarea · Lunes · Pagar luz` o `☑ Tarea sin fecha`—. La tarjeta se
  superpone al final de la lista de eventos mientras se escribe, en vez de reservar una fila
  fija, para que la rejilla del mes no se mueva al empezar a teclear. **Enter sigue sin hacer
  nada: todavía no se guarda.**
- Fase 3: `--text=...` precarga el campo de texto en `--render-snapshot`, que es la única
  forma de revisar la vista previa en un PNG.
- Fase 2: el panel se adapta al monitor. Su alto es el 42 % del área de trabajo, recortado
  entre 380 y 560 DIP, y el ancho sale de la proporción 340:420 del diseño. Todo lo de dentro
  escala uno a uno con él —letras, círculos de los días, tarjetas y espacios—, así que un panel
  más grande significa contenido más grande y no el mismo contenido flotando en más hueco. Las
  medidas se calculan una vez en `MakeLayout` y las leen igual el dibujo y la detección de
  clics; a 340×420 salen exactamente los números del sistema de diseño.
- Fase 2: `--panel=WxH` fuerza el tamaño del panel en DIP, en la app y en las capturas, para
  poder juzgar en una pantalla un tamaño que esa pantalla no produciría.
- Fase 2: sistema de diseño en `src/ui/theme.{h,cpp}` con los tokens de CLAUDE.md (colores,
  tipografía, radios y rejilla de 4 DIP) y un tema claro derivado. La app sigue el tema del
  sistema leyendo `AppsUseLightTheme` del registro, solo lectura, y lo relee cada vez que se
  abre el popup.
- Fase 2: el popup ya no está vacío. Título del mes en 15 semibold, fila `L M X J V S D`
  empezando en lunes, rejilla fija de seis semanas con el día de hoy en círculo de acento, el
  seleccionado en anillo y un punto de 4 DIP bajo los días con eventos, lista de eventos del
  día seleccionado y campo de texto en cápsula con el marcador `mañana 5pm dentista…`.
- Fase 2: componentes de dibujo reutilizables. `src/ui/paint.{h,cpp}` trae los formatos de
  DirectWrite (Segoe UI Variable Text con respaldo a Segoe UI), texto con elipsis, rectángulos
  redondeados y círculos; `src/ui/components.{h,cpp}` trae la rejilla del mes, la tarjeta de
  evento y el campo de texto.
- Fase 2: campo de texto editable en `src/ui/text_input.{h,cpp}`, sin Direct2D ni Win32, con
  cursor, selección, movimiento por código de punto (un emoji no se parte por la mitad) y
  filtrado de caracteres de control. La ventana le conecta `WM_CHAR`, el portapapeles
  (`Ctrl+C`, `Ctrl+X`, `Ctrl+V` y `Ctrl+A`) y el IME (`WM_IME_*`), que dibuja la composición
  subrayada en el cursor y coloca ahí la ventana de candidatos.
- Fase 2: estados de hover y foco con transición de 100 ms, y deslizamiento de 160 ms al
  cambiar de mes con las flechas `‹ ›`. Los mueve un temporizador de 16 ms que solo corre
  mientras algo está en movimiento, y respetan la opción de reducir animaciones de Windows.
- Fase 2: navegación con teclado. Con el campo vacío las cuatro flechas mueven el día; con
  texto, `←` y `→` mueven el cursor (con `Shift` seleccionan) y `↑` y `↓` siguen moviendo el
  día una semana. El día seleccionado viaja con el mes, así que la lista siempre muestra un
  día que está en pantalla.
- Fase 2: `--theme=dark|light` para ver cualquiera de los dos temas sin tocar la configuración
  de Windows, tanto en la app como en `--render-snapshot`. Las capturas fijan el 22 de
  septiembre de 2026 como «hoy» para que el PNG solo cambie cuando cambie el diseño.
- Fase 2: datos de ejemplo en memoria (`src/ui/sample_data.h`), indexados por día del mes para
  que cualquier mes se vea poblado. **No se toca SQLite todavía.**
- Fase 2: `src/core/dates.h` con la aritmética del calendario sobre `<chrono>`: rejilla que
  empieza en lunes, seis semanas fijas, meses recortados al último día que existe (31 de enero
  más un mes es 28 de febrero) y nombres de mes en español independientes del locale.
- Fase 2: pruebas de Catch2 para las fechas, el campo de texto y la geometría del panel, que es
  la que comparten el dibujo y la detección de clics.
- Fase 1: atajo global con `RegisterHotKey`, configurable con la clave `hotkey` de
  `config.json` y `Alt+Shift+C` por defecto. Si ya está ocupado, se registra el error, aparece
  un globo de aviso en la bandeja y la app sigue viva para poder abrirla desde el icono.
- Fase 1: ventana popup de 340×420 DIP (`WS_POPUP` sin bitmap de redirección) compuesta con
  DirectComposition sobre un swap chain con alfa premultiplicado, con fondo acrylic
  (`DWMWA_SYSTEMBACKDROP_TYPE`), esquinas redondeadas y panel opaco de respaldo en Windows 10.
  Se precrea oculta al arrancar y se coloca en la esquina inferior derecha del área de trabajo
  del monitor objetivo, escalada con `GetDpiForWindow`.
- Fase 1: apertura de 160 ms y cierre de 120 ms con opacidad y desplazamiento de 8 DIP,
  ajustables con `popup.openMs` y `popup.closeMs`, que respetan la opción de reducir
  animaciones de Windows. Se cierra con Esc, al perder el foco o con el propio atajo.
- Fase 1: icono en la bandeja con menú de **Abrir** y **Salir**, y `assets/agenda.ico`, que es
  también el icono del ejecutable.
- Fase 1: `--render-snapshot=popup --out=<archivo.png>`, que renderiza la vista fuera de
  pantalla con Direct2D sobre un bitmap WIC. No necesita monitor ni instancia libre, así que
  funciona con la app abierta.
- Fase 0: esqueleto del proyecto con CMake 3.28, presets `debug` y `release` para MSVC x64 y
  manifiesto de vcpkg (nlohmann-json, sqlite3 y catch2), con respaldo por `FetchContent`
  cuando no hay vcpkg en la máquina.
- `src/core`: logging a `%LOCALAPPDATA%\Agenda\logs\agenda-AAAAMMDD.log` en UTF-8 y con copia
  a la salida del depurador, y lectura de configuración que fusiona `config.json` con el
  `config.local.json` opcional.
- `src/app/monitors`: `FindMonitorByDisplayNumber(n)`, que resuelve `\\.\DISPLAYn` con
  `EnumDisplayMonitors` y `MONITORINFOEXW` en vez del índice de enumeración, y el parseo de
  `--monitor=N` y `AGENDA_DEV_MONITOR` (3 por defecto en Debug). Si el monitor no existe, se
  registra el error y el proceso sale con código 2.
- `wWinMain` con manifiesto DPI Per-Monitor v2 e instancia única por mutex con nombre.
- Pruebas de Catch2 para el parseo de argumentos, del atajo y de la geometría del popup.
- README.md, CHANGELOG.md, `.gitignore` (build, secretos y `config.local.json`) y
  `.editorconfig`.

### Corregido

- El panel se dibujaba a 96 ppp dentro de una ventana dimensionada al DPI del monitor, así que
  en una pantalla escalada ocupaba solo una esquina y el resto de la ventana se veía como un
  rectángulo gris vacío. El contexto de Direct2D no hereda el DPI del bitmap de destino, de modo
  que ahora se le fija con `SetDpi` en cada cuadro. No se notaba en el monitor de desarrollo,
  que está al 100 %, ni en las capturas, que se renderizan siempre a 96 ppp.

### Cambiado

- Las transiciones simples (abrir, cerrar, fundidos) usan animaciones de DirectComposition, que
  interpola la GPU, en lugar del motor propio de springs sobre temporizador vsync. Ese motor se
  escribirá en la fase 6, que es la que lo necesita para la expansión del popup a la app.
  CLAUDE.md queda actualizado.
