# Cambios

El proyecto entero cabe en una sesión, así que en vez de versiones el registro va por
hitos, que es como se construyó: cada uno tenía que compilar y ejecutarse antes de
empezar el siguiente.

Los números que aparecen aquí están medidos, no estimados. El detalle de cada medición
está en el mensaje de su commit.

---

## Sin publicar

Todo lo que hay. Falta probarlo en otros equipos y con otras aplicaciones de música.

### Los recordatorios de Agenda, en la isla

**La primera entrada que abre la isla**, con su enmienda escrita y commiteada antes que el
código (SEGURIDAD.md §3.7). Un buzón por tubería con nombre donde una app de la casa deja un
aviso —título, línea, color y hasta cuatro botones— y espera a saber cuál se pulsó. Hoy lo usa
Agenda, como la isla de Xiaomi: el recordatorio asoma 8 s y se queda como un punto de su color
junto a la isla; pasar el ratón por el punto abre la tarjeta.

- **Se daba por hecho que .NET rechazaba clientes remotos, y no.** `NamedPipeServerStream` no
  pone `PIPE_REJECT_REMOTE_CLIENTS`: se miró en su código fuente antes de escribir la enmienda.
  La tubería lleva una ACL propia que deniega el SID `NETWORK`, y `auditar.ps1` comprueba que
  sigue ahí.
- **Abrir trae a Agenda al frente sin que la isla toque su ventana.** Quien pulsa es la isla,
  así que es la isla la que puede ceder el primer plano: `AllowSetForegroundWindow` al proceso
  que está al otro lado de la tubería, una vez y solo a ese —nunca `ASFW_ANY`—.
- **Arreglado de paso:** `Ctrl+Alt+I` desde la asomada nunca llegaba a abierta, porque la
  asomada caducaba en el tic siguiente. Ahora el atajo renueva su plazo.

### Arreglado: el separador volvió a ser un punto, y no «A-circunfleja punto»

Mojibake, y de cosecha propia: para ajustar un número se usó `Get-Content | Set-Content` de
PowerShell 5.1, y `Get-Content` sin `-Encoding` lee en la página ANSI del sistema. El
fichero era UTF-8, así que cada carácter no ASCII salió leído como CP1252 y se reescribió
como UTF-8. De regalo, `Set-Content -Encoding utf8` le puso un BOM que ningún otro fichero
del proyecto tiene.

**No era solo el aviso de volumen:** se llevó por delante seis líneas, dos de ellas de
código que ya estaba escrito y funcionando —el separador del título de canción y el del
aviso de batería—. Reparado invirtiendo exactamente la transformación (encode cp1252,
decode utf-8), que además sirve de comprobación: si algún carácter no ASCII no viniera de
ese ida y vuelta, el encode lanza en vez de estropearlo más. El proyecto queda sin un solo
mojibake: solo `§`, `«»`, `·`, `í` y los puntos suspensivos, todos donde deben estar.

**La regla que deja:** nunca editar fuentes con `Get-Content`/`Set-Content` de PowerShell
5.1. Corrompe sin avisar y el diff parece inocente.

### El volumen, con nombre y apellidos — y la isla se muda contigo

**Trabajo en colaboración con el [HUD](../hud/README.md).** Los dos proyectos leen el
mismo volumen del mismo sistema y ninguno sabía decir de qué altavoces estaba hablando.
Se arregló en los dos, y lo que viajó de uno a otro **no fue código ni una interfaz entre
procesos: fue una medición.** Está contada entera en `../hud/CHANGELOG.md`.

**Lo que se midió allí y valía aquí igual:** cambiar el dispositivo de salida **no**
invalida el endpoint de audio que tienes abierto. No falla, se queda contestando del
dispositivo anterior —47 muestras con otro predeterminado puesto, cero excepciones, y el
endpoint viejo diciendo 38 % cuando el real era 100 %—. `Audio.cs` tenía escrita la
suposición contraria desde el primer día, en el medidor de pico y en el nivel: los dos se
tiraban solo cuando algo lanzaba, y no lanzaba nunca. **La onda seguía latiendo con el
audio del dispositivo que ya no sonaba.**

