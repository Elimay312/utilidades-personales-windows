# Cambios

Todo el proyecto cabe en dos días, así que en vez de versiones el registro va por fases,
que es como se construyó: cada una tenía que compilar y ejecutarse antes de empezar la
siguiente.

Los números que aparecen aquí están medidos, no estimados. El detalle de cada medición
está en el mensaje de su commit.

---

## Sin publicar

La reescritura en C++. Hasta que iguale al de C#, el de uso diario es el de la sección de
abajo.

### Añadido

- **El esqueleto en C++**: una ventana por pantalla, appbar, región, topmost, pantalla
  completa, ppp por monitor y rebuild al cambiar de pantallas, todavía sin dibujar nada.
  **1,3 MB privados y 1 hilo**, frente a los 46 MB del de C# al arrancar. Con tres pantallas
  (100/125/175 %) la franja escondida mide 3, 4 y 6 px; sin autoocultar, el área de trabajo
  pasa de 1080 a 1000. Tras reiniciar explorer las tres appbars vuelven a registrarse
  (~5,6 s, lo que tarda explorer en difundir `TaskbarCreated`).
- **`--config=<ruta>`** para arrancar con otro `dock.json` sin tocar el del usuario.
- **La barra de acrílico en C++**: Windows.UI.Composition con un Compositor para todo el
  proceso, esquinas redondeadas recortadas en el compositor y tinte blanco. Esconder la
  desliza hacia abajo con un muelle (amortiguado 1,0, 70 ms) y la región se encoge 150 ms
  después, cuando ya ha bajado. **2,4 MB privados y 5 hilos** con las tres pantallas.
- **Iconos, lupa y etiquetas en C++**: la misma curva cerrada que el de C# (expresiones de
  Composition sobre el cursor), la etiqueta encima del icono y el hover con muelle. Con diez
  apps en tres pantallas, **de 46 a ~25 MB privados** y los iconos en pantalla **de 579 a
  ~460 ms** desde el arranque.
  - Los iconos se extraen en un **proceso hijo** (`Dock.exe --extraer`), en paralelo, un
    hilo STA por icono: la maquinaria del shell se va con él. En serie y dentro del dock
    eran ~1,2 s y 537 handles; ahora 245.
  - Cada icono se sube **una vez por tamaño y se comparte entre pantallas**, reducido a su
    tamaño máximo de dibujo, en vez de 256×256 por icono y pantalla.
  - El reducido es `MULTI_SAMPLE_LINEAR`: con `HIGH_QUALITY_CUBIC` WARP compilaba sombreadores
    y el dock subía a 32,7 MB, para una diferencia de 27 píxeles de 30800.
- **La config completa en C++**: `dock.local.json` (orden, añadidas, quitadas, por pantalla
  y por perfil), `pantallas`, `perfiles`, la papelera y las apps que se actualizan de
  carpeta (Discord `app-1.0.9258` → `9259`). Con la config real del usuario las tres
  pantallas salen con la misma lista que el de C#.
- **Recarga en caliente de verdad**: cambiar `magnification` de 1,3 a 1,6 reajusta la
  ventana (287 → 301 px de alto) en 44 ms, y cambiar el orden en `dock.local.json` en 32 ms.
  En C# la altura, el atajo y el autoarranque solo se leían al arrancar. Los docks de las
  otras pantallas se enteran solos de un cambio en `dock.local.json`.
- **Las apps abiertas en C++**: puntitos, las abiertas sin anclar detrás de un separador (la
  papelera siempre la última) y el autoocultar inteligente (sin nada debajo el dock no se
  esconde). Por avisos del shell con una red de seguridad de 10 s; abrir y cerrar una
  ventana ajena se refleja en menos de un segundo.
  - **El barrido baja de 13-16 ms a 1,7 ms** en release: el de C# (y la primera versión de
    este) recorría los ~430 procesos del sistema para usar los ~13 que tienen ventana.
  - **Abrir una app sin anclar solo extrae su icono** (2 iconos en 83 ms), no los de todo el
    dock: las superficies sobreviven a las reconstrucciones y se podan en vez de vaciarse.
    Lo mismo vale para la recarga en caliente.
  - Con trece apps abiertas en tres pantallas: 25,3 MB privados y 18 hilos.
