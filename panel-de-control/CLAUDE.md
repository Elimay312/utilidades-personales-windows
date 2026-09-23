# CLAUDE.md — Panel (centro de control para Windows)

> Nombre en clave: **Panel**. Si el usuario cambia el nombre, actualiza este archivo, el README,
> CMake y `SEGURIDAD.md`.
>
> **Estado: 1.0.0, terminado el 2026-09-23.** Todas las fases están hechas. Lo que venga después
> (el menú de Configuración, oscurecer por software un monitor sin DDC/CI) entra como fase
> nueva, con su enmienda a `SEGURIDAD.md` antes del código si toca el sistema.

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
| SSID | `NetworkInformation` y `GetConnectedSsid()`, sin permiso de ubicación |
| Redes de alrededor y conectar a una guardada (5b-2) | WlanAPI (`WlanScan`, `WlanGetAvailableNetworkList`, `WlanConnect` con perfil), solo en `system/wifi.cpp` y solo con la tarjeta de Wi-Fi abierta: pide la ubicación |
| Dispositivos Bluetooth (5b-3) | Dos `DeviceWatcher` de emparejados, uno clásico y otro LE, con `IsConnected` y la clase; una fila por dirección |
| Conectar o desconectar audio Bluetooth (5b-3) | `KSPROPERTY_ONESHOT_RECONNECT`/`DISCONNECT` por `IOCTL_KS_PROPERTY`, solo en `system/bt_audio.cpp` |
| Luz nocturna | Blob CloudStore del registro, formato Bond CompactBinary (sin API pública) |
| Utilidades | Toolhelp para saber cuáles corren, `CreateProcessW` para arrancar y `WM_CLOSE` a la clase de su ventana para cerrar (fase 7: el Restart Manager no sirve con estas apps) |

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
  core/          config, log, paths, hr, i18n, autostart (de calendario/src/core), hotkey.h (de
                 calendario/src/app), options (--monitor, --render-snapshot, --theme)
  model/         state.h: PanelState, el esquema único; sample.cpp: los datos de ejemplo
  ui/            panel_window (ventana, composición y entrada), panel_view (el dibujo),
                 layout (todos los rectángulos, puro), controls (qué hay bajo el ratón, orden
                 del foco, valor de un deslizador; puro), spring, vsync (de Agenda), theme,
                 paint, glyphs, snapshot
  system/        audio (Core Audio y la lista de salidas), policy_config (la única API no
                 documentada, en un solo archivo), worker (el hilo para lo que bloquea),
                 brightness (WMI del portátil y el aviso de Windows), radios (Wi-Fi,
                 Bluetooth, SSID; el único archivo con C++/WinRT), wifi (las redes de
                 alrededor y conectar a una guardada; el único con WlanAPI), bt_audio
                 (conectar y desconectar audio Bluetooth; el único con IOCTL), nightlight_blob
                 (el códec Bond, puro), nightlight (el registro), apps (la fila de
                 utilidades) y display_ids (qué monitor es cuál, puro)
  installer/     Instalar-Panel.exe, el de Agenda sin TerminateProcess ni cmd (SEGURIDAD §2.8)
tests/           doctest: hotkey, options, layout, controls
assets/          manifiestos del Panel y del instalador, y .rc
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

### Decisiones de la fase 2

- **Un solo reloj para todo lo que se mueve.** Es `FrameClock` (de Agenda), que avisa una vez
  por fotograma compuesto. Mueve los fundidos de hover, el hundido de los botones y los muelles
  de las tarjetas, y se para cuando nada se mueve. No hay `SetTimer` de 16 ms: se desfasa con
  la frecuencia del monitor.
- **Muelle de las tarjetas:** 250 ms de periodo y amortiguación 0,85 (`ui/spring.h`, al
  estilo de Brújula). La prueba mide 6-60 fotogramas hasta el reposo, y en la app, unos 200 ms
  visibles. Si se siente lento o brusco, se toca el periodo, y se juzga con la app delante.
- **Mientras una tarjeta se mueve, el buffer ya tiene el tamaño final.** Así crecer es un
  `SetWindowPos` por fotograma y no un `ResizeBuffers`, que parpadearía. Es lo que hace la
  expansión de Agenda. Al llegar al reposo, el buffer vuelve al tamaño de la ventana.
- **Una fila que todavía entra en su tarjeta no se puede pulsar**, ni con el ratón ni con Tab
  (`Shown` en `controls.h`). Solo cuenta lo que la tarjeta enseña entero.
