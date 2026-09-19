# Superficie de riesgo — documento vinculante

Este documento gobierna todo el desarrollo del HUD. Si una función futura necesita algo
prohibido, **la función se rediseña o se descarta**. No se piden excepciones de palabra: se
enmienda este documento por escrito, con su justificación, y queda en el historial de git.

El objetivo es el mismo que en el dock y en la isla: **que ningún antivirus tenga un motivo
razonable para marcar este binario**, y que alguien que lea el código entienda en diez minutos
por qué cada API que se llama está donde está.

> **Escrito el 19 de septiembre de 2026, antes de la primera línea de código.** Ese es el
> momento en que sirve para algo: decidir qué no vas a hacer mientras todavía no cuesta nada
> renunciar a ello.

**Este documento es del HUD y solo del HUD.** El dock y la isla tienen los suyos, con reglas
distintas, porque hacen cosas distintas. Lo que comparten es el método, no la lista. Y este
proyecto tiene **una regla más abierta** que sus vecinos, la 15, por un motivo que ocupa la §1
entera.

---

## 1. El criterio, y la regla que este proyecto tuvo que abrir

El HUD hace cuatro cosas: **escucha** las teclas de volumen, **cambia** el volumen, **dibuja**
un indicador, y **aparta el aviso que Windows dibuja encima del nuestro**. Las tres primeras
son triviales de defender. La cuarta no lo es, y es la razón de ser de este apartado.

### El problema

Si el HUD no oculta el flyout nativo, salen dos indicadores a la vez y el proyecto no sirve
para nada. Se puede vivir con eso, pero entonces esto no es un reemplazo: es un adorno. La
isla lo resolvió por diseño —reflejaba el volumen solo si ya estaba abierta— y su
`SEGURIDAD.md` §4 llegó a anotar que ocultarlo *"no tiene forma documentada; las que circulan
pasan por tocar `explorer.exe`"*. **Esa anotación sigue siendo correcta sobre los métodos que
describe, y ninguno de ellos es el que se usa aquí.**

### Lo que NO se hace

Las tres familias de soluciones que circulan por internet, y por qué las tres están prohibidas
aquí igual que en los otros dos proyectos:

| Método que circula | Por qué no |
|---|---|
| Parchear o reemplazar componentes de `explorer.exe` / `ShellExperienceHost.exe` | Reglas 5 y 6. Inyección y reemplazo del shell. Es la definición de malware |
| Matar `ShellExperienceHost.exe` en un bucle | Regla 16. Además rompe el centro de notificaciones del usuario |
| `DWMWA_CLOAK` sobre la ventana ajena, o `SetWindowsHookEx` para adelantarse a que se muestre | Reglas 3 y 15. El cloaking de ventanas de otros y los hooks globales siguen vetados |

### Lo que sí se hace, y el corte exacto

**Una sola operación, sobre una sola ventana, y de las que cualquier gestor de ventanas hace
cien veces al día:** localizar el host del flyout por su clase de ventana y moverlo fuera del
área visible con `SetWindowPos`.

Lo que hace que se sostenga:

1. **No se entra en el proceso ajeno.** Ni DLL, ni hilo remoto, ni memoria, ni handle de
   proceso. `SetWindowPos` es una petición al gestor de ventanas del sistema, no un acceso al
   programa dueño. Es la misma API con la que el HUD se coloca a sí mismo.
2. **No se lee nada.** No se capturan sus píxeles, no se lee su texto, no se mira su contenido.
   Mover no es observar, y esa distinción es la que separa esto de la regla 15 original.
3. **No se destruye nada.** La ventana sigue existiendo, viva y funcionando. Se aparta. Al
   cerrar el HUD **se devuelve a su sitio** (§3.5), y si el HUD muere de golpe, el propio
   `ShellExperienceHost` la recoloca la próxima vez que la muestra.
4. **Es una ventana, no cualquier ventana.** El criterio de búsqueda es cerrado y literal, y se
   fija **midiendo en la máquina**, no copiando de un foro. Si no encuentra exactamente esa, no
   toca nada: no hay ningún camino que mueva una ventana no identificada.
5. **Se pregunta.** `ocultarFlyoutNativo` existe en `hud.json` y el usuario lo pone en `false`
   cuando quiera. Un programa que aparta una ventana del sistema sin decirlo es otra cosa.

### Los cortes, escritos para que `auditar.ps1` los pueda comprobar

