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
distintas, porque hacen cosas distintas. Lo que comparten es el método, no la lista.

---

## 1. La excepción que se abrió y se cerró el mismo día

Este apartado ocupaba la mitad del documento y hoy no concede nada. Se queda escrito porque el
razonamiento es el que impide volver a abrirla dentro de seis meses.

### Lo que se temía

El HUD no sirve de nada si Windows dibuja su propio aviso encima del nuestro: eso no es un
reemplazo, es un adorno. Así que la primera versión de este documento **abrió una grieta en la
regla 15**: permitía localizar el host del aviso nativo y apartarlo con `SetWindowPos`, acotado
a un solo fichero y comprobado por una regla propia de `auditar.ps1`.

### Lo que se midió

Antes de escribir ese fichero se midió, porque la clase de ventana que usan las soluciones que
circulan por internet (`NativeHWNDHost`) **ya no existe** en este Windows.

Una sonda de scratchpad capturó 20 segundos de la capa de ventanas mientras se pulsaban las
teclas de volumen. Resultado: **6111 muestras, mediana de 3 ms, peor hueco de 36 ms, 22 teclas
pulsadas, y ni una sola ventana top-level apareció, se movió o se hizo visible.** El aviso dura
unos 2 segundos, así que con ese muestreo no se pudo escapar.

> La primera versión de esa sonda decía lo mismo y estaba mintiendo: hacía
> `Process.GetProcessById` por cada una de las 417 ventanas y por cada muestra, y muestreaba
> **una vez cada cinco segundos**. La segunda versión mide y reporta su propia cadencia, y se
> declara no concluyente si algún hueco pasa de 900 ms. Esto es el caso de libro de
> *"comprueba que la sonda mide lo que crees"*, y costó dos intentos.

**El aviso no es una ventana.** Se dibuja dentro de algo que ya estaba ahí. Apartar eso sería
apartar a su dueño entero, que es explorer.

### Lo que resultó ser la respuesta

**El aviso nativo sale porque el shell recibe la tecla.** Si la capturamos nosotros con
`RegisterHotKey` (§3.1), el shell no la recibe, y cambiar el volumen por
`IAudioEndpointVolume` (§3.2) no dispara ningún aviso porque no es una pulsación.

Medido con una segunda sonda que registra las tres teclas y no hace nada más:
**el recuadro gris de Windows no apareció ni una vez.** El volumen tampoco se movió, que es la
otra mitad de la prueba: demuestra que la tecla se la tragó el registro y no llegó a nadie.

### La conclusión

**La excepción se retira entera.** El HUD no toca ninguna ventana ajena, de ninguna forma, y la
regla 15 vuelve a ser absoluta. No hay `FlyoutNativo.cs`, no hay `FindWindow`, no hay
`ocultarFlyoutNativo` en la configuración: no hay nada que apagar porque no hay nada que hacer.

La función que parecía necesitar el permiso más caro del documento **no necesitaba existir**.

### Lo que queda sin resolver, dicho en voz alta

**Con el brillo no funciona.** Las teclas Fn de brillo van por ACPI, no llegan como tecla, y no
hay nada que registrar — medido: la sonda no las ve. Windows sigue enseñando su aviso al
pulsarlas. Si el HUD enseña el brillo, ahí sí habrá dos indicadores, y no hay ninguna forma
permitida de evitarlo: el aviso de brillo tampoco es una ventana.

Eso es una decisión de producto, no de seguridad, y se toma fuera de este documento. Lo que
este documento fija es que **no se resuelve tocando nada de nadie**.

---

## 2. Lo que este programa no va a hacer, nunca