- **El clic en C++, como la barra de Windows**: sin ventana se lanza y el icono bota hasta
  que aparece (~730 ms con la app de prueba); con el foco se minimiza; sin foco, esté
  minimizada o solo tapada, viene al frente. Clic central, otra instancia. Las carpetas se
  abren en el Explorador hasta que lleguen los stacks (F6).
- **Arrastrar en C++**: reordenar (los vecinos se apartan con muelle) y sacar del dock
  tirando hacia arriba, con el icono desvaneciéndose. Se guarda en `dock.local.json` con el
  mismo formato que el de C#, solo el bloque de esa pantalla y ese perfil, y se recarga en
  ~40 ms en todas las pantallas. Lo abierto sin anclar no se arrastra ni se guarda.
- **El menú del clic derecho en C++**: los 4 documentos recientes de la app (o las carpetas
  frecuentes, en el Explorador), «Quitar del dock», «Anclar al dock» para lo abierto sin
  anclar, y «Salir del dock». Sale al instante y los recientes llegan después desde el
  worker: de 11,6 a 12,3 MB con el menú abierto, sin proceso hijo. Un reciente se abre con
  su app (las de la Store, por `ActivateForFile`). Con un dock de un solo icono el menú es
  más ancho que la barra: se encaja en la ventana y la región crece para cubrirlo.
- **Soltar ficheros en C++**: sobre el icono de una app se abren con ella (el icono se
  levanta y enseña su nombre); sobre la papelera van a la papelera de Windows (~0,3 s,
  deshacible, y lo que no cabe lo pregunta el shell); en el «+» que asoma a la derecha de
  la barra se añaden a lo anclado de esa pantalla. Un `.lnk` se guarda por su destino con
  sus argumentos y su icono, un `.url` de Steam por su URL, una app de la Store por su AUMID.
  De 241 a 294 handles por el registro OLE de las tres ventanas; la memoria queda dentro del
  ruido entre arranques (14,4–16,6 frente a 15,8–17,0 MB).
- **La rueda en C++**: sobre una app con varias ventanas enseña la lista de sus ventanas con
  un ✕ por fila (pide cerrar, como la X de la ventana) y la **miniatura en vivo** de la
  elegida, también de una minimizada. Clic en la fila o en el icono, y esa ventana viene al
  frente. Con la lista abierta el dock pasa **de 13,1 a 13,3 MB**; en el de C#, que la
  capturaba con `PrintWindow` cada 250 ms, eran +27 MB.
- **Los stacks en C++**: una carpeta del dock (o la papelera) se despliega en una rejilla de
  hasta 20 elementos, 5 por fila, con las carpetas primero y los números en orden («2» antes
  que «10»). Las subcarpetas se recorren dentro, con «Atrás»; un fichero se abre y la
  rejilla se cierra, y también 0,7 s después de salir el ratón. Al cerrarla se devuelve la
  memoria a WARP: con 20 iconos, de 25,2 a 11,3 MB (sin eso, se quedaba en 25).
- **El atajo de perfil en C++** (`atajoPerfil`): rota «sin perfil» → cada perfil → vuelta, y
  las tres pantallas se recargan en ~20 ms. El perfil activo se guarda en `dock.local.json`.
  Tiene que llevar un modificador: una tecla sola se le quitaría a todo el sistema.
- **Anclar arrastrando**: una app abierta sin anclar se arrastra hasta lo anclado y se queda
  en el puesto donde se suelta, como en macOS. Soltada entre las abiertas o tirada hacia
  arriba, vuelve a su sitio. Antes solo se anclaba con el menú del clic derecho.
- **El genio en C++, al minimizar desde cualquier sitio** (el botón de la ventana o el dock):
  40 miniaturas DWM de la ventana de verdad en vez de capturas, y la ventana se minimiza ya,
  sin esperar a la animación. Por genio, de **8-11 MB por captura a 0** (13,7 → 13,6 MB
  tras diez seguidos); 98 % de los fotogramas a tiempo. Varias a la vez (Win+D) van sin genio.
  Mientras el dock vive, las ventanas que enseña no hacen la animación propia de Windows;
  al cerrarlo la recuperan.
