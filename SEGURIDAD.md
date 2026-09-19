# Superficie de riesgo — documento vinculante

Este documento gobierna todo el desarrollo del dock. Si una función futura necesita algo
prohibido, **la función se rediseña o se descarta**. No se piden excepciones de palabra:
se enmienda este documento por escrito, con su justificación, y queda en el historial de
git.

El objetivo es concreto y medible: **que ningún antivirus tenga un motivo razonable para
marcar este binario**. El referente visual del proyecto, MyDockFinder, carga un driver de
kernel para leer sensores, y por eso Defender lo marca. Aquí no se hace nada parecido.

> **Estado a 19 de septiembre de 2026.** `auditar.ps1` sale limpio y Defender no encuentra
> nada en el binario publicado. El documento se reescribió entero ese día: hasta entonces
> era la tabla original más cuatro enmiendas encadenadas, y había que leerse las cinco en
> orden para saber qué estaba permitido. Ahora está por temas. El historial de cómo se
> llegó aquí está en git, no aquí.

---

## 1. El criterio

Lo que separa lo legítimo de lo sospechoso **no es qué API se llama**, sino tres cosas:

| | Legítimo | Sospechoso |
|---|---|---|
| **Quién empieza** | El usuario, con un gesto sobre un icono | Un temporizador, un hilo de fondo, un evento del sistema |
| **Sobre qué** | Apps que el usuario puso en `dock.json` | Ventanas o procesos arbitrarios |
| **A dónde va** | Se usa, se dibuja y se tira | A disco, a la red, a un análisis |

`SetForegroundWindow` sobre la ventana de la app cuyo icono acabas de clicar es
literalmente lo que hace la barra de tareas. La misma llamada desde un temporizador, sobre
cualquier ventana, es otra cosa. **El código tiene que hacer imposible la segunda**, no
solo evitarla.

Y hay un detalle que conviene saber: **Windows impone parte de esta regla por su cuenta**.
`SetForegroundWindow` la ignora salvo que el proceso haya recibido el último evento de
entrada. Se descubrió por accidente: la primera prueba automatizada mandaba el clic con
`PostMessage` en vez de pinchar de verdad, y el foco no cambiaba nunca. El código era
correcto; lo que faltaba era el consentimiento. Aunque alguien modificara el dock para
llamarla desde un temporizador, **no funcionaría**.

---

## 2. Lo que este programa no va a hacer, nunca

