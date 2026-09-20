# Superficie de riesgo — documento vinculante

Este documento gobierna todo el desarrollo de Quick Look. Si una función futura necesita
algo prohibido, **la función se rediseña o se descarta**. No se piden excepciones de
palabra: se enmienda este documento por escrito, con su justificación, y queda en el
historial de git.

Es hermano del `SEGURIDAD.md` del dock, pero **no es el mismo documento**, y la diferencia
importa: aquí el hook de teclado está **abierto y acotado**, y allí sigue prohibido. Cada
utilidad responde de su propio binario. Si algún día los dos se unifican en un documento
central, será una decisión consciente y con las dos listas delante, no un descuido.

El objetivo es el mismo que el del dock: **que ningún antivirus tenga un motivo razonable
para marcar este binario**. Con un hook de teclado dentro, ese objetivo deja de ser
gratis, y por eso el §2 es más estricto que el del dock en todo lo demás: es lo único que
sostiene la defensa.

---

## 1. El criterio

Lo que separa lo legítimo de lo sospechoso **no es qué API se llama**, sino tres cosas:

| | Legítimo | Sospechoso |
|---|---|---|
| **Quién empieza** | El usuario, pulsando espacio sobre un archivo que él ha seleccionado | Un temporizador, un hilo de fondo, un evento del sistema |
| **Sobre qué** | El archivo seleccionado, uno, ahora | Carpetas que el usuario no ha abierto, otras teclas, otras ventanas |
| **A dónde va** | Se lee, se dibuja y se tira | A disco, a la red, a un análisis |

Y hay una cuarta que aquí pesa más que las tres: **cuánto se ve**. El hook de teclado no
se defiende diciendo que es pequeño, se defiende **siendo legible**: unas 40 líneas en un
fichero que se llama `Hook.cs`, sin buffers, sin estado, sin ramas ocultas. Un auditor
tiene que poder leerlo entero en un minuto y quedarse tranquilo. Si algún día ese fichero
necesita crecer, la pregunta no es cómo escribirlo mejor: es si la función que lo hace
crecer merece existir.

---

## 2. Lo que este programa no va a hacer, nunca