- **El genio de vuelta**: restaurar desde el dock (clic, lista de la rueda) saca la ventana
  del icono y la restaura al acabar (~440 ms), no al empezar, así que no asoma entera debajo.
  Si el genio se corta, un plazo de 700 ms la restaura igual. Restaurar desde fuera del dock
  (Alt+Tab) es instantáneo: sin la animación de Windows y sin genio.
- **`Instalar-Dock.exe`** (1,3 MB, con el dock dentro), el instalador de Panel: por usuario en
  `%LOCALAPPDATA%\Programs\Dock`, sin administrador, con `--silent` y `Desinstalar.exe`. Al
  dock en marcha le pide cerrarse y no toca el de C#. `empaquetar.ps1` lo compila y
  `actualizar.ps1` lo reinstala donde ya esté instalado.
- **La primera vez escribe el `dock.json` de ejemplo** que lleva dentro, como el de C# con el
  que iba a su lado; nunca encima de uno que ya exista.
- **El autoarranque solo lo toca la copia instalada**, para que una build de desarrollo no
  se registre en el inicio de sesión del usuario.
- **`--check` detecta iconos genéricos**: los compara con el de una extensión sin asociar.
  Extraer desde MTA no da error, da la hoja en blanco (35789 píxeles opacos), y solo se nota
  con manejadores como los `.url` de Steam.

### Arreglado

- **Abrir desde el dock una app de Chromium ya no parpadea en negro al final del genio**
  (Brave, Discord, Spotify, VS Code…): **de 6 de 10 aperturas a 0 de 35**, con Edge
  maximizado. Sin la animación de Windows, Chromium enseña 1-3 fotogramas negros mientras
  vuelve a pintar, y la miniatura del genio también, porque es la ventana en vivo. Ahora, al
  acabar el genio, lo que hay en pantalla se copia y se queda encima como imagen quieta hasta
  120 ms después de restaurar; los docks van recortados y siguen vivos encima. Cuesta 53-60 ms
  en una pantalla de 1920×1080 y 68-88 ms en una de 2560×1080 (la ventana viva llega eso más
  tarde), y la copia: el pico de privados sube de 1,2 a 12,0 MB a 2560×1080 y vuelve a 1,3 en
  cuanto la imagen está en su ventana (en el programa de prueba que compila el mismo
  `genie.cpp`; GDI quieto en 3 tras 10-15 aperturas). Con el dock instalado, 10 de 10
  aperturas por clic en el icono sin negro, y la imagen vive ~120 ms. Las apps normales (GDI)
  no lo hacían: 0 de 5 sin nada.
- **Abrir una minimizada por otro camino (Alt+Tab, la propia app) tampoco parpadea**: de 10
  de 10 aperturas en negro a 0 de 10, con el dock instalado. Mientras una ventana está
  minimizada vuelve a tener la animación de Windows, que retiene la ventana hasta que pinta;
  se quita otra vez al verse, porque minimizar es el genio, y el clic del dock la quita justo
  antes de restaurar. El cambio se hace 600 ms después de minimizar o restaurar: abrirla
  antes todavía puede salir en negro (5 de 10 a los 150 ms).
- **El genio ya no sale de la pantalla vieja tras mover una ventana con Win+Mayús+flecha.**
  El dock recordaba dónde estaba cada ventana al arrastrarla o en cada barrido (hasta 10 s), y
  moverla por teclado no avisa. Ahora lo recordado no vale si no está en la pantalla de la
  ventana. Medido moviendo una ventana de DISPLAY2 a DISPLAY3 sin arrastrarla y minimizándola
  en seguida: el genio iba al dock de DISPLAY2 y ahora sale de DISPLAY3 y va a su dock.
- **Abrir una ventana que vuelve maximizada ya no deja un marco sin pintar**: si se maximizó
  estando minimizada, el genio y la imagen congelada iban a su tamaño normal (609×430) y la
  ventana salía maximizada, en negro alrededor. Ahora, de vuelta, manda dónde la va a poner
  Windows: la imagen cubre 1920×1080 y el rectángulo del dock pasa de +97 % de oscuro a +9 %.
