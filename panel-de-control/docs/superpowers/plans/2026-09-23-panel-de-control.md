# Panel de control: plan

## Contexto

Quieres un centro de control al estilo macOS para Windows 11. Se abre con un atajo y concentra lo que hoy está repartido por la configuración de Windows: Wi-Fi, Bluetooth, luz nocturna, brillo y volumen.

- Tiene que ser **C++ nativo y ligero**.
- El botón «Concentración» del wireframe se sustituye por **Configuración**. Por ahora no hace nada; más adelante abrirá un menú para configurar los otros proyectos del repo.
- Además del wireframe entran tres cosas relacionadas con los otros proyectos:
  1. encender y apagar utilidades,
  2. elegir la salida de audio,
  3. un brillo por monitor.

`panel-de-control/` existe y está vacía. Es un proyecto aparte, como los demás: su propio proceso, su propia configuración y su propio `SEGURIDAD.md` (README raíz, «Por qué cada uno va por separado»).

**Decisiones tomadas:**

| Pregunta | Respuesta |
|---|---|
| Atajo | `Ctrl+Alt+A`, cambiable en `panel.json` |
| Posición | abajo a la derecha, encima de la bandeja, en el monitor donde esté el ratón |
| Integraciones | utilidades, salida de audio y brillo por monitor |

Win+A no puede usarse: lo reserva el shell y `RegisterHotKey` falla con el error 1409.

---

## 1. Stack y paquetes (investigados el 2026-09-23)

**Base: el popup de Agenda (`calendario/`).** Es el mismo tipo de ventana, ya resuelta en este repo:
- Win32 puro.
- D3D11 con un swap chain de composición, DirectComposition y Direct2D/DirectWrite.
- Los detalles de DWM: acrylic, esquinas redondeadas y quitar el fondo antes del fundido.

Nada de WinUI 3 ni del Windows App SDK: exigen un runtime aparte y cargan XAML, decenas de MB para una ventana que vive escondida.

| Dependencia | Versión | Por qué | Verificación |
|---|---|---|---|
| Windows SDK (D2D, DWrite, DComp, Core Audio, dxva2, WMI, BluetoothAPIs) | la instalada | Viene con Windows; no es un paquete de terceros | — |
| C++/WinRT | **las cabeceras que trae el Windows SDK** (Brújula ya las usa con `windowsapp`) | Solo para `Windows.Devices.Radios`, `DeviceWatcher` y `NetworkInformation` | Sin descarga. La 3.0.260818.1 del 19 de agosto es incompatible hacia atrás (quita `/await`) y no hace falta |
| nlohmann/json | **v3.12.0**, FetchContent con `URL_HASH SHA256` | `panel.json`, reutilizando `calendario/src/core/config.cpp` | MIT, repo oficial, sin avisos de seguridad. El «nlohmann-json» malicioso de 2025 (MAL-2025-711) es un typosquat de **npm**, no esta librería |
| doctest | **v2.5.3** (6 de julio de 2026), FetchContent con hash | Tests. Es solo cabecera y compila más rápido que Catch2 v3 | MIT, sin avisos, es la que ya usa Brújula |

**Qué queda fuera y por qué:**
- **WIL.** El repo ya tiene `hr.h` y `ComPtr`; otra librería para lo mismo sobra.
- **vcpkg.** `VCPKG_ROOT` no está definido en esta máquina. Agenda y Brújula ya usan FetchContent de verdad, y fijar la versión por hash cubre lo que pides de que los paquetes no sean maliciosos.

**Compilación:**
- C++20, MSVC, x64. Tiene que ser x64: con x86 sobre x64, `Radio::GetRadiosAsync` devuelve una lista vacía.
- Flags: `/W4 /permissive- /utf-8 /EHsc`. CRT estático `/MT`, como Brújula y Rayo.
- `CMakePresets.json` con los presets debug y release, como Agenda.

**Una API por función:**