| # | Prohibido | Por qué |
|---|---|---|
| 1 | Driver de kernel (`.sys`), servicio de Windows, tarea programada, o cualquier componente elevado | Es lo que hace que un antivirus marque a un programa de escritorio. Corre siempre como usuario normal, `requestedExecutionLevel` `asInvoker`. Y el brillo **no lo necesita**: WMI lo deja leer al usuario de la sesión interactiva |
| 2 | Leer sensores de hardware: temperaturas, voltajes, RPM, puertos I/O, MSR, SMBus | Requiere driver. No existe forma en modo usuario |
| 3 | `SetWindowsHookEx` global y `SetWinEventHook` | Un hook global carga una DLL nuestra dentro de otros procesos, o instala un callback de bajo nivel. *Ese* es el patrón de keylogger. Para las teclas de volumen se usa `RegisterHotKey`, que es otra cosa — ver §3.1 |
| 4 | Leer el teclado: `GetAsyncKeyState`, `GetKeyboardState`, `keybd_event`, `SendInput`, `WH_KEYBOARD` | **La prohibición que más caro parecía.** Un HUD de volumen quiere saber cuándo pulsas una tecla, y la forma fácil es un hook de bajo nivel. La forma fácil está prohibida: se registran tres teclas concretas y nada más. Resultó ser además la forma **mejor**, ver §1 |
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
| 15 | **Tocar ventanas ajenas, de cualquier forma.** Enumerarlas (`EnumWindows`), buscarlas (`FindWindow`), moverlas, cerrarlas, minimizarlas, ocultarlas con cloaking, leer sus píxeles o robarles el foco | Absoluta, sin excepciones. Hubo una durante unas horas y la §1 cuenta por qué se abrió y por qué se cerró. **El HUD no conoce la existencia de ninguna ventana que no sea la suya** |
| 16 | Matar procesos: `TerminateProcess`, `TerminateThread`, `EndTask`, `ExitWindowsEx` | Pierde datos sin preguntar. Y era la tentación evidente de este proyecto: matar `ShellExperienceHost` haría desaparecer el aviso. También rompería el centro de notificaciones |

---

## 3. Lo que sí se hace, y por qué se sostiene

### 3.1 Tres teclas de volumen

`RegisterHotKey(hwnd, id, 0, VK_VOLUME_UP | VK_VOLUME_DOWN | VK_VOLUME_MUTE)`.

**No es un hook.** Se le pide a Windows que mande `WM_HOTKEY` a **nuestra propia ventana**
cuando se pulse una de esas tres teclas. No carga nada en ningún proceso, no ve ninguna otra
tecla, y **no puede verlas**: el sistema entrega el mensaje ya filtrado. Es lo contrario de
`SetWindowsHookEx`, que está prohibido en la regla 3, y es lo mismo que hacen el dock
(Ctrl+Alt+D) y la isla (Ctrl+Alt+I).

Medido antes de escribir el código: las tres se registran sin problema en esta máquina, ninguna
estaba cogida por otro programa, y `MOD_NOREPEAT` **no** se usa — al mantener pulsada la tecla
tiene que repetir.

**Consecuencia, y es la que sostiene el proyecto entero:** `RegisterHotKey` **consume** la
tecla. Windows ya no la ve, así que ni cambia el volumen ni enseña su aviso. Lo cambia el HUD
(§3.2). Eso no es un efecto secundario que haya que disimular: es lo que hace innecesario todo
lo que la §1 temía.

**Si el registro falla** porque otro programa se adelantó, se dice en la consola y el HUD sigue
arrancando en modo solo-observación. Ahí sí saldrían dos avisos, el suyo y el nuestro; es el
único caso en que pasa, y es preferible a no arrancar. Un atajo global que falla en silencio es
media hora perdida.

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

**Solo se lee.** Las teclas de brillo van por ACPI y Windows ya lo cambia solo, así que
`WmiSetBrightness` no aporta nada y **no entra en el código**. Si algún día se quiere un atajo
propio de brillo, se añade esa llamada y se enmienda este párrafo.

Medido en esta máquina: el panel interno (AUO) responde con 101 niveles y su valor actual, sin
elevación. **No hace falta elevación** y no se pide. Si en otra máquina la suscripción de
eventos fuese denegada, la función se degrada a sondeo; **no se eleva el proceso** (regla 1).

