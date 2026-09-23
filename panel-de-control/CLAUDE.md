# CLAUDE.md — Panel (centro de control para Windows)

> Nombre en clave: **Panel**. Si el usuario cambia el nombre, actualiza este archivo, el README,
> CMake y `SEGURIDAD.md`.

## Qué es

Un centro de control al estilo de macOS, en C++ nativo:

1. El usuario pulsa **`Ctrl+Alt+A`** y aparece un panel abajo a la derecha, encima de la
   bandeja, en el monitor donde está el ratón.
2. El panel tiene:
   - cuatro tiles: **Wi-Fi**, **Bluetooth**, **Luz nocturna** y **Configuración**;
   - dos tarjetas con deslizador: **Brillo** y **Volumen**;
   - una fila de **utilidades** del repo, que se arrancan o se cierran con un clic.
3. Brillo y volumen se despliegan con su chevron:
   - **Brillo:** un deslizador por pantalla.
   - **Volumen:** la lista de salidas de audio.
4. **Configuración no hace nada todavía.** Algún día abrirá un menú para configurar los
   otros proyectos de la carpeta. Hasta entonces se ve pulsada y dice «Próximamente».

El plan completo, con la investigación de APIs y paquetes, está en
`docs/superpowers/plans/2026-09-23-panel-de-control.md`.

## Prioridades (en este orden)

1. **No estropear el equipo del usuario.** El panel toca ajustes de toda la sesión: radios,
   salida de audio, luz nocturna y brillo. Si algo no se entiende, se deja como estaba (ver
   `SEGURIDAD.md`).
2. **Ligero.** Escondido gasta 0 % de CPU y menos de 20 MB, y se abre en 50 ms o menos. Nada
   consulta en bucle: todo son suscripciones.
3. **Diseño.** Tiene que parecer de la misma familia que Agenda y Brújula.

## Regla de monitores (OBLIGATORIA)

- **Enumera las pantallas antes de lanzar nada.** El puesto de tres pantallas no siempre
  está montado: fuera de casa solo hay `DISPLAY1`.
- **Con las tres pantallas:** el usuario trabaja en la 1 y orquesta desde la 2. La app, las
  ventanas de prueba y las capturas van a la **3**.
- **Con una sola pantalla:** se usa la que haya. Cierra lo que abras y dilo en el resumen.
- **Cómo se elige el monitor:**
  - la app acepta `--monitor=N` y `PANEL_DEV_MONITOR=N`;
  - se resuelve por el nombre `\\.\DISPLAYN`, nunca por el índice de enumeración;
  - si el monitor no existe, la app lo registra y **no se abre**.
- **Cómo se verifica cada cosa:**
  - el diseño, con `--render-snapshot`;
  - el DPI y el movimiento, con la app delante en un monitor escalado, porque la captura
    sale siempre a 96 ppp;
  - **nunca moviendo el ratón con arrastres sintéticos:** la interacción se prueba con
    mensajes a la ventana.

## Stack fijado (no cambiar sin preguntar)

| Área | Decisión |
|---|---|
| Lenguaje | C++20, MSVC, **solo x64**. Con x86 sobre x64, `Radio::GetRadiosAsync` devuelve una lista vacía |
| Build | CMake ≥ 3.28 con presets `debug` y `release`, y dependencias por FetchContent **fijadas por `URL_HASH SHA256`**. Sin vcpkg |
| Flags | `/W4 /permissive- /utf-8 /EHsc`, CRT estático (`/MT`), `UNICODE NOMINMAX WIN32_LEAN_AND_MEAN` |
| Ventana y render | Win32 puro y D3D11 con swap chain de composición, DirectComposition y Direct2D 1.1/DirectWrite. Es el esqueleto del popup de Agenda (`calendario/src/ui/popup_window.cpp`) |
| Fondo | `DWMSBT_TRANSIENTWINDOW` (acrylic) con el panel pintado al 85 %. En esta máquina el acrylic sale plano, y es del entorno: no se rediseña la composición por eso |
| WinRT | Las cabeceras C++/WinRT **que trae el Windows SDK**, sin paquete aparte. Solo para `Windows.Devices.Radios`, `DeviceWatcher` y `NetworkInformation`. Sin corrutinas: `.get()` en el hilo de trabajo |
| JSON | nlohmann/json v3.12.0 |
| Tests | doctest v2.5.3, ejecutable `panel_tests`, registrado en ctest |
| Configuración | `%LOCALAPPDATA%\Panel\panel.json`, con `merge_patch` y guardado atómico, como en `calendario/src/core/config.cpp` |