- **Hacer clic en algo le da el foco**, pero sin anillo, como en Windows. Las flechas mueven el
  foco hasta que llegan a un deslizador; ahí cambian su valor.
- **Los niveles se guardan en porcentajes enteros** (`Quantize`). Lo que dice la cabecera es
  lo que se escribe, y WMI solo acepta enteros.
- **Las acciones aún cambian solo `state_`** (`PanelWindow::Activate` y `SetSliderLevel`).
  Esos dos son los sitios donde cada fase siguiente conecta la llamada real.
- **Cómo se prueba sin mover el ratón:** los clics y las teclas se postean
  (`WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP`/`WM_KEYDOWN`). El hover real no se puede
  probar así: `TrackMouseEvent` ve que el cursor no está encima y lo quita al momento. Si el
  usuario está usando el equipo, un clic suyo esconde el panel a mitad de una prueba; eso es
  lo correcto, no un fallo.

### Decisiones de la fase 3a

- **`Audio` es de la ventana,** y `PanelWindow::Shutdown()` lo suelta desde `main` antes de
  `CoUninitialize`. Si lo soltara el destructor, sería después de cerrar COM.
- **El rol es `eMultimedia`,** el mismo que usan el HUD y la isla, y el aviso de cambio se filtra
  a ese rol. Windows manda un aviso por rol.
- **El endpoint caducado:** el aviso pone `stale_` y avisa a la ventana; el siguiente
  `Endpoint()` suelta el viejo antes de usarlo. Así también lo hace una escritura que llegue
  entre el aviso y el mensaje.
- **El nombre corto** (`PKEY_Device_DeviceDesc`), con el largo de respaldo. La isla enseña el
  largo; aquí no cabe en la cabecera.
- **Escondido no se atienden los avisos del audio.** `Show()` lee una vez, y así un panel
  cerrado no despierta con cada tecla de volumen.
- **Solo se escribe un porcentaje nuevo.** Un arrastre manda muchos movimientos dentro del
  mismo 1 %, y esos no llegan a Core Audio.
- **Probar sin tocar lo del usuario:** una sonda en PowerShell con su propia interop de Core
  Audio lee el volumen antes, comprueba cada paso contra Windows y lo deja como estaba. Para
  cambiar la salida usa `IPolicyConfig`, solo en la sonda, y vuelve a la original en un
  `finally`. **Pregunta antes de cambiar la salida:** si algo está sonando, se oye un momento
  por la otra.
### Decisiones de la fase 3b

- **La lista se lee en cada apertura y con cada aviso del enumerador** (dispositivo añadido,
  quitado o que cambia de estado). Cuesta poco: el panel se abre igual en 9-15 ms.
- **Los nombres cortos repetidos pasan al largo** (`ChooseOutputNames`, puro y con pruebas). En
  esta máquina hay tres «Altavoces».
- **Elegir una salida marca la fila al instante** y no espera al aviso de Windows, que llega un
  momento después y vuelve a leerlo todo.
- **Los avisos (`PanelState::notice`) duran lo que dura una apertura:** se borran en `Shelve()`,
  con el panel ya escondido, para que no cambie de alto a mitad del fundido.
- **`PKEY_AudioEndpoint_FormFactor` está escrito a mano** en `audio.cpp`. El SDK solo lo declara,
  y definirlo son dos líneas frente a meter `INITGUID` en todo el archivo.

### Decisiones de la fase 4a

- **Las teclas Fn se siguen con `GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS`** (`WM_POWERBROADCAST`),
  no con `WmiMonitorBrightnessEvent`. Así no hay `unsecapp.exe` ni un hilo sondeando
  (`SEGURIDAD.md` §2.3). Windows manda el valor actual también al registrarse.
- **`Worker` es genérico:** trabajos en orden, uno a uno. Si llega uno con la misma clave
  mientras otro espera, lo sustituye. DDC/CI (4b) y las radios (5) irán por él.
- **Los objetos COM que se crean en el hilo de trabajo se sueltan en él:** `Brightness::Stop()`
  encola la liberación y después `Worker::Stop()` ejecuta lo pendiente y termina. En
  `Shutdown()`, ese orden importa.
- **Eco:** 400 ms después de escribir se ignoran los avisos, y lo mismo durante un arrastre.
  Si la pantalla tuviera menos niveles que 101 y redondeara, la barra enseñaría el valor
  pedido hasta el siguiente aviso.