| # | Prohibido | Por qué |
|---|---|---|
| 1 | Driver de kernel (`.sys`), servicio de Windows, tarea programada, o cualquier componente elevado | Corre siempre como usuario normal, `requestedExecutionLevel` `asInvoker`. Un hook de teclado en un proceso elevado es otra categoría de cosa. |
| 2 | Leer sensores de hardware: temperaturas, voltajes, RPM, puertos I/O, MSR, SMBus | Requiere driver. No hace ninguna falta para previsualizar un archivo. |
| 3 | `SetWinEventHook`, y `SetWindowsHookEx` con cualquier valor que **no** sea `WH_KEYBOARD_LL` | Los hooks globales *de los otros tipos* (`WH_CBT`, `WH_GETMESSAGE`, `WH_SHELL`…) cargan una DLL nuestra dentro de cada proceso enganchado: eso sí es inyección. Ver §3.1 para el único que se abre y con qué cortes. |
| 4 | `CreateRemoteThread`, `WriteProcessMemory`, `VirtualAllocEx`, o cualquier código dentro de `explorer.exe` | Inyección de proceso. Bandera roja inmediata de EDR. Con el Explorador se habla por COM, desde fuera. |
| 5 | Reemplazo del shell (`Winlogon\Shell`), IFEO, `AppInit_DLLs`, parcheo de binarios | Persistencia de malware por definición. |
| 6 | **Cualquier** llamada de red: sin telemetría, sin updater, sin comprobación de versión, sin analytics, sin informes de fallo | 100% offline. No se enlaza ninguna librería HTTP. Verificable con `netstat` mientras corre. **Con un hook de teclado dentro, esta regla es la que separa esto de un keylogger.** Un keylogger que no puede hablar con nadie no es un keylogger. |
| 7 | Persistencia oculta | El autoarranque va en `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, sale en la pestaña Inicio del Administrador de tareas, y se pregunta antes de escribirlo. Nada de `schtasks`, servicios ni carpeta Startup. |
| 8 | Ofuscación, packers, compresión del ejecutable, cifrado de cadenas, single-file comprimido | Bandera roja #1 de las heurísticas. **Y aquí es innegociable**: un binario con un hook de teclado y alta entropía no tiene defensa posible. Se compila con PDB y secciones normales. |
| 9 | Descargar o generar código en runtime: `Assembly.Load(byte[])`, `Reflection.Emit`, plugins | Ejecutar código no firmado en runtime es comportamiento de loader. |
| 10 | Escribir en disco cualquier cosa que venga del teclado, del contenido de un archivo previsualizado, o del nombre de un archivo | La única escritura del programa es `quicklook.local.json` (geometría y preferencias) y la clave de autoarranque. Nada más. |
| 11 | El **portapapeles** | Ni leerlo ni escribirlo. No hace falta para nada de lo que hace esto. |
| 12 | Enviar teclas o mover el ratón: `SendInput`, `keybd_event`, `mouse_event`, o mensajes de teclado a ventanas ajenas | Leer una tecla para decidir si abrir un panel es una cosa; **sintetizar** entrada es automatizar al usuario sin su gesto, y es la otra mitad del perfil de un troyano. El programa no escribe en ningún sitio. |
| 13 | Matar procesos, gobernar ventanas ajenas, cambiar ajustes globales del sistema | Quick Look dibuja una ventana propia encima. No manda sobre nada de nadie. |

---

## 3. Lo que sí se hace, y por qué se sostiene

### 3.1 El hook de teclado

**Qué se hace:** `SetWindowsHookEx(WH_KEYBOARD_LL, …, hMod, 0)` para detectar **una sola
tecla**, la barra espaciadora, y solo cuando la ventana en primer plano es el Explorador de
archivos o el Escritorio.

**Por qué no hay alternativa.** Se evaluaron las tres:

- `RegisterHotKey(VK_SPACE)` sin modificador reserva la barra espaciadora **en todo el
  sistema** mientras el programa vive. Escribir deja de funcionar en todas partes.
- Registrarla y liberarla según quién esté en primer plano obliga a un temporizador que
  sondea, y sigue comiéndose el espacio al renombrar un archivo o al escribir en la caja de
  búsqueda del propio Explorador.
- Un modificador (`Ctrl+Alt+Espacio`) no es Quick Look. Lo que se extraña de macOS es el
  espacio pelado; con modificador la función no merece existir.

**Por qué es defendible, y qué lo distingue de un keylogger:**

1. **No inyecta nada en ningún proceso.** Es la confusión más común y conviene dejarla
   zanjada. `WH_KEYBOARD_LL` no carga ninguna DLL en ningún sitio; la documentación de
   Microsoft lo dice explícitamente:

   > *"This hook is called in the context of the thread that installed it. The call is made
   > by sending a message to the thread that installed the hook. Therefore, the thread that
   > installed the hook must have a message loop."*

   El callback corre **en nuestro propio proceso, en nuestro propio hilo**. Ni una línea de
   código nuestro entra en `explorer.exe` ni en ningún otro sitio. La regla 4 sigue intacta
   y es comprobable desde fuera: ningún proceso ajeno tiene un módulo nuestro cargado (el
   comando está en el §6).

2. **Lo mismo hace Microsoft.** PowerToys —de Microsoft, firmado por Microsoft, instalado
   por millones— usa `WH_KEYBOARD_LL` en Keyboard Manager, en PowerToys Run y en Shortcut
   Guide. Es también como AutoHotkey, Everything y cualquier gestor de atajos hacen su
   trabajo. No es una API oscura: es *la* forma soportada de que una app de escritorio vea
   un atajo global.

3. **Lo que no se hace es lo que define al keylogger**, y está prohibido por escrito en el
   §2: no hay red (regla 6), no se escribe nada del teclado en disco (regla 10), y no se
   sintetiza entrada (regla 12). Un observador de una sola tecla, sin salida, sin
   almacenamiento y sin escritura, no exfiltra nada porque no tiene a dónde.

**Cortafuegos, y están en el código, no aquí:**

- **Uno.** `Hook.cs` es el **único** fichero que nombra `WH_KEYBOARD_LL`, y lo comprueba
  `auditar.ps1`. Si aparece en otro sitio, la auditoría falla.
- **Dos teclas, y la segunda solo a veces.** El callback compara contra `VK_SPACE`, y
  contra `VK_ESCAPE` **únicamente mientras hay un panel abierto**. `auditar.ps1` comprueba
  que dentro de `Hook.cs` no hay ninguna otra constante `VK_*` salvo esas dos y las de
  modificador, y que el interruptor que acota la segunda existe.
- **Solo hacia delante.** `nCode < 0`, o cualquier mensaje que no sea `WM_KEYDOWN` /
  `WM_SYSKEYDOWN`, sale por `CallNextHookEx` en la primera línea del callback.
- **Solo con el Explorador delante.** `GetForegroundWindow` + `GetClassNameW`:
  `CabinetWClass`, `ExploreWClass`, `WorkerW` o `Progman`. En cualquier otra app el espacio
  pasa de largo sin que nadie lo mire.
- **Sin memoria.** Ni buffer, ni contador, ni fichero, ni traza. El callback hace
  `PostMessage` a nuestra ventana y devuelve. No hay ninguna variable en el programa que
  acumule nada relacionado con el teclado.
- **Se desinstala al salir.** `UnhookWindowsHookEx` en el camino de cierre y en el de
  excepción.

**`GetKeyState` se abre, y solo aquí.** Hace falta para dejar pasar `Ctrl+Espacio`,
`Alt+Espacio` y `Shift+Espacio`, que son atajos del sistema y de otras apps. Se llama
**únicamente dentro del callback** y **únicamente** con `VK_CONTROL`, `VK_MENU` y
`VK_SHIFT`. `GetAsyncKeyState` sigue prohibido: puede consultarse desde cualquier sitio en
cualquier momento sin que haya ocurrido ningún evento, y esa es justo la diferencia.

**Y sigue cerrado lo más importante: que el programa se cruce con lo que escribes.** Si el
foco está en un campo de texto —renombrar con F2, la caja de búsqueda, la barra de
direcciones— el espacio **pasa de largo**. Se comprueba con `GetGUIThreadInfo` sobre el
hilo de la ventana en primer plano, leyendo `hwndFocus` y su clase. Esto no es solo una
cortesía de usabilidad: es el cortafuegos que garantiza que el programa nunca se interpone
en un sitio donde el usuario está escribiendo.

**Sigue cerrado:** cualquier otra tecla, cualquier otro tipo de hook, el hook activo
cuando el Explorador no está delante, guardar lo que sea, y `SendInput` en cualquiera de
sus formas.

> **Enmienda 2, M7.1 — `Esc` cierra el panel.** Se abre una segunda tecla, `VK_ESCAPE`, y
> hay que decir por qué no es el principio de una pendiente.
>
> **Qué la acota, y está en el código:** el callback solo la mira **si hay un panel
> abierto**. Con el panel cerrado —que es el 99,9% del tiempo que el programa está vivo— la
> tecla `Esc` sale por `CallNextHookEx` en la misma comparación que cualquier otra, y el
> programa no se entera de que existe. El interruptor lo pone y lo quita `HostWindow`, que
> es quien abre y cierra el panel, y no hay ningún otro camino que lo toque.
>
> **Por qué merece la pena:** cerrar con `Esc` es lo que hace Quick Look en macOS y es el
> gesto que el usuario ya tiene en los dedos. Sin él la única salida cómoda era pulsar
> espacio otra vez, y eso obliga a volver al Explorador si te habías ido.
>
> **Y por qué no rompe nada del Explorador:** `Esc` ahí cancela un renombrado, cierra la
> caja de búsqueda y quita la selección. Los dos primeros siguen funcionando porque el
> cortafuegos de `GetGUIThreadInfo` ya deja pasar cualquier tecla cuando hay un cursor de
> texto parpadeando, y el tercero solo se ve afectado mientras tienes un panel delante —
> momento en el que lo que quieres cerrar es el panel.
>
> **Sigue cerrado:** mirar `Esc` con el panel cerrado, y una tercera tecla. Si alguna vez
> hacen falta las flechas para cambiar de archivo desde el panel, será otra enmienda y
> tendrá que justificar por qué no vale el temporizador que ya existe.

### 3.2 Leer qué archivo está seleccionado en el Explorador

**Qué se hace:** `IShellWindows` → `IServiceProvider` → `IShellBrowser` → `IShellView` →
`IFolderView2`, para pedirle a la ventana del Explorador que está en primer plano **la ruta
del elemento seleccionado**.

**Por qué es defendible:** es COM público y documentado, **fuera de proceso**, y es la vía
que Microsoft documenta para automatizar el Explorador. No se inyecta nada (regla 4
intacta), no se lee memoria ajena, no se toca el portapapeles (regla 11).

Pero **esto es leer datos del usuario**, así que lleva los tres cortes del §1 y se
sostienen en el código:

- **Solo durante el gesto.** Sin espacio pulsado no se pregunta nada: con el panel cerrado
  no hay temporizador, no hay hilos y no existe ningún camino de código que llegue a
  `Selection.cs`. **Mientras el panel está abierto** —y solo mientras— se vuelve a preguntar
  cada 200 ms, para que marcar otro archivo haga que la tarjeta cambie a él en vez de
  obligar a cerrar y abrir. Es el mismo trato que el dock le da a las miniaturas de ventana
  en su §3.3: se repite **mientras dura el gesto**, sobre lo mismo, y se para al acabarlo.
  El gesto aquí no es la pulsación, es el rato que tienes el panel delante.
- **Un solo sitio la llama.** `Selection.Path` solo se invoca desde `HostWindow.cs`, que es
  quien abre el panel y quien lleva su temporizador. Lo comprueba `auditar.ps1`: si aparece
  en cualquier otro fichero —un hilo de fondo, un handler, lo que sea— la auditoría falla.
  Eso es lo que sostiene el punto de arriba en el código y no solo en este párrafo.
- **Solo lo seleccionado, y solo su ruta.** `SVGIO_SELECTION`, no `SVGIO_ALLVIEW`. Nunca se
  enumera el contenido de la carpeta, ni carpetas que el usuario no tenga abiertas. De lo
  que devuelve el shell se saca la ruta y se ignora todo lo demás.
- **Se usa y se tira.** La ruta vive en una variable mientras el panel está abierto y se
  suelta al cerrarlo. No se guarda, no se escribe, no se acumula un historial de lo que has
  mirado. Las reglas 6 y 10 siguen intactas.

**Sigue cerrado:** `SVGIO_ALLVIEW` y enumerar la vista entera, leer columnas o metadatos
más allá de lo que se dibuja en la ficha, hablar con ventanas del Explorador que no sean la
que está en primer plano, y preguntar nada con el panel cerrado.

> **Enmienda 1, M4.** El párrafo "solo durante el gesto" decía antes *"no existe ningún
> camino de código que llegue a `Selection.cs` que no venga del `WM_APP_QUICKLOOK` que manda
> el hook"*, y el morph al cambiar de archivo lo rompía: lo llama un temporizador. Se podía
> haber dejado pasar —el temporizador solo vive mientras el panel está abierto— pero
> entonces este documento habría empezado a describir un programa que ya no era. Se
> reescribe el cortafuegos con lo que de verdad hace, se apoya en el precedente del §3.3 del
> dock, y se le añade la comprobación en `auditar.ps1` que lo ata: una sola llamada, desde
> un solo fichero.

### 3.3 Leer el archivo para dibujarlo

**Qué se hace:** abrir el archivo seleccionado en **solo lectura** y con
`FileShare.ReadWrite`, y sacarle píxeles o texto. Cuatro caminos, todos del sistema:

| Camino | Para qué |
|---|---|
| `IShellItemImageFactory::GetImage` | La miniatura que el shell ya sabe hacer. Cubre imágenes, PDFs, vídeos y Office. |
| `Windows.Data.Pdf.PdfDocument` | Páginas de PDF, con el renderizador del propio Windows. |
| `Windows.Media.Playback.MediaPlayer` | Vídeo y audio, con el decodificador del propio Windows. |
| Lectura directa + DirectWrite | Texto y código. **Los primeros 256 KB y ni un byte más.** |

**Cortafuegos:**

- **Solo el archivo seleccionado.** Uno cada vez, el que el usuario tiene marcado.
- **Solo lectura.** El programa **nunca** abre un archivo del usuario con permiso de
  escritura. Ni para mover, ni para renombrar, ni para borrar. No hay ninguna llamada de
  escritura sobre archivos del usuario en todo el código, y `auditar.ps1` lo comprueba.
- **Se dibuja y se tira.** Los píxeles van a una superficie del compositor y se liberan al
  cerrar el panel. Sin caché en disco, sin miniaturas guardadas, sin índice.
- **Con techo.** 256 KB para texto, una página de PDF en memoria cada vez, la miniatura a
  la resolución del panel. No se carga un archivo de 4 GB en RAM para enseñar la primera
  pantalla.

**Sigue cerrado:** escribir, renombrar, mover o borrar archivos del usuario; cachear
previsualizaciones en disco; y previsualizar nada que el usuario no haya seleccionado él.

### 3.4 Una ventana que no roba el foco

**Qué se hace:** `CreateWindowEx` con `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST`,
`ShowWindow(SW_SHOWNOACTIVATE)` y `MA_NOACTIVATE` como respuesta a `WM_MOUSEACTIVATE`.

**Por qué:** es criterio de aceptación, igual que en el dock, y aquí además es funcional. Si
el panel robase el foco, el Explorador perdería el resaltado de la selección, las flechas
dejarían de cambiar de archivo, y el segundo espacio no llegaría por el mismo camino que el
primero.

Actúa **solo sobre nuestro propio HWND**. No se llama `SetForegroundWindow`, ni
`AttachThreadInput`, ni `SetWindowPos` sobre ventanas ajenas.

### 3.5 Autoarranque

`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, escrito solo si el usuario lo pide
desde la config, visible en la pestaña Inicio del Administrador de tareas y borrable desde
ahí. Es el **único** sitio del registro donde este programa escribe.

