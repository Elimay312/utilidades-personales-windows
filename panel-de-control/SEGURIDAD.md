# Seguridad del Panel

Este documento es del Panel y solo del Panel. No es una norma de la casa: el HUD prohíbe
`IPolicyConfig` y WMI, y aquí las dos cosas se abren, porque este programa hace otra cosa.

**Qué hace el Panel.** Ninguna otra utilidad de la carpeta toca tantos **ajustes de toda la
sesión** como este:

- enciende y apaga radios;
- cambia la salida de audio predeterminada;
- escribe el estado de la luz nocturna en el registro;
- cambia el brillo de pantallas físicas;
- arranca y cierra otros programas.

No maneja credenciales ni habla con nadie, así que las reglas no van sobre secretos. Van sobre
dos cosas:

1. **Dejar el equipo como estaba si algo no se entiende.**
2. **No abrir más puertas que las del botón que pulsaste.**

Está escrito en la fase 0, antes de la primera línea de código. Renunciar ahora a algo que no
existe es gratis. Se rediseñará al llegar al producto mínimo, que es cuando se sabrá qué de
esto hacía falta.

---

## 1. Lo que el Panel no va a hacer, nunca

### 1.1 Hablar con la red

Ni WinHTTP, ni WinINet, ni sockets, ni descargas, ni comprobación de actualizaciones. Tampoco
telemetría, analítica ni informes de fallo remotos. `NetworkInformation` se usa para
**preguntar** a Windows a qué red está conectado el equipo, que es un dato local. El panel no
hace ninguna petición.

### 1.2 Pedir la ubicación

Desde Windows 11 24H2, leer el SSID con `WlanQueryInterface`
(`wlan_intf_opcode_current_connection`), listar redes o escanear exige permiso de ubicación. El
primer uso lanza un aviso, y desde entonces la app sale en el icono de «ubicación en uso».

Un panel que se abre veinte veces al día no puede aparecer ahí. Por eso:

- **Prohibido:** `WlanQueryInterface`, `WlanGetAvailableNetworkList`,
  `WlanGetNetworkBssList`, `WlanScan` y cualquier API de `Windows.Devices.Geolocation`.
- **Cómo se lee el SSID:** con `WlanConnectionProfileDetails::GetConnectedSsid()`, que
  devuelve solo el nombre y no el BSSID. Por eso Windows no lo trata como ubicación (Chromium
  hizo el mismo cambio).
- **Si algún día hace falta la lista de redes:** eso es la ubicación, y va con su propia
  enmienda.

### 1.3 Pedir administrador

El manifiesto pide `asInvoker`. Nada de lo que hace el panel necesita elevarse, y si algún
día pareciera que sí, la respuesta es no hacerlo.

Tampoco se instala como servicio ni deja procesos detrás al cerrarse.

### 1.4 Escuchar el teclado

Sin `SetWindowsHookEx` y sin Raw Input. El atajo es `RegisterHotKey` y nada más.

Por eso no se puede quitar `Win+A` a Windows. Hacerlo exigiría un gancho de teclado de bajo
nivel, que ve **todo lo que se escribe** en la sesión. Por abrir un panel, no.

### 1.5 Matar procesos

Sin `TerminateProcess` y sin `RmShutdown` con `RmForceShutdown`. Cerrar una utilidad es
**pedírselo**, con el Restart Manager. Si no se cierra, el panel lo dice y no insiste. Un
cierre forzado se lleva lo que la app no haya guardado, y deja un icono muerto en la bandeja.

### 1.6 Ejecutar texto

Sin `system`, `_wsystem`, `popen`, `WinExec` ni intérpretes.

- **`ShellExecuteW`** se usa solo con URIs `ms-settings:` escritas en el código o con el propio
  `panel.json`. Nunca con una cadena que venga de la configuración.
- **`CreateProcessW`** se usa solo en `system/apps.cpp`, y solo con una ruta de `utilidades[]`
  que exista, sea absoluta y termine en `.exe`. Se pasa como `lpApplicationName`, sin línea
  de comandos compuesta.

### 1.7 Tocar el registro fuera de dos sitios

Solo se **escribe** en estos:

