# Changelog

Todos los cambios relevantes de Agenda se documentan aquí.

El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el versionado
sigue [SemVer](https://semver.org/lang/es/).

## [Unreleased]

### Añadido

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
- `wWinMain` con manifiesto DPI Per-Monitor v2 e instancia única por mutex con nombre. De
  momento solo registra el monitor resuelto y su rectángulo de trabajo, y termina.
- Pruebas de Catch2 para el parseo de argumentos.
- README.md, CHANGELOG.md, `.gitignore` (build, secretos y `config.local.json`) y
  `.editorconfig`.
