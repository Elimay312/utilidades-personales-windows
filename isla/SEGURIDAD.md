# Superficie de riesgo — documento vinculante

Este documento gobierna todo el desarrollo de la isla. Si una función futura necesita algo
prohibido, **la función se rediseña o se descarta**. No se piden excepciones de palabra: se
enmienda este documento por escrito, con su justificación, y queda en el historial de git.

El objetivo es concreto y medible: **que ningún antivirus tenga un motivo razonable para
marcar este binario**, y que alguien que lea el código entienda en diez minutos por qué cada
API que se llama está donde está.

> **Escrito el 19 de septiembre de 2026, antes de la primera línea de código.** Ese es el
> momento en que sirve para algo: decidir qué no vas a hacer mientras todavía no cuesta nada
> renunciar a ello. Se reescribirá entero al llegar al producto mínimo viable, cuando ya se
> sepa qué hacía falta de verdad y qué me inventé.

**Este documento es de la isla y solo de la isla.** El dock tiene el suyo, con reglas
distintas, porque hace cosas distintas. Lo que comparten es el método, no la lista.

---

## 1. El criterio

La isla lee lo que están reproduciendo otras aplicaciones. Eso suena mal dicho así, y merece
una regla más afilada que la del dock —que se apoya en *"lo empieza el usuario con un
gesto"*— porque **aquí no hay gesto**: la isla se entera de que cambiaste de canción porque
el sistema se lo dice, sin que nadie toque nada.

Lo que hace que eso se sostenga son otras tres cosas:

| | Legítimo | Sospechoso |
|---|---|---|
| **De dónde sale el dato** | De un canal que el sistema construyó **para esto**, y en el que cada app entra porque quiere | De rascar la memoria, el disco o la ventana de otro proceso |
| **Cuánto dato es** | Lo que ya se ve en el aviso de medios de Windows y en la pantalla de bloqueo | Todo lo que se pueda sacar, por si acaso |
| **Qué se hace con él** | Se pinta y se tira. No se guarda, no se acumula, no sale del proceso | Un historial, un fichero, una petición de red |

**El dato que la isla lee ya es público dentro de tu sesión.**
`GlobalSystemMediaTransportControlsSessionManager` es el canal que Windows creó para que las
teclas de multimedia del teclado, los auriculares Bluetooth, la pantalla de bloqueo y el
aviso de volumen sepan qué suena y puedan pausarlo. Una app aparece ahí **porque se registró
ella**: publicar el título en ese canal es una decisión de Spotify o de Brave, no algo que
les quitemos. La isla es un consumidor más de una lista que ya existe y que el propio sistema
ya pinta en tres sitios.

De ahí sale el corte que de verdad importa: **la isla no puede enterarse de nada que no esté
ya en esa lista.** No enumera procesos, no mira ventanas ajenas, no lee sus píxeles, no abre
ficheros del perfil. Si una app no publica lo que suena, para la isla no suena nada.

### El caso difícil: el medidor de audio

Hay una función —la onda que late— que necesita saber **cuánto** suena, no **qué** suena. Ahí
la línea hay que dibujarla a mano, porque es el sitio donde este proyecto podría convertirse
en algo que no quiero.

**`IAudioMeterInformation::GetPeakValue` devuelve un `float`.** Un número entre 0 y 1: el
pico de la última pasada del mezclador. No es una muestra, no es un espectro, no es un
fotograma de audio. De un escalar por pasada **no se reconstruye contenido**: no hay palabras
ahí, ni música, ni nada que se pueda volver a escuchar.

Lo que sí sería grabar es `IAudioCaptureClient` con `AUDCLNT_STREAMFLAGS_LOOPBACK`, que
entrega el búfer de audio real. **Eso está prohibido** (regla 11) y está en `auditar.ps1`. La
diferencia entre las dos no es de grado: una da un número y la otra da el sonido.

---

## 2. Lo que este programa no va a hacer, nunca