- **Ya no sale un cuadro oscuro sobre el dock al abrir una app maximizada.** El genio iba en
  una ventana encima de todo, dock incluido; tapado 400 ms, el dock tardaba un fotograma en
  volver a dibujarse y ese fotograma salía como un rectángulo negro (gris con Brave) del tamaño
  de su región. Ahora el genio y la imagen congelada van justo debajo de los docks: la ventana
  se funde bajo el dock y el dock no deja de dibujarse. Medido con el rectángulo del dock
  fotograma a fotograma: con el Brave del dueño maximizado en DISPLAY1, de +14 % de oscuro
  (8 de 8 aperturas) a +9 %, lo que ocupan sus iconos al bajar (3 de 3); con un Edge de prueba
  que tarda en pintar, de +12-19 % a +9 % (7 de 7).
- **Minimizar desde el dock devuelve la ventana al icono clicado**, no al dock de la pantalla
  donde está: abrir ya salía del dock clicado y cerrar se iba a otro. Medido con una ventana
  en DISPLAY3 y un clic en su icono del dock de DISPLAY2: el genio iba a 5600,975 (dock de
  DISPLAY3) y ahora va a 1403,1020 (el icono clicado). Minimizar con el botón de la ventana
  sigue yendo al dock de su pantalla.
- **Un clic en el icono de una app minimizada sin activar otra ventana ya la abre**: si se
  minimizaba con `SW_SHOWMINNOACTIVE` seguía siendo la de primer plano, y el clic la volvía a
  minimizar (9 de 10 clics sin efecto; ahora 10 de 10).
- **Un dock con `autoStart` apagado ya no borra el arranque de otro**: el valor Run «Dock» lo
  comparten el de C# y el de C++, y apagarlo en uno borraba el del otro. Ahora solo se borra
  si apunta a ese mismo dock.
- **Cambiar `atajoPerfil` se aplica sin reiniciar el dock**: el de C# solo lo registraba al
  arrancar. Ahora la recarga suelta el viejo y coge el nuevo.
- **Soltar sobre la papelera ya no ancla el fichero**: el de C# añadía al dock todo lo que
  no cayera sobre una app, papelera incluida. Ahora solo añade el «+»; el separador y los
  márgenes enseñan el cursor de prohibido.
- **Enfocar una ventana que se ve pero no tiene el foco ya es un clic**, no dos: el de C#
  minimizaba todo lo que estuviera a la vista. Ahora solo se minimiza la que tiene el foco.
- **El clic acierta aunque algo active el propio dock**: se decide con la última ventana
  ajena que tuvo el foco. Lo destapó computer use, que activa la ventana antes de hacer
  clic: el dock veía su propia ventana como primer plano y traía al frente en vez de
  minimizar.
- **Una app UWP minimizada desde antes de arrancar el dock ya no sale como
  «ApplicationFrameHost»** entre las abiertas: sin su ventana real no se sabe qué app es. La
  identificación buena, por AppUserModelID, llega en F9.
- **Ya no falta un icono al azar**: extrayendo en paralelo, la caché de iconos del shell
  contestaba `E_PENDING` a alguno (6 de 6 pasadas con el Explorador o Brave sin icono). Se
  reintenta; 8 de 8 pasadas completas.
- **Un `dock.json` con comas finales ya no se lee mal en C++**: `nlohmann_json` 3.12 no las
  admite (no existe `ignore_trailing_commas`), así que se limpian antes, respetando las
  cadenas. `--check` lo cubre.
- **La config se relee al cambiar de pantallas.** El de C# reconstruía con la leída al
  arrancar y perdía lo editado desde entonces.
- **El dock ya no tapa un segundo un vídeo a pantalla completa tras un rebuild**: la
  pantalla completa se decide antes de enseñar la ventana, no al primer tic del vigilante.

### Cambiado

- **La rueda también sirve con apps de una sola ventana**: abre la lista de una fila con su
  miniatura en vivo y la ✕ para cerrarla, sin abrir la ventana. Antes solo respondía con dos o
  más ventanas, así que no había forma de ver la miniatura de una app con una. Medido con una
  ventana de prueba: de ninguna línea `[rueda]` a «ventana 1 de 1» y la miniatura de 283×190.
