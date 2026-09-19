# Cambios

El proyecto entero cabe en una sesión, así que en vez de versiones el registro va por
hitos, que es como se construyó: cada uno tenía que compilar y ejecutarse antes de
empezar el siguiente.

Los números que aparecen aquí están medidos, no estimados. El detalle de cada medición
está en el mensaje de su commit.

---

## Sin publicar

Todo lo que hay. Falta probarlo en otros equipos y con otras aplicaciones de música.

### M0 — Las reglas, antes del código

- **`SEGURIDAD.md` propio**, escrito antes de la primera línea. No es el del dock con
  otro nombre: el criterio del dock se apoya en *"lo empieza el usuario con un gesto"* y
  aquí no hay gesto — la isla se entera de que cambiaste de canción porque el sistema se
  lo dice. El criterio nuevo se apoya en de dónde sale el dato, cuánto dato es y qué se
  hace con él.
- **Tres reglas que el dock no tenía.** Leer qué reproducen otras apps: permitido, es el
  canal de las teclas de multimedia. Controlarlas: permitido, es lo que hace la tecla de
  play. **Grabar audio: prohibido**; leer el medidor de pico: permitido. La línea es que
  `GetPeakValue` devuelve un `float` por pasada y de un `float` no se reconstruye nada.
- **`auditar.ps1` con 15 reglas propias.** Medido en las dos direcciones: vacío da
  `TODO LIMPIO`; con una sonda de 7 violaciones detecta las 7; y los **mismos nombres**
  dentro de comentarios no saltan ninguno.

### M1 — El morph

- **Tres muelles sobre la caja** —tamaño, radio y despegue— y seis expresiones que leen su
  alto en vivo. Se aparta del dock a propósito: las animaciones de movimiento natural
  arrancan desde donde estén y son interrumpibles.
- Medido: la curva de la esquina da 21, 16, 10, 5 y 0 px de sangrado según la fila, que es
  un radio de 28. Brasa de **140 × 5 px exactos**, panel de **380**.
- **Hay que quedarse 240 ms** en la franja para abrirla. Sin eso, cruzar el borde de camino
  al botón de cerrar la abría: medido, pasaba a opaca antes de 300 ms.

### M2 — Texto

- **DirectWrite sobre superficie propia**, con la cadena D3D11 → D2D → Composition.
- **Marquesina en vez de puntos suspensivos**: cortar el título esconde justo la parte que
  distingue dos canciones del mismo disco. Va y vuelve, con parada en los dos extremos.
- **WARP y no HARDWARE** para el device: solo sube píxeles y no renderiza un fotograma en
  su vida. 15 MB privados frente a los ~22 que midió el dock.

### M3 — La música

- **Por eventos y sin sondear.** Título, artista, app de origen, carátula, posición y
  duración de lo que suene en cualquier app.
- Todo el `async` de WinRT en el pool de hilos; el resultado cruza al hilo de UI con un
  `PostMessage`. Sin `SynchronizationContext` y sin esperas.
- **Asoma cuatro segundos** al cambiar la canción y se retira sola.

### M4 — Los mandos

- Play/pausa, anterior, siguiente y **barra de progreso arrastrable**. Un botón que la
  sesión no admite no se dibuja.
- Medido pidiéndole la señal al sistema: clic en el botón de pausa → `Playing` pasa a
  `Paused`; clic en un hueco del panel → no cambia. Arrastre: se pidió el 65% y el vídeo
  quedó en 20:01 contra los 20:02 esperados.
- **Región fija de 380 × 190.** Sin ella, abierta se tragaría los clics de los 70 px de
  margen de cada lado.

### M5 — Que respire

- **La onda y el latido de la brasa** con el medidor de pico, y el **aura** del color
  dominante de la carátula.
- **Ganancia automática.** El pico depende del volumen del sistema, así que una constante
  fija dejaría la onda plana en unos equipos y saturada en otros.

### M6 — El titular

- **Fila compacta** para la pastilla asomada, que se cruza con la ficha entera.
- **Pomodoro** con `Ctrl+Alt+T`: mientras corre, la isla se queda asomada con la cuenta
  atrás. **Batería** por `WM_POWERBROADCAST`, cero sondeo. **Volumen** leyendo el nivel;
  solo se lee.

### M7 — Convivir

- **`isla.json` con recarga en caliente**, elegir pantalla por nombre de dispositivo,
  apartarse de lo que esté a pantalla completa, volver al frente de la banda topmost, y
  autoarranque en `HKCU\...\Run` (apagado por defecto).
- Medido: mover la isla a una pantalla con otro DPI la lleva de `520x260` a `650x325`, que
  es lo mismo al 125%. Al rehacerse recalcula todas las medidas.
- **`README.md`** (con las ocho cosas que se midieron y no son obvias) y **`CHANGELOG.md`**.

### Arreglado durante el camino

- **La pastilla asomada salía vacía.** Medía 38 px de alto y la rampa del contenido no
  arrancaba hasta 40, así que el aviso que existe justo para decir *qué ha cambiado* no
  decía nada. Llevaba así desde M1 y no se había visto porque todas las capturas estaban
  cogidas a mitad de animación.
- **El proceso se moría al rehacerse.** `WM_DESTROY` llamaba a `PostQuitMessage`, que
  cierra el bucle de mensajes: la ventana nueva nacía bien y el proceso se cerraba detrás
  de ella. El dock tuvo el mismo fallo con sus tres ventanas.
- **La CPU en reposo se fue a 11,7% y volvió a 0,4%.** La carátula se decodificaba en cada
  evento y el texto se repintaba en cada evento — y Spotify empuja la línea de tiempo cada
  dos por tres. Con Brave, que apenas avisa, no se habría visto nunca.
- **La carátula salía aplastada.** Las miniaturas de vídeo son 16:9 y se forzaban a un
  cuadrado. Ahora se escala por el lado corto y se recorta por el centro.
- **El cristal estaba demasiado transparente.** Al 11% se leían los botones de minimizar y
  cerrar de la ventana de detrás a través del panel. Bajó al 4%.
- **El latido de la brasa no latía**: 138 a 140 px de 140. Tres intentos de normalización
  hasta entender que el pico vive en una banda estrecha y lo que se ve es cuánto se separa
  de su media.
- **La isla salía en el dock como una app abierta.** Se arregló **en el dock**, que no
  miraba `WS_EX_TOOLWINDOW`. Excluirla por nombre habría acoplado los dos proyectos.

### Premisas del plan que resultaron falsas

- **«`CreateHostBackdropBrush` funciona, lo demuestra el dock».** No funciona: se crea sin
  lanzar excepción y se pinta negro. Y el dock **tampoco tiene acrílico** — la premisa
  venía de sus comentarios, no de una medición.
- **«La posición de reproducción avanza sola».** No: solo cambia cuando la app la empuja.
  Medida cinco veces con 6 s entre medias, con `Playing` todo el rato, dio `05:00` las
  cinco.
