# Cambios

Todo el proyecto cabe en dos días, así que en vez de versiones el registro va por fases,
que es como se construyó: cada una tenía que compilar y ejecutarse antes de empezar la
siguiente.

Los números que aparecen aquí están medidos, no estimados. El detalle de cada medición
está en el mensaje de su commit.

---

## Sin publicar

Lo que hay ahora en `main`. Se está probando en varios equipos antes de darlo por bueno.

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