- **El log dice por qué no hubo genio**, sin `DOCK_HOVER_LOG`: la ventana no estaba en ningún
  dock (p. ej. minimizada antes del primer barrido: medido con una ventana minimizada a los
  ~5 ms de nacer), o ningún dock tenía su icono; y una línea por genio hecho y por imagen
  congelada. De 0 líneas a 1 por minimizado o apertura, para cazar el «a veces no sale».
- **El dock de C# sale del repo**: pasa a `legacy/` (ignorada por git) y queda entero en el
  tag `dock-csharp-final`. Lo que se midió antes de decidirlo: 46 MB privados al arrancar,
  una meseta nativa de ~62 MB que en C++ sería igual, y 107 MB comprometidos por el GC a
  las 20 h con solo 19 MB vivos. Cada genio o miniatura copiaba la ventana entera 3-4
  veces, de 8 a 11 MB por copia, porque las miniaturas DWM estaban prohibidas.
- **Fuera `SEGURIDAD.md`, `auditar.ps1` y `NativeMethods.txt`**, sin sustituto. Sustituir a
  la barra de tareas necesita justo lo que prohibían: miniaturas DWM, hooks de eventos y
  ocultar la barra de explorer.
- **La pila de dibujo del de C++ es Windows.UI.Composition**: 9,0 MB privados con tres
  ventanas, acrílico y 20 superficies, frente a 46 MB del de C# al arrancar. El resto de
  sondas de riesgo (genio con miniaturas DWM, bandeja, ocultar la barra) están en el README.
- **`actualizar.ps1` ya no instala el dock** hasta que el de C++ tenga instalador, y el
  SDK de .NET de hud, quicklook y renombrar sale ahora del `preparar.ps1` del lanzador.

## C#: la última versión (tag `dock-csharp-final`)

Lo que había en `main` antes de la reescritura. Se estaba probando en varios equipos antes
de darlo por bueno.

### Añadido

- **Las apps abiertas salen aunque no estén ancladas**, detrás de un separador, como la
  barra de tareas. Se identifican por la ruta del ejecutable y no por el proceso —un
  navegador aportaría treinta entradas— y se ordenan por nombre para que los iconos no
  bailen. Clic derecho sobre una de ellas dice *Anclar al dock*. Se apaga con
  `"showRunning": false`.
- **`CHANGELOG.md`** (esto) y **`CLAUDE.md`** con las convenciones del proyecto.

### Cambiado

- **Con la lista de la rueda abierta, el clic sobre el icono abre la ventana elegida**, y
  ya no hace falta bajar el ratón al título. Ese viaje es justo el que la rueda venía a
  ahorrar: el puntero ya está sobre el icono, que es donde se gira. La elegida recibe lo
  mismo que un clic normal —sale del icono con el genio, o se la traga si ya la estabas
  viendo—, porque la regla está escrita una sola vez y la usan los dos caminos. Medido con
  las dos ventanas de Brave: dos muescas y un clic dan `al frente 'Brave (2 de 2)'`, y el
  siguiente clic, `minimizada 'Brave (2 de 2)'`.

- **El clic esconde la ventana que estás viendo, tenga el foco o no.** Con varias
  pantallas el foco no dice lo que se ve: la del monitor 1 sigue delante de tus ojos
  mientras escribes en la del 2, y el clic sobre su icono le hacía la animación de entrada
  a algo que no se había ido. Lo que se ve se distingue de lo que solo está abierto
  preguntando quién está encima del centro de la ventana: sobre una ventana tapada del todo
  eso da «no se ve», donde la medida anterior —no minimizada— daba «se ve». Minimizada o
  tapada sigue saliendo del icono con el genio; y como para esas no hay fotograma guardado
  se captura en ese momento con `PrintWindow`, que le pide a la app que se dibuje en vez de
  leer la pantalla y por eso la saca entera aunque esté detrás de otras. Lo que cuesta:
  darle el foco desde el dock a una ventana que se ve pasa a ser de dos clics, porque el
  primero se la traga.

- **La memoria privada baja de 80 a 36 MB, y los hilos de 70 a 24.** El device de D3D se
  pide sobre WARP y no sobre la GPU: solo sube píxeles a superficies de composición y no
  renderiza ni un fotograma, así que el adaptador de verdad solo servía para mapear el
  driver de usuario entero con su pool de compilación de shaders. Dibuja igual —comparado
  con capturas— y sigue por delante de una ventana maximizada.