| # | Prohibido | Por qué |
|---|---|---|
| 1 | Driver de kernel (`.sys`), servicio de Windows, tarea programada, o cualquier componente elevado | Es lo que hace que un antivirus marque a un programa de escritorio. Corre siempre como usuario normal, `requestedExecutionLevel` `asInvoker`. |
| 2 | Leer sensores de hardware: temperaturas, voltajes, RPM, puertos I/O, MSR, SMBus | Requiere driver. No existe forma en modo usuario. La isla no enseña sensores: no es una carencia, es una prohibición. |
| 3 | `SetWindowsHookEx` global y `SetWinEventHook` | Un hook global carga una DLL nuestra dentro de otros procesos, o instala un callback de bajo nivel. *Ese* es el patrón de keylogger. Para el atajo se usa `RegisterHotKey`, que es otra cosa — ver §3.4. |
| 4 | Leer el teclado: `GetAsyncKeyState`, `GetKeyboardState`, `keybd_event`, `SendInput` | Ni siquiera para los controles de reproducción. Lo que la isla necesita del teclado es un atajo registrado, nada más. |
| 5 | `CreateRemoteThread`, `WriteProcessMemory`, `VirtualAllocEx`, o cualquier código dentro de otro proceso | Inyección. Bandera roja inmediata de EDR. |
| 6 | Reemplazo del shell (`Winlogon\Shell`), IFEO, `AppInit_DLLs`, parcheo de binarios | Persistencia de malware por definición. |
| 7 | **Cualquier** llamada de red: sin telemetría, sin updater, sin comprobación de versión, sin carátulas descargadas de internet | 100% offline. Verificable con `netstat` mientras corre. **La carátula sale de la propia sesión de medios**, que ya la trae; no se busca en ningún sitio. |
| 8 | Persistencia oculta | El autoarranque va en `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, sale en la pestaña Inicio del Administrador de tareas, y se pregunta antes de escribirlo. Nada de `schtasks`, servicios ni carpeta Startup. |
| 9 | Ofuscación, packers, compresión del ejecutable, cifrado de cadenas, single-file comprimido | Alta entropía y sin secciones legibles es indistinguible de un dropper. Se compila con PDB y secciones normales. |
| 10 | Descargar o generar código en runtime: `Assembly.Load(byte[])`, `Reflection.Emit`, plugins | Ejecutar código no firmado en runtime es comportamiento de loader. |
| 11 | **Grabar audio.** `IAudioCaptureClient`, `AUDCLNT_STREAMFLAGS_LOOPBACK`, `WasapiLoopbackCapture`, `waveIn*`, cualquier captura de micrófono o de salida | La regla que este proyecto necesitaba y el dock no tenía. Ver §1: lo que sí se lee es un medidor de pico, que es un `float` por pasada. |
| 12 | **Guardar un historial de lo que se reproduce.** Ni a disco, ni en memoria más allá de la canción actual y la anterior | Un registro de qué escuchas y cuándo es un perfil de comportamiento. La isla pinta lo que suena ahora y lo olvida. La única razón de recordar la anterior es la animación de transición. |
| 13 | Leer las notificaciones de otras apps (`UserNotificationListener`) | Exige identidad de paquete y una capability, así que hoy ni se puede. Y si algún día se pudiera, sigue prohibido: el contenido de tus notificaciones no es asunto de una pastilla decorativa. |
| 14 | Portapapeles, credenciales, navegadores, documentos | La isla no tiene ningún motivo para tocarlos, y por eso la prohibición es gratis. |
| 15 | Tocar ventanas ajenas: moverlas, cerrarlas, minimizarlas, leer sus píxeles, enumerarlas | Eso es trabajo del dock y del gestor de ventanas, cada uno con sus reglas. La isla **flota y no toca nada**. La única ventana que mira es la que está en primer plano, y solo para saber si está a pantalla completa — ver §3.5. |
| 16 | Matar procesos: `TerminateProcess`, `TerminateThread`, `EndTask`, `ExitWindowsEx` | Pierde datos sin preguntar. La isla no cierra nada de nadie. |

---

## 3. Lo que sí se hace, y por qué se sostiene

### 3.1 Leer qué se está reproduciendo

`GlobalSystemMediaTransportControlsSessionManager.RequestAsync()`, y de cada sesión:
`TryGetMediaPropertiesAsync` (título, artista, álbum, carátula), `GetPlaybackInfo` (estado y
qué controles admite) y `GetTimelineProperties` (posición y duración).

**Por qué se sostiene**, en orden de peso:

1. **Es el canal que Windows construyó para esto.** Lo consumen las teclas de multimedia del
   teclado, los auriculares Bluetooth, la pantalla de bloqueo y el aviso de medios del propio
   sistema. La isla hace lo mismo que ya hace el hardware que tienes conectado.
2. **La app entra en esa lista porque quiere.** Publicar el título ahí es una decisión de
   Spotify o de Brave. No se lo quitamos de ningún sitio.
3. **Es una API pública, documentada y sin capability.** Verificado antes de escribir este
   documento: funciona desde un `.exe` suelto, sin identidad de paquete.
4. **No se guarda nada** (regla 12) y **no hay red** (regla 7).

**Los cortes:**

- Solo lo que la lista da. No se resuelve el proceso dueño, no se busca su ventana, no se
  mira su ejecutable. El nombre de la app se pinta tal cual llega (`SourceAppUserModelId`).
- La carátula es la que trae la sesión. **No se busca ninguna en internet**, ni en disco.
- Ni la lista de sesiones ni ninguna propiedad se escriben en `isla.json`, en el log, ni en
  ningún fichero.

### 3.2 Controlar la reproducción

`TryPlayAsync`, `TryPauseAsync`, `TrySkipNextAsync`, `TrySkipPreviousAsync`,
`TryChangePlaybackPositionAsync`, sobre la sesión que la isla está enseñando.

**Es exactamente lo que hace la tecla de play de tu teclado.** Misma API, mismo alcance, y
siempre detrás de un clic tuyo en un botón que estás viendo. No hay ningún camino que llame a
esto desde un temporizador, y así debe seguir.

Un límite que pone la propia API y conviene tener escrito: `GetPlaybackInfo().Controls` dice
qué admite cada sesión. **Un botón que la sesión no admite no se dibuja**, en vez de dibujarlo
y que no haga nada.

### 3.3 Leer el medidor de pico de la salida

`IMMDeviceEnumerator::GetDefaultAudioEndpoint(eRender, eMultimedia)` →
`IMMDevice::Activate(IAudioMeterInformation)` → `GetPeakValue()`.

Ya está justificado en §1 y es la parte de este documento que más merece leerse dos veces:
**un pico es un número, no es audio.** Se lee a 20 Hz, solo mientras hay reproducción activa,
y alimenta una animación. No se guarda, no se acumula, no se promedia en el tiempo.

El dispositivo es el **de salida** (`eRender`). El micrófono (`eCapture`) no se abre nunca, y
`auditar.ps1` lo comprueba.

### 3.4 Un atajo de teclado

`RegisterHotKey(hwnd, id, MOD_CONTROL | MOD_ALT, 'I')`.

**No es un hook.** Registra una combinación concreta para **nuestra propia ventana**: el
sistema manda `WM_HOTKEY` a nuestro `WndProc` cuando se pulsa esa combinación y solo esa. No
carga nada en ningún proceso, no ve ninguna otra tecla, y no puede verlas. Es lo contrario de
`SetWindowsHookEx`, que está prohibido en la regla 3.

Si `RegisterHotKey` falla porque otro programa ya tiene esa combinación, **se dice en la
consola**. Un atajo global que falla en silencio es media hora perdida.

### 3.5 Saber si hay algo a pantalla completa

`GetForegroundWindow` + `GetWindowLongPtr(GWL_STYLE)` + `GetWindowRect`, comparado contra
`GetMonitorInfo`. Sirve para apartarse cuando hay un juego o un vídeo.

**Se lee una sola ventana: la que está en primer plano.** De ella, la geometría, la clase y
el estilo. Si esa ventana es una `CoreWindow`, también el estilo de su marco raíz y el
nombre del exe (`OpenProcess` con `PROCESS_QUERY_LIMITED_INFORMATION` y
`QueryFullProcessImageName`): hace falta para no confundir un juego con el escritorio
(`Progman`) ni con `TextInputHost`, que cubren el monitor en cuanto la barra de tareas se
oculta. No se enumera nada, no se toca nada, no se mira el contenido.

Es la misma comprobación que hace el dock, y por la misma razón medida allí:
`SHQueryUserNotificationState` devuelve `BUSY` de forma transitoria después de cualquier
minimizado, y eso ya provocó un fallo real en el vecino.

### 3.6 Autoarranque

`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, y nada más. Sale en la pestaña Inicio
del Administrador de tareas, se puede quitar desde ahí, y **se pregunta antes de escribirlo**.