- **Vive en un solo fichero: `FlyoutNativo.cs`.** Si `FindWindowW` o `FindWindowExW` aparecen
  en cualquier otro `.cs`, la auditoría falla. Un auditor tiene que poder leer un fichero y
  saberlo todo sobre esta excepción.
- **`EnumWindows` y `EnumChildWindows` siguen prohibidos.** No se recorre el escritorio de
  nadie: se pregunta por una clase concreta. Barrer la lista de ventanas para ver qué hay es
  exactamente lo que la regla 15 quería impedir, y sigue impedido.
- **`ShowWindowAsync`, `DWMWA_CLOAK`, `SetForegroundWindow`, `AttachThreadInput`, `PrintWindow`
  y `DestroyWindow` sobre ventana ajena: siguen prohibidos.** De todo lo que se puede hacer con
  un HWND que no es tuyo, aquí se permite **uno**: `SetWindowPos` para moverlo.
- **No se abre el proceso dueño.** `OpenProcess`, `GetWindowThreadProcessId` y
  `QueryFullProcessImageName` no entran en `NativeMethods.txt`. Ni siquiera se comprueba de
  quién es la ventana: para eso habría que mirar dentro de otro proceso, y la clase de ventana
  ya identifica lo que buscamos sin salir del gestor de ventanas.

**Esta es la única excepción del documento y ocupa su §1 a propósito.** Si algún día hace falta
una segunda, se escribe aquí con el mismo nivel de detalle o no se hace.

---

## 2. Lo que este programa no va a hacer, nunca