- **El arranque baja de 645 a 579 ms** con `PublishReadyToRun`. No recorta ni comprime, así
  que no toca la regla 8 de `SEGURIDAD.md`; NativeAOT sí recortaría y por eso no se ha
  puesto.
- **La miniatura de la rueda ya no sube la ventana entera.** Para enseñarla en un chip de
  300x190 subía la captura a tamaño completo dos veces por segundo: de 35 MB a 26,7.
- **Escondido, el dock solo se revela desde el filo de la pantalla.** Reclamaba el ratón en
  los 86 px de la altura de la barra aunque ahí no se viera nada, así que saltaba al
  *acercarse* y tapaba lo que vive pegado al borde inferior: el botón de un chat, la barra
  de una tienda. Ahora la región escondida es la franja de 3 px del filo y por encima se
  responde `HTTRANSPARENT`, medido con `WM_NCHITTEST` preguntado desde fuera. El borde de la
  pantalla se acierta sin mirar, así que la precisión no cuesta puntería. La región no se
  encoge hasta que la barra ha acabado de bajar (150 ms), porque también recorta el dibujo.

### Arreglado

- **Reiniciar explorer ya no deja al dock fuera de la lista de appbars.** El shell la
  vaciaba y el dock no se enteraba, así que dejaba de recibir `ABN_FULLSCREENAPP`. Ahora
  vuelve a registrarse con `TaskbarCreated`: tras un reinicio de explorer, de 0 a 3 de
  las 3 ventanas siguen registradas, y la barra de tareas no muestra ningún aviso.
- **Cerrar una app con el botón pulsado sobre el dock ya no lo tumba.** La barra encogía
  por debajo de la pulsación y el primer movimiento hacía `RemoveAt(-1)`. Dos caídas en el
  visor (19 y 20 sep); reproducido con mensajes y una ventana de prueba: de caerse a
  seguir vivo. La lista nueva ahora la cambia el hilo de UI, y cancela la pulsación.
- **Cambiar de pantallas ya no puede dejar el dock a 0 ppp y tumbarlo.** Con el monitor
  ya desaparecido, `GetDpiForMonitor` falla sin escribir y el DPI quedaba en 0; la curva
  pasaba a radio 0 y la expresión a `Infinity`. Dos caídas en el visor (22 y 24 sep).
  Ahora se conserva el último DPI bueno.
- **Un clic derecho a la derecha del último icono ya no cierra el dock.** Componía el
  texto «Quitar …» con el índice -1 aunque no hubiera icono debajo, y el dock moría con
  `ArgumentOutOfRange`. Reproducido con mensajes: de caerse a seguir vivo.
- **Un icono ya no se despega solo al pasar el ratón.** Si el botón se soltaba fuera del
  dock antes del umbral de arrastre, la pulsación quedaba viva: al volver, el icono seguía
  al cursor sin botón —70 px encima del vecino, medido con mensajes— y el siguiente clic lo
  dejaba reordenado o quitado. Ahora un movimiento sin botón la cancela: de 70 px a 0.
- **Un juego de Steam recién instalado ya no espera a reiniciar el dock.** La caché
  guardaba también los fallos y para siempre; ahora el «no está instalado» caduca a los
  30 s y el acierto se queda.
- **Soltar varios ficheros sobre una app de la Store los abre todos**, por los dos
  caminos: el de contrato y el de argumentos. Antes se quedaba con el primero.
- **El dock se apartaba entero cada vez que minimizabas una ventana.** Al minimizar,
  `SHQueryUserNotificationState` devuelve `BUSY` durante un instante y el shell llega a
  disparar `ABN_FULLSCREENAPP`; el dock lo daba por pantalla completa y se ocultaba. En ese
  hueco el borde inferior dejaba de ser suyo y bastaba bajar el ratón para sacar la barra de
  tareas de Windows. Ahora el pleno lo decide **la ventana** —estilos y rectángulo—, no el
  estado del sistema.