| # | Prohibido | Por qué |
|---|---|---|
| 1 | Driver de kernel (`.sys`), servicio de Windows, tarea programada, o cualquier componente elevado | Es exactamente lo que hace que Defender marque a MyDockFinder. Corre siempre como usuario normal, `requestedExecutionLevel` `asInvoker`. |
| 2 | Leer sensores de hardware: temperaturas, voltajes, RPM, puertos I/O, MSR, SMBus | Requiere driver. No existe forma en modo usuario. El dock no muestra sensores: no es una carencia, es una prohibición. |
| 3 | `SetWindowsHookEx` global y `SetWinEventHook` | Un hook global carga una DLL nuestra dentro de otros procesos, o instala un callback de bajo nivel. *Ese* es el patrón de keylogger. Ver §3.2 para lo que sí se usa. |
| 4 | `CreateRemoteThread`, `WriteProcessMemory`, `VirtualAllocEx`, o cualquier código dentro de `explorer.exe` | Inyección de proceso. Bandera roja inmediata de EDR. |
| 5 | Reemplazo del shell (`Winlogon\Shell`), IFEO, `AppInit_DLLs`, parcheo de binarios | Persistencia de malware por definición. |
| 6 | **Cualquier** llamada de red: sin telemetría, sin updater, sin comprobación de versión, sin analytics, sin informes de fallo | 100% offline. No se enlaza ninguna librería HTTP. Verificable con `netstat` mientras corre. |
| 7 | Persistencia oculta | El autoarranque va en `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, sale en la pestaña Inicio del Administrador de tareas, y se pregunta antes de escribirlo. Nada de `schtasks`, servicios ni carpeta Startup. |
| 8 | Ofuscación, packers, compresión del ejecutable, cifrado de cadenas, single-file comprimido | Bandera roja #1 de las heurísticas: alta entropía y sin secciones legibles es indistinguible de un dropper. Se compila con PDB y secciones normales. |
| 9 | Descargar o generar código en runtime: `Assembly.Load(byte[])`, `Reflection.Emit`, plugins | Ejecutar código no firmado en runtime es comportamiento de loader. |
| 10 | Leer datos del usuario más allá de lo necesario: navegadores, credenciales, documentos, **portapapeles** | Ver §3.6 para el único caso que se abrió, y con qué cortes. |
| 11 | Tocar ventanas ajenas fuera del consentimiento explícito del usuario | Ver §3.1. |
| 12 | Matar procesos: `TerminateProcess`, `TerminateThread`, `EndTask`, `ExitWindowsEx` | Pierde datos sin preguntar. Lo que sí se hace es **pedir** el cierre con `WM_CLOSE`, que la app puede rechazar. Ver §3.1. |

---

## 3. Lo que sí se hace, y por qué se sostiene

Cada apartado dice **qué se abrió**, **por qué es defendible** y **qué sigue cerrado**.
Todos comparten los tres cortafuegos del §1.

### 3.1 Tocar ventanas ajenas

**Qué se hace:** traer al frente, minimizar, restaurar, subir en el orden de apilado, y
**pedir** el cierre con `PostMessage(WM_CLOSE)`.

**Por qué:** es lo que hace la barra de tareas al pulsar su botón, y lo que hace el dock de
macOS. Un dock que abre otra instancia en vez de traer al frente la que ya está abierta no
sirve para nada.

`WM_CLOSE` merece su propio párrafo porque suena peor de lo que es: **no cierra, pide**. Es
el mismo mensaje que manda el botón de cerrar de la propia ventana. La app puede ignorarlo,
o enseñar un "¿guardar los cambios?" — y entonces contesta el usuario, no el dock.

**Cortafuegos:**

- Solo tras un clic del usuario sobre ese icono concreto. No existe ningún camino de código
  que llegue aquí desde un temporizador o un hilo de fondo.
- Solo apps que están en `dock.json`.
- Cerrar, solo desde la ✕ de esa fila de la lista de ventanas. No hay "cerrar todas".

**Sigue cerrado:** matar procesos por cualquier vía (regla 12), responder que sí a los
diálogos de la app, y actuar sobre apps que no estén en el dock.

**Apéndice — la animación de minimizar.** Antes de minimizar se pone
`DWMWA_TRANSITIONS_FORCEDISABLED` en la ventana destino, para que Windows no reproduzca SU
animación encima del efecto genio. Es un atributo **visual** de DWM, **se restaura
inmediatamente** después, y se midió que hace falta: sin él, a los 25 ms de pedir el
minimizado la ventana sigue encogiéndose; con él ya ha desaparecido. Nunca se toca
`SPI_SETANIMATION`, que es un ajuste global del sistema.

### 3.2 Saber qué ventanas hay abiertas

**Qué se hace:** un inventario de las ventanas de nivel superior del escritorio —handle,
proceso, monitor, título— refrescado cuando el shell avisa de un cambio.

**Por qué:** es lo que hace la barra de tareas de Windows, y lo que hacen Rainmeter o
TranslucentTB. De aquí salen el punto de "app abierta", la lista de ventanas de la rueda y
el autoocultar inteligente.

**El aviso NO es un hook.** Se usa `RegisterShellHookWindow`, y la documentación de
Microsoft lo contrasta ella misma con lo otro, textualmente:

> *"Many of the messages are the same as those that can be received after calling the
> SetWindowsHookEx function and specifying WH_SHELL... **The difference with
> RegisterShellHookWindow is that the messages are received through the specified window's
> WindowProc and not through a call back procedure.**"*

No se carga ninguna DLL en ningún proceso ajeno y no se instala ningún callback de bajo
nivel: los avisos llegan a nuestro propio `WndProc` como cualquier otro mensaje. Queda
citado aquí porque la palabra "ShellHook" en un grep se confunde con lo prohibido, y no lo
es.

**Cortafuegos:** el inventario se usa y se tira. No se guarda, no se escribe, no sale del
proceso. Un inventario de handles no es exfiltración; mandarlo a algún sitio sí lo sería, y
la regla 6 sigue intacta.

**Sigue cerrado:** `SetWinEventHook` (regla 3) — se consideró y **no hace falta**.

### 3.3 Leer los píxeles de una ventana

**Qué se hace:** `PrintWindow` con `PW_RENDERFULLCONTENT` sobre un `CreateDIBSection`
propio. Un fotograma cada vez. Lo usan el efecto genio y las miniaturas.

**Por qué así y no con `Windows.Graphics.Capture`:** se probó primero el camino barato y
resultó bastar para las apps reales. Eso elimina de raíz el framepool, la sesión asíncrona
y el **borde amarillo de grabación** que Windows dibuja alrededor de lo que se captura.

**Cortafuegos:**

- Los píxeles van del DIB a una superficie del compositor y **se liberan al acabar**. Sin
  disco, sin red, sin análisis.
- Para el genio: un solo fotograma, tras un clic.
- Para las miniaturas: se repite cada 500 ms **solo mientras el puntero está sobre ese
  icono**, solo esa ventana, y **se para al salir**. No es una sesión de captura: no hay
  nada grabando entre fotograma y fotograma.
- Si la captura vuelve en blanco se detecta y se degrada —minimizar sin animación—. No se
  reintenta por otras vías ni se escala el privilegio.

**Sigue cerrado:** `Windows.Graphics.Capture` con `IsBorderRequired = false`. Quitarle al
usuario el aviso de que le están capturando la pantalla es precisamente lo que querría un
espía. Si algún día hay miniaturas por esa vía, **con el borde puesto**.

### 3.4 Declararse barra de herramientas de escritorio

**Qué se hace:** `SHAppBarMessage` con `ABM_NEW`, `ABM_QUERYPOS`, `ABM_SETPOS`,
`ABM_REMOVE`, `ABM_ACTIVATE`, `ABM_WINDOWPOSCHANGED`, `ABM_GETTASKBARPOS` y
`ABM_SETAUTOHIDEBAREX`.

**Por qué:** API pública de `Shell32` desde XP, actúa sobre **nuestro propio HWND**, no
necesita elevación, y su único uso documentado es literalmente "soy una barra de
herramientas de escritorio" — que es lo que el dock es. `ABM_REMOVE` al salir es
obligatorio: *"An application should always send ABM_REMOVE before destroying an appbar."*

**Sigue cerrado: `ABM_SETSTATE`.** Escribe el ajuste **global** de la barra de tareas del
usuario, el mismo checkbox de sus propiedades. Devuelve siempre `TRUE`, así que no se puede
detectar el fallo, y **no hay ninguna API que lo restaure**. Cambiar un ajuste global sin
pedirlo y sin poder deshacerlo es el perfil del adware de barras de herramientas. Y
encima no consigue lo que se quería: apagar el autoocultar deja la barra *permanentemente*
visible.

### 3.5 Recibir cosas arrastradas

**Qué se hace:** `IDropTarget` + `RegisterDragDrop`. Del objeto soltado se leen rutas y el
AppUserModelID.

**Leer una suelta NO es leer el portapapeles**, aunque los nombres se parezcan —`CF_HDROP`
es un formato *de portapapeles*— y un auditor pueda confundirlos:

- El portapapeles es un almacén del sistema que cualquiera lee en cualquier momento sin que
  el usuario se entere. **Sigue prohibido** (regla 10).
- El objeto de arrastre nos lo entrega el usuario con su gesto, sobre nuestra ventana, y
  deja de ser válido en cuanto acaba la suelta.

**Lanzar con argumentos** (`ShellExecuteExW`) es *la* API de lanzar procesos y es común en
malware, así que: nunca el verbo `runas` ni nada que eleve; la ruta se normaliza con
`GetFullPathName` y se rechaza cualquiera con comillas dobles; va entre comillas en
`lpParameters`; y `ShellExecuteEx` no pasa por `cmd.exe`, así que no hay inyección de
comandos.

**Los accesos directos se leen, nunca se escriben.** `IPersistFile::Load` sí,
**`IPersistFile::Save` prohibido**: escribir `.lnk`, sobre todo en Inicio o el escritorio,
es persistencia de malware.

**Corolario — correr sin elevar es también un requisito funcional.** UIPI bloquea los
mensajes de ventana de un proceso de integridad baja a uno alta, y el arrastre se
implementa con mensajes de ventana. Si alguien arrancase el dock elevado, arrastrar y
soltar dejaría de funcionar **en silencio**. El dock lo detecta al arrancar y lo avisa por
consola. Y **no** se usa `ChangeWindowMessageFilterEx`: relajar UIPI es patrón de escalada
de privilegios y no hace falta corriendo sin elevar.

### 3.6 Leer los documentos recientes de una app

**Qué se hace:** `IApplicationDocumentLists` con `SetAppID` + `GetList`, para enseñar media
lista de saltos en el menú del clic derecho.

**Por qué es defendible:** API pública implementada por el sistema, y la doc no restringe el
AppID al del propio proceso. Pero **esto es leer datos del usuario**, o sea la regla 10, así
que lleva los tres cortes y se sostienen en el código:

- Solo apps que están en `dock.json`.
- Solo al abrir el menú con el clic derecho. **No hay ningún camino desde un temporizador ni
  desde un hilo de fondo.**
- Nunca enumerando las apps del sistema: se pregunta por **una**, la del icono clicado.

Sin esos tres cortes esto es "enumerar los documentos recientes del usuario", y eso se
parece a un infostealer aunque la API sea pública.

**Sigue cerrado:** parsear a mano `%AppData%\...\AutomaticDestinations\*.ms`. Formato no
documentado, ficheros internos del perfil. Es lo que hacen las herramientas forenses y el
malware de exfiltración — y existiendo la API pública no tiene defensa.

### 3.7 Un atajo de teclado

**Qué se hace:** `RegisterHotKey` para **una** combinación, la que el usuario escriba en
`dock.json`, que rota entre perfiles de dock.

**No observa el teclado.** Le pide a Windows que mande `WM_HOTKEY` a **nuestra** ventana
cuando se pulse esa combinación. Sin DLL en procesos ajenos, sin callback de bajo nivel, y
**no se ve ninguna otra tecla**. Es lo contrario de `WH_KEYBOARD_LL`, que sigue prohibido.

Lo único que hay que saber: la combinación queda reservada en todo el sistema mientras el
dock vive. Si otra app ya la tenía, el registro falla y **se avisa por consola** en vez de
quedarse callado. Se libera con `UnregisterHotKey` al cerrar.

**Sigue cerrado:** más de una combinación, registrar cualquiera que el usuario no haya
escrito, y cualquier forma de leer el teclado que no sea esta.

### 3.8 Saber qué carpeta ocupa un juego de Steam

**Qué se hace:** de un acceso directo de Steam que el usuario ha soltado en el dock se lee
su `URL=steam://rungameid/19680`, y con ese número se leen **dos ficheros de texto de
Steam**: `steamapps\libraryfolders.vdf`, que dice dónde están las bibliotecas, y
`steamapps\appmanifest_19680.acf`, que dice en qué carpeta está instalado ese juego.