**Se salda la deuda del sondeo.** El volumen se leía a 2 Hz y el propio comentario decía
por qué no podía ir a 8: `GetMasterVolumeLevelScalar` cruza al servicio de audio, y a 8 Hz
la CPU en reposo subía de 0,42 % a 2,29 % —más que todo lo demás junto—. Ahora avisa COM
(`IAudioEndpointVolumeCallback`), que no cuesta nada y no llega medio segundo tarde. Eran
«unas cuarenta líneas de COM que no compensan todavía»; compensaron cuando el HUD
descubrió que hacían falta de todas formas.

**El aviso dice por dónde sale.** `Volumen 48 % · LG ULTRAWIDE (NVI…`, y cambiar de salida
tiene su propio aviso, que es el que de verdad faltaba: el volumen que vas a oír a partir
de ahora es otro. El nombre es **una** propiedad del endpoint que ya estaba abierto y no
se enumera nada — `SEGURIDAD.md` §3.3 y los dos centinelas que lo comprueban.

**Un fallo que llevaba ahí desde el primer día y que este nombre destapó:** la píldora
compacta rotula sin marquesina y con `InsetClip`, así que un aviso largo no se recortaba,
se **cortaba** a mitad de letra. Ahora entra por búsqueda binaria —6 medidas de DirectWrite
en vez de 43— y acaba en puntos suspensivos.

**Y la isla se muda a la pantalla donde estás trabajando.** `pantalla` vacío en
`isla.json` pasa a querer decir *sigue al ratón*; con un nombre puesto se queda clavada
como antes. No hay ajuste nuevo: el que había ya tenía el hueco. Se aprovecha el
`Rehacer()` que existía para enchufar monitores —destruir y crear la ventana entera—
porque **medido cuesta 17-29 ms, mediana 21**, y reescala solo: 520x260 al 100 %, 650x325
al 125 %, 910x455 al 175 %. Histéresis de tres tics (~375 ms) para no mudarse al rozar un
borde de paso.

**Los dos avisos se van a la vez.** Con el HUD delante, la cápsula desaparecía y la isla
se quedaba dos segundos más sola: 4 s de asomo contra los 1,6 s del otro. Ahora el aviso
de audio —y **solo** el de audio; la batería, el pomodoro y la canción siguen con sus 4 s—
dura lo justo para terminar cuando termina el HUD.

Y no es copiarle el número, que fue el primer intento y dejaba **162 ms** de diferencia
con la isla yéndose antes: empiezan a la vez, pero el HUD tarda ~370 ms en salir (340 de
animación) y la isla ~150 (muelle de periodo 55). **Lo que hay que igualar es el final, no
el principio.** Medido muestreando los dos a la vez —el HUD por `IsWindowVisible`, la isla
por píxeles, porque no se esconde sino que se retrae—: **20, 30 y 48 ms en tres pasadas, y
el signo cambia entre ellas**, o sea el suelo de ruido del muestreo.

Los dos siguen sin hablarse: el 1815 sale de una resta hecha a mano sobre el
`msAutoocultar` del vecino, con su `ponytail:` al lado diciendo que si tocas ese ajuste
hay que tocar este, y por qué no se lee `hud.json` —acoplaría los dos procesos justo donde
presumen de no conocerse—.

Y lo que se vio en cuanto se usó de verdad: **la isla se presentaba otra vez en cada
mudanza.** Cruzabas de pantalla y te saltaba encima la ficha entera —carátula, título,
artista— de la canción que ya estabas escuchando. La causa es la misma familia que el
pomodoro: `_sonando`, el título que la isla ya había anunciado, es un campo de instancia,
así que la isla renacía sin él, `Medios` se volvía a enganchar y `OnMedios` creía que la
canción era nueva. Ahora también sobrevive, y **mudarse es mudarse, no volver a
presentarse**. La traza lo dice sola: la línea de la canción se imprime justo antes de
asomar, y tras el arreglo ya no aparece en ninguna mudanza.