- **El dock reclamaba el ratón en toda la columna sobre la barra.** Con el hueco del menú a
  200 px la ventana medía 287, y esa columna invisible marcaba el icono desde doscientos
  píxeles más arriba. Ahora la región llega solo hasta lo que se dibuja: 115 px medidos en
  vez de 287.
- **Enchufar o quitar una pantalla apagaba el dock.** No se estrellaba: se salía limpio, sin
  dejar nada en el visor de eventos, y por eso costaba de ver. Al cambiar las pantallas se
  tiran los docks y se vuelven a crear, y el `WM_DESTROY` del último veía la lista vacía y
  posteaba `WM_QUIT`: los tres docks nuevos nacían con la sentencia ya en la cola. Medido
  mandando `WM_DISPLAYCHANGE` desde fuera: de 3 ventanas a 0 y el proceso fuera; ahora
  quedan las 3 y el mismo PID.
- **Soltar un icono reordenado bajaba el dock con el ratón encima.** Reordenar pasa por la
  reconstrucción, y la reconstrucción se escondía siempre, sin mirar si había alguien
  encima. Molestaba poco cuando bastaba mover el ratón para recuperarlo, pero con la franja
  del filo obligaba a bajar hasta abajo otra vez. Medido con un arrastre sintético que
  devuelve el icono a su sitio: la región pasaba de 123 px a 3 al soltar, y ahora se queda
  en 123. Sigue escondiéndose al apartar el ratón, de golpe o poco a poco (559 y 681 ms).

---

## Fase 4 — multipantalla, ventanas y la barra de tareas

Con tres monitores a 100%, 125% y 175%, que es donde salieron casi todos los fallos.

### Añadido

- **Un dock por pantalla, con sus propias apps.** La identidad es el nombre de dispositivo
  del monitor, no el `HMONITOR`, que cambia entre arranques. Quitar un icono en una pantalla
  deja las otras intactas.
- **El dock se declara AppBar** (`SHAppBarMessage`). Con el autoocultar apagado reserva su
  hueco: el área de trabajo baja de 1080 a 1000 y una ventana maximizada deja de taparlo.
- **Rueda sobre un icono:** la lista de sus ventanas, con miniatura de la elegida y una ✕
  para cerrarla. La lista se desplaza con la selección cuando hay más de las que caben.
- **Clic central:** una instancia nueva aunque ya haya ventana.
- **Documentos recientes** en el menú del clic derecho.
- **Anclar direcciones, protocolos y comandos con argumentos.** El icono de una dirección es
  el de la app que la va a abrir.
- **Perfiles de dock** con un atajo para rotar entre ellos y el dock de siempre.
- **Autoocultar inteligente:** solo se esconde si alguna ventana lo tapa de verdad.
- **Miniaturas** de la ventana que estás eligiendo en la lista.
- **`SEGURIDAD.md` reescrito por temas** y `auditar.ps1`, que comprueba el código y devuelve
  0 o 1.

### Cambiado

- **El inventario de ventanas va por avisos del shell**, no por reloj. De 3 barridos por
  segundo a uno al arrancar más un aviso cuando algo cambia. Medido con tres pantallas y 60 s
  en reposo: de **5,07%** a **1,90%** de un núcleo.
- **Un solo `Compositor` para todo el proceso** en vez de uno por pantalla: de 128 a 117 MB.
- **`AppState` guarda todas las ventanas de cada app**, no solo la primera.

### Arreglado

- **El texto se dibujaba con la escala del monitor equivocado.** Un único
  `IDWriteTextFormat` estático se creaba con la escala del primer dock que pintara una
  etiqueta. Con el fallo, las tres pantallas daban pastillas de 42 px; arreglado, 29 al 100%
  y 35 al 125%.
- **Los docks no se enteraban de lo que hacía el otro.** Reordenar en una pantalla dejaba a
  las demás con el orden viejo hasta reiniciar.
- **El genio se dibujaba en el monitor equivocado** cuando la ventana estaba en otra
  pantalla. Ahora la superposición cubre la unión de los dos rectángulos.
- **Un juego a pantalla completa escondía los docks de las tres pantallas**, porque
  `SHQueryUserNotificationState` es global.
- **Cerrar una ventana cerraba la app entera:** `WM_DESTROY` llamaba a `PostQuitMessage`
  siempre. Ahora solo el último apaga la luz.
