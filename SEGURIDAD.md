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

- Cerrar ventanas ajenas. <sup>(la enmienda 4 abre una sola vía: `WM_CLOSE` desde la ✕ de la lista de ventanas)</sup>
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

## Enmienda 3 — inventario de ventanas, la barra de tareas y las miniaturas (2026-09-18)

La fase 4 trae multipantalla, previsualizaciones y "que la barra de tareas no aparezca".
Casi todo eso choca contra lo escrito arriba, así que se enmienda por escrito antes de
tocar una línea de código. **No se levanta todo**: se abren cinco cosas, se dejan las
demás prohibidas y se añade una prohibición nueva.

### Qué se abre

**1. Inventariar ventanas, cualquiera, no solo las de las apps del dock.**

La enmienda 1 prohibía *"enumerar ventanas para algo que no sea localizar la app del
icono clicado"*. A partir de ahora el dock mantiene un inventario de las ventanas de
nivel superior del escritorio: handle, proceso y monitor.

Por qué sigue siendo defendible: es exactamente lo que hace la barra de tareas de
Windows, y lo que hacen Rainmeter o TranslucentTB. Lo que separa esto de algo sospechoso
no cambia en nada: **no se inyecta nada en otro proceso**, **no se lee el contenido de
ninguna ventana salvo para la animación que el usuario pidió**, **nada sale del proceso**
y **no hay red**. Un inventario de handles que se usa y se tira no es exfiltración;
mandarlo a algún sitio sí lo sería, y la regla 6 sigue intacta.

**2. `RegisterShellHookWindow`, que NO es un hook.**

Se usa para enterarse de que una ventana nace o muere sin sondear cuatro veces por
segundo. La regla 3 prohíbe `SetWindowsHookEx` y `SetWinEventHook`, y esto no es ninguno
de los dos. La documentación de Microsoft lo contrasta ella misma, textualmente:

> *"Many of the messages are the same as those that can be received after calling the
> SetWindowsHookEx function and specifying WH_SHELL... **The difference with
> RegisterShellHookWindow is that the messages are received through the specified
> window's WindowProc and not through a call back procedure.**"*

Es decir: **no se carga ninguna DLL en ningún proceso ajeno** y no se instala ningún
callback de bajo nivel. Los mensajes llegan a nuestro propio `WndProc` como cualquier
otro mensaje de ventana. Queda citado aquí porque la palabra "ShellHook" en un `grep`
puede confundirse con lo otro, y no lo es.

**La regla 3 no se toca.** `SetWindowsHookEx` global sigue prohibido, y `SetWinEventHook`
también: con `RegisterShellHookWindow` no hace falta.

**3. Registrarse como AppBar** (`SHAppBarMessage`: `ABM_NEW`, `ABM_QUERYPOS`,
`ABM_SETPOS`, `ABM_REMOVE`, `ABM_ACTIVATE`, `ABM_WINDOWPOSCHANGED`,
`ABM_GETTASKBARPOS`, `ABM_SETAUTOHIDEBAREX`).

Es API pública de `Shell32` desde XP, actúa sobre **nuestro propio `HWND`**, no requiere
elevación, y su único uso documentado es literalmente "soy una barra de herramientas de
escritorio" — que es lo que el dock es. `ABM_REMOVE` es obligatorio al salir, y la doc lo
dice así: *"An application should always send ABM_REMOVE before destroying an appbar."*

**`ABM_SETSTATE` queda prohibido** — ver la tabla de abajo.

**4. Capturar un fotograma de una ventana ajena de forma repetida mientras el ratón está
sobre su icono.**

El apéndice de la enmienda 1 presumía de *"Es un solo fotograma, no una sesión de
captura: no hay nada que siga grabando"*. Una miniatura que se refresca rompe esa frase,
así que se reescribe el límite en vez de fingir que no ha cambiado:

- Sigue siendo `PrintWindow` con `PW_RENDERFULLCONTENT`. **Sigue sin haber sesión de
  captura, ni framepool, ni `Windows.Graphics.Capture`.**
- Solo mientras el puntero está sobre ese icono. **Se para al salir**, no al cabo de un
  rato.
- Solo las ventanas de esa app, que está en `dock.json`.
- El fotograma vive en memoria, se dibuja y se tira. No se guarda a disco ni se analiza.