- **El nombre de la pantalla interna es «Portátil»,** y en la 4a es siempre la de `here`. La 4b
  decide cuál es por el monitor donde se abre el panel.
- **`CanUnfold`:** con una sola cosa que elegir, la tarjeta no tiene chevron, no se despliega y
  su cabecera no es parada del Tab.
- **`Trim()`** recorta la memoria al terminar `Create`, en `Shelve` y cuando llega la primera
  lectura de WMI con el panel escondido.
- **Probar el brillo:** la sonda lee y escribe con `Get-CimInstance`/`Invoke-CimMethod` sobre
  `root/WMI`, guarda el valor del principio y lo repone en un `finally`. Las teclas Fn en sí no
  se pueden pulsar desde una sonda. Lo que se prueba es el aviso, con un cambio hecho desde
  fuera por WMI, que llega por el mismo camino.

### Decisiones de la fase 4b

- **Sin `GetMonitorCapabilities`:** en el LG tardó 4,9 s, y bloquearía el hilo de trabajo al
  arrancar y con cada `WM_DISPLAYCHANGE`. La prueba es leer el brillo (62–67 ms), y se
  escribe solo a un monitor cuya lectura funcionó (enmienda §2.4).
- **El handle de un monitor físico puede ser 0,** y es válido. `dxva2` los numera desde 0 en
  cada proceso. Se usa una marca `open`, nunca «el handle no es nulo».
- **El intervalo de 100 ms se aplica en el hilo de trabajo:** la escritura espera lo que falte.
  La cola ya guarda solo el último valor por pantalla (una clave por ID), así que al soltar
  siempre se escribe el final.
- **Orden de las filas:** por el borde izquierdo de cada pantalla en el escritorio. En casa
  sale LG, Portátil y ARZOPA.
- **Nombre:** «Portátil» para la interna; para las demás, el nombre del EDID
  (`monitorFriendlyDeviceName`), o «Pantalla N» si no lo tiene.
- **`DisplayState::device`** (`\\.\DISPLAYn`) es un campo añadido, no un cambio de forma. Con él
  la ventana sabe en qué pantalla se abre.
- **Un monitor sin DDC/CI se queda sin barra.** El ARZOPA contesta `0xC0262582` (nadie
  responde en el cable). El usuario decidió dejarlo así. La alternativa sería oscurecer por
  software, con una capa negra que deja pasar los clics: necesitaría enmienda, porque sería
  dibujar sobre otras ventanas, y no baja la retroiluminación. La curva de gamma queda
  descartada: choca con la luz nocturna, y para oscurecer mucho hace falta HKLM.
- **Probar:** con `--monitor=3`, la tarjeta habla del LG. Una sonda escribe y lee el brillo por
  DDC/CI desde fuera, para comprobar el monitor y para simular sus botones. No se probó que la
  tarjeta cerrada hable del portátil o del ARZOPA al abrir el panel en ellos: habría que abrirlo
  en las pantallas de trabajo del usuario.

### Decisiones de la fase 5

- **C++/WinRT solo en `system/radios.cpp`:** el estado de WinRT vive en un `State` escondido en
  el `.cpp`, así que nada más compila esas cabeceras. `.get()` solo en el hilo de trabajo:
  C++/WinRT no deja bloquear un STA como el de la interfaz.
- **`RequestAccessAsync` desde el hilo de trabajo (MTA)** contesta «Allowed» en esta app sin
  paquete. Si algún día contesta otra cosa, el panel lo dice en la línea de avisos.
- **El SSID sale de cualquier perfil Wi-Fi con conectividad**, no solo del que tiene internet: una
  red sin internet sigue siendo la red en la que estás.
- **`Publish()` va bajo `publish_`**, leyendo y guardando juntos. Lo encontró la sonda: el tile
  se quedaba encendido con la radio apagada.
- **Un `DeviceWatcher` necesita un manejador de `Updated`,** aunque no haga nada, para que
  `Removed` llegue cuando un dispositivo deja de cumplir el filtro de «conectado».
- **Probar las radios corta cosas de verdad:** el Wi-Fi corta internet unos segundos, y el
  Bluetooth desconecta los auriculares del usuario (soundcore P31i), con lo que la salida de
  audio cambia. **Pregunta antes siempre.** La sonda deja las dos encendidas en un `finally` y
  espera a que vuelva la red.
- **Sin probar de forma automática:** que el clic derecho abra Configuración, porque abriría una
  ventana en la pantalla del usuario. Es un `ShellExecuteW` con cada URI escrita entera.