| Función | API | Notas |
|---|---|---|
| Volumen y silencio | Core Audio: `IAudioEndpointVolume`, `IAudioEndpointVolumeCallback` e `IMMNotificationClient::OnDefaultDeviceChanged` | Hay que copiar el arreglo del endpoint caducado (sección 4.3). Se pasa un GUID de contexto propio para ignorar el eco de los cambios que hace el propio panel |
| Nombre del dispositivo | `PKEY_Device_FriendlyName` | Mismo método que `isla/Audio.cs:228-247` |
| Lista de salidas y cambio de salida | `EnumAudioEndpoints` y `IPolicyConfig::SetDefaultEndpoint` en los tres roles | **No está documentado**, pero es estable desde Windows 10 RS1. Lo usan EarTrumpet (2026-07) y SoundSwitch (7.3.3, 2026-09). No existe alternativa documentada |
| Brillo del portátil | WMI `root\WMI`: `WmiMonitorBrightness` y `WmiSetBrightness`; `WmiMonitorBrightnessEvent` para seguir las teclas Fn | Funciona sin elevar. Este portátil tiene 101 niveles (`hud/CHANGELOG.md:268`) |
| Brillo de monitores externos | DDC/CI por `dxva2`: `GetPhysicalMonitorsFromHMONITOR`, `GetMonitorCapabilities` y `Get/SetMonitorBrightness` | Tarda **50–200 ms por llamada**. Va en un hilo aparte, enviando solo el último valor, como mucho unas 10 veces por segundo |
| Qué monitor es cuál | `QueryDisplayConfig` y `DISPLAYCONFIG_TARGET_DEVICE_NAME`, de donde sale un ID `DISPLAY\hw\inst` que se compara con el `InstanceName` de WMI | Método de Monitorian. Un monitor es interno si su salida es `INTERNAL`, `DISPLAYPORT_EMBEDDED` o `UDI_EMBEDDED` |
| Encender y apagar el Wi-Fi y el Bluetooth | `Windows.Devices.Radios`: `RequestAccessAsync` una vez y luego `SetStateAsync`; el evento `StateChanged` avisa de los cambios | Documentado, sin permisos de administrador |
| SSID | `NetworkInformation::GetInternetConnectionProfile()`, luego `WlanConnectionProfileDetails().GetConnectedSsid()` | **No se usa `WlanQueryInterface`**: desde 24H2 pide permiso de ubicación y el panel saldría en el icono de «ubicación en uso» |
| Nº de dispositivos Bluetooth | Dos `DeviceWatcher`, uno para `BluetoothDevice` y otro para `BluetoothLEDevice`, con el filtro de conectados | Se quitan los duplicados por dirección: los dispositivos duales salen en las dos listas |
| Luz nocturna | Blob CloudStore en `HKCU\...\CloudStore\Store\DefaultAccount\Current\default$windows.data.bluelightreduction.{bluelightreductionstate,settings}`, valor `Data` | **Sin API pública.** Formato Bond CompactBinary, documentado por kvnxiao/win-nightlight-cli (MIT, 2026-06). En esta máquina el blob decodifica a 21:00–07:00. En 26200 hay además claves `…perdevice` que hay que medir |
| Utilidades | `CreateToolhelp32Snapshot` para ver qué está corriendo, `CreateProcessW` para arrancar y el Restart Manager (`RmShutdown`) para cerrar con cortesía | Sin `TerminateProcess`. Si una app no se cierra, el panel lo dice y no la fuerza |

---

## 2. Diseño refinado

Paleta y medidas salen de lo que ya usan Agenda y Brújula, para que el panel parezca de la misma familia.

- **Paleta oscura:**
  - panel `#1E1F24` al 85 % sobre acrylic,
  - tarjetas `#2A2B31`,
  - texto `#F2F2F5` y `#8B8C94`,
  - acento: el color de acento del sistema, con `#0A84FF` como respaldo.