- **Enchufar o quitar una pantalla no hacía nada.**
- **La lupa se encendía sola al arrastrar un icono.** Las tres reconstrucciones de la
  configuración copiaban los campos a mano y las tres se olvidaban de `magnification`; dos,
  también de `trash`.

### Descartado, con motivo

- **`ABM_SETAUTOHIDEBAREX`** para que la barra de tareas no se revele: devuelve 0, la barra
  ya tiene ese hueco. Pero al medirlo salió que **no hacía falta** — el dock ya ocupa el
  borde inferior y la barra no asoma.
- **`DwmRegisterThumbnail`** para las miniaturas: no existe forma de convertir un
  `HTHUMBNAIL` en algo que el compositor pueda dibujar.
- **Ciclar ventanas con la rueda:** Windows no concede el derecho a cambiar la ventana en
  primer plano desde un evento de rueda, y `SetWindowPos(HWND_TOP)` sobre una ventana ajena
  devuelve `TRUE` sin mover nada. Por eso la rueda enseña una lista y es el clic quien abre.
- **`ITaskbarList3`** para leer el progreso de otras apps: no existe ningún método `Get*`.

---

## Fase 3 — arrastrar, menús y carpetas

### Añadido

- **Arrastrar para reordenar**, y sacar un icono del dock tirando hacia arriba.
- **Soltar un fichero sobre un icono** lo abre con esa app; soltarlo en la zona del `+` lo
  añade al dock.
- **Menú del clic derecho**, dibujado en el compositor. No se usa `TrackPopupMenu` porque
  exige foco para cerrarse bien, y el dock nunca lo tiene.
- **Papelera** al final, quitable como cualquier otro icono.
- **Etiqueta con el nombre** al pasar por encima.
- **Stacks:** clicar una carpeta la despliega en rejilla, y se puede entrar en subcarpetas y
  volver.
- **La configuración se muda a `%LOCALAPPDATA%\Dock`.** Vivía junto al ejecutable, o sea en
  la carpeta de compilación: un `dotnet clean` se llevaba por delante lo que hubieras
  reordenado.

### Arreglado

- **`HTTRANSPARENT` no atraviesa procesos.** Se midió poniendo Paint debajo: el clic no le
  llegaba igual. Lo único que deja pasar el ratón de verdad es `SetWindowRgn`.
- **Añadir el acceso directo de Brave guardaba un destino inválido.** Su `.lnk` lleva un
  `AppUserModelID` que no es una entrada de `AppsFolder`. Ahora la ruta de disco gana.
- **El dock se tragaba los clics de toda su columna** y tapaba los iconos del escritorio.
- **Soltar exigía demasiada puntería:** ahora vale cualquier altura dentro del dock.

---

## Fase 2 — el efecto genio

### Añadido

- **Clic de tres estados:** lanzar, traer al frente, minimizar.
- **El efecto genio**, una malla de 40 franjas deformadas sobre una captura de la ventana,
  con su camino de vuelta al restaurarla.

### Arreglado

- **La Calculadora abría instancias duplicadas.** Al minimizarse, una app UWP pierde su
  `CoreWindow` de dentro del marco y deja de cruzar con su icono.
- **El Explorador salía siempre abierto** y clicarlo minimizaba el escritorio: la ventana
  del escritorio tiene título y pasaba todos los filtros.
- **El genio se sentía muerto.** De 700 a 400 ms, de lineal a cúbica, y el destino pasó de
  ser el icono entero a una franja fina en su centro — apuntando al icono completo, la
  ventana acababa como una miniatura parada encima.

---

## Fase 1 — que exista

- Ventana Win32 sin bordes, topmost, que **no roba el foco** y no sale en Alt+Tab.
- Iconos del shell subidos al compositor, y clic que lanza la app.
- **Magnificación animada en el compositor**, no en el hilo de UI: si el hilo se bloquea, la
  animación sigue fluida.
- Acrílico y esquinas redondeadas.
- Autoocultar, paso de clics, y un dock por monitor.
- Recarga en caliente de `dock.json`.
- Autoarranque en `HKCU\...\Run`, visible en el Administrador de tareas.
