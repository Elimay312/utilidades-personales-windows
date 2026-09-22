# Changelog

Todos los cambios relevantes de Agenda se documentan aquí.

El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el versionado
sigue [SemVer](https://semver.org/lang/es/).

## [Unreleased]

### Añadido

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

### Cambiado

- Las transiciones simples (abrir, cerrar, fundidos) usan animaciones de DirectComposition, que
  interpola la GPU, en lugar del motor propio de springs sobre temporizador vsync. Ese motor se
  escribirá en la fase 6, que es la que lo necesita para la expansión del popup a la app.
  CLAUDE.md queda actualizado.