**5. Leer los documentos recientes de otra app** (`IApplicationDocumentLists` con
`SetAppID` + `GetList`).

Es API pública implementada por el sistema, y la doc no restringe el AppID al del propio
proceso. Pero **es leer datos del usuario**, o sea la regla 10, así que lleva los mismos
tres cortafuegos que la enmienda 1:

- Solo apps que están en `dock.json`.
- Solo tras un gesto explícito del usuario sobre ese icono (abrir el menú contextual).
- **Nunca en un temporizador, y nunca enumerando las apps del sistema.**

Sin esos tres cortes esto es "enumerar los documentos recientes del usuario", y eso se
parece a un infostealer aunque la API sea pública.

### Qué NO se levanta

| Sigue prohibido | Por qué se queda |
|---|---|
| Reglas 1, 2, 4, 5, 6, 8 y 9 enteras | Driver, sensores, inyección, reemplazo del shell, red, ofuscación, código en runtime. Son las que hacen que Defender ignore este binario. |
| **`SetWindowsHookEx` global y `SetWinEventHook`** (regla 3, intacta) | Un hook global carga una DLL nuestra dentro de otros procesos. *Ese* es el patrón de keylogger, y con `RegisterShellHookWindow` no hace falta para nada. |
| Persistencia oculta (regla 7) | El autoarranque sigue en `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, visible y preguntado. |
| Portapapeles y credenciales (regla 10 y enmienda 2) | Sin cambios. |
| Matar procesos ajenos (`TerminateProcess`, `EndTask`) | La **enmienda 4** abre `WM_CLOSE`, que es *pedir* que se cierre y la app puede negarse. Matar un proceso pierde datos sin preguntar. |
| Guardar, transmitir o analizar una imagen capturada | Sin cambios respecto a la enmienda 1. |
| Parsear a mano los ficheros de `AutomaticDestinations` | Formato no documentado, ficheros internos del perfil del usuario. Es lo que hacen las herramientas forenses y el malware de exfiltración — y existiendo `IApplicationDocumentLists` no tiene defensa. |
| **`ABM_SETSTATE`** | Escribe el ajuste **global** de la barra de tareas del usuario, el mismo checkbox de sus propiedades. Devuelve siempre `TRUE`, así que no se puede detectar el fallo, y **no hay ninguna API que lo restaure**. Cambiar un ajuste global sin pedirlo y sin poder deshacerlo es el perfil del adware de barras de herramientas. Además no consigue lo que se quería: apagar el autoocultar deja la barra *permanentemente* visible. |
| `Windows.Graphics.Capture` con `IsBorderRequired = false` | Quitarle al usuario el aviso de que estás capturando su pantalla es precisamente lo que querría un espía. Si algún día hay miniaturas por esa vía, **con el borde puesto**. |

### Y una prohibición NUEVA: `DWMWA_CLOAK` sobre ventanas ajenas

Se propuso ocultar la ventana con `DwmSetWindowAttribute(DWMWA_CLOAK)` en vez de
minimizarla, para que el efecto genio conserve la textura viva. **Se prohíbe**, y la
razón hay que dejarla escrita porque la idea volverá:

- **No está documentado que funcione entre procesos.** La doc de `DwmRegisterThumbnail`
  sí dice que la ventana destino *"must... be owned by the process that is calling"*.
  La de `DwmSetWindowAttribute` no dice nada. Donde DWM quiere poner un límite de
  proceso, lo escribe; que aquí calle no es permiso, es silencio. Y el valor de lectura
  se llama `DWM_CLOAKED_APP` = *"cloaked by **its owner** application"*.
- **No hay rollback.** No existe ningún concepto documentado de "dueño del cloak" ni de
  limpieza cuando muere el proceso que lo puso. Si el dock revienta después de cloakear
  la ventana de otra app, **esa ventana se queda invisible** y el usuario no tiene forma
  de recuperarla salvo cerrar la app a ciegas.

Compárese con el apéndice de `DWMWA_TRANSITIONS_FORCEDISABLED`, que se defiende bien
justamente porque *"se restaura inmediatamente después. La ventana no queda alterada."*
Cloakear no tiene esa propiedad: es estado persistente e irreversible dentro del proceso
de otro. `DWMWA_CLOAKED` **de lectura** sigue permitido — ya se usa para filtrar ventanas
fantasma del inventario.

### Lo que se descartó por imposible, no por prohibido

`ITaskbarList3::SetProgressValue` y `SetOverlayIcon` **solo escriben**, y solo sobre un
`HWND` propio. No existe ningún método `Get*` en `ITaskbarList` 1, 2, 3 ni 4: el estado
vive dentro de `explorer.exe` y no está expuesto. Leer el progreso que publicó otra app
no se puede con ninguna API documentada. No se añade el P/Invoke.

## Enmienda 4 — cerrar una ventana desde la lista de la rueda (2026-09-18)

La rueda sobre un icono despliega la lista de ventanas de esa app (ver M5). El usuario
pide una **✕ en cada fila para cerrar esa ventana**, que es lo que hacen la barra de
tareas de Windows y el dock de macOS.

Eso choca de frente con una prohibición escrita. La enmienda 1 decía, textualmente, que
la enmienda **NO autoriza** *"cerrar ventanas ajenas"*, y la enmienda 3 lo repitió en su
tabla: *"El inventario es para saber, no para matar"*. Se enmienda ahora, y con límites.

### Qué se abre

**Pedirle a una ventana ajena que se cierre**, con `PostMessage(hwnd, WM_CLOSE, 0, 0)`.

### Por qué es defendible

`WM_CLOSE` **no cierra nada por la fuerza: lo pide**. Es el mismo mensaje que manda el
botón de cerrar de la propia ventana, y la app es libre de ignorarlo, de enseñar un
"¿guardar los cambios?" o de no hacer nada. Si el usuario dice que no, la ventana se
queda. No hay pérdida de datos que la app no haya aceptado.

Es exactamente lo que ofrece el menú del clic derecho de la barra de tareas de Windows
sobre cualquier botón, y lo que ofrece el dock de macOS. No hay nada aquí que un dock no
haga.

### Los límites, que son los mismos tres de siempre

- **Solo `PostMessage(WM_CLOSE)`.** Quedan prohibidos, y esto es lo importante:
  `TerminateProcess`, `TerminateThread`, `EndTask`, `ExitWindowsEx`, `NtTerminateProcess`
  y cualquier otra forma de matar un proceso o forzar el cierre. Matar un proceso pierde
  datos sin preguntar; `WM_CLOSE` no.
- **Solo ventanas de apps que están en `dock.json`.** La lista sale del inventario, que
  ya está acotado a los iconos del dock.
- **Solo como respuesta directa a un clic del usuario sobre esa ✕.** Nunca desde un
  temporizador, nunca desde un hilo de fondo, nunca en lote, y **no existe ningún
  "cerrar todas"**.

### Lo que la enmienda NO autoriza

- Matar procesos, por ninguna vía.
- Cerrar la ventana de una app que no esté en `dock.json`.
- Cerrar nada sin un clic encima de la ✕ de esa fila concreta.
- Responder que sí a los diálogos de la app. Si pregunta si guardar, contesta el usuario.

### Cómo se audita

```sh
# La unica forma de cerrar es WM_CLOSE. No debe aparecer ninguna de estas.
git ls-files '*.cs' NativeMethods.txt | xargs grep -rnE "TerminateProcess|TerminateThread|EndTask|ExitWindowsEx|NtTerminate"
```

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

# Enmienda 3: cloakear ventanas ajenas y tocar el ajuste de la barra de tareas
# estan prohibidos. No debe devolver nada. DWMWA_CLOAKED (lectura) no casa aqui.
git ls-files '*.cs' NativeMethods.txt | xargs grep -rnE "DWMWA_CLOAK\b|ABM_SETSTATE|IsBorderRequired|GraphicsCapture"

# Enmienda 3: RegisterShellHookWindow si esta permitido, pero tiene que ser el unico
# mecanismo de aviso de ventanas. Si aparece SetWinEventHook, la regla 3 esta rota.
git ls-files '*.cs' NativeMethods.txt | xargs grep -rn "RegisterShellHookWindow\|SetWinEventHook"
```

La lista completa y cerrada de P/Invokes está en `NativeMethods.txt`, que es el fichero
de entrada de CsWin32 y por tanto **no puede desviarse de lo que el binario realmente
usa**: si una función no está ahí, no se genera, y el código no compila.