**Por qué hace falta:** el dock reconoce que una app está abierta por el nombre de su
ejecutable, y el acceso directo de un juego de Steam **no nombra ningún ejecutable**: dice
`steam://rungameid/19680` y nada más. Sin esto, un juego anclado nunca se encendía y, al
abrirlo, aparecía un **segundo** icono en la zona de apps abiertas sin anclar: el del `.exe`
del juego, que el dock no sabía relacionar con el icono que el usuario ya tenía puesto.

**Por qué es defendible:** son ficheros de texto plano, de lectura, sin nada de la cuenta
del usuario dentro —un `.acf` dice el id, el nombre y la carpeta de un juego instalado— y se
consultan por la misma razón por la que el dock ya mira el nombre de un `.exe`: para dibujar
un punto debajo de un icono. Es lo mismo que hace la barra de tareas al agrupar la ventana
de un juego con su botón.

**Cortafuegos:**

- **Solo el appid que ya está en `dock.json`.** No se enumera la biblioteca del usuario ni
  se lee ningún `.acf` que no sea el del juego que el propio usuario ancló.
- **Solo se saca de ahí una ruta de carpeta.** El nombre, el tiempo jugado, las fechas y
  todo lo demás que traiga el fichero se ignora.
- **Se lee y se tira**, como el inventario de ventanas: no se guarda, no se escribe, no sale
  del proceso. La regla 6 sigue intacta.