- **Tema claro y alto contraste:** salen de `calendario/src/ui/theme.cpp`.
- **Fondo plano:** en esta máquina el acrylic se ve plano. El color al 85 % tiene que verse bien también así.
- **Medidas:**
  - panel de 344 DIP de ancho, radio 14, relleno 12, separación 8;
  - tarjetas de radio 10;
  - deslizadores de 28 DIP de alto, en forma de píldora.
- **Tipografía:** Segoe UI Variable a 13 y 11 px. Iconos: Segoe Fluent Icons a 16 px.

```
┌────────────────────────────────────────┐
│ ┌──────────────────┐ ┌───────────────┐ │
│ │ (wifi)  Wi-Fi    │ │ (bt) Bluetooth│ │  activo = relleno de acento
│ │         Casa_5G  │ │      2 disp.  │ │  clic = encender/apagar
│ └──────────────────┘ └───────────────┘ │  clic derecho = ms-settings:
│ ┌──────────────────┐ ┌───────────────┐ │
│ │ (luna) Luz noct. │ │ (⚙) Configur. │ │  Configuración: se ve pulsada,
│ │        21:00     │ │   Próximamente│ │  no hace nada todavía
│ └──────────────────┘ └───────────────┘ │
│ ┌────────────────────────────────────┐ │
│ │ Brillo · Monitor 2             ›   │ │  › despliega un deslizador
│ │ [☀▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░░]    62 %  │ │    por pantalla
│ └────────────────────────────────────┘ │
│ ┌────────────────────────────────────┐ │
│ │ Volumen · Auriculares          ›   │ │  › despliega las salidas
│ │ [🔊▓▓▓▓▓▓▓▓▓░░░░░░░░░░░]    45 %  │ │  clic en el altavoz = silencio
│ └────────────────────────────────────┘ │
│  ◉ Dock  ◉ Isla  ○ HUD  ◉ QLook  ◉ Lanz│  punto verde = corriendo
└────────────────────────────────────────┘     clic = arrancar o cerrar
```

**Cambios respecto al wireframe:**
- **Iconos en vez de círculos blancos.**
- **Deslizadores gruesos con el icono dentro**, como en macOS.
- **El porcentaje** solo se ve al pasar el ratón o al arrastrar.
- **Chevron en cada tarjeta que se despliega.**
- **Fila de utilidades al pie.**
- **El brillo de la tarjeta es el del monitor donde se abre el panel.**

**Movimiento:**
- Abrir: 160 ms ease-out, subiendo 8 DIP. Cerrar: fundido de 120 ms. Es lo de Agenda.
- Desplegar una sección: muelle con periodo de unos 250 ms.
- Fundidos de color: 120 ms fijos.
- Se afina con el periodo y se juzga con la app delante, nunca con `SettleMs`.
- Si Windows tiene las animaciones desactivadas (`SPI_GETCLIENTAREAANIMATION`), solo fundidos.

**Teclado:**
- `Tab` y las flechas recorren los controles.
- `Espacio` activa.
- En un deslizador, las flechas mueven ±2 % y `RePág`/`AvPág` ±10 %.
- `Esc` cierra y `Alt+F4` esconde.

**Cuándo se esconde:** al perder el foco, con `Esc` o con `Alt+F4`.

---

## 3. Estructura

