# Historial

Por hitos, con lo que se midió. Los descartes también se anotan: la idea que no se escribe
vuelve sola al cabo de un mes.

---

## M0 — Andamio y SEGURIDAD.md

El documento vinculante se escribió **antes** que el código, que es lo que manda el §7 del
documento del dock.

**La decisión del hito:** abrir `WH_KEYBOARD_LL`, que en el dock está prohibido por la
regla 3. Se evaluaron las tres alternativas antes de decidirlo:

- `RegisterHotKey(VK_SPACE)` sin modificador reserva la barra espaciadora en todo el
  sistema mientras el programa vive. Escribir deja de funcionar en todas partes.
- Registrarla y liberarla según quién esté en primer plano obliga a un temporizador que
  sondea, y **sigue** comiéndose el espacio al renombrar un archivo con F2 o al escribir en
  la caja de búsqueda del propio Explorador. El problema que se quería evitar no se evita.
- Un modificador (`Ctrl+Alt+Espacio`) no es Quick Look. Lo que se extraña de macOS es el
  espacio pelado.

Así que el hook, con seis cortafuegos en el código y su comprobación en `auditar.ps1`. Y la
defensa no descansa en que el hook sea pequeño, sino en lo que el §2 cierra alrededor: sin
red, sin escribir nada del teclado en disco, sin sintetizar entrada, sin ofuscar el
binario.

**`auditar.ps1` cambia de forma respecto al del dock.** Allí basta con buscar
`WH_KEYBOARD_LL` y fallar si aparece. Aquí está permitido, así que el script no comprueba
que *no* esté: comprueba que esté **donde tiene que estar** y que no haga más de lo que
dice el §3.1.

- `WH_KEYBOARD_LL` y `SetWindowsHookEx` solo pueden aparecer en `Hook.cs`.
- Dentro de `Hook.cs`, la única constante `VK_*` permitida además de `VK_SPACE` son las de
  modificador — y están abiertas solo para poder **dejar pasar** `Ctrl+Espacio` y
  compañeros.
- `UnhookWindowsHookEx` tiene que existir.
- Se vigila que `Hook.cs` no engorde: el §5 dice que se queda pequeño, y el script avisa
  pasadas las 90 líneas de código.

Reglas nuevas que el dock no tiene: **12 — no sintetizar entrada** (`SendInput`,
`keybd_event`), que es la otra mitad del perfil de un troyano y aquí había que cerrarla
explícitamente; y **A — no escribir archivos del usuario**, porque un previsualizador que
puede escribir no es un previsualizador.

**Descartado y anotado:** la extensión de shell (preview handler propio). Funcionaría, pero
es una DLL nuestra dentro de `explorer.exe`: un fallo nuestro tira el Explorador del
usuario, y es exactamente lo que la regla 4 existe para evitar. La ventana propia por
encima es más fea de montar y mucho más fácil de defender.

**Medido:** `dotnet build` en 3,9 s, 0 errores y 0 advertencias. `auditar.ps1` sale `TODO
LIMPIO` con 16 reglas comprobadas y 10 entradas en `NativeMethods.txt`. El programa arranca
y se queda en el bucle de mensajes sin hacer nada, que es todo lo que se le pide a este
hito.

**Ficheros:** `QuickLook.csproj`, `app.manifest`, `NativeMethods.txt`, `SEGURIDAD.md`,
`auditar.ps1`, `README.md`, `CLAUDE.md`, `CHANGELOG.md`, `quicklook.json`, `.gitignore`,
`Program.cs`, `HostWindow.cs`.

---

## M1 — Espacio → panel

El hook, la ventana-host y el panel acrílico vacío. Ya se abre y se cierra con el mismo
gesto; todavía no enseña nada dentro.

**`Hook.cs`, 88 líneas de código.** El §5 dice que se queda pequeño y el script avisa a las
90, así que el margen es de dos líneas a propósito: la siguiente función que quiera entrar
ahí tendrá que justificarse.