- **Nunca se escribe** nada dentro de la carpeta de Steam.

**Sigue cerrado:** `loginusers.vdf`, `config.vdf`, `localconfig.vdf`, los `ssfn*` y todo
`Steam\userdata`. Ahí sí hay cuentas, tokens de sesión y datos personales, y leerlos es
exactamente lo que hace el malware que roba cuentas de Steam. No hacen ninguna falta para
saber en qué carpeta está un juego, así que la línea está donde tiene que estar. Lo
comprueba `auditar.ps1`.

**Y sigue cerrado** hacer esto con ningún otro lanzador por su cuenta: Epic, GOG y
Battle.net no se tocan. Si algún día hace falta, se enmienda otra vez.

---

## 4. Descartado, y por qué

No basta con no usarlo: se anota para que se vea que la decisión fue consciente, y para que
la idea no vuelva sin leer esto.

| Idea | Por qué no |
|---|---|
| **`DWMWA_CLOAK` sobre ventanas ajenas** (para el genio, en vez de minimizar) | **No hay rollback.** No existe ningún concepto documentado de "dueño del cloak" ni de limpieza cuando muere el proceso que lo puso: si el dock revienta, esa ventana se queda invisible y el usuario no puede recuperarla. Y no está documentado que funcione entre procesos — la doc de `DwmRegisterThumbnail` sí pone un límite de proceso explícito; que aquí calle no es permiso, es silencio. Compárese con `DWMWA_TRANSITIONS_FORCEDISABLED`, que se defiende bien justamente porque *se restaura*. `DWMWA_CLOAKED` **de lectura** sí se usa, para filtrar ventanas fantasma. |
| **`DwmRegisterThumbnail`** (para las miniaturas) | Descartado por **imposible, no por prohibido**: no existe ninguna API que convierta un `HTHUMBNAIL` en un `Visual`, `CompositionSurface` ni `CompositionBrush`. Solo se dibuja contra un HWND. Para un dock hecho entero con el compositor, eso es sin esquinas redondeadas, sin material de fondo y sin poder animarlo. |
| **`ITaskbarList3`** (para leer el progreso de otras apps) | **Imposible.** No existe ningún método `Get*` en `ITaskbarList` 1, 2, 3 ni 4: son interfaces de solo escritura sobre HWND propios y el estado vive dentro de `explorer.exe`. No se añade el P/Invoke. |
| **`ChangeWindowMessageFilterEx`** | Relajar UIPI es patrón de escalada de privilegios, Microsoft lo desaconseja para este caso, y no hace falta corriendo sin elevar. |
| **`IAssocHandler::MakeDefault`** | Cambiar la app por defecto de un tipo de fichero es comportamiento de *hijacker*. |
| **MSIX con `graphicsCaptureWithoutBorder`** | Es el único camino para quitar el borde de captura, y ver §3.3: quitarlo es lo que querría un espía. |