Eso destapó lo que ese camino se llevaba por delante sin que importase: **`Rehacer()`
perdía el pomodoro.** Daba igual cuando solo pasaba al enchufar un monitor; con esto
pasaría cada vez que cruzas de pantalla con el ratón. Comprobado con capturas en las dos
pantallas: **24:58 antes de mudarse, 24:56 después** — la misma cuenta, dos segundos más
tarde, en el otro monitor.

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

### M8 — Instalarla

- **`instalar.ps1`**: publica a `%LOCALAPPDATA%\Isla\app`, deja acceso directo en el
  menú Inicio, enciende el autoarranque y la arranca. Con `-Desinstalar` lo deshace todo
  menos el `isla.json`, que es del usuario. Copiado del `install.ps1` de Rayo, sin los
  verbos del menú contextual.
- **El autoarranque se enciende en el JSON, no en el registro.** La isla reescribe
  `HKCU\...\Run` en cada arranque según lo que diga `autoArranque`, así que una entrada
  puesta a mano por el instalador duraría hasta el siguiente inicio de sesión y después
  desaparecería sola. El instalador toca el json y deja que la isla escriba el registro.
- **Desinstalar sí borra el valor de Run a mano**, porque desinstalada ya no arranca
  nunca y no hay quien lo limpie: quedaría una entrada muerta en la pestaña Inicio.

### No era un fallo: "la isla se bugueó al cambiar de la ultrawide al portátil"

No se había bugueado, **no estaba corriendo**: `autoArranque` venía en `false` y nadie la
había lanzado. Medida en el portátil (`DISPLAY1` al 125%, ventana 650 × 325 en 635,0) la
maqueta cae donde toca: carátula de 116 px, barra de 20 a 455, botones en 175 / 237 / 300,
franja recogida de 872 a 1047 centrada en 960 — o sea las medidas lógicas por 1,25. El
cambio de pantalla ya estaba resuelto desde M0: `WM_DISPLAYCHANGE` y `WM_DPICHANGED`
rehacen la ventana entera. De aquí sale M8.

### Arreglado: ocultar la barra de Windows escondía la isla

No era que la isla se hubiera caído. Al poner la barra en auto-ocultar, el área de trabajo
pasa a ser el monitor entero. Medido en esta máquina: una maximizada (Chrome) pasa de
acabar en y=1040 a colgar `-8,-8-1928,1088` sobre un monitor de 1920×1080. El criterio
viejo —sin marco y cubre el monitor— la daba por un juego en cuanto esa ventana no tenía
caption. El escritorio (`Progman`, `0,0-1920,1080`) y `TextInputHost` cubren el monitor
sin marco y tampoco son un vídeo; con el foco ahí la isla se quedaba escondida. Esos ya
no cuentan, y un rectángulo que sobresale por los cuatro lados es una maximizada, no un
vídeo.

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
- **Recogida se comía los clics de un rectángulo invisible de 380 × 190.** Lo encontró el
  usuario al no poder clicar un texto de la terminal que caía debajo. Reproducido: con la
  isla recogida, un clic a 129 px de alto movía la barra de progreso **123 segundos**.
  `WS_EX_TRANSPARENT` estaba puesto y **no sirve para esto** — era la única cosa que M1
  dejó sin comprobar, tras siete sondas que ninguna pasó su control. Ahora la región se
  ajusta a cada estado; recogida son los 140 × 5 px que se ven.

### Premisas del plan que resultaron falsas

- **«`CreateHostBackdropBrush` funciona, lo demuestra el dock».** No funciona: se crea sin
  lanzar excepción y se pinta negro. Y el dock **tampoco tiene acrílico** — la premisa
  venía de sus comentarios, no de una medición.
- **«La posición de reproducción avanza sola».** No: solo cambia cuando la app la empuja.
  Medida cinco veces con 6 s entre medias, con `Playing` todo el rato, dio `05:00` las
  cinco.