El filtro, en orden, y cada corte sale por `CallNextHookEx` sin mirar nada más:

1. `code < 0`, o no es `WM_KEYDOWN` / `WM_SYSKEYDOWN`.
2. No es `VK_SPACE`.
3. Hay Ctrl, Alt o Shift pulsado — `Ctrl+Espacio` y compañía son de otros.
4. La ventana en primer plano no es `CabinetWClass`, `ExploreWClass`, `WorkerW` ni
   `Progman`.
5. Se está escribiendo: hay un cursor de texto (`hwndCaret`) o el foco está en un `Edit`,
   `ComboBox`, `RichEditD2DPT` o la caja de búsqueda.

El 5 es el que decide si esto se puede usar a diario. **Se comprueba primero con
`hwndCaret`, no con el nombre de la clase**: un caret parpadeando significa que hay alguien
escribiendo, y no depende de acertar con el nombre interno de un control que Microsoft puede
cambiar. La lista de clases queda como red de seguridad debajo.

**`SetWindowsHookEx` no devuelve un SafeHandle**, devuelve un `HHOOK` pelado, así que
`UnhookWindowsHookEx` se llama a mano en `Dispose`. Mejor para la auditoría: la llamada
aparece literal en el código en vez de escondida en el `Dispose` de un tipo generado.

**Medido con una sonda** (`scratchpad/sonda-panel.ps1`, que manda el `WM_APP` con
`PostMessage` en vez de pulsar la tecla): el panel abre a 1587x777 centrado en el área de
trabajo, `IsWindowVisible` = true, **`GetForegroundWindow` no devuelve el panel** —no roba
el foco, que es el criterio de aceptación— y el segundo aviso lo cierra.

**La sonda se equivocó dos veces antes de que el código se equivocara ninguna**, que es
exactamente lo que avisa el `CLAUDE.md`:

- `GetClassNameW` declarado sin `CharSet=CharSet.Unicode`: el `StringBuilder` se marshala
  como ANSI, la función W escribe UTF-16 dentro, y al leerlo todas las clases salían
  cortadas en la primera letra (`QuickLookHostClass` → `Q`). Parecía que el registro de
  clase estaba roto.
- PowerShell convierte `$null` en cadena vacía al pasarlo a un parámetro `string`, así que
  `FindWindow(clase, $null)` buscaba una ventana **sin título** y no encontraba nada. Hay
  que pasar `[NullString]::Value`. Esto dio un "la ventana-host no existe" con la ventana
  perfectamente creada.

Las dos trampas están anotadas en la cabecera de la sonda para no repetirlas.

**Lo que la sonda NO prueba**, y hay que pulsar a mano porque sintetizar teclas está
prohibido por la regla 12: que el espacio llegue entero a Word y a Chrome, y que renombrar
con F2 siga aceptando espacios.

**Ficheros:** `Hook.cs`, `Visuals.cs`, `Panel.cs`, `HostWindow.cs`, `Program.cs`,
`NativeMethods.txt` (33 entradas).

---

## M2 + M3 — La selección del Explorador, la ficha y la miniatura

Van juntos porque son la misma llamada: `IShellItemImageFactory::GetImage` **con**
`SIIGBF_ICONONLY` da el icono del tipo, y **sin** esa bandera da la miniatura real del
contenido. Imágenes, PDFs, vídeos y Office salen del mismo camino, sin decodificador
propio y sin una sola dependencia nueva.

### Lo que costó encontrar

**`IShellBrowser::GetWindow` no devuelve el marco del Explorador.** Con una ventana de
carpeta en primer plano (`0x812B6`), la colección `IShellWindows` daba `0x390EA2` y
`0x51402`, y ninguna coincidía: en el Explorador con pestañas de Windows 11, cada pestaña
es una ventana de shell y **cuelga** del marco. Se empareja por `GetAncestor(GA_ROOT)`, y
además se exige `IsWindowVisible` sobre la ventana de la pestaña, porque un marco con
varias pestañas tiene varias ventanas de shell y solo la de delante está visible.