- **`ponytail:`** al cerrar la app, un hilo del pool que ya estuviera dentro de un manejador
  podría llamar a `Publish` mientras se suelta `State`. Solo puede pasar al salir del proceso;
  si aparece en un volcado, hay que esperar al `Stopped` de los watchers.

### Decisiones de la fase 5b-1

- **Una sola tarjeta de Wi-Fi o Bluetooth a la vez.** Con una abierta, los tiles no están: no
  se puede pulsar la otra franja. Con eso, la geometría solo tiene que resolver un morph.
- **El layout pone el contenido de la tarjeta donde acabará**, y `module.card` es la forma de
  este fotograma. El dibujo mueve el contenido con el borde izquierdo de la forma y lo recorta
  con ella. Es el mismo patrón que las filas de las otras tarjetas.
- **Ritmo del morph** (`panel_view.cpp`, `DrawModule`): el color se cruza hasta el 60 %, la cara
  del tile se va antes del 40 % y el contenido entra desde el 45 %. Se afina con el periodo
  (`kMorphPeriodSeconds`) y con estos tres cortes, juzgando con la app delante.
- **Mientras no llegan las listas reales,** `TakeRadios` conserva las de ejemplo al tomar el
  estado de las radios.
- **Lección de la prueba: no dejar el panel abierto sin foco en la pantalla del usuario.**
  Abierto con un `WM_HOTKEY` posteado, no es la ventana activa y no se esconde al hacer clic
  fuera. Y Windows manda la rueda a la ventana que queda debajo del cursor, no a la que tiene el
  foco. Durante unos 40 s de medición el brillo y el volumen del usuario acabaron al 0 %,
  probablemente por su rueda. Con el atajo de verdad esto no pasa. Las sondas esconden el
  panel en cuanto acaban de mirar.

### Decisiones de la fase 5b-2

- **WlanAPI y no `WiFiAdapter` de WinRT:** las funciones responden al momento, dicen qué redes
  tienen perfil (`WLAN_AVAILABLE_NETWORK_HAS_PROFILE`) y cuál está conectada, y conectar con un
  perfil es una llamada. El precio de la ubicación es el mismo por las dos vías.
- **`wanted_` corta toda lectura** con la tarjeta plegada o el panel escondido, aunque llegue
  un aviso de una búsqueda que haya hecho otro programa.
- **Cómo se comprueba la ubicación:** la clave
  `HKCU\...\CapabilityAccessManager\ConsentStore\location\NonPackaged\<ruta del exe>` guarda
  `LastUsedTimeStart`/`Stop`. Solo debe moverse al desplegar la tarjeta de Wi-Fi.
- **Las filas se ordenan** poniendo primero la conectada, luego las guardadas, luego por señal y
  luego por nombre. Así lo que se puede pulsar sin contraseña queda arriba.

### Decisiones de la fase 5b-3

- **Un solo par de `DeviceWatcher`,** con el filtro de emparejados y `IsConnected` entre las
  propiedades pedidas. Sustituyen a los que solo contaban los conectados. `Updated` trae el
  cambio de conexión.
- **Clase:** Class of Device clásico (major 4 es audio; en major 5, minor 0x10 es teclado y 0x20
  ratón) o apariencia LE (categoría 15, subcategoría 1 teclado y 2 ratón). Los valores llegan
  como enteros de tamaños distintos, y `UIntOf` los lee todos.
- **Conectar audio:** los filtros de `KSCATEGORY_AUDIO` cuya ruta lleva `bthenum` y la dirección
  entera. En estos soundcore solo uno tomó la petición (el de A2DP), y bastó.
- **«Ocupado» (`BluetoothDevice::busy`)** dura hasta que `IsConnected` deja de ser el de cuando
  se pidió, o 12 s (`kBluetoothBusyTimer`). Se publica antes de la llamada lenta, para que la
  fila lo diga ya.
- **Probar con auriculares de verdad:** se conectan solos al salir del estuche, así que la fila
  cambia de sitio (los conectados van arriba). La sonda tiene que mirar el orden antes de
  pulsar. Desconectar cambia la salida de audio del usuario: comprueba al final que vuelve a
  «Auriculares».

### Decisiones de la fase 6

- **El códec es genérico** (`cb::Struct`, un campo con su id, su tipo y sus bytes). Así lo que no
  entiende pasa intacto, y «ida y vuelta exacta» significa algo. Solo se cambian el campo 0 del
  estado y las dos marcas de tiempo, la del sobre y el campo 20.