```
panel-de-control/
  CLAUDE.md  SEGURIDAD.md  auditar.ps1  README.md  CHANGELOG.md
  CMakeLists.txt  CMakePresets.json  panel.example.json  empaquetar.ps1
  assets/        panel.manifest (PerMonitorV2, asInvoker, UTF-8), panel.rc, icono
  src/
    main.cpp               mutex, ventana de mensajes, atajo, bucle
    core/                  config, log, paths, hr, autostart, i18n    ← copiados de calendario/src/core
    ui/                    panel_window, theme, paint, layout, spring, snapshot ← del popup de Agenda
      controls.{h,cpp}     tile, slider, expander, chip de utilidad (nuevos)
    model/state.h          PanelState: el esquema único que pinta la UI
    system/
      audio.cpp            volumen, silencio, nombre, lista de salidas
      policy_config.h      IPolicyConfig (aislado: único sitio con la API no documentada)
      brightness.cpp       WMI (interno) + DDC/CI (externos) + emparejado
      display_ids.cpp      conversión de rutas → ID de instancia (pura, con tests)
      radios.cpp           Wi-Fi/Bluetooth: Radios, SSID, DeviceWatchers
      nightlight_blob.cpp  lectura/escritura Bond del blob (pura, con tests)
      nightlight.cpp       registro, copia de seguridad, RegNotifyChangeKeyValue
      apps.cpp             detectar / arrancar / cerrar utilidades
      worker.{h,cpp}       un hilo para llamadas bloqueantes (DDC, WinRT .get(), WMI set)
  tests/                   doctest: atajo, blob, display_ids, colocación, valor del deslizador
```

**Reglas de arquitectura:**

1. **`PanelState` se fija en la fase 1 con todos sus campos**, aunque de momento tengan datos falsos. Las fases siguientes solo lo rellenan, así el esquema no cambia de una a otra.
   ```cpp
   wifi{on, ssid}, bt{on, count}, night{on, from, to},
   displays[]{name, id, level, internal, reachable},
   audio{level, muted, device, outputs[]},
   apps[]{name, running}
   ```
2. **La interfaz nunca espera al sistema.**
   - Lo que puede tardar (DDC/CI, las llamadas asíncronas de WinRT, WMI al escribir) va a `worker`, que avisa con `PostMessageW` a la ventana.
   - Los callbacks de COM y WinRT solo publican un mensaje; nunca liberan nada dentro del callback.
3. **Con el panel escondido, 0 % de CPU.**
   - Nada consulta en bucle: todo son suscripciones, y el estado se actualiza aunque el panel no se vea, pero solo se pinta estando visible.
   - La ventana se crea una vez al arrancar y al esconderla se recorta la memoria de trabajo (`Shelve()` de Agenda).
4. **Configuración:** `%LOCALAPPDATA%\Panel\panel.json`, con `merge_patch` y guardado atómico, como Agenda.
   - Claves: `hotkey`, `pasoVolumen`, `ddcIntervaloMs` y `utilidades[]` con `{nombre, exe}`.
   - Las rutas por defecto de las utilidades salen de las entradas `Exe` de `actualizar.ps1:104-148`.
5. **Código:** identificadores y comentarios en inglés; README y CHANGELOG en español. Commits `feat(panel): …`. Es la convención de `calendario/CLAUDE.md`.

---

## 4. Fases, en orden de prioridad

Una fase por sesión. Al acabar cada una:
- Debug y Release compilan sin warnings nuevos.
- `panel_tests` pasa.
- `auditar.ps1` devuelve 0.
- Se actualiza el CHANGELOG y se hace commit.

Las más grandes están partidas en a y b.