---

## 4. Descartado, y por qué

| Idea | Por qué no |
|---|---|
| **Extensión de shell (preview handler propio)** | Es una DLL que `explorer.exe` carga dentro de sí mismo. Funcionaría, pero un fallo nuestro tira el Explorador del usuario, y una DLL propia dentro del shell es exactamente lo que la regla 4 existe para evitar. La ventana propia por encima es más fea de montar y mucho más fácil de defender. |
| **`RegisterHotKey(VK_SPACE)`** | Reserva la barra espaciadora en todo el sistema mientras el programa vive. Rompe escribir en todas partes. Ver §3.1. |
| **`SetWinEventHook` para saber cuándo cambia la selección** | Es un callback del sistema sobre eventos de accesibilidad de *todos* los procesos, y para esto no hace falta: mientras el panel está abierto, un `SetTimer` propio de 200 ms pregunta por la selección, y cuando está cerrado no hay nada corriendo. |
| **Cachear miniaturas en disco** | Arrancaría más rápido la segunda vez y crearía un índice de los archivos que el usuario ha mirado. La regla 10 dice que no, y el shell ya tiene su propia caché. |
| **Previsualizar dentro de `.zip`** | Descomprimir en temporal es escribir en disco contenido del usuario. Si algún día hace falta, se enmienda con su propio apartado. |
| **Abrir el archivo con permiso de escritura "por si acaso"** | No hay ningún "por si acaso". Ver §3.3. |