Cualquier dependencia que no esté en esta tabla requiere **preguntar antes**.

### Una API por función

| Función | API |
|---|---|
| Volumen y silencio | Core Audio: `IAudioEndpointVolume`, más `IMMNotificationClient` en el enumerador |
| Salidas de audio | `EnumAudioEndpoints` y `IPolicyConfig::SetDefaultEndpoint` (no documentada, ver `SEGURIDAD.md`) |
| Brillo del portátil | WMI `root\WMI` con las clases `WmiMonitorBrightness*` |
| Brillo de monitores externos | DDC/CI por `dxva2`, en el hilo de trabajo y con un intervalo mínimo entre escrituras |
| Emparejar monitores | `QueryDisplayConfig` para obtener el ID de instancia, que se compara con el `InstanceName` de WMI (el método de Monitorian) |
| Wi-Fi y Bluetooth, encender y apagar | `Windows.Devices.Radios` |
| SSID | `NetworkInformation` y `GetConnectedSsid()`. **Nunca WlanAPI**, que pide la ubicación |
| Dispositivos Bluetooth conectados | Dos `DeviceWatcher`, uno clásico y otro LE, sin contar dos veces el mismo dispositivo por su dirección |
| Luz nocturna | Blob CloudStore del registro, formato Bond CompactBinary (sin API pública) |
| Utilidades | Toolhelp para saber cuáles corren, `CreateProcessW` para arrancar y Restart Manager para cerrar |

## Sistema de diseño

- **Colores (oscuro):**
  - panel `#1E1F24` al 85 %;
  - tarjeta `#2A2B31`;
  - texto `#F2F2F5` y `#8B8C94`;
  - acento: el del sistema, con `#0A84FF` como respaldo.
- **Claro y alto contraste:** salen de `calendario/src/ui/theme.cpp`.
- **Medidas:**
  - panel de 344 DIP de ancho, radio 14, relleno 12, separación 8;
  - tiles y tarjetas de radio 10;
  - deslizador en píldora de 28 DIP, con el icono dentro.
- **Tipografía:** Segoe UI Variable a 13 y 11. Iconos Segoe Fluent Icons a 16. Números
  tabulares en los porcentajes.
- **Tiles:**
  - un clic los enciende o apaga; encendidos, van rellenos de acento;
  - el clic derecho abre su página de `ms-settings:`;
  - el subtítulo da el estado: el SSID, «2 disp.», la hora programada o «Próximamente».
- **Deslizadores:** el porcentaje solo se ve al pasar el ratón o al arrastrar.
- **Movimiento:**
  - al abrir, 160 ms de ease-out subiendo 8 DIP;
  - al cerrar, un fundido de 120 ms, quitando antes el material de DWM;
  - desplegar una sección es un muelle de unos 250 ms de periodo;
  - los cambios de color, 120 ms fijos.
  - **Se afina con el periodo y se juzga con la app delante**, nunca con `SettleMs`.
  - Respeta `SPI_GETCLIENTAREAANIMATION`.
- **Teclado:**
  - `Tab` y las flechas mueven el foco, `Espacio` activa;
  - en un deslizador, las flechas cambian ±2 % y `RePág`/`AvPág` ±10 %;
  - `Esc` cierra.
- **Cuándo se esconde:** al perder el foco, con `Esc` o con `Alt+F4`. `Alt+F4` esconde, no destruye.

## Arquitectura

```
src/
  main.cpp       mutex, ventana de mensajes, atajo, bucle
  core/          config, log, paths, hr, i18n (de calendario/src/core), hotkey.h (de
                 calendario/src/app), options (--monitor, --render-snapshot, --theme)
  model/         state.h: PanelState, el esquema único; sample.cpp: los datos de ejemplo
  ui/            panel_window (ventana y composición), panel_view (el dibujo), layout (todos
                 los rectángulos, puro), theme, paint, glyphs, snapshot
  system/        (desde la fase 3) audio, policy_config, brightness, display_ids, radios,
                 nightlight_blob, nightlight, apps, worker
tests/           doctest: hotkey, options, layout
assets/          manifiesto (PerMonitorV2, asInvoker, UTF-8) y .rc
```