| # | Prohibido | Por qué |
|---|---|---|
| 1 | Driver de kernel (`.sys`), servicio de Windows, tarea programada, o cualquier componente elevado | Es lo que hace que un antivirus marque a un programa de escritorio. Corre siempre como usuario normal, `requestedExecutionLevel` `asInvoker`. Y el brillo **no lo necesita**: WMI lo deja leer al usuario de la sesión interactiva |
| 2 | Leer sensores de hardware: temperaturas, voltajes, RPM, puertos I/O, MSR, SMBus | Requiere driver. No existe forma en modo usuario |
| 3 | `SetWindowsHookEx` global y `SetWinEventHook` | Un hook global carga una DLL nuestra dentro de otros procesos, o instala un callback de bajo nivel. *Ese* es el patrón de keylogger. Para las teclas de volumen se usa `RegisterHotKey`, que es otra cosa — ver §3.1 |
| 4 | Leer el teclado: `GetAsyncKeyState`, `GetKeyboardState`, `keybd_event`, `SendInput`, `WH_KEYBOARD` | **La prohibición que más caro salió.** Un HUD de volumen quiere saber cuándo pulsas una tecla, y la forma fácil es un hook de bajo nivel. La forma fácil está prohibida: se registran tres teclas concretas y nada más |
| 5 | `CreateRemoteThread`, `WriteProcessMemory`, `VirtualAllocEx`, o cualquier código dentro de otro proceso | Inyección. Bandera roja inmediata de EDR |
| 6 | Reemplazo del shell (`Winlogon\Shell`), IFEO, `AppInit_DLLs`, parcheo de binarios | Persistencia de malware por definición |
| 7 | **Cualquier** llamada de red: sin telemetría, sin updater, sin comprobación de versión | 100% offline. Verificable con `netstat` mientras corre |
| 8 | Persistencia oculta | El autoarranque va en `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, sale en la pestaña Inicio del Administrador de tareas, y se pregunta antes de escribirlo. Nada de `schtasks`, servicios ni carpeta Startup |
| 9 | Ofuscación, packers, compresión del ejecutable, single-file comprimido | Alta entropía y sin secciones legibles es indistinguible de un dropper. Se compila con PDB y secciones normales |
| 10 | Descargar o generar código en runtime: `Assembly.Load(byte[])`, `Reflection.Emit`, plugins | Ejecutar código no firmado en runtime es comportamiento de loader |
| 11 | **Grabar audio.** `IAudioCaptureClient`, `AUDCLNT_STREAMFLAGS_LOOPBACK`, `waveIn*`, `eCapture`, cualquier captura | Heredada de la isla y aquí es todavía más gratis: el HUD necesita **un número**, el nivel del volumen maestro. No abre ningún flujo de audio |
| 12 | Guardar historial a disco | `hud.json` guarda ajustes. Nada de qué volumen tuviste a qué hora: es un perfil de comportamiento barato de construir y sin ninguna razón para existir |
| 13 | Leer las notificaciones de otras apps (`UserNotificationListener`) | El HUD no tiene ningún motivo para tocarlas |
| 14 | Portapapeles, credenciales, navegadores, documentos | Prohibición gratis: no hay ninguna función que los quiera |
| 15 | Tocar ventanas ajenas — **con la única excepción de §1** | Se permite mover el host del flyout nativo con `SetWindowPos`, desde `FlyoutNativo.cs` y solo desde ahí. **Enumerarlas, leer sus píxeles, cerrarlas, minimizarlas, ocultarlas con cloaking o robarles el foco sigue prohibido** |
| 16 | Matar procesos: `TerminateProcess`, `TerminateThread`, `EndTask`, `ExitWindowsEx` | Pierde datos sin preguntar. Y es la tentación evidente de este proyecto: matar `ShellExperienceHost` haría desaparecer el flyout. También rompería el centro de notificaciones |

---

## 3. Lo que sí se hace, y por qué se sostiene

### 3.1 Tres teclas de volumen

`RegisterHotKey(hwnd, id, 0, VK_VOLUME_UP | VK_VOLUME_DOWN | VK_VOLUME_MUTE)`.

**No es un hook.** Se le pide a Windows que mande `WM_HOTKEY` a **nuestra propia ventana**
cuando se pulse una de esas tres teclas. No carga nada en ningún proceso, no ve ninguna otra
tecla, y **no puede verlas**: el sistema entrega el mensaje ya filtrado. Es lo contrario de
`SetWindowsHookEx`, que está prohibido en la regla 3, y es lo mismo que hacen el dock
(Ctrl+Alt+D) y la isla (Ctrl+Alt+I).

**Consecuencia que hay que tener escrita:** `RegisterHotKey` **consume** la tecla, así que
Windows ya no cambia el volumen — lo cambia el HUD (§3.2). Eso no es un efecto secundario que
haya que disimular, es el diseño: es lo que permite tener un paso propio.

Si el registro falla porque otro programa ya tiene esas teclas, **se dice en la consola y el
HUD sigue arrancando** en modo solo-observación. Un atajo global que falla en silencio es media
hora perdida.

### 3.2 Leer y cambiar el volumen maestro

`IMMDeviceEnumerator::GetDefaultAudioEndpoint(eRender, eMultimedia)` →
`IMMDevice::Activate(IAudioEndpointVolume)` → `GetMasterVolumeLevelScalar`,
`SetMasterVolumeLevelScalar`, `GetMute`, `SetMute`.

Es la API pública del mezclador de Windows, la misma que usa el control de volumen de la barra
de tareas. **Es la salida** (`eRender`); el micrófono (`eCapture`) no se abre nunca, y
`auditar.ps1` lo comprueba. **Un escalar de 0 a 1 no es audio** — la distinción está razonada
en `..\isla\SEGURIDAD.md` §1 y aquí se hereda entera.

A diferencia de la isla, aquí sí se **escribe**. Se sostiene porque siempre viene de una tecla
que acabas de pulsar: no hay ningún camino que llame a `SetMasterVolumeLevelScalar` desde un
temporizador, y así debe seguir.

### 3.3 Leer el brillo de la pantalla interna

WMI, espacio `root\WMI`: `WmiMonitorBrightness` para el nivel actual y
`WmiMonitorBrightnessEvent` para enterarse de que has pulsado Fn+brillo.

**Solo se lee.** El HUD no cambia el brillo: las teclas de brillo van por ACPI y Windows ya lo
cambia solo, así que `WmiSetBrightness` no aporta nada y **no entra en el código**. Si algún
día se quiere un atajo propio de brillo, se añade esa llamada y se enmienda este párrafo.

**No hace falta elevación** y no se pide: se consulta el espacio WMI de la sesión interactiva
como usuario normal. Si en alguna máquina la suscripción de eventos fuese denegada, la función
se degrada a sondeo; **no se eleva el proceso** (regla 1).

Este es el motivo de la segunda dependencia del proyecto, `System.Management`. Está anotado en
`Hud.csproj` y en `CLAUDE.md`.

### 3.4 Dibujar

`Windows.UI.Composition` del sistema sobre un `DesktopWindowTarget` de **nuestro propio HWND**,
y la cadena D3D11 → D2D1 → `CreateDrawingSurface` para los glifos. Actúa solo sobre nuestras
superficies: **no lee la pantalla ni la ventana de nadie**, y en particular no hay ninguna
captura de pantalla en este proyecto.

El fondo acrílico es `CreateHostBackdropBrush()`, que desenfoca lo que hay detrás. Conviene
tenerlo escrito porque suena a leer la pantalla y no lo es: **el desenfoque lo hace DWM y el
resultado nunca vuelve a nuestro proceso.** No hay ningún píxel de nadie en nuestra memoria.

### 3.5 Apartar el flyout nativo

Justificado entero en §1. Los detalles operativos:

- Búsqueda por clase de ventana, criterio literal y cerrado, fijado midiendo en la máquina.
- `SetWindowPos` con `SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER` a una coordenada fuera de
  todos los monitores.
- **Se restaura al salir.** El HUD guarda la posición original y la devuelve en el cierre
  limpio. Si el proceso muere de golpe no pasa nada: `ShellExperienceHost` la recoloca solo.
- Re-comprobación periódica por si `ShellExperienceHost` se reinicia. Un temporizador nuestro,
  no un hook.
- Con `ocultarFlyoutNativo: false` en `hud.json`, este fichero **no hace nada en absoluto**.

### 3.6 Autoarranque

`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, y nada más. Sale en la pestaña Inicio del
Administrador de tareas, se puede quitar desde ahí, y **se pregunta antes de escribirlo**.

