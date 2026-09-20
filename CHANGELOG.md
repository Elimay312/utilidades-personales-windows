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

---

## M3.1 — Que se pueda cerrar

Salio probandolo a mano: el panel se abria bien pero **la unica forma de cerrarlo era
volver al Explorador y pulsar espacio otra vez**. Si te habias ido a otra app, la ventana
se quedaba ahi y no habia manera. Una ventana que no se puede cerrar es peor que no
tenerla, asi que va antes del morph.

Tres caminos, y ninguno necesita enmendar `SEGURIDAD.md`:

1. **Un clic en cualquier parte del panel lo cierra.** El panel ya recibia raton, asi que
   es un caso mas en su `WndProc`. Avisa a la ventana-host con `PostMessage` en vez de
   cerrarse el solo: la duena del panel es ella, y destruir una ventana desde dentro de su
   propio `WndProc` es la clase de cosa que revienta tres mensajes despues.
2. **Una ✕ arriba a la izquierda**, como en macOS. No es un boton — el panel entero cierra
   al clicarlo— pero hacia falta que se **viera** que se puede cerrar. Eso no se adivina.
3. **Irse a otra app lo cierra solo.** Un `SetTimer` de 200 ms que **solo existe mientras
   hay panel abierto**: con el panel cerrado no hay temporizador, no hay hilos y no hay
   sondeo, que es lo que dice el §5. Compara por HWND y no por foco, porque el panel nunca
   toma el foco.