### Reglas de arquitectura

1. **`PanelState` se fija en la fase 1 con todos sus campos.** Las fases siguientes lo
   rellenan con datos reales, pero no le cambian la forma.
2. **La interfaz nunca espera al sistema.**
   - DDC/CI, las escrituras de WMI y los `.get()` de WinRT van a `system/worker`, que
     devuelve el resultado con `PostMessageW`.
   - Los callbacks de COM y WinRT solo publican un mensaje. Nunca sueltan ni registran nada
     dentro del callback.
3. **Escondido no se pinta.** El estado se sigue actualizando por suscripción, pero solo se
   dibuja con el panel visible. La ventana se crea una vez al arrancar, y al esconderla se
   recorta la memoria de trabajo.
4. **Lo que se puede probar sin Windows es una función pura con tests:**
   - leer y escribir el blob de la luz nocturna;
   - convertir las rutas de monitor en IDs;
   - pasar de la posición del deslizador a un valor;
   - colocar el panel;
   - leer el atajo.
5. **Endpoint de audio caducado.** Cambiar la salida de audio **no invalida** el endpoint que
   ya está abierto: sigue contestando con los datos del dispositivo anterior, sin dar ningún
   error.
   - `OnDefaultDeviceChanged` pone una bandera y avisa por mensaje.
   - El hilo de la interfaz suelta el endpoint y vuelve a registrarse.
   - Es lo que midió el HUD (`hud/CHANGELOG.md`) y el patrón está en `isla/Audio.cs:151-156`.
6. **Errores como valores**, mostrados dentro del panel. Nunca un `MessageBox`. El sitio es
   `PanelState::notice`: una línea, en dos renglones si hace falta, al pie del panel.

### Decisiones de la fase 1

- **El alto del panel sale de su contenido.** El ancho es fijo, de 344 DIP; cerrado mide 392 DIP
  de alto. Abrir una tarjeta hace crecer la ventana hacia arriba desde la esquina. Una ventana
  del alto máximo con zonas transparentes también recibiría los clics en esas zonas.
- **El porcentaje de un deslizador va en la cabecera de su tarjeta**, a la izquierda del
  chevron. Dentro de la barra tendría que cambiar de color según dónde cae el relleno.
- **Una pantalla sin DDC/CI conserva su fila,** con «Sin control de brillo» en el sitio de la
  barra. Si desapareciera, parecería una pantalla que no se ha detectado.
- **Las capturas usan siempre el acento `#0A84FF`,** y la app el de Windows (`DWM\AccentColor`,
  solo lectura). Un PNG commiteado no puede cambiar de color según la máquina.
- **`panel.json` no se recarga en caliente.** Los cambios entran al reiniciar. Se añade
  cuando haya algo que se cambie a menudo.
- **`small` es una macro de `rpcndr.h`** (`#define small char`). La fuente de 11 se llama
  `caption`.

## Convenciones

- Código, identificadores y comentarios en **inglés**. README, CHANGELOG y estos documentos
  en **español**.
- La interfaz es bilingüe con `T(L"es", L"en")` (`core/i18n.h`), y el español es el idioma
  por defecto.
- **Recursos:** RAII con `Microsoft::WRL::ComPtr` y `hr.h`. Nunca `new` ni `delete` a pelo.
- **Errores:** no se lanzan excepciones a través de callbacks de Win32. Se devuelve `bool` y
  el motivo va al log.
- **Commits:** Conventional Commits con el ámbito del proyecto, por ejemplo
  `feat(panel): …` o `docs(panel): …`.
- **Qué copiar:** el código que viene de Agenda se copia y se renombra el espacio de nombres
  (`agenda` → `panel`). No se enlaza entre proyectos: el README raíz dice por qué.

## Documentación viva (OBLIGATORIO)

- **`CHANGELOG.md`:** formato *Keep a Changelog*. Cada fase añade su entrada bajo
  `[Unreleased]`.
- **`README.md`:** se crea en la fase 1 y se actualiza cuando cambian los atajos, la
  configuración o los requisitos.
- **Si una decisión se desvía de este archivo,** se actualiza aquí en el mismo cambio y se
  dice en el resumen.

## Seguridad y límites

- **`SEGURIDAD.md` manda.** Toda API nueva que toque el sistema pasa antes por una enmienda
  suya, en un commit propio y **anterior** al código.