**`IFolderView::Items(SVGIO_SELECTION, IID_IShellItemArray)` contesta `0x80070490`
(ERROR_NOT_FOUND) aunque haya selección.** Costó porque parecía una carrera: la sonda
creaba la carpeta y medía enseguida. Lo que zanjó la duda fue preguntarle a otro por el
mismo dato — `Shell.Application`, `$w.Document.SelectedItems()` decía **1 elemento** en el
mismo instante. Con eso ya no era el Explorador, éramos nosotros. La llamada correcta es
**`IFolderView2::GetSelection`**, que contesta a la primera. Va con
`fNoneImpliesFolder = false`: sin selección se quiere "nada", no la carpeta entera —
enseñar la carpeta porque el usuario no marcó ningún archivo sería peor que no abrir.

**`Compositor.As<ICompositorInterop>()` tiraba `InvalidCastException`, y el problema no
era COM.** El mismo código funciona en el dock a diario, el IID generado es idéntico
(`25297D5C-…`) y el `.csproj` no se diferencia en nada. En aislado funcionaba; dentro del
flujo real, no. Lo resolvió meter el objeto en una variable local: `Compositor` nombra a
la vez la propiedad estática de `Visuals` y el **tipo** `Windows.UI.Composition.Compositor`
importado arriba, y en esa llamada el compilador lo resuelve contra el tipo, **compila sin
una sola advertencia** y revienta en ejecución con algo que parece un `E_NOINTERFACE` del
compositor. Se confirmó volviendo a poner el código malo y viendo fallar otra vez. Arreglo:
dentro de `Visuals` se usa `Ensure()` y nunca la propiedad; desde fuera se escribe
`Visuals.Compositor`, que no es ambiguo.

**Si `Build` fallaba, la ventana se quedaba huérfana.** `Panel.Open` crea el HWND antes
que el contenido, así que el `catch` devolvía null con la ventana ya creada y sin nadie que
la cerrara. Peor todavía: la sonda la encontraba por clase, medía 460x300 y daba el caso
por bueno mientras el log decía que no se había podido abrir. Ahora el `catch` hace
`Dispose`.

### La sonda, otra vez, antes que el código

Tres fallos de método más, todos anotados en la cabecera de `sonda-contenido.ps1`:

- Ventanas del Explorador de sondas anteriores apuntando a carpetas ya borradas: el shell
  contesta `0x80070490` y parece que la selección está rota cuando lo que está rancia es la
  ventana. Se cierran todas antes de medir y se usa una carpeta nueva en cada pasada.
- El filtro de líneas del log (`seleccion|shell|preview|panel`) se comía las trazas `[paso]`
  que acababa de poner para diagnosticar, y parecía que el código no llegaba a ejecutarse.
- Una ventana del Explorador recién abierta tarda en asentar la selección, así que el
  `/select` se repite en cada reintento.

### Medido

- Imagen de 800x400 → panel de **832x478**: la imagen queda a **800x400** exactos dentro
  (16 de margen por lado y 46 de pie), proporción 2,000. **No se agranda** por encima de su
  tamaño nativo: estirar una miniatura solo enseña los píxeles más grandes.
- `.zip` → panel de **460x300**, la ficha fija, con icono y datos.
- `--check` con cuatro bloques: premultiplicado, encaje del panel, clasificador de
  extensión y tamaño legible. Se le metió el fallo a propósito —quitar el tope de escala—
  y la prueba lo cazó: *"una miniatura de 64 se estiró a 674"*.
- `auditar.ps1`: `TODO LIMPIO`, 85 entradas en `NativeMethods.txt`, `Hook.cs` sigue en 88
  líneas.

**Ficheros:** `Selection.cs`, `Shell.cs`, `Text.cs`, `Content/Preview.cs`, `SelfCheck.cs`,
`Visuals.cs`, `Panel.cs`, `HostWindow.cs`, `Program.cs`, `NativeMethods.txt`.