---

## 4. Descartado, y por qué

| Se quería | Por qué no está |
|---|---|
| Notificaciones de otras apps en la isla | `UserNotificationListener` exige identidad de paquete. Y aunque no la exigiera, regla 13 |
| Progreso de descargas del navegador | **No existe API.** `ITaskbarList3` es de solo escritura: no hay ningún `Get*` en las cuatro versiones de la interfaz, y el estado vive dentro de `explorer.exe` sin exponerse. Medido en el proyecto del dock |
| Ocultar el aviso de volumen de Windows para no tener dos | No hay forma documentada. Las que circulan pasan por tocar `explorer.exe`, que es la regla 5 y la 6. Se resuelve por diseño: el volumen solo se refleja si la isla ya está abierta |
| Espectro de audio de verdad (FFT sobre el sonido) | Necesita captura de loopback, que es la regla 11. La onda se hace con el medidor de pico, que es un número |
| Próxima reunión, correo, clima | Necesitarían red o cuentas. Regla 7 |
| Cuánto tiempo pasas en cada app | Es un perfil de comportamiento. Regla 12, en espíritu |

---

## 5. Corolarios de diseño

Cosas que el código hace de una forma concreta **porque este documento existe**, no por gusto:

- **La isla no tiene inventario.** No hay ninguna estructura que guarde qué apps hay, qué
  ventanas hay ni qué se reprodujo antes. Solo existe la sesión actual y la anterior, y la
  anterior solo mientras dura una animación.