---

## 4. Descartado, y por qué

| Se quería | Por qué no está |
|---|---|
| Brillo de monitores externos por DDC/CI | No es un problema de seguridad: `dxva2.dll` es API pública y no pide permisos. Es que va lento (50-200 ms por llamada), funciona en unos monitores sí y en otros no, y hoy no hace falta. Si entra, entra con su párrafo en §3 |
| Teclas de brillo capturadas como las de volumen | No llegan al teclado: van por ACPI. No hay VK que registrar, y la alternativa —un hook— es la regla 3 y la 4 |
| Quitar el flyout nativo de verdad, no apartarlo | No hay API. Todo lo que lo consigue de verdad pasa por dentro de `ShellExperienceHost`, que es la regla 5 |
| Un HUD que también controle el micrófono | Abriría `eCapture`, que la regla 11 veta. Y el indicador de micrófono de Windows 11 ya existe y funciona |
| Historial de volumen, "tu media de esta semana" | Regla 12 |

---

## 5. Corolarios de diseño

Cosas que el código hace de una forma concreta **porque este documento existe**:

- **`FlyoutNativo.cs` es un fichero entero para veinte líneas.** No se mezcla con nada: es el
  sitio donde mira un auditor, y `auditar.ps1` falla si `FindWindow` se escapa de ahí.
- **El HUD no tiene inventario de ventanas.** No hay ninguna estructura que guarde qué ventanas
  hay. Solo un `HWND` —el del flyout— y su posición original.
- **Nada de lo que el HUD lee se escribe a disco.** `hud.json` guarda ajustes. Nunca estado.
- **`NativeMethods.txt` es la lista cerrada de P/Invokes.** Si no está ahí, no se genera y no
  compila. Cada grupo lleva encima un comentario que dice para qué es, y cada entrada tiene que
  poder señalarse a una sección de §3. Si una no se puede, o sobra la entrada o falta una
  sección.

---

## 6. Cómo se audita

```powershell
pwsh -File auditar.ps1        # tiene que decir TODO LIMPIO y salir con 0
```

Mira **solo código**, saltando comentarios y bloques `/* */`. Hace falta: este documento nombra
todas las APIs prohibidas para explicar por qué lo están, y los fuentes llevan comentarios del
estilo *"el hook no está aquí y no va a estar"*. Un `grep` a secas se encuentra a sí mismo y la
auditoría nunca sale limpia, con lo que deja de servir de puerta.

A mano, las que un script no puede comprobar:

```powershell
# Sin red: con el HUD corriendo, no debe aparecer ninguna conexion suya.
Get-NetTCPConnection | Where-Object { $_.OwningProcess -eq (Get-Process Hud).Id }

# Sin microfono: Configuracion > Privacidad > Microfono, lista de apps recientes.
# El HUD no debe aparecer nunca.

# El flyout se devuelve a su sitio: cerrar el HUD y comprobar que el aviso nativo
# de volumen vuelve a salir donde siempre.

# Y un escaneo de Defender sobre el binario publicado.
```

---

## 7. Cómo se enmienda

1. Se edita **este fichero** antes de escribir el código, explicando qué se abre, **con qué
   cortes**, y por qué sigue siendo defendible.
2. Se añade la regla nueva a `auditar.ps1` si la enmienda cierra algo, o se relaja la existente
   si abre algo.
3. Va en su propio commit, **antes** del commit que usa la API.

Nunca al revés. Un documento que se actualiza después de escribir el código no es una regla, es
un parte de daños.
