# Changelog

Todos los cambios relevantes de Agenda se documentan aquí.

El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el versionado
sigue [SemVer](https://semver.org/lang/es/).

## [Unreleased]

### Añadido

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