Este es el motivo de la segunda dependencia del proyecto, `System.Management`.

### 3.4 Dibujar

`Windows.UI.Composition` del sistema sobre un `DesktopWindowTarget` de **nuestro propio HWND**,
y la cadena D3D11 → D2D1 → `CreateDrawingSurface` para los glifos. Actúa solo sobre nuestras
superficies: **no lee la pantalla ni la ventana de nadie**, y en particular no hay ninguna
captura de pantalla en este proyecto.

El fondo acrílico es `CreateHostBackdropBrush()`, que desenfoca lo que hay detrás. Conviene
tenerlo escrito porque suena a leer la pantalla y no lo es: **el desenfoque lo hace DWM y el
resultado nunca vuelve a nuestro proceso.** No hay ningún píxel de nadie en nuestra memoria.

### 3.5 Autoarranque

`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, y nada más. Sale en la pestaña Inicio del
Administrador de tareas, se puede quitar desde ahí, y **se pregunta antes de escribirlo**.

---

## 4. Descartado, y por qué

| Se quería | Por qué no está |
|---|---|
| Apartar el aviso nativo de Windows | No es una ventana: medido, 6111 muestras y cero eventos. Y no hace falta para el volumen, porque capturar la tecla ya lo suprime. Ver §1 |
| Que el aviso de **brillo** de Windows tampoco salga | Mismo motivo: no es una ventana. Y sus teclas no se pueden capturar porque van por ACPI. No hay forma permitida, y tampoco prohibida que funcione |
| Brillo de monitores externos por DDC/CI | No es un problema de seguridad: `dxva2.dll` es API pública y no pide permisos. Es que va lento (50-200 ms por llamada), funciona en unos monitores sí y en otros no, y hoy no hace falta |
| Teclas de brillo capturadas como las de volumen | No llegan al teclado: van por ACPI. Medido con una sonda que sí mira el teclado — no aparecen. La alternativa, un hook, es la regla 3 y la 4, y tampoco las vería |
| Un HUD que también controle el micrófono | Abriría `eCapture`, que la regla 11 veta. Y el indicador de micrófono de Windows 11 ya existe y funciona |
| Historial de volumen, "tu media de esta semana" | Regla 12 |

---

## 5. Corolarios de diseño

Cosas que el código hace de una forma concreta **porque este documento existe**:

- **El HUD no tiene inventario de ventanas.** No hay ninguna estructura que guarde qué ventanas
  hay, ni una sola llamada que pregunte por una que no sea la nuestra. Es la propiedad más
  fuerte del programa y la más fácil de comprobar: `auditar.ps1` regla 15.
- **Nada de lo que el HUD lee se escribe a disco.** `hud.json` guarda ajustes. Nunca estado.
- **`NativeMethods.txt` es la lista cerrada de P/Invokes.** Si no está ahí, no se genera y no
  compila. Cada grupo lleva encima un comentario que dice para qué es, y cada entrada tiene que
  poder señalarse a una sección de §3. Si una no se puede, o sobra la entrada o falta una
  sección.
- **Las sondas de medición no viven en el repo.** Van al scratchpad. Pueden usar APIs que la app
  tiene prohibidas —`EnumWindows`, `GetAsyncKeyState`— porque no son el programa; así se midió
  todo lo de la §1. Lo que sí vive en el repo es `--check`, para lógica pura.

---

## 6. Cómo se audita

```powershell
pwsh -File auditar.ps1        # o powershell -File, si no hay PowerShell 7
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

Y la lección de la §1, que es por lo que este procedimiento vale la pena: **la excepción se
abrió con todo el papeleo hecho, y aun así sobraba.** Escribirla obligó a medirla, y medirla
demostró que la función no hacía falta. El documento no frenó el proyecto; le ahorró un fichero.