**Y un cerrojo de instancia unica**, que salio del mismo rato de pruebas: cuatro copias
lanzadas sin querer son cuatro hooks, el espacio se procesa cuatro veces y desde fuera
parece que el filtro del hook esta roto. Un `Mutex` con nombre, en `Local\` y no en
`Global\`: esto es por sesion de usuario, no de maquina, y `Global\` pediria permisos que
no hacen falta.

**De propina, `Hook.cs` adelgaza de 88 a 73 lineas.** La lista de clases del Explorador se
fue a `Foreground.cs`, donde la comparten los tres sitios que la preguntan: el hook, el
cierre automatico y `Selection`. Estaba duplicada en dos, y el dia que se anadiera una
clase se habria arreglado en uno.

**Medido** (`scratchpad/sonda-cierre.ps1`): segunda instancia dice *"ya hay una instancia
corriendo"* y se va, quedando 1 viva; `WM_LBUTTONDOWN` sobre el panel lo cierra; y con el
Bloc de notas en primer plano el panel ya no esta. `auditar.ps1` sigue `TODO LIMPIO` con 87
entradas.

**Ficheros:** `Foreground.cs`, `Panel.cs`, `HostWindow.cs`, `Hook.cs`, `Selection.cs`,
`Program.cs`, `NativeMethods.txt`.

---

## M4 — El morph

La parte que decide si esto se siente como Quick Look o como abrir y cerrar una ventana.

### La decisión que lo hizo posible

**La ventana pasa a ser siempre la caja máxima, y lo que cambia de tamaño es la tarjeta de
dentro.** Antes la ventana se ajustaba a cada contenido, y con eso el morph al cambiar de
archivo habría necesitado un `SetWindowPos` por fotograma desde nuestro hilo. Una animación
movida a mano desde el hilo de UI se atasca en cuanto el shell tarda en devolver una
miniatura, que es justo cuando más se nota. Así no se mueve ninguna ventana: la tarjeta
crece, encoge y se recoloca dentro, y eso lo lleva el hilo de DWM.

El precio es que la ventana recoge clics en toda la caja máxima, también fuera de la
tarjeta. No es un problema: un clic ahí cierra el panel, que es lo que hace Quick Look en
macOS al clicar fuera.

### Las tres animaciones

**Abrir.** Nace en el cursor —acabas de clicar el archivo, el ratón está encima— usando el
`CenterPoint` del visual, así que la tarjeta crece *saliendo* de por donde estás mirando.
Muelle con amortiguación 0,82 y periodo 40 ms para la escala, y una cúbica (0,16 · 1 · 0,3 ·
1) de 260 ms para la opacidad: a mitad del muelle el panel ya se lee.

```
ponytail: crece desde el borde, no vuela desde el icono. Volar de verdad pide una
ventana más grande que el panel para tener sitio por donde venir, y eso se come
clics de todo lo que quede debajo.
```

**Cerrar.** 180 ms encogiendo a 0,92 y desvaneciéndose, y la ventana se destruye en el
`Completed` de un `CompositionScopedBatch`, **no en un temporizador**: un temporizador
acierta el tiempo pero no el fotograma, y destruir la ventana un fotograma antes es
exactamente el corte seco que se quería evitar.

**Morfar.** Al marcar otro archivo sin cerrar el panel se animan cuatro cosas a la vez y
todas tienen que llegar juntas o se nota: el tamaño de la tarjeta, el de su geometría de
esquinas —si no la acompaña, el material queda recortado a la forma vieja—, el radio de esas
esquinas, y la posición, porque la tarjeta está centrada y al cambiar de tamaño su esquina
se mueve. El contenido se cruza en 120 ms solapados: el viejo sale creciendo a 1,04 y el
nuevo entra desde 0,96, los dos en el mismo sentido, para que se lea como que uno pasa por
delante del otro y no como dos imágenes fundidas.

El muelle del morph va más amortiguado que el de la apertura (0,9 frente a 0,82) **a
propósito**: lo que rebota aquí es el tamaño de la tarjeta, y un sobrepaso se sale del
recorte de las esquinas y se ve como un mordisco en el borde. Abriendo rebota la escala
entera, que no se recorta contra nada.

### Enmienda 1 a SEGURIDAD.md

El morph **rompió un cortafuegos del propio documento**. El §3.2 prometía que *"no existe
ningún camino de código que llegue a `Selection.cs` que no venga del `WM_APP_QUICKLOOK` que
manda el hook"*, y detectar que has marcado otro archivo lo hace un temporizador.

Se podía haber dejado pasar —el temporizador solo vive mientras el panel está abierto— pero
entonces el documento habría empezado a describir un programa que ya no era, que es
exactamente como estas cosas se vuelven decorativas. Así que se reescribe el cortafuegos con
lo que de verdad hace (*"solo **durante** el gesto"*, apoyado en el precedente del §3.3 del
dock: repetir mientras dura el gesto, sobre lo mismo, y parar al acabarlo), y se le añade la
comprobación que lo ata en el código: **`Selection.Path` solo puede aparecer en
`HostWindow.cs`**.

Se le metió el fallo a propósito —una llamada colada en `Panel.cs`— y la auditoría la cazó:
`3.2 la seleccion solo desde HostWindow INCUMPLE`, salida 1.

### Medido

- **Cierre animado:** a los 67 ms la ventana sigue ahí (o sea que anima) y desaparece a los
  238 ms. Lo que importaba era lo segundo: el aviso sale de un `CompositionScopedBatch`, que
  se despacha por la `DispatcherQueue` del hilo, y si esa cola no se bombeara el `Completed`
  no llegaría nunca y la ventana quedaría colgada para siempre sin que nada fallara.
- **Morph:** el HWND del panel es **el mismo** antes y después de cambiar de selección
  (`0x240F12`), y la tarjeta pasa de `460x300 (ficha)` a `832x478 (miniatura)`. Si se hubiera
  cerrado y reabierto sería otra ventana.
- El tamaño de la tarjeta ya no se puede medir desde fuera, porque la ventana es siempre la
  caja máxima. Se traza con `QL_LOG` — señal de texto en vez de diff de píxeles, como manda
  el `CLAUDE.md`.
- Las trazas de `Selection` se quitaron: con el temporizador corriendo escupían sesenta
  líneas por prueba y dejaban el log inservible. Lo que explicaban vive en los comentarios.

**Ficheros:** `Motion.cs`, `Panel.cs`, `HostWindow.cs`, `Selection.cs`, `SEGURIDAD.md`,
`auditar.ps1`, `NativeMethods.txt`.

---

## M5 — Texto y código

Un `.txt`, un `.md` o un `.cs` ya no caen a la ficha: se leen y se dibujan con DirectWrite,
en una tarjeta con forma de página (720x560 lógicos, más alta que ancha, porque lo que se lee
son líneas y no una imagen). Sin resaltado de sintaxis: esto es un vistazo, no un editor.

### Los casos feos, que son los de leer el archivo

**Detectar que no es texto.** Un byte cero en los primeros 8 KB. Es la señal clásica y aquí
es la correcta: UTF-8 y las codificaciones de un byte no lo producen nunca, y cualquier
formato binario lo suelta enseguida. **Pero UTF-16 va lleno de ceros**, así que la
comprobación va *después* de descartar el BOM — si no, un `.txt` guardado desde el Bloc de
notas en UTF-16 se daría por binario y caería a la ficha. Ese es el caso que se escapa, y es
el que tiene su propia línea en `--check`. Se le metió el fallo a propósito (quitar el
descarte del BOM) y la prueba lo cazó: *"un .txt en UTF-16 SI es texto"*.

**El corte a 256 KB cae donde cae**, casi nunca en un final de línea. Se tira la última
línea a medias y se pone un `…`: menos feo que enseñarla partida por la mitad.

**`FileShare.ReadWrite`** a propósito, para poder mirar un log que alguien está escribiendo
ahora mismo. Y `FileAccess.Read`, que es lo que dice el §3.3 y lo que vigila `auditar.ps1`.

**Consolas y no Cascadia Mono** para el código. Cascadia es más bonita pero viene con Windows
Terminal, no con Windows: si falta, DirectWrite sustituye por una proporcional **sin avisar**
y el código se ve desalineado. Consolas está desde Vista en todas las instalaciones. Prosa
(`.md`, `.txt`) va con la proporcional; lo que se lee en columnas (`.json`, `.cs`, `.csv`…)
va monoespaciado.

### Desplazar

La superficie se dibuja entera y lo que se mueve es su `Offset`, así que el desplazamiento
también corre en el hilo de DWM y no repinta nada. Con una cúbica de 140 ms por muesca, no de
golpe: un salto seco hace perder el sitio donde ibas leyendo.

```
ponytail: el vistazo se corta a 8000 px de alto. Una superficie más alta se acerca al
límite de textura de la GPU (16384 en el hardware corriente) y reventaría sin avisar.
Si hace falta ver más, toca paginar.
```

**Una advertencia que no está en nuestra mano:** las ruedas llegan a la ventana bajo el cursor
sin necesidad de foco solo porque Windows trae activado *"desplazar ventanas inactivas al
pasar el puntero"*. Está así por defecto desde Windows 10, pero si alguien lo apaga el panel
no recibirá `WM_MOUSEWHEEL`, y no hay forma de arreglarlo sin robar el foco — que es justo lo
que no se puede hacer.

### Medido

`.cs` → `720x560 (texto)`. `.md` → `720x560 (texto)`. `.zip` → `460x300 (ficha)`, o sea que
el camino nuevo no se comió a los demás. Diez muescas de rueda abajo y tres arriba y el panel
sigue siendo la misma ventana. `auditar.ps1` `TODO LIMPIO`, y `--check` con cinco bloques.

**Ficheros:** `Content/TextFile.cs`, `Content/Preview.cs`, `Text.cs`, `Visuals.cs`,
`Panel.cs`, `Motion.cs`, `SelfCheck.cs`.

---

## M6 — PDF paginado

**Sin dependencia nueva.** `Windows.Data.Pdf` viene en el SDK y es el mismo renderizador que
usa el visor de Edge. Nada de PDFium ni PDFBox: un binario propio dentro del paquete es justo
lo que la regla 8 no quiere.

El `.pdf` **sale de la lista de miniaturas del shell** y pasa a tener su propio camino. La
miniatura del shell de un PDF es la portada a baja resolución y no deja pasar página; aquí se
rasteriza a 1800 px de ancho, que es más que una imagen normal porque lo que se mira en un
PDF es texto pequeño y ahí la resolución se nota enseguida.

La rueda pasa página en vez de desplazar, se para en la primera y en la última, y el contador
`2 / 3` va en el pie, en la misma línea del tipo y la fecha.

```
ponytail: el documento se abre y se cierra en cada página. Pasar página cuesta unas
decenas de milisegundos de más, pero no hay que llevar la cuenta de quién es dueño de
un objeto WinRT abierto entre gestos, ni cerrarlo en los cinco caminos por los que el
panel puede morir. Si pasar página se nota lento, ese es el salto.
```

Todo el trabajo asíncrono va a un hilo del pool y se espera ahí: bloquear el hilo de UI sobre
un `await` de WinRT es como se montan los interbloqueos más tontos, y ese hilo además es el
que despacha la `DispatcherQueue` del compositor.

### La sonda mintió, otra vez, y el código estaba bien

La primera pasada dio una secuencia de páginas sin pies ni cabeza —`1, 2, 3, 2, 1, 2, 3, 2`
para cuatro muescas hacia abajo— y el veredicto decía que el panel se había recreado. Parecía
un fallo feo de estado.

No lo era: **el bucle de reintentos de la sonda manda `WM_APP_QUICKLOOK` en cada intento, y
ese mensaje es un interruptor**. Abría el panel, el siguiente intento lo cerraba, el siguiente
lo volvía a abrir. Siete aperturas contaminando el log antes de que empezara la prueba.

Se arregló añadiendo trazas de ciclo de vida (`[panel] abierto`, `[panel] cerrando`) y de
rueda (`[rueda] delta=-120 pagina 1 -> 2`), que es lo que hacía falta para distinguir "el
código se equivoca" de "la sonda se equivoca". Con ellas la segunda pasada salió limpia a la
primera.

### Medido

- PDF de 3 páginas generado a mano (1122 bytes, escrito en PDF crudo por `hacer_pdf.py`) para
  que la sonda dé el mismo fichero byte por byte en cualquier máquina, sin depender de que
  haya Word o un "imprimir a PDF" instalado.
- Tarjeta `573x777`: proporción de la página dentro **0,774**, contra 0,773 de una carta
  vertical.
- Rueda abajo cuatro veces: `1 → 2 → 3 → 3 → 3`. Rueda arriba cuatro veces: `3 → 2 → 1 → 1 →
  1`. Se para en los dos extremos y **es la misma ventana** de principio a fin.

**Ficheros:** `Content/PdfFile.cs`, `Content/Preview.cs`, `Panel.cs`, `SelfCheck.cs`.