- **Nada de lo que la isla lee se escribe a disco.** `isla.json` guarda tus ajustes: qué
  monitor, qué atajo, qué avisos quieres. Nunca contenido.
- **El único P/Invoke que toca audio devuelve un `float`**, y va solo en su grupo dentro de
  `NativeMethods.txt` con el porqué encima. Es el sitio donde mira un auditor.
- **La isla no se registra como AppBar.** Flota y no reserva hueco. No es una regla de
  seguridad, pero sale del mismo criterio: no cambiar nada del escritorio de nadie si se
  puede evitar.
- **`NativeMethods.txt` es la lista cerrada de P/Invokes.** Si no está ahí, no se genera y no
  compila. Cada grupo lleva encima un comentario que dice para qué es.

---

## 6. Cómo se audita

```powershell
pwsh -File auditar.ps1        # tiene que decir TODO LIMPIO y salir con 0
```

Mira **solo código**, saltando comentarios y bloques `/* */`. Hace falta: este documento
nombra todas las APIs prohibidas para explicar por qué lo están, y los fuentes llevan
comentarios del estilo *"el loopback no está aquí y no va a estar"*. Un `grep` a secas se
encuentra a sí mismo y la auditoría nunca sale limpia, con lo que deja de servir de puerta.

A mano, las tres que un script no puede comprobar:

```powershell
# Sin red: con la isla corriendo, no debe aparecer ninguna conexion suya.
Get-NetTCPConnection | Where-Object { $_.OwningProcess -eq (Get-Process Isla).Id }

# Sin microfono: Configuracion > Privacidad > Microfono, lista de apps recientes.
# La isla no debe aparecer nunca.

# Y un escaneo de Defender sobre el binario publicado.
```

Y entrada por entrada: cada línea de `NativeMethods.txt` tiene que poder señalarse a una
sección de §3. Si una no se puede, o sobra la entrada o falta una sección.

---

## 7. Cómo se enmienda

1. Se edita **este fichero** antes de escribir el código, explicando qué se abre, **con qué
   cortes**, y por qué sigue siendo defendible.
2. Se añade la regla nueva a `auditar.ps1` si la enmienda cierra algo, o se relaja la
   existente si abre algo.
3. Va en su propio commit, **antes** del commit que usa la API.

Nunca al revés. Un documento que se actualiza después de escribir el código no es una regla,
es un parte de daños.
