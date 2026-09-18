# Superficie de riesgo — documento vinculante

Este documento gobierna todo el desarrollo del dock. Si una función futura necesita
algo de la tabla de prohibiciones, **la función se rediseña o se descarta**. No se
piden excepciones de palabra: se enmienda este documento por escrito, con su
justificación, y queda en el historial de git.

El objetivo es concreto: que ningún antivirus tenga un motivo razonable para marcar
este binario. El referente visual del proyecto, MyDockFinder, carga un driver de
kernel para leer sensores y por eso Defender lo marca. Aquí no se hace nada parecido.

## Lo que este programa NO va a hacer, nunca

| # | Prohibido | Por qué es la regla |
|---|---|---|
| 1 | Driver de kernel (`.sys`), servicio de Windows, tarea programada, o cualquier componente elevado | Es exactamente lo que hace que Defender marque a MyDockFinder. La app corre siempre como usuario normal, con `requestedExecutionLevel` `asInvoker`. |
| 2 | Leer sensores de hardware: temperaturas, voltajes, RPM, acceso a puertos I/O, MSR, SMBus | Requiere driver. No existe forma en modo usuario. El dock no muestra sensores: no es una feature ausente, es una prohibición. |
| 3 | `SetWindowsHookEx` global (`WH_KEYBOARD_LL`, `WH_MOUSE_LL`, `WH_CBT`, `WH_SHELL`) y `SetWinEventHook` | Un hook global carga la DLL en otros procesos o instala un callback de bajo nivel: firma clásica de keylogger. El dock recibe ratón solo en su propia ventana. |
| 4 | `CreateRemoteThread`, `WriteProcessMemory`, `VirtualAllocEx`, `NtMapViewOfSection`, o cualquier código dentro de `explorer.exe` | Inyección de proceso. Bandera roja inmediata de EDR/AV. |
| 5 | Reemplazo del shell (`Winlogon\Shell`), parcheo de binarios del sistema, IFEO, AppInit_DLLs | Persistencia de malware por definición. |
| 6 | **Cualquier** llamada de red: sin telemetría, sin updater, sin check de versión, sin analytics, sin crash reporting | La app es 100% offline. No se enlaza ninguna librería HTTP. Verificable con `netstat` durante la ejecución. |
| 7 | Persistencia oculta | El autoarranque va en `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, visible en el Administrador de tareas, y se pregunta antes de escribirlo. Nada de `schtasks`, servicios ni carpeta Startup oculta. |
| 8 | Ofuscación, packers (UPX, Themida), compresión del ejecutable, cifrado de strings, single-file comprimido | Bandera roja #1 de las heurísticas: un ejecutable con alta entropía y sin secciones legibles es indistinguible de un dropper. Se compila con PDB y secciones normales. |
| 9 | Descarga o generación de código en runtime: `Assembly.Load(byte[])`, `Reflection.Emit`, descarga de plugins | Ejecutar código no firmado en runtime es comportamiento de loader. Sin sistema de plugins. |
| 10 | Lectura de datos de usuario fuera de lo necesario: navegadores, credenciales, documentos, portapapeles | El dock lee su propio JSON y los iconos de las apps que el usuario configuró. Nada más. |
| 11 | Manipular ventanas ajenas **fuera del consentimiento explícito del usuario** | Ver la enmienda de abajo. |

## Enmienda 1 — ventanas ajenas (2026-09-18)

### Qué decía antes

> **11.** Enumerar o manipular ventanas ajenas más allá de lo mínimo. No `EnumWindows`
> para espiar, no `SetForegroundWindow` sobre terceros, no minimizar/cerrar ventanas
> ajenas.

### Qué dice ahora

> **11.** Enumerar o manipular ventanas ajenas **fuera del consentimiento explícito del
> usuario**. El dock solo actúa sobre ventanas que cumplen **las dos** condiciones:
> pertenecen a una app que el usuario puso en `dock.json`, **y** el usuario acaba de
> hacer clic en ese icono. Nunca por iniciativa propia, nunca en segundo plano, nunca
> sobre una app que no esté en el dock.

### Por qué se enmienda

Dos funciones lo pedían, y una de ellas es la que más se echa de menos a diario:

- **Clic en una app ya abierta debería traerla al frente**, no lanzar otra instancia.
  Es lo que hace la barra de tareas de Windows y lo que hace el Dock de macOS.
- **El efecto genio** al minimizar: deformar la ventana hacia su icono exige leer sus
  píxeles y luego minimizarla.

### Por qué sigue siendo defendible

Lo que separa lo legítimo de lo sospechoso no es *qué API se llama*, sino **quién
inicia la acción y sobre qué**. `SetForegroundWindow` sobre la ventana de la app en la
que el usuario acaba de hacer clic es literalmente lo que hace la barra de tareas al
pulsar su botón. Lo que un antivirus busca es lo contrario: enumeración en segundo
plano, sin interacción, sobre ventanas arbitrarias, y lectura de su contenido.

Tres cortafuegos que se sostienen en el código, no en la buena voluntad:

1. **Nunca se actúa sin un clic inmediatamente anterior sobre un icono.** No existe
   ningún camino de código que toque una ventana ajena desde un temporizador, un hilo
   de fondo o un evento del sistema. La única excepción de lectura es comprobar si un
   proceso está vivo, para pintar el punto de "app abierta", y eso no mira ventanas.
2. **Ninguna captura se guarda ni sale del proceso.** Los píxeles van directos a una
   superficie del compositor y se liberan al terminar la animación. Sin disco, sin red
   — la regla 6 no se toca.
3. **Sigue sin haber ningún hook.** Se consideró `SetWinEventHook` con
   `EVENT_SYSTEM_MINIMIZESTART` para detectar minimizados en todo el sistema y se
   **descartó**: el dock no observa el sistema, reacciona a sus propios clics. La
   regla 3 se amplía explícitamente para dejarlo cerrado.

### El propio Windows impone esta regla

No es solo una promesa de este documento. `SetForegroundWindow` **la ignora Windows**
salvo que el proceso que llama cumpla alguna condición, y la que cumple el dock es
haber **recibido el último evento de entrada**: el clic del usuario sobre el icono.

Se comprobó por accidente al implementarlo. La primera versión de la prueba
automatizada mandaba el clic con `PostMessage` en vez de pinchar de verdad, y el foco
no cambiaba nunca — porque sin clic real el sistema no concede el permiso. El código
era correcto; lo que faltaba era el consentimiento.

Dicho de otro modo: aunque alguien modificara el dock para llamar a
`SetForegroundWindow` desde un temporizador, **no funcionaría**.

### Lo que la enmienda NO autoriza

- Cerrar ventanas ajenas.
- Enumerar ventanas para algo que no sea localizar la app del icono clicado.
- Guardar, transmitir o analizar la imagen capturada.
- Hooks de ningún tipo.
- Actuar sobre apps que no estén en `dock.json`.
- Modificar el estado de otro proceso más allá de minimizar, restaurar, enfocar y lo
  que se detalla justo debajo.

### Apéndice — desactivar la transición de minimizado

El efecto genio necesita una cosa más, y se anota aquí porque la lista de arriba no la
cubría: antes de minimizar se pone `DWMWA_TRANSITIONS_FORCEDISABLED` en la ventana de
destino, para que Windows no reproduzca SU animación de minimizar encima de la nuestra.

- Es un atributo **visual** de DWM, no de comportamiento ni de seguridad.
- Se **restaura inmediatamente** después. La ventana no queda alterada.
- Se midió que hace falta: sin él, a los 25 ms de pedir el minimizado la ventana sigue
  encogiéndose en pantalla; con él, ya ha desaparecido.
- Sigue atado a la misma condición: solo tras un clic del usuario sobre ese icono.

### Apéndice — cómo se captura la ventana

La enmienda autoriza leer los píxeles de la ventana clicada, y este es el detalle de
cómo, porque el método elegido no es el que el plan preveía.

Se usa `PrintWindow` con `PW_RENDERFULLCONTENT` sobre un `CreateDIBSection` propio, y
**no** `Windows.Graphics.Capture`. Se probó primero el camino barato y resultó bastar
para las apps reales del dock — Explorador, Paint y Bloc de notas devuelven la ventana
entera. Eso elimina de raíz el framepool, la sesión asíncrona y el borde amarillo de
grabación que Windows dibuja alrededor de lo que se está capturando.

- Es **un solo fotograma**, no una sesión de captura: no hay nada que siga grabando.
- Los píxeles van del DIB a una superficie del compositor y se **liberan al acabar** la
  animación. Sin disco, sin red, sin análisis.
- Si la captura vuelve en blanco, se detecta y se minimiza sin animación. No se
  reintenta por otras vías ni se escala el privilegio.
- Los P/Invoke que esto añade son de GDI corriente: `CreateCompatibleDC`,
  `CreateDIBSection`, `SelectObject`, `DeleteDC`, `DeleteObject`. Dibujar en un bitmap
  propio.

## Enmienda 2 — arrastrar y soltar (2026-09-18)

La fase 3 hace que el dock acepte ficheros arrastrados desde el Explorador. Eso toca
tres sitios que rozan la lista de prohibiciones, y se aclaran aquí antes de escribir el
código, no después.

### 1. Leer una suelta NO es leer el portapapeles

La regla 10 prohíbe leer el portapapeles. Recibir un `IDataObject` en `IDropTarget::Drop`
usa nombres que se le parecen — `CF_HDROP` es un formato *de portapapeles*— y un auditor
puede confundirlos. Son canales distintos:

- El portapapeles es un almacén del sistema que cualquiera puede leer en cualquier
  momento, sin que el usuario se entere. **Sigue prohibido.**
- El objeto de arrastre nos lo entrega el usuario con su gesto, sobre nuestra ventana, y
  deja de ser válido en cuanto acaba la suelta.

Quedan prohibidas explícitamente `OleGetClipboard`, `GetClipboardData` y
`OpenClipboard`, y se auditan con grep abajo. Del objeto soltado solo se leen rutas y el
AppUserModelID; no se guarda en disco ni se analiza su contenido.

### 2. `ShellExecuteExW` con parámetros

Abrir un fichero con una app concreta obliga a pasar un argumento al lanzarla. Es *la*
API de lanzar procesos y es común en malware, así que:

- Nunca con el verbo `runas` ni con nada que eleve. El verbo va a `NULL`, el de defecto.
- La ruta viene de una suelta del usuario, no de entrada libre: se normaliza con
  `GetFullPathName` y se rechaza cualquiera que contenga comillas dobles.
- Va siempre **entre comillas** en `lpParameters`, que es el patrón canónico del registro.
- `ShellExecuteEx` no pasa por `cmd.exe`, así que no hay inyección de comandos.

### 3. Los accesos directos se leen, nunca se escriben

Para resolver un `.lnk` soltado hace falta `CLSID_ShellLink` + `IPersistFile::Load`.
**`IPersistFile::Save` queda prohibido**: escribir accesos directos, sobre todo en
Inicio o en el escritorio, es persistencia de malware. Solo lectura, y solo del fichero
que el usuario acaba de soltar.

### 4. Ausencias deliberadas

No basta con no usarlas: se anotan para que se vea que la decisión fue consciente.

| No se usa | Por qué |
|---|---|
| `ChangeWindowMessageFilterEx` | Relajar UIPI es patrón de escalada de privilegios, y Microsoft lo desaconseja para este caso concreto. No hace falta corriendo sin elevar |
| `IPersistFile::Save` | Escribir `.lnk` es persistencia |
| `IAssocHandler::MakeDefault` | Cambiar la app por defecto de un tipo de fichero es comportamiento de *hijacker* |
| `OleGetClipboard`, `GetClipboardData` | Regla 10, intacta |

### Corolario — correr sin elevar pasa a ser también un requisito funcional

UIPI bloquea los mensajes de ventana de un proceso de integridad baja a uno de
integridad alta, y el arrastre se implementa con mensajes de ventana. El Explorador
corre a integridad media. La regla 1 ya obliga a `asInvoker`, pero a partir de ahora, si
alguien arrancase el dock elevado, **el arrastrar y soltar dejaría de funcionar en
silencio**. El dock lo detecta al arrancar y lo avisa por consola en vez de quedarse
mudo.

## Corolarios de diseño

- **Sin single-file comprimido.** `EnableCompressionInSingleFile` produce exactamente
  el perfil de entropía que dispara heurísticas.
- **Sin `PublishTrimmed` agresivo ni ofuscación de IL.** El binario debe ser legible
  por un analizador estático.
- **La config vive en texto plano legible** junto al ejecutable. Nada de formato
  binario propietario.
- **Lanzar apps** se hace con `Process.Start` + `UseShellExecute = true`, o con
  `ShellExecuteExW` cuando hay que pasarle un fichero. Ambas delegan en el shell. No
  `CreateProcess` con flags raros.
- **La configuración que el dock escribe va aparte.** `dock.json` es del usuario y no se
  toca; lo que el dock cambia al reordenar o añadir va a `dock.local.json`, también en
  texto plano legible y junto al ejecutable.

## Cómo se audita

```sh
# Prohibiciones 1 a 5. No debe devolver nada.
git ls-files | xargs grep -riE "\.sys\b|WinRing0|SetWindowsHookEx|SetWinEventHook|CreateRemoteThread|WriteProcessMemory|VirtualAllocEx"

# Regla 6: sin red.
git ls-files '*.cs' | xargs grep -rinE "HttpClient|WebClient|Socket|WebRequest|Dns\."

# Regla 7: la única escritura en el registro es el autoarranque, en HKCU\...\Run.
git ls-files '*.cs' | xargs grep -rn "SetValue\|DeleteValue\|CreateSubKey"

# Regla 11: toda llamada que toque una ventana ajena tiene que salir de un clic.
git ls-files '*.cs' | xargs grep -rn "SetForegroundWindow\|ShowWindow\|PrintWindow"

# Enmienda 2: el portapapeles sigue prohibido, y las ausencias deliberadas.
git ls-files | xargs grep -rn "OleGetClipboard\|GetClipboardData\|OpenClipboard\|ChangeWindowMessageFilterEx\|MakeDefault"

# Enmienda 2: los .lnk se leen, nunca se escriben. No debe aparecer ningun Save.
git ls-files '*.cs' | xargs grep -rn "IPersistFile"
```

La lista completa y cerrada de P/Invokes está en `NativeMethods.txt`, que es el fichero
de entrada de CsWin32 y por tanto **no puede desviarse de lo que el binario realmente
usa**: si una función no está ahí, no se genera, y el código no compila.