- **`auditar.ps1` tiene que devolver 0 al acabar cada fase.**
- **Detente y pregunta antes de:**
  - borrar archivos;
  - añadir dependencias;
  - escribir en el registro fuera de lo que permite `SEGURIDAD.md`;
  - tocar otro proyecto de la carpeta. Por ejemplo, que el HUD ignore los cambios de volumen
    del panel: eso va en la fase 8, y solo con permiso;
  - hacer cualquier cosa fuera de la carpeta del repo.
- **Durante el desarrollo, la luz nocturna y las radios se prueban de verdad.** Déjalas como
  estaban al terminar y dilo en el resumen.

## Protocolo de trabajo por fases (aplica a TODA sesión)

- Cada sesión trabaja **una sola fase**. No adelantes trabajo de las siguientes.
- Tras cada paso relevante escribe: ✅ [qué se completó].
- Si un build o un test falla 3 veces seguidas por la misma causa, detente y explica el problema.
- Al terminar una fase:
  1. Compila Debug y Release sin warnings nuevos.
  2. Pasa `ctest --preset debug`.
  3. `auditar.ps1` devuelve 0.
  4. Actualiza el CHANGELOG, y el README si aplica.
  5. Haz el commit, **solo con archivos de `panel-de-control/`**: el árbol suele tener
     cambios ajenos de otros proyectos.
  6. Da un resumen de 10 líneas como mucho y **ESPERA la aprobación** del usuario.

## Fases

- [x] **0. Documentos antes del código:** este archivo, `SEGURIDAD.md`, `auditar.ps1` y el plan.
- [x] **1. Esqueleto con datos falsos:**
  - CMake y presets;
  - lo copiado de Agenda: core, atajo, monitor y DPI, tema, ventana;
  - `PanelState` completo;
  - el diseño pintado de forma estática;
  - `--render-snapshot`;
  - el menú del clic derecho: «Abrir panel.json» y «Salir».
- [ ] **2. Controles:**
  - tile, deslizador, sección que se despliega y chip de utilidad;
  - estados de hover, pulsado y foco;
  - uso completo con el teclado.
- [ ] **3a. Volumen:** Core Audio, el nombre del dispositivo, el silencio y el arreglo del
  endpoint caducado.
- [ ] **3b. Elegir la salida de audio:** `EnumAudioEndpoints` e `IPolicyConfig`.
- [ ] **4a. Brillo del portátil:** WMI y su evento de cambio.
- [ ] **4b. Monitores externos:**
  - DDC/CI;
  - emparejar cada monitor con su ID;
  - un deslizador por pantalla;
  - volver a enumerar con `WM_DISPLAYCHANGE`.
- [ ] **5. Wi-Fi y Bluetooth:** Radios, SSID sin ubicación y `DeviceWatcher`s.
- [ ] **6. Luz nocturna:**
  - el blob, con tests sobre los datos reales de esta máquina; solo se escribe el valor del
    estado, nunca el del horario;
  - la copia de seguridad;
  - la comprobación de ida y vuelta;
  - las claves `…perdevice`.
- [ ] **7. Utilidades:**
  - detectar, arrancar y cerrar con Restart Manager;
  - comprobar que cada app se cierra de verdad.
- [ ] **8. Pulido y entrega:**
  - medir;
  - autoarranque;
  - instalador;
  - `actualizar.ps1`;
  - el README raíz;
  - que el HUD y la isla ignoren el volumen del panel, preguntando antes.

## Comandos

```
cmake --preset debug && cmake --build --preset debug
ctest --preset debug
build\debug\Panel.exe --monitor=3
build\debug\Panel.exe --render-snapshot=panel --out=docs\img\panel.png
build\debug\Panel.exe --render-snapshot=panel-brillo --out=docs\img\panel-brillo.png
build\debug\Panel.exe --render-snapshot=panel-volumen --out=docs\img\panel-volumen.png
build\debug\Panel.exe --render-snapshot=panel --theme=light --out=docs\img\panel-claro.png
build\debug\Panel.exe --render-snapshot=panel --theme=contrast --out=docs\img\panel-contraste.png
powershell -NoProfile -ExecutionPolicy Bypass -File auditar.ps1
```

El `cmake` de WinLibs que hay en el PATH no trae certificados, así que `CMakeLists.txt` usa el
almacén de certificados de Git para Windows. Si no lo encuentra, define antes `CURL_CA_BUNDLE`.