1. El valor `Panel` de `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, para el
   autoarranque. Lo escriben `core/autostart.h` y el instalador. El instalador también escribe
   su propia clave `Uninstall`.
2. El valor `Data` del **estado** de la luz nocturna (§2.5), desde `system/nightlight.cpp`.
   El de su horario se lee, pero no se escribe.

Nada en `HKEY_LOCAL_MACHINE`. Leer es libre.

### 1.8 Traer dependencias sin fijar

Las dependencias son dos, nlohmann/json y doctest, y entran por `FetchContent` **con
`URL_HASH SHA256`**. Nada apunta a una rama ni a una etiqueta que se pueda mover.

- **Sin vcpkg** y sin registros de terceros.
- **C++/WinRT sale del Windows SDK**, así que no es una descarga.
- Una dependencia nueva se consulta antes.

---

## 2. Lo que sí se toca, y con qué cortes

### 2.1 Volumen, silencio y nombre de la salida

Core Audio. Es la misma superficie que ya usan el HUD y la isla:

- `IAudioEndpointVolume` sobre `eRender`/`eMultimedia`, e `IMMNotificationClient` sobre el
  enumerador;
- `PKEY_Device_DeviceDesc` para el nombre de la salida («Auriculares»), con
  `PKEY_Device_FriendlyName` («Auriculares (Realtek Audio)») de respaldo si el primero viene
  vacío. *Enmienda de la fase 3a:* el corto es el que cabe en la cabecera de la tarjeta. Las
  dos propiedades se leen del almacén del dispositivo en modo `STGM_READ` y nunca se escriben.

**Cortes:**

- Solo el volumen maestro. Nada por aplicación.
- Solo se escribe por una acción del usuario: arrastrar, la rueda, una tecla o un clic en el
  altavoz. Nunca desde un temporizador.
- Cada escritura lleva el GUID de contexto propio del panel. Con él, el panel ignora su propio
  eco, y algún día el HUD y la isla podrán ignorarlo también.
- El endpoint se suelta y se vuelve a pedir en `OnDefaultDeviceChanged`. El abierto no falla:
  sigue contestando con el dispositivo anterior (medido en `hud/CHANGELOG.md`).

### 2.2 Cambiar la salida predeterminada: `IPolicyConfig`

**Es la única API no documentada del proyecto, aparte del blob de la luz nocturna.**

- **Por qué se abre:** elegir la salida es una de las tres cosas que el usuario pidió, y no
  existe ninguna alternativa documentada.
- **Por qué se sostiene:**
  - su IID ha sido estable desde Windows 10 RS1 hasta hoy;
  - la usan EarTrumpet (activo en julio de 2026) y SoundSwitch (7.3.3, septiembre de 2026),
    que es donde se nota primero si Windows la cambia;
  - no necesita elevación.

**Cortes:**

- **Vive en un solo archivo:** `system/policy_config.h`. Es el único sitio con la definición
  del interfaz, su CLSID y su IID. La auditoría busca `IPolicyConfig` y esos GUID en el resto
  del código.
- **Solo se llama por un clic** en una fila de la lista de salidas, y solo con un ID que acaba
  de devolver `EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE)`.
- **Se llama con los tres roles** (`eConsole`, `eMultimedia`, `eCommunications`), que es lo
  que hace Windows desde su propio panel.
- **Solo la salida predeterminada.** El interfaz de rutas por aplicación
  (`IAudioPolicyConfigFactory`) no se usa: cambió de IID en 21H2 y hacerlo bien es otro
  proyecto.
- **Si `CoCreateInstance` falla,** la lista se enseña pero no deja cambiar nada, y el panel
  dice por qué. No se prueban IIDs alternativos a ver si alguno cuela.

### 2.3 Brillo del portátil: WMI

**Qué se usa:**

- `IWbemServices` en el espacio de nombres `ROOT\WMI`;
- las clases `WmiMonitorBrightness` (leer), `WmiMonitorBrightnessMethods`
  (`WmiSetBrightness`) y `WmiMonitorBrightnessEvent` (seguir las teclas Fn).

**Cortes:**

- **Ningún otro espacio de nombres ni otra clase:** nada de `root\cimv2` ni de `Win32_*`. WMI
  sabe hacer casi cualquier cosa en el equipo, así que la puerta se abre para una clase
  concreta y no para WMI entero.
- **Una sola conexión** abierta mientras vive el proceso, y las escrituras van por el hilo de
  trabajo.

### 2.4 Brillo de monitores externos: DDC/CI

Se usa `dxva2`: `GetPhysicalMonitorsFromHMONITOR`, `GetMonitorCapabilities` y
`Get/SetMonitorBrightness`. Es hablar por el cable con el firmware del monitor.

**Cortes:**

- **Solo el brillo.** Nada de `SetVCPFeature` ni de otros códigos VCP: contraste, entrada o
  apagado se quedan como están. Un código VCP equivocado puede dejar un monitor en un estado
  que solo se arregla con su menú.
- **No se escribe en ráfaga.** Muchos monitores guardan cada valor en su memoria no volátil,
  que tiene ciclos contados. Al arrastrar se escribe como mucho una vez cada
  `ddcIntervaloMs` (100 por defecto), solo el último valor, y siempre una vez al soltar.
- **Un monitor que no anuncia `MC_CAPS_BRIGHTNESS`** sale como «no disponible». No se le
  prueba nada.
- **Todo en el hilo de trabajo.** Cada llamada tarda entre 50 y 200 ms.
- **Los manejadores se sueltan** con `DestroyPhysicalMonitors` al volver a enumerar.

### 2.5 Luz nocturna: el blob de CloudStore

**No hay API pública.** El estado vive en dos valores `Data` (`REG_BINARY`) bajo
`HKCU\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\DefaultAccount\Current\`:

- `default$windows.data.bluelightreduction.bluelightreductionstate\...`, que dice si está
  encendida;
- `default$windows.data.bluelightreduction.settings\...`, con el horario y la temperatura.

El formato es Bond CompactBinary dentro de un sobre de CloudStore. El explorador vigila la
clave y aplica los cambios.

- **Por qué se abre:** es uno de los cuatro tiles del diseño, y es lo que hacen todas las
  herramientas que existen, incluido `kvnxiao/win-nightlight-cli` (MIT, probado en 26100).
- **Por qué es la parte más peligrosa del proyecto:** un blob mal escrito puede borrar la
  configuración de la luz nocturna del usuario. Y CloudStore puede sincronizarse con la
  copia de seguridad de Windows.

**Cortes, todos a la vez:**

1. **Solo se escribe el campo de encendido.** El horario y la temperatura se leen para el
   subtítulo, pero no se escriben. El valor `settings` no se escribe nunca.
2. **Ida y vuelta antes de escribir.** El blob que se lee se decodifica y se vuelve a
   codificar. Si no sale **byte a byte igual**, el panel no lo entiende: no escribe, y el tile
   dice «No compatible».
3. **Copia de seguridad.** Antes de la primera escritura se guarda el valor original en
   `%LOCALAPPDATA%\Panel\luz-nocturna.bak`, una sola vez: no se machaca con copias
   posteriores.
4. **Las claves `…perdevice`** que aparecen en Windows 11 26200 se **leen** en la fase 6
   para saber si mandan sobre la de por defecto. No se escriben sin una enmienda que diga qué
   se midió.
5. **Todo el código que escribe está en `system/nightlight.cpp`.** El codificador, en
   `system/nightlight_blob.cpp`, es una función pura con tests sobre blobs reales.

### 2.6 Wi-Fi y Bluetooth: `Windows.Devices.Radios`

Se usan `Radio::RequestAccessAsync` una vez y `SetStateAsync`; `StateChanged` avisa de los
cambios. Es la API documentada, la misma que usa el panel de Windows.

**Cortes:**

- **Solo encender y apagar** la radio de ese tipo.
- **Sin emparejar, sin conectar y sin desconectar dispositivos.**
- **Los `DeviceWatcher` solo leen el estado** de conexión de lo que ya está emparejado.

### 2.7 Arrancar y cerrar utilidades

**Qué se hace:**

- **Qué está corriendo:** una instantánea de Toolhelp cada vez que se abre el panel, sin
  sondeo en segundo plano.
- **Arrancar:** `CreateProcessW`, con los cortes de §1.6.
- **Cerrar:** Restart Manager (`RmStartSession`, `RmRegisterResources`, `RmShutdown` sin
  forzar).

**Cortes:**

- **Solo lo que está en `utilidades[]`**, cuyas rutas por defecto son las de instalación que
  conoce `actualizar.ps1`.
- **Solo se cierra un proceso cuya ruta completa** (`QueryFullProcessImageNameW`) coincide con
  la configurada. Si otro programa se llama igual, no se toca.

### 2.8 Dibujar

Direct2D, DirectWrite y DirectComposition sobre la ventana propia, como en Agenda. El panel no
lee ni captura la pantalla ni las ventanas de otros.

---

## 3. Cómo se audita

```
powershell -NoProfile -ExecutionPolicy Bypass -File auditar.ps1
```

**Qué hace el script:**

- Quita los comentarios de C++ y de CMake, conservando las cadenas. Si no, se denunciaría a sí
  mismo, porque este documento y los comentarios que lo citan están llenos de las palabras
  prohibidas.
- Comprueba cada regla de las §1 y §2 que se puede comprobar leyendo el código.
- Devuelve 0 si todo está limpio y 1 si algo falla.

**Estado de cada regla:**

- `bien`: se cumple.
- `FALLA`: no se cumple.
- `pendiente`: la regla existe pero todavía no hay código que vigilar. Marcarla `bien`
  entonces sería mentir en la tabla.

**Qué no puede ver:** si el usuario pulsó el botón, o si un monitor guarda el brillo en su
memoria no volátil. Eso lo garantizan la estructura del código (§2) y las pruebas con la app
delante.

## 4. Cómo se enmienda

1. Se edita **este archivo** antes de escribir el código. Hay que explicar:
   - qué se abre;
   - con qué cortes;
   - por qué sigue siendo defendible.
2. Se añade la regla a `auditar.ps1` si la enmienda cierra algo, o se relaja la que hay si
   abre algo.
3. Va en su propio commit, **antes** del commit que usa la API.

Nunca al revés. Un documento que se actualiza después del código no es una regla, es un parte
de daños.