---

## 5. Corolarios de diseño

- **`Hook.cs` se queda pequeño.** Es el fichero que mira un auditor primero. Si crece, la
  pregunta es qué función lo hizo crecer y si merece existir.
- **Sin single-file comprimido ni `PublishTrimmed`.** `QuickLook.csproj` los pone a `false`
  explícitamente, no por omisión.
- **La config vive en texto plano legible.** `quicklook.json` es del usuario y no se toca;
  lo que el programa cambia va a `quicklook.local.json`.
- **La lista de P/Invokes es cerrada y auditable.** `NativeMethods.txt` es el fichero de
  entrada de CsWin32: si una función no está ahí, no se genera, y el código **no compila**.
  No puede desviarse de lo que el binario realmente usa.
- **Nada corre cuando no hay panel abierto.** Sin temporizadores, sin hilos, sin sondeo. Lo
  único vivo es el hook esperando una tecla. Esto no es una optimización: un proceso que no
  hace nada en segundo plano no tiene nada que explicar.

---

## 6. Cómo se audita

```powershell
pwsh -File auditar.ps1
```

Comprueba cada regla y sale con código 0 si todo está limpio. **Mira solo código, no
comentarios ni documentación**: este documento nombra todas las APIs prohibidas para
explicar por qué lo están, y un grep a secas se encontraría a sí mismo y nunca saldría
limpio.

Además del script, tres comprobaciones a mano antes de dar por bueno un binario:

```powershell
# No debe encontrar nada. Con un hook de teclado dentro, esta es LA comprobacion.
& "C:\Program Files\Windows Defender\MpCmdRun.exe" -Scan -ScanType 3 -File "$env:LOCALAPPDATA\QuickLook\app\QuickLook.exe"

# Sin red: con el programa corriendo, no debe aparecer ninguna conexion suya.
Get-NetTCPConnection -OwningProcess (Get-Process QuickLook).Id -ErrorAction SilentlyContinue

# Sin modulos nuestros dentro de otros procesos: demuestra que WH_KEYBOARD_LL no inyecta.
(Get-Process explorer).Modules | Where-Object { $_.ModuleName -like "*QuickLook*" }
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