- **Encender** es añadir el campo 0 como `int32` 0 (`10 00`), y el formato dice que lo que cuenta
  es que esté, no su valor. Apagar es quitarlo.
- **Vigilar sin hilo propio:** `RegNotifyChangeKeyValue` asíncrono sobre un evento de reinicio
  automático, más `RegisterWaitForSingleObject`. El aviso llega al pool, que pasa la lectura al
  hilo de trabajo, y este vuelve a armarlo. Tiene que ser siempre el mismo hilo: la notificación
  muere con el hilo que la pidió.
- **La copia de seguridad se escribe una sola vez:** una segunda guardaría la escritura del propio
  panel, que no es lo que nadie querría recuperar.
- **Probar sin el interruptor de Windows:** escribir desde una sonda el blob que el panel ya
  escribió es lo mismo que hace el interruptor. Lo que la captura no ve es el color de la
  pantalla, porque el filtro no sale en `CopyFromScreen`. Eso lo confirma el usuario.

### Decisiones de la fase 7

- **`WM_CLOSE`, no `RmShutdown`:** lo midió un agente leyendo las seis apps. Ninguna atiende
  `WM_QUERYENDSESSION` ni `WM_ENDSESSION`, y todas cierran limpio con `WM_CLOSE` a su ventana
  principal, que es lo que ya hace el instalador de Agenda.
- **Las clases por defecto** (`DefaultUtilities`): `DockWindowClass` (una por pantalla, todas),
  `IslaDinamica`, `HudVolumen`, `QuickLookHostClass`, `LanzadorVentana` y `AgendaApp`. Las
  ventanas se buscan en cada cierre: el Dock y la Isla las rehacen, y un identificador guardado
  caduca.
- **Brújula, Rayo y Renombrar no van en la fila por defecto:** son apps con ventana, cerrarlas
  desde aquí podría perder trabajo, y el Lanzador ya las abre. Quien las quiera las añade a
  `panel.json`, y si no pone `ventana`, solo se arrancan.
- **Probar con la utilidad que esté parada** y dejarla como estaba. Cerrar una que el usuario
  está usando (el Dock, el Lanzador) se lo quita de delante.
- **Juzgar una captura por sus píxeles:** `GetPixel` en el punto exacto, no el ojo sobre la
  imagen reducida.

### Decisiones de la fase 8

- **El instalador es el de Agenda** con tres cambios, que fija `SEGURIDAD.md` §2.8:
  - sin `TerminateProcess`;
  - sin `cmd.exe` para borrar la carpeta;
  - sin `ShellExecute` para abrir Panel.
- **Sin AppUserModelID en el acceso directo:** Agenda lo necesita para sus notificaciones, y
  el Panel no tiene.
- **Sin icono propio:** el instalador y el Panel llevan el de Windows por defecto. Se añade
  cuando haya un `.ico`.
- **Probar el instalador en una carpeta de pruebas:** apuntar `LOCALAPPDATA` y `APPDATA` a
  ella, con `PANEL_INSTALLER_NO_REGISTRY=1`. Así no se toca el registro, y el Panel del build,
  si está abierto, no se cierra, porque su ruta no es la instalada.
- **Cerrar el Panel desde fuera:** `WM_CLOSE` a `PanelDeControlHost`. Sirve también para las
  pruebas, en vez de matar el proceso.

- **El HUD y la isla reconocen `kPanelVolumeContext`** en el `guidEventContext` de su aviso de
  COM (commits propios en cada proyecto). Con esa firma guardan el nivel sin enseñarlo. **Si
  cambia el GUID aquí, hay que cambiarlo en `hud/Volumen.cs` y en `isla/Audio.cs`
  (`ContextoPanel`).**

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
  - tocar otro proyecto de la carpeta;
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
- [x] **2. Controles:**
  - tile, deslizador, sección que se despliega y chip de utilidad;
  - estados de hover, pulsado y foco;
  - uso completo con el teclado.
- [x] **3a. Volumen:** Core Audio, el nombre del dispositivo, el silencio y el arreglo del
  endpoint caducado.
- [x] **3b. Elegir la salida de audio:** `EnumAudioEndpoints` e `IPolicyConfig`.
- [x] **4a. Brillo del portátil:** WMI y su evento de cambio.
- [x] **4b. Monitores externos:**
  - DDC/CI;
  - emparejar cada monitor con su ID;
  - un deslizador por pantalla;
  - volver a enumerar con `WM_DISPLAYCHANGE`.