### Fase 0: Documentos antes del código
- `CLAUDE.md`: la visión, el stack fijado (la tabla de la sección 1), las reglas de arquitectura, el protocolo de fases y la regla de monitores. Esa regla dice: enumerar las pantallas antes de lanzar nada y usar `--monitor=N` con la que haya.
- `SEGURIDAD.md`, con sus reglas:
  1. **Red:** ninguna llamada de red. `NetworkInformation` solo consulta el estado local.
  2. **Permisos:** manifiesto `asInvoker` y ninguna elevación.
  3. **Registro:** solo se escribe la clave `Run` propia y el valor `Data` del **estado** de la luz nocturna; el del horario solo se lee (SEGURIDAD §2.5, más estricto que el borrador). Antes del primer cambio se guarda una copia en `%LOCALAPPDATA%\Panel\`.
  4. **Wi-Fi:** nunca `WlanQueryInterface`, `WlanGetAvailableNetworkList` ni `WlanScan`, para que no aparezca el aviso de ubicación.
  5. **Salida de audio:** `IPolicyConfig` solo en `system/policy_config.h` y solo por un clic del usuario.
  6. **WMI:** solo `root\WMI` y solo las clases `WmiMonitorBrightness*`.
  7. **Procesos:** solo se arrancan los ejecutables de `utilidades[]`, y solo si la ruta existe y termina en `.exe`. Nada de `TerminateProcess`, `system`, `WinExec` ni `ShellExecute` con datos de la configuración, salvo URIs `ms-settings:` fijas.
  8. **Teclado:** sin ganchos de teclado. Solo `RegisterHotKey`.
  9. **Dependencias:** FetchContent fijado por hash. Ningún registro de terceros.
  10. **Telemetría:** ninguna.
- `auditar.ps1`: la plantilla de `proyectos-github/auditar.ps1`, que quita comentarios, define `Regla` y sale con 0 o 1. Las reglas de las partes aún sin código quedan en `pendiente`.
- Una copia de este plan en `docs/superpowers/plans/`.
- Commit: `docs(panel): SEGURIDAD, CLAUDE.md y plan`.

### Fase 1: Esqueleto que ya se siente bien, con datos falsos
- Primero la sensación, como la fase 1 de Brújula: si la base visual no convence, aquí es barato corregirla.
- CMake y los presets. Se copian de `calendario/src`:
  - `core/*`: config, log, paths, hr, autostart;
  - `app/hotkey.h` con sus tests;
  - de `ui/layout.h`: `MonitorDpi`, `TargetMonitor` y `ScaleDip`;
  - `ui/theme.*`, `ui/paint.h` y `ui/spring.h`;
  - el esqueleto de `ui/popup_window.cpp`: `Create`, `ApplyDwmAttributes`, `CreateDevices`, `Render` con `SetDpi`, `Show`/`Hide`/`Toggle`, `Shelve`, y `WM_ACTIVATE`/`WM_CLOSE`/`WM_SETTINGCHANGE`/`WM_DPICHANGED`;
  - de `app/main.cpp`: el mutex `Local\PanelSingleInstance`, la ventana de mensajes y el aviso si el atajo está ocupado.
- `PanelState` completo, con valores falsos, y el diseño de la sección 2 pintado de forma estática, incluidas las secciones desplegadas.
- `--render-snapshot` para revisar el diseño en PNG.
- Clic derecho en el fondo abre un menú con «Abrir panel.json» y «Salir», como en la isla.
- **Criterios de aceptación:**
  - `Ctrl+Alt+A` abre y cierra el panel abajo a la derecha del monitor del ratón, sin destellos.
  - Se ve nítido al 125 % y al 175 %: se comprueba con la app delante; la captura es a 96 ppp.
  - Tiempo de apertura por debajo de 50 ms.
  - Con el panel escondido: 0 % de CPU y menos de 20 MB de memoria.

### Fase 2: Controles
- En `ui/controls`:
  - **tile**: estados normal, hover, pulsado (escala 0,97), activo, y foco de teclado;
  - **deslizador**: arrastrar, clic para saltar, rueda, teclado, y un evento `onChange` que también avisa al soltar;
  - **expander**: desplegar y plegar con un muelle interrumpible;
  - **chip de utilidad**.
- Todo sigue con datos falsos, pero ya interactivos.
- **Criterios de aceptación:**
  - Arrastrar va a la frecuencia del monitor.
  - Plegar a mitad de un despliegue no salta.
  - Se puede usar todo solo con el teclado.

### Fase 3a: Volumen (lo que más se usa)
- `system/audio.cpp`, que copia el patrón de la isla:
  - enumerador persistente con `IMMNotificationClient`, filtrado a `eRender` y `eMultimedia`;
  - en `OnDefaultDeviceChanged`, una bandera más `PostMessage`; en el hilo de la interfaz se suelta el endpoint y se vuelve a registrar (`isla/Audio.cs:151-156`, `hud/Volumen.cs:269-273`);
  - si COM falla de verdad, se suelta todo y se reintenta a los 2 s.
- Se muestra el nombre del dispositivo y el silencio se activa con un clic en el icono del altavoz.
- Los cambios que hace el panel llevan el GUID `PANEL_VOLUME_CONTEXT` en `SetMasterVolumeLevelScalar`, y su propio eco se ignora.
- **Criterio de aceptación:** cambias la salida con el panel abierto y el valor y el nombre siguen al dispositivo nuevo. Es la prueba de la medición del HUD.

### Fase 3b: Elegir la salida de audio
- `IPolicyConfig` ya quedó regulada en `SEGURIDAD.md` §2.2 en la fase 0, con sus cortes. Si al implementarla hace falta algo que esa sección no cubre, la enmienda va en su propio commit, antes del código.
- `EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE)` alimenta la lista desplegable.
- Un clic llama a `IPolicyConfig::SetDefaultEndpoint` en los roles `eConsole`, `eMultimedia` y `eCommunications`.
- Si la interfaz no está (`CoCreateInstance` falla), la lista se ve pero no se puede cambiar, y el panel dice por qué.

### Fase 4a: Brillo del portátil
- WMI con una sola conexión `IWbemServices` abierta.
- Se escucha `WmiMonitorBrightnessEvent` para seguir las teclas Fn.
- `WmiSetBrightness` va por `worker`.
- Si no hay panel interno, porque es un sobremesa o la tapa está cerrada, la tarjeta pasa a mostrar el monitor externo.

### Fase 4b: Monitores externos y brillo por pantalla
- `display_ids.cpp`, una función pura con tests, convierte `\\?\DISPLAY#hw#inst#{guid}` en `DISPLAY\hw\inst` y lo compara sin distinguir mayúsculas con el `InstanceName` de WMI sin el `_0`.
- DDC/CI en `worker`:
  - `GetMonitorCapabilities` una vez por monitor al enumerar;
  - al arrastrar, solo se envía el último valor, cada `ddcIntervaloMs` (100 por defecto), y siempre al soltar.
- Los monitores sin DDC salen como «no disponible», no desaparecen.
- Se vuelve a enumerar con `WM_DISPLAYCHANGE`, así que funciona igual con tres pantallas que con una sola.
- **Criterio de aceptación:** en el puesto de tres pantallas, cada deslizador mueve la suya. Fuera de casa, con una sola pantalla, no queda ninguna fila fantasma.

### Fase 5: Wi-Fi y Bluetooth
- `Radio::RequestAccessAsync` una vez, en el primer uso.
- El resultado se prueba de verdad. Si hay un «denegado» o un aviso para una app sin paquete, se anota en CLAUDE.md.
- `SetStateAsync` va por `worker`, y `StateChanged` refresca el estado.
- **SSID:** con `GetConnectedSsid`. Se refresca con `NetworkInformation::NetworkStatusChanged`.
- **Bluetooth:** dos `DeviceWatcher` vivos todo el tiempo, que llevan la cuenta de conectados sin duplicados por dirección.
- Clic derecho en el tile abre `ms-settings:network-wifi` o `ms-settings:bluetooth`. Las URIs ya están en `lanzador/Proveedores.cs:19-38`.
- **Criterios de aceptación:**
  - Encender y apagar se refleja en la bandeja de Windows, y al revés.
  - **No aparece nunca el icono de «ubicación en uso».**

### Fase 6: Luz nocturna (la más frágil, por eso va tarde)
- `nightlight_blob.cpp`, pura, con tests:
  - leer y escribir la envoltura CloudStore (`43 42 01 00`, marca de tiempo varint, `list<int8>`);
  - el estado: si el campo 0 está presente, está encendida;
  - la configuración: campos 0, 10, 20 y 30, las horas y la temperatura.
- Los tests usan como fixture los blobs reales de esta máquina.
- Antes de escribir se valida que el blob leído se reconstruye byte a byte. Si no coincide, **no se escribe** y la tarjeta muestra «No compatible».
- Hay que medir las claves `…perdevice` de 26200: ¿basta con escribir la clave por defecto?
- `RegNotifyChangeKeyValue` va en `worker`.
- El subtítulo es la hora programada (21:00) o «Sí»/«No».
- **Criterio de aceptación:** encenderla desde el panel y desde la bandeja de Windows da el mismo estado en los dos sitios, y un blob raro nunca se sobrescribe.

### Fase 7: Utilidades
- Al abrir el panel se hace una sola instantánea de Toolhelp; no hay sondeo en segundo plano.
- **Arrancar:** `CreateProcessW` de la ruta en la configuración, si existe.
- **Cerrar:** Restart Manager (`RmStartSession`, `RmRegisterResources` y `RmShutdown` sin forzar).
- Antes de dar la fase por buena hay que comprobar que dock, isla, HUD, QuickLook y lanzador se cierran así. Si alguno no, se anota y su chip solo arranca.
- Una utilidad que no está instalada sale atenuada, con el tooltip «No instalada».

### Fase 8: Pulido y entrega
- Medir con la app delante: tiempo de apertura, memoria y CPU con el panel escondido y abierto (`Get-Process`, contadores).
- `autoArranque` por la clave `Run` (`core/autostart.h`).
- Instalador copiado de `calendario/src/installer` más `empaquetar.ps1`.
- Una entrada en `actualizar.ps1` siguiendo el patrón de Agenda (:120-127).
- Filas en el README raíz: «Qué hay» y «Atajos › Globales», con `Ctrl+Alt+A`.
- **Opcional, pero toca otros proyectos:** que el HUD y la isla ignoren los cambios que llevan `PANEL_VOLUME_CONTEXT`. Así, al arrastrar el deslizador del panel no saltan también la cápsula del HUD ni el aviso de la isla. Es una línea en cada `OnNotify`, y no hace falta ninguna interfaz nueva. **Se pregunta antes de tocarlos.**

**Queda fuera:**
- lista de redes Wi-Fi,
- emparejar dispositivos Bluetooth,
- el contenido del menú de Configuración.

Cualquiera de ellos entra como fase propia cuando haga falta.

---

## 5. Verificación de extremo a extremo

1. `cmake --preset debug && cmake --build --preset debug`, lo mismo con release, y `ctest --preset debug`: sin warnings, todo en verde.
2. `powershell -File auditar.ps1` devuelve 0.
3. Enumerar las pantallas, lanzar `Panel.exe --monitor=N` y probar cada tarjeta contra lo que muestra Windows:
   - Win+A para el Wi-Fi, el Bluetooth y la luz nocturna;
   - el mezclador para el volumen;
   - las teclas Fn para el brillo.
4. Cambiar la salida de audio con el panel abierto: el nombre y el nivel siguen al dispositivo nuevo.
5. Rendimiento, con `Get-Process Panel`:
   - con el panel escondido, CPU ≈ 0 y memoria por debajo de 20 MB;
   - al abrirlo, 50 ms o menos.
6. Nitidez en los monitores al 125 % y al 175 % con la app delante. Si no están conectados, queda como pendiente en el resumen.
7. **No se mueve el ratón con arrastres sintéticos**. La interacción se prueba enviando mensajes a la ventana (`WM_LBUTTONDOWN`, `WM_MOUSEMOVE`, `WM_KEYDOWN`) y con tests puros de la conversión posición → valor del deslizador. Lo que depende de la mano se deja para que lo pruebes tú con la app delante.
