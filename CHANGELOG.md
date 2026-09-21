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

- **El clic solo esconde la ventana que tienes delante**, la del primer plano. Cualquier
  otra la muestra saliendo del icono con el genio: minimizada, tapada por otra o en la
  pantalla que no estás mirando, da igual. Antes escondía cualquier ventana no minimizada,
  y eso obligaba a dar dos clics para ver una ventana que ya estaba tapada: el primero la
  minimizaba —escondía algo que no se veía— y el segundo la traía. Para sacar una ventana
  que el dock no minimizó no hay fotograma guardado, así que se captura en ese momento con
  `PrintWindow`, que le pide a la app que se dibuje en vez de leer la pantalla y por eso la
  saca entera aunque esté detrás de otras.

### Arreglado

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