- [x] **5. Wi-Fi y Bluetooth:** Radios, SSID sin ubicación y `DeviceWatcher`s.
- [x] **5b. Desplegar Wi-Fi y Bluetooth** (lo pidió el usuario en la fase 5). Decidido: Wi-Fi
  con la opción A (buscar pidiendo la ubicación; las redes nuevas con contraseña se dejan a la
  lista de Windows). Bluetooth: los emparejados, conectar y desconectar los de audio
  (`KSPROPERTY_ONESHOT_RECONNECT`/`DISCONNECT`), y «buscar» abre la pantalla de Windows. Va en
  tres pasos:
  - [x] **5b-1:** el morph con listas de ejemplo;
  - [x] **5b-2:** el Wi-Fi real, con su enmienda antes;
  - [x] **5b-3:** el Bluetooth real, con su enmienda antes.

  El planteamiento de partida: una flecha
  en cada uno de los dos tiles. El clic en el tile sigue siendo encender y apagar; la flecha
  despliega una tarjeta debajo:
  - **Wi-Fi:** las redes que hay alrededor para cambiar de una a otra, y buscar. **Choca con
    `SEGURIDAD.md` §1.2:** listar redes (`WiFiAdapter.ScanAsync`, `WlanGetAvailableNetworkList`)
    exige el permiso de ubicación desde 24H2. Windows lo pide la primera vez, y desde entonces
    el panel sale en el icono de «ubicación en uso» cada vez que busca. Hace falta una enmienda
    y que el usuario decida si lo acepta, antes de escribir código. La alternativa sin ubicación
    es listar solo los perfiles guardados (`GetConnectionProfiles`), sin ver qué hay alrededor.
  - **Bluetooth:** los dispositivos emparejados, para conectar y desconectar uno, y buscar
    nuevos para emparejarlos. **Choca con `SEGURIDAD.md` §2.6**, que hoy prohíbe emparejar,
    conectar y desconectar. Hace falta una enmienda antes, con sus cortes: solo por un clic, solo
    dispositivos que se ven en la lista, y sin tocar nunca los que no son de audio o de entrada
    sin confirmarlo.
  - **Encaje en el diseño:** la tarjeta desplegada va entre los tiles y el brillo, con el mismo
    muelle que las otras. El `PanelState` actual no tiene sitio para las listas: habrá que
    añadir `wifi.networks[]` y `bluetooth.devices[]`, que es añadir campos, no cambiar los que
    hay.
- [x] **6. Luz nocturna:**
  - el blob, con tests sobre los datos reales de esta máquina; solo se escribe el valor del
    estado, nunca el del horario;
  - la copia de seguridad;
  - la comprobación de ida y vuelta;
  - las claves `…perdevice`.
- [x] **7. Utilidades:**
  - detectar, arrancar y cerrar con Restart Manager;
  - comprobar que cada app se cierra de verdad.
- [x] **8. Pulido y entrega:**
  - [x] medir;
  - [x] autoarranque;
  - [x] instalador;
  - [x] `actualizar.ps1`, con permiso;
  - [x] el README raíz;
  - [x] que el HUD y la isla ignoren el volumen del panel, con permiso.

## Comandos

```
cmake --preset debug && cmake --build --preset debug
ctest --preset debug
powershell -NoProfile -ExecutionPolicy Bypass -File empaquetar.ps1
build\debug\Panel.exe --monitor=3
build\debug\Panel.exe --render-snapshot=panel --out=docs\img\panel.png
build\debug\Panel.exe --render-snapshot=panel-brillo --out=docs\img\panel-brillo.png
build\debug\Panel.exe --render-snapshot=panel-volumen --out=docs\img\panel-volumen.png
build\debug\Panel.exe --render-snapshot=panel-estados --out=docs\img\panel-estados.png
build\debug\Panel.exe --render-snapshot=panel --theme=light --out=docs\img\panel-claro.png
build\debug\Panel.exe --render-snapshot=panel --theme=contrast --out=docs\img\panel-contraste.png
powershell -NoProfile -ExecutionPolicy Bypass -File auditar.ps1
```

El `cmake` de WinLibs que hay en el PATH no trae certificados, así que `CMakeLists.txt` usa el
almacén de certificados de Git para Windows. Si no lo encuentra, define antes `CURL_CA_BUNDLE`.