---

## 5. Corolarios de diseño

Consecuencias de las reglas de arriba que afectan a cómo se compila y se escribe el código.

- **Sin single-file comprimido ni `PublishTrimmed`.** `EnableCompressionInSingleFile`
  produce exactamente el perfil de entropía que dispara heurísticas, y recortar IL hace el
  binario ilegible para un analizador estático. `Dock.csproj` los pone a `false`
  explícitamente, no por omisión.
- **La config vive en texto plano legible.** Nada de formato binario propietario.
- **`dock.json` es del usuario y no se toca.** Lo que el dock cambia al reordenar, añadir o
  cambiar de perfil va a `dock.local.json`, también en texto plano.
- **Lanzar apps** se hace con `Process.Start` + `UseShellExecute = true`, o
  `ShellExecuteExW` cuando hay que pasar un fichero. Ambas delegan en el shell. Nada de
  `CreateProcess` con flags raros.
- **La lista de P/Invokes es cerrada y auditable.** `NativeMethods.txt` es el fichero de
  entrada de CsWin32: si una función no está ahí, no se genera, y el código **no compila**.
  No puede desviarse de lo que el binario realmente usa.

---

## 6. Cómo se audita

```powershell
pwsh -File auditar.ps1
```

Comprueba cada regla y sale con código 0 si todo está limpio.

**Mira solo código, no comentarios ni documentación**, y eso no es un atajo: este mismo
documento nombra todas las APIs prohibidas para explicar por qué lo están, y los fuentes
llevan comentarios del estilo *"`ABM_SETSTATE` no está aquí y no va a estar"*. Un `grep` a
secas se encuentra a sí mismo, nunca sale limpio, y deja de servir de puerta. La versión
anterior de este documento tenía siete greps y **ninguno** podía salir limpio.

Además del script, dos comprobaciones a mano antes de dar por bueno un binario:

```powershell
# No debe encontrar nada.
& "C:\Program Files\Windows Defender\MpCmdRun.exe" -Scan -ScanType 3 -File "$env:LOCALAPPDATA\Dock\app\Dock.exe"

# Sin red: con el dock corriendo, no debe aparecer ninguna conexion suya.
Get-NetTCPConnection -OwningProcess (Get-Process Dock).Id -ErrorAction SilentlyContinue
```

---

## 7. Cómo se enmienda

1. Se escribe **antes** de tocar el código, no después.
2. Dice qué se abre, por qué es defendible, **qué cortafuegos lo sostienen en el código** y
   qué sigue cerrado.
3. Si añade una API que un grep podría confundir con algo prohibido, se cita la
   documentación que las distingue.
4. Se añade su comprobación a `auditar.ps1`.
5. Se commitea en el mismo commit que el código que la necesita, o antes.

El historial de las cuatro enmiendas que llevaron a este documento —ventanas ajenas,
arrastrar y soltar, inventario y barra de tareas, cerrar ventanas— está en `git log`.
