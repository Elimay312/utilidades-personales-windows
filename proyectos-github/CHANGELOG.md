# Cambios

El proyecto va por fases, no por versiones: cada una tiene que compilar sin warnings y
pasar sus pruebas antes de empezar la siguiente.

Los números que aparecen aquí están medidos, no estimados. Cuando algo no se pudo medir,
lo dice.

---

## Sin publicar

### Fase 8 — Pulido final y rendimiento

El movimiento se volvió a afinar, y esta vez contra el número correcto. Se arregló el
desplazamiento de la rueda, que era el peor defecto de uso que quedaba. La aplicación se
abre desde un `brujula://`, tiene icono y dice qué versión es. Y el arranque se midió de
verdad, con el resultado incómodo de que el objetivo de la fase no se puede cumplir — con
las medidas que lo demuestran.

**Lo que se descubrió antes de tocar nada**

`periodMs` y `dampingRatio` no son parámetros nuestros: son **exactamente** los dos de
Apple. Composition define `Period` como «el tiempo que tarda el muelle en completar una
oscilación», y `Spring(duration:bounce:)` de Apple fija rigidez `(2π/duration)²` con masa 1.
Las dos cosas fijan la misma ωn = 2π/T, así que:

    periodMs      ==  la «duración perceptual» de Apple
    dampingRatio  ==  1 − bounce

Y eso convierte en un error las dos afinaciones anteriores. Apple define la *settling
duration* —lo que aquí se llama `SettleMs`— y dice explícitamente que no se afina con ella:
*«depends on many different factors and can be unpredictable»*. El mando es el periodo,
porque es el número que se eligió para ser predecible. Las fases 1 y 6 afinaron el otro.

Con eso a la vista, la tabla de la fase 6 era de 80 a 195 ms de duración perceptual. Los
tres presets de iOS están **los tres en 500**. No era una aplicación rápida: era una
aplicación que corta.

**Los muelles**

| Muelle | Fase 1 | Fase 6 | Ahora | Rebote | Asienta |
|---|---|---|---|---|---|
| Rígido (hover, pulsar) | 120 | 80 | **130** | 0,10 | 92 ms |
| Estándar (paneles, inspector) | 220 | 130 | **200** | 0,15 | 150 ms |
| Suave (reordenar, mover) | 300 | 165 | **250** | 0,20 | 199 ms |
| Expresivo (hojas, revisión) | 350 | 195 | **340** | 0,25 | 289 ms |

Las amortiguaciones no se han tocado desde la fase 1 y no se tocan ahora: en el idioma de
Apple son rebotes de 0,10 a 0,25, y sus tres presets van de 0 a 0,3 — la tabla entera cae
dentro, y por encima de 0,3 el movimiento se lee como un dibujo animado. Lo que se abre es
la ESCALA: de un factor 2,4 entre el primero y el último a 2,6, porque lo que pide tiempo es
la distancia recorrida y un anillo de foco que crece un 4 % no puede estar a un factor de
dos de una hoja modal que cruza la pantalla.

**Y el arreglo que más se nota no es ese: es el fundido de la tinta.**

`Resolve()` calculaba el fundido como `SettleMs × 0,6`. Con la tabla de la fase 6, eso
dejaba el cruce de color del hover en **34 ms: dos fotogramas a 60 Hz**. Un corte, no un
fundido. Y estaba en los cinco sitios donde se enciende algo por estado — botones, botones
de ventana, anillo de foco, píldora de prioridad.

Ahora hay `Motion::kInkMs` = 120 ms, separado de los muelles, y `Animator::InkMs()`. Es
gratis en lo que a esta aplicación le importa: **un fundido de color no retrasa ni un clic**,
porque nadie espera a que termine para poder pulsar. Todo lo que la fase 6 quería ganar
acortando sigue ganado, y lo áspero se fue.

**El escalonado de entrada: 20 → 45 ms**

Un escalonado existe para que una lista se lea como una secuencia. Por debajo de unos 40 ms
no se distingue de que entren todas a la vez: cuesta el retardo y no compra el ritmo. Lo
publicado pone el punto dulce entre 40 y 80. El techo sube de 120 a 180 ms.

**Cambiar de lista a cuadrícula: de 1,4 segundos de tarjetas cruzándose a un fundido**

Reportado con una captura de en medio del viaje: un hueco en blanco en mitad de la
cuadrícula, con una fila cortada arriba. El estado final siempre era correcto; lo que estaba
roto era el camino.

La fase 4 había decidido que el tamaño de una celda no se anima y la posición sí, con una
razón buena: animar el tamaño reasigna la textura de cada celda en cada fotograma. La
consecuencia no se vio hasta usarlo. Entre lista y cuadrícula la celda pasa a un **tercio**
de ancho, así que con el ancho ya puesto y la posición viajando, las que van a la segunda y
la tercera columna cruzan por encima de las de la primera — y como las tarjetas son
translúcidas, se leen tres textos superpuestos.

Medido contando cada cuánto la columna se parece a su estado final:

| | tiempo hasta quedar igual al final |
|---|---|
| deslizando cada celda | ~1400 ms, y por el camino tarjetas encima de tarjetas |
| **fundiendo la columna** | **0 diferencias a partir de los 400 ms**, sin nada superpuesto |

Lo que cambia aquí no es dónde está cada tarjeta: es la forma de toda la rejilla. Eso se lee
como un cambio de plano y no como un viaje, así que se recoloca de golpe y lo que se anima
es la columna entera apareciendo — **una animación de opacidad sobre un visual en vez de una
por celda**. Comprobado en las dos direcciones y con la lista al final del todo, que es el
caso de la captura.

**Dos animaciones que no cumplían ninguna función**

- **La escala de vuelta de `Views::Main::Recede`.** Al terminar la revisión semanal, la
  lista, la barra lateral y el inspector volvían de 0,94 a 1,0 con un muelle que **no mira
  nadie** —están en opacidad cero hasta que el fundido los trae— y que dejaba una animación
  de escala colgando del visual. La fase 7 ya había medido que eso basta para que el texto
  se rasterice filtrado. O sea: una animación invisible que dejaba media aplicación un poco
  borrosa a partir de la primera revisión. Ahora hay `Ui::Element::ResetScale`, que para la
  escala Y el `CenterPoint` y escribe la identidad.
- **El retardo del temblor con las animaciones del sistema apagadas.** `DragCard::Refuse`
  esperaba los 320 ms del temblor antes de apagarse, y en ese modo no hay temblor: la
  tarjeta se quedaba un tercio de segundo parada en el aire. Es no animar **y** hacer
  esperar.

**La auditoría con el modo lento, y los dos fallos que sacó**

Nueve transiciones, contando los píxeles que cambian entre fotogramas consecutivos —uno cada
110 ms— con el multiplicador ×5 puesto. Esa cuenta dibuja la forma del movimiento: un cero
en medio con números a los lados es un hueco, y un pico después de una racha de ceros es
algo que apareció de golpe. Las dos cosas salieron.

- **Cerrar el inspector daba un pico del tamaño del panel en el último fotograma.** La fase
  5 lo dejó solo encogiendo hasta su tarjeta, contando con que al final ES la tarjeta. No lo
  es: su material es el velo del panel. Ahora se apaga mientras encoge — de un salto a
  **206 ms limpios**.
- **Cambiar de vista dejaba la columna casi vacía mientras se rellenaba.** A los 130 ms
  había cuatro tarjetas de diez y no estaba llena hasta casi los quinientos. El escalonado
  de entrada es para unas pocas filas que llegan, no para una lista entera que cambia. Ahora
  `Update` los distingue por un criterio que significa algo —se fueron todas y no se quedó
  ninguna, o sea que nada se ha movido— y funde la columna. De dos oleadas separadas a
  **dos fotogramas**.

Y el estado de las demás, en Release: cerrar el inspector 206 ms, lista↔cuadrícula 235-389,
cambiar de vista 220 con una cola pequeña, el hover 82. Ninguna con huecos.

**`SettleMs` no predice lo que se ve, y eso también está medido**

El morfeo de apertura del inspector es la única que sigue larga, y sirvió para poner número
a lo que se sospechaba desde el arreglo de la rueda:

| muelle estándar | lo que dice `SettleMs` | lo que se ve en pantalla |
|---|---|---|
| periodo 200 ms | 150 ms | **2115 ms** |
| periodo 100 ms | 75 ms | **1313 ms** |

Tres repeticiones cada uno, ±10 ms. El periodo manda —el doble de periodo da 1,6 veces el
tiempo— pero lo que se ve dura un orden de magnitud más que `SettleMs`, porque un muelle se
acerca asintóticamente y el último medio píxel tarda. Cuánto de eso lo ve un ojo, un
contador de píxeles no lo sabe decir.

Por eso la tabla **no se ha vuelto a tocar**: ya se afinó a ciegas dos veces contra este
mismo número y las dos salieron mal. Queda escrito en `MotionSpec.h`, con las medidas, para
quien lo afine con la aplicación delante. Y queda dicho también por qué cerrar el inspector
va a 206 ms y abrirlo no: al cerrar, el fundido tapa la cola del muelle.

**El modo lento de depuración (F10, solo en Debug)**

`Motion::TimeScale()` multiplica TODO lo que dura algo: periodos, fundidos, retardos, el
temblor, el parpadeo del cursor, el desplazamiento de la lista y **los dos temporizadores
que esperan a que un muelle acabe** para esconder lo que viajó. Sin esos dos últimos, el
modo lento escondería justo el salto que se está buscando: el inspector desaparecería en el
aire a mitad de su viaje.

Es una global mutable, que es lo que normalmente no se hace y aquí es lo correcto: es un
mando de la aplicación entera, y pasarlo por parámetro obligaría a llevarlo encima a
`Gfx::Scroller` y a dos vistas que no tienen ningún otro motivo para saber que existe.

**El desplazamiento: la rueda manda destino, no velocidad**

Lo reportado fue exacto: *«scrolleo y pierdo mis proyectos porque se desliza sobre el
hielo»*. La fase 2 había decidido mandarle al `InteractionTracker` un impulso, para que DWM
calculara la inercia y dos muescas seguidas llegaran más lejos que el doble de una. Las dos
cosas son ciertas y las dos están mal para una rueda: **una rueda no tiene velocidad que
medir, tiene muescas**, y cada muesca es una distancia que Windows ya define
(`SPI_GETWHEELSCROLLLINES`). Dejar que DWM decidiera dónde parar es exactamente lo que se
sintió.

Ahora `Scroller::By` suma la muesca a un DESTINO y va con muelle. Tres consecuencias:

- **Cinco muescas recorren exactamente cinco muescas**, estén o no las anteriores todavía en
  el aire: se suma sobre el destino y no sobre la posición actual, que es lo que antes se
  perdía por el camino.
- **Y con DURACIÓN FIJA, que es el único sitio de la aplicación donde un muelle no vale.**
  Esto salió de medir, y de medir dos veces porque la primera respuesta era la esperada y
  la pantalla decía otra cosa. Con el muelle puesto —periodo 180, amortiguación 1, que
  debería asentar en 115 ms— la columna **seguía cambiando píxeles durante un segundo y
  medio** después de una sola muesca. Contando cada cuántos milisegundos cambia la lista:

  | rueda | movimiento en pantalla |
  |---|---|
  | sin animar | acabado antes del primer fotograma |
  | muelle, periodo 60 ms | ~1000 ms |
  | muelle, periodo 180 ms | ~1500 ms |
  | **duración fija de 200 ms** | **~200 ms** |

  Tres veces el periodo no da tres veces el tiempo, así que dentro del `InteractionTracker`
  no está corriendo solo el muelle que se le pasa. Y da igual el porqué: una muesca que deja
  la lista moviéndose un segundo y medio ES el hielo del que se quejó quien la usa, con
  inercia o sin ella. Con una duración escrita, termina cuando dice que termina — medido:
  una muesca acaba a los 200 ms, y una ráfaga de cinco, 190 ms después de la última.

  Lo que se pierde al no ser muelle —retomar la velocidad al reapuntar a mitad— aquí casi no
  se nota, porque doscientos milisegundos son más cortos que el hueco entre dos muescas de
  una mano girando la rueda. Es la excepción a la regla de la fase 1, y la única.

`PositionInertiaDecayRate` ya no se toca: no queda inercia que decaer. Y `Scroller::To`
respeta ahora las animaciones del sistema, que no lo hacía — con «Mostrar animaciones»
apagado, navegar con el teclado seguía deslizándose.

**El arranque: medido, y el objetivo no se puede cumplir**

`ms_arranque` y `ms_arranque_gpu` van a la tabla de ajustes, que es el mismo papel de
cuaderno de laboratorio que la fase 3 le dio a los dos pases de la sincronización. Desde la
creación del PROCESO, con `GetProcessTimes`, porque lo que se quiere saber es lo que espera
quien hace doble clic.

Antes de tocar nada, en caliente: **269 ms de media** (256-291, n=5). Y el desglose por
etapas dijo dónde:

    cargador 13  apartamento 3  ventana 6  escena 4  DISPOSITIVO 177  texto 0
    animador 3  host 24  caché 1  vista 14  tema 7  primer fotograma 35

Con un banco aparte, `D3D11CreateDevice` en un proceso limpio, tres ejecuciones cada uno:

    hardware                            172 ms
    WARP                                 20 ms
    adaptador explícito   factory 14 + device 162
    DLL precargadas       dll 0 + device 162

Las dos últimas filas son las que cierran el caso: **no es el cargador ni la enumeración de
adaptadores, es el controlador de la tarjeta inicializándose**, y no hay manera de pedirle
que tarde menos. WARP tarda 20 pero es el rasterizador por software, que la fase 1 descartó
a propósito porque esta aplicación repinta texto al redimensionar, al cambiar de DPI y al
cambiar de tema.

Lo que sí se podía hacer, y se hizo: `Gfx::Device::BeginCreate()` lanza el dispositivo en un
hilo en la **primera línea** de `Init`, antes incluso del apartamento COM, y el resto de la
función le va quitando trabajo al camino crítico — la ventana, la escena, el tema, abrir
SQLite y leer la caché entera, que estaban todos detrás del dispositivo sin necesitarlo.

Resultado, mismas condiciones: **246 ms de media** (239-267, n=6), de los cuales **120-146
esperando al dispositivo**. Contra los 172 que tarda solo: el hilo escondió entre 30 y 50 ms.
La diferencia no se ve en el total porque el ruido entre ejecuciones es mayor que eso, y por
eso se mide directamente lo que el hilo de UI estuvo parado en vez de deducirlo del total.

**En frío no baja de 300, y no puede.** El suelo es ~160 ms de controlador + ~30 de ventana
y escena + ~50 de árbol de vistas y primer fotograma, y en frío las tres suben. La única
manera de bajar de ahí es que el primer fotograma no necesite Direct2D, y como todo lo que
se lee en esa pantalla es texto, eso significa enseñar la ventana antes de tener texto — que
es dejar de cumplir «primer frame con datos», que era el punto. Queda dicho aquí para quien
lo quiera decidir, no escondido en un número.

**`brujula://repo/NOMBRE` y `brujula.exe --repo NOMBRE`**

- **El parser es puro y está en `brujula_core` con pruebas**, y no es ceremonia: un esquema
  propio lo puede disparar **cualquier página web** que alguien abra sin mirar. Lo que sale
  de `App::RepoFromUrl` es un nombre de repositorio o nada, con lista blanca —letras, cifras
  y `-_./`, una sola barra, sin empezar por punto, tope de 200— y las pruebas incluyen
  `../../Windows/System32`, `uno%20dos` y un nombre de 500 caracteres.
- **Una sola Brújula a la vez**, con mutex. Dos procesos serían dos conexiones de escritura
  a la misma caché y dos hilos sincronizando la misma cuenta. Y es lo que hace que el
  esquema sirva de algo: quien pulsa un `brujula://` casi siempre tiene la aplicación
  abierta, y abrir una segunda ventana al lado no es abrir el repositorio.
- **El traspaso va por `WM_COPYDATA`**, que es el único mensaje que el sistema copia de un
  espacio de direcciones al otro; un puntero dentro de un mensaje propio sería un puntero de
  otro proceso. Se comprueba el sello, que el tamaño sea un número entero de caracteres y se
  copia con largo explícito, y el nombre se vuelve a validar al recibirlo: por ahí entra
  cualquiera que sepa el nombre de nuestra clase de ventana.
- **El esquema se registra en HKCU** apuntando a este ejecutable, y se reescribe si cambió —
  Brújula es un `.exe` que se copia, y un esquema apuntando a una ruta que ya no existe abre
  un error del shell que el usuario no tiene de dónde agarrar para arreglar. Se lee antes de
  escribir, así que lo normal es que el arranque no toque el registro.
- **Y por NOMBRE y no por identificador**, que es lo que trae la URL: nadie va a escribir un
  node id de GraphQL en un lanzador. `App::State::IdOfName` compara primero «dueño/nombre» y
  después el nombre a secas, sin distinguir mayúsculas, que es como los trata GitHub.

Comprobado de punta a punta con la cuenta real: el esquema queda registrado con la ruta
correcta, la segunda instancia entrega y sale con código 0 sin dejar un segundo proceso, y
el repositorio pedido queda seleccionado y **con el inspector abierto**.

**Fugas: ninguna en cuarenta minutos de uso denso**

No es una hora seguida, y se dice: son **40 minutos y 1476 vueltas** de abrir y cerrar el
inspector, mover la selección, desplazar la lista y sincronizar cada minuto — unas cinco mil
pulsaciones y cincuenta y seis sincronizaciones, que es bastante más de lo que caben en una
hora de uso humano. Muestreando cada medio minuto (124 muestras):

    memoria privada  58,8 - 63,2 MB      handles  721 - 767
    hilos            61 - 64             GDI      12 (constante)     USER  22 - 26

La memoria sube 4 MB en un escalón puntual y se queda plana el resto; los handles y los
hilos oscilan alrededor de su valor y **terminan por debajo de donde empezaron**. Ninguna
de las cinco series tiene pendiente. Una sesión de una hora ENTERA sin interrupción no se
llegó a completar, así que eso queda como lo único no comprobado del punto.

**Icono y «Acerca de»**

Una aguja de brújula sobre el azul de acento, inclinada al nordeste —recta y vertical se lee
como un triángulo cualquiera— con nueve tamaños de 16 a 256. Se genera con un script y el
`.ico` se guarda en el repositorio: compilar no depende de tener PowerShell.

La versión vive en `src/app/Version.h`, que incluyen el `.rc` y la aplicación. Con el número
escrito en los dos sitios no falla nada el día que se suba uno y no el otro: simplemente las
propiedades del archivo dicen una cosa y la aplicación otra, y nadie se entera hasta que
hace falta saber qué versión tiene alguien delante. La hoja de «Acerca de» enseña además el
último arranque medido y cuánto de él fue esperar a la tarjeta gráfica — cuando dentro de
seis meses el arranque se sienta lento, lo primero que hace falta es contra qué compararlo.

---

### Fase 7 — Revisión semanal

Ctrl+Mayús+R: la lista se aleja y llega una pila de tarjetas, una por repositorio que
necesita una decisión. Se pasa entera con el teclado —1-4 la clasifican, E edita el
siguiente paso sin salir, Espacio la manda al final, Esc termina— y al acabar sale un
resumen de cómo quedó cada grupo. Con un recordatorio opcional que avisa el día y la hora
que se elija.

La segunda fase que se prueba **con la aplicación en uso**, y salieron cuatro fallos que
ninguna captura enseñaba. Dos eran de esta fase y dos son reglas del kit mordiendo por el
lado contrario al que se aprendieron.

**Lo que hay**

- **`Views::Review`**, hijo de `Views::Main` y no capa flotante del Host: las capas se
  cierran todas juntas cuando la ventana pierde el foco, y una revisión a medias que
  desaparece por mirar el navegador un momento perdería por dónde iba. Es la misma razón
  por la que `Views::DragCard` tampoco es una capa.
- **La pila la decide `App::ReviewQueue`**, que es puro y tiene prueba: primero los
  desajustes de «Necesita decisión» y después los «Sin clasificar». «Enfoque sin actividad»
  no se pide aparte porque ya ES uno de los desajustes —`Mismatch::FocusDormant`— y pedirlo
  dos veces enseñaría la misma tarjeta dos veces. Devuelve identificadores y no posiciones:
  el estado se reconstruye entero después de cada guardado.
- **Dos caras que se alternan.** La que sale tiene que seguir viéndose mientras entra la
  siguiente, o entre una decisión y la otra hay un hueco en blanco — y con veinte
  repositorios en dos minutos ese hueco es la mitad del tiempo.
- **La tarjeta no decide nada**: pregunta a `App::ApplyPriority`, que ahora devuelve si
  pudo. Con un «no» —Enfoque lleno— la tarjeta tiembla y se queda, la hoja de la fase 6 sale
  por encima y, cuando alguien elige quién baja, la tarjeta sale volando entonces.
- **Barra de progreso** de tres DIP arriba y **resumen animado** al terminar: cuántos se
  decidieron y una barra por grupo, entrando escalonadas veinte milisegundos.
- **Recordatorio** (`Shell::Balloon`): un globo del área de notificación, no una
  `ToastNotification` de WinRT. Una aplicación sin empaquetar necesitaría un AppUserModelID
  en un acceso directo del menú Inicio y un servidor COM registrado para enterarse del clic;
  eso es un instalador, y esto es un .exe que se copia. El icono se añade para el globo y se
  quita en cuanto el globo se va.

**Los cuatro fallos que salieron de usarla**

- **La tarjeta que entra salía diminuta.** Al salir volando hacia su diana encogía con una
  animación de escala; la cara se reutiliza dos decisiones después y `Visual().Scale({1,1,1})`
  no bastaba para devolverla: **escribir una propiedad que tuvo una animación encima no la
  deja escrita**. Es la misma lección de la fase 6 en su tercera visita. Se quitó la escala
  entera: la tarjeta sale volando con desplazamiento y fundido, que es lo que dice hacia
  dónde va; el tamaño no añadía nada y sí un modo de fallo.
- **El campo del siguiente paso no dejaba escribir ni una letra.** La revisión es un modo y
  se quedaba con todas las teclas, y `Shell::Window` se come el `WM_CHAR` de cualquier tecla
  consumida —lo que desde la fase 5 evita que un atajo de una letra se escriba dentro del
  campo que acaba de abrir—. Mientras se edita, todo lo que el campo no quiera se devuelve
  SIN consumir; solo Esc se consume.
- **El texto del renglón se leía a través del campo.** El campo es un pozo translúcido que
  se coloca justo encima, así que «Sin siguiente paso — pulsa E para escribirlo» salía
  cruzado con lo que se escribía. Mientras se edita, la tarjeta no pinta ese renglón.
- **El bloque estaba pegado al título** con media pantalla de negro debajo del pie, y los
  fantasmas de la pila no se veían. La tarjeta, la pila y el pie van centrados en lo que
  queda, y los fantasmas asoman trece DIP y son opacos: debajo de la de delante está su
  sombra, y un fantasma translúcido bajo una sombra es el mismo gris que el fondo.

**Y uno que no se ve nunca**

El marcador de «ya sonó hoy» del recordatorio se guardaba con el día **UTC** y el disparo
mira la hora **local**. Un aviso del lunes a las nueve de la noche se marcaba como del
martes, y al cruzar la medianoche UTC el marcador dejaba de coincidir y volvía a sonar esa
misma noche: dos avisos del mismo recordatorio. El día se escribe ahora del mismo reloj con
el que se decidió disparar.

**Aplazar una pregunta (P)**

Lo que faltaba para que la revisión sirva la segunda semana: sin esto, los mismos desajustes
vuelven a preguntarse cada vez y la única salida es cambiar la prioridad o hacer un push.

- **Esquema v4**: `pospuesto_hasta` (el día, como número de días desde la época) y
  `pospuesto_por` (qué desajuste se aparcó) en `local`, las dos por omisión vacías, así que
  una caché de la fase 7 se ve igual después de migrar. Comprobada la migración v3→v4 contra
  la base real, dos veces.
- **Silencia una pregunta, no un repositorio.** Si durante el plazo aparece un desajuste
  distinto, se vuelve a preguntar. El caso que lo justifica: aparcar «está en Enfoque y
  parado» no puede tapar durante un mes que un archivado volvió a recibir pushes, que es el
  desajuste más informativo de los tres.
- **Vence por día y no por segundo** (`Model::DayNumber`): quien aplaza algo un mes no espera
  que vuelva el día que vence a la hora exacta a la que lo aplazó, en medio de otra cosa.
- **Vista «Pospuestos»** en la barra lateral, con su contador. Un aplazado SIGUE contando en
  «Necesita decisión» —esa vista dice lo que pasa— y cuando la pila sale vacía por
  aplazamientos, la aplicación lo dice en vez de decir «no hay nada».
- **Se deshace** con Ctrl+Z, como todo lo demás, y se cuenta aparte en el resumen: mueve la
  barra de progreso pero no entra en ningún grupo.

**Sobre «se ve un toque borroso»: tres hipótesis, tres medidas, y ninguna era**

Vale la pena dejarlo escrito porque las tres parecían buenas:

- *Remuestreo por una escala.* No: el borde de la tarjeta pasa de fondo a tarjeta en UN
  píxel y mide 620 px justos. (La animación de escala se quitó igual, pero por otro fallo.)
- *La sombra y su LayerVisual.* No: quitando `CreateShadow` el texto sale idéntico.
- *Está temblando.* No: con la revisión abierta y sin tocar nada, tres capturas separadas
  350 ms y una cuarta a los dos segundos salen **idénticas píxel a píxel**.

Lo que queda es el suavizado en gris de toda la aplicación —la fase 1 lo eligió porque
ClearType no existe sobre alfa premultiplicado— que canta aquí porque es la primera pantalla
con texto de 26 DIP. Apuntado para la fase 8 con el arreglo a probar: `IDWriteRenderingParams`
propios con más contraste y otra gamma.

**Medido**

- Los 275 tests pasan, nueve nuevos: dos sobre `App::ReviewQueue` y siete sobre aplazar —el
  borde del último día, que otra pregunta no queda tapada, y que los nombres del desajuste
  van y vuelven.
- Compila sin warnings con `/W4 /permissive-`.
- `auditar.ps1`: las once reglas limpias.
- Probado contra la cuenta real de 109 repositorios: la pila entera, el límite de Enfoque
  con su hoja y su relevo, editar el siguiente paso, saltar, aplazar —y que el aplazado
  desaparezca de la pila siguiente y aparezca en «Pospuestos»—, el resumen y la vuelta a la
  lista. La base de datos se respaldó antes y se restauró después.

**Segunda pasada, con entrada real (22 de septiembre)**

Cerrado el navegador que retenía el primer plano, se repitió todo con teclado y ratón de
verdad. Dos fallos más, los dos heredados:

- **Ctrl+K no abría la paleta desde la lista** (fase 6). `Ui::List::OnKey` trata la `J` y la
  `K` como «abajo» y «arriba» sin mirar los modificadores, y la lista tiene el foco al
  arrancar: Ctrl+K subía la selección y se comía el atajo. Ahora una letra con Control no es
  de la lista — ni de la revisión, que tenía lo mismo con su `E` y su `P`.
- **El nombre de la aplicación salía como «BrÃºjula»** (fase 1). `rc.exe` lee un `.rc` sin
  BOM con la página de códigos del sistema; el UTF-8 se compilaba como CP1252 y la mojibake
  se veía en las propiedades del archivo y en la cabecera de la notificación. Un
  `#pragma code_page(65001)` y arreglado. No daba ningún error al compilar.

Y dos falsos positivos del arnés que conviene no repetir: la estructura `INPUT` de
`SendInput` mide 40 bytes en x64 y sin la parte del ratón en la unión sale de 32, con lo que
la llamada devuelve cero y no llega ni una tecla; y entre dos ejecuciones del script la
ventana pierde la activación y `onDeactivate` se lleva menús, hojas y paleta.

Verificado con entrada real: Ctrl+Mayús+R, E y escribir, Espacio, 1, P, Esc, Ctrl+K con su
acción, y **el globo del recordatorio pulsado con el ratón**, que trae la ventana al frente
y abre la revisión.

**Lo que no se ha podido comprobar**, cuatro cosas, todas heredadas: la nitidez
a otras escalas —`WM_DPICHANGED` no se puede disparar aquí: una sola pantalla al 100 %,
comprobado con `GetDpiForWindow` desde un proceso DPI-aware—, el IME de verdad, el panel
táctil de precisión y los tres cuadros de archivo.

---

### Fase 6 — Priorizar

La fase que convierte una lista en algo con lo que se decide: arrastrar tarjetas entre
grupos, reordenarlas dentro del suyo, las teclas 1-4, el menú de cada tarjeta, la paleta de
comandos y deshacer.

Y la primera que se probó **con la aplicación en uso** en vez de con capturas. En cuanto
alguien hizo clic deprisa salieron tres cosas que ninguna captura había enseñado, las tres
heredadas de fases anteriores: un panel congelado a medio viaje, las tarjetas en blanco y
unas animaciones correctas sobre el papel que en la mano se sentían lentas.

**Lo que hay**

- **Arrastrar** (`Ui::List` + `Views::DragCard`): la tarjeta se levanta —escala 1,03 y
  sombra—, las demás se apartan con el muelle suave dejando el hueco donde caería, y al
  soltar cae en su sitio. Sobre un grupo de la barra lateral cambia de prioridad; entre dos
  tarjetas, reordena. Soltar en cualquier otro sitio la devuelve de donde salió.
- **Teclas 1-4** sobre la tarjeta elegida, por el mismo camino y con el mismo viaje hacia su
  grupo. Sin tecla para «Sin clasificar»: no es una prioridad, es la falta de una, y para
  quitarla está el menú.
- **Límite de Enfoque**: la tarjeta se para con un temblor horizontal corto y una hoja
  pregunta cuál de los cinco baja a Secundario, ordenados por el que lleva más tiempo sin un
  push. Los dos cambios —el que baja y el que sube— se apuntan como UNA entrada de deshacer.
- **Menú contextual** en cada tarjeta: abrir, las cinco prioridades —la de ahora dicha con
  palabras y no con una marca—, editar el siguiente paso, añadir una novedad, abrir en
  GitHub y abrir la carpeta local.
- **Paleta de comandos** (Ctrl+K, `Views::Palette`): entra desde arriba con el muelle
  estándar y el fondo atenuado. Busca entre los 109 repositorios y las acciones del momento,
  con el MISMO filtro que la búsqueda de la lista.
- **Deshacer** (Ctrl+Z) para prioridad, estado, siguiente paso, carpeta, modo repo, orden y
  novedades. Cincuenta de fondo. Con el foco en un campo de texto, el Ctrl+Z sigue siendo
  del historial del campo.
- **Esquema v3**: una columna `orden` en `local`, por omisión cero —«este nunca se ha
  arrastrado»—, así que una caché de la fase 5 se ve exactamente igual después de migrar.

**Los tres arreglos que salió de usarla**

- **El panel congelado a medio crecer.** Volver a pulsar una tarjeta mientras el inspector
  todavía se estaba abriendo lo dejaba clavado a mitad de camino, flotando sobre la lista.
  La causa: **escribir una propiedad que tiene una animación encima no para la animación**.
  `Element::SetFrame` paraba `Offset` antes de escribirlo pero no `Size`; el tamaño escrito
  se perdía, y acto seguido `Material::SetSize` paraba esa misma animación por su otro
  extremo —`Animator::SizeTogether` la arranca en el visual y en la geometría a la vez—, así
  que el visual se quedaba con el último valor animado para siempre. Reproducido con ocho
  clics puestos en la cola de la ventana con 90 ms entre ellos, y ya no pasa. De paso,
  `Inspector::Rebuild` recoloca lo de dentro sin reescribir su propio marco: hacerlo plantaba
  el panel en su destino a mitad de la transición.
- **Las tarjetas en blanco.** «Desaparecen los proyectos y tengo que pasarles el mouse para
  que aparezcan.» `Ui::List::OnArrange` recolocaba las celdas vivas —reservando su textura al
  ancho nuevo, y reservar una textura la VACÍA— sin repintarlas. Abrir el inspector estrecha
  la columna, así que todas las tarjetas se quedaban en blanco menos la que tuviera el ratón
  encima, que se repintaba por el hover. **Comprobado que venía de antes**: se compiló el
  árbol anterior a esta fase y un solo clic lo reproduce. Ahora repinta `PlaceRow`, que es
  por donde pasan los cinco sitios que recolocan —dos ya se habían olvidado—, y solo cuando
  la textura de verdad ha cambiado de tamaño.
- **El tiempo de las animaciones.** Los periodos de la fase 1 asentaban en 85, 165, 239 y
  297 ms; con la aplicación llena de datos, ir de un proyecto a otro se sentía lento. Los
  cuatro bajan a 80, 130, 165 y 195 ms de periodo —asientan en 57, 97, 131 y 166— sin tocar
  las amortiguaciones, que son las que dan el carácter. El escalonado de los que entran baja
  de 200 a 120 ms de techo, y la inercia del desplazamiento de 0,92 a 0,85 de frenada por
  fotograma: una muesca se para en poco más de medio segundo en vez de en casi uno, y
  recorre lo mismo —la distancia se deriva del mismo número—.

**Lo decidido**

- **Un solo camino para la prioridad.** Cinco sitios la cambian —inspector, teclas,
  arrastre, menú y paleta— y los cinco llaman a `Application::ApplyPriority`. Con la
  comprobación del límite copiada en cada uno, el sexto Enfoque entra por el que se olvidó.
- **El arrastre avisa de su final SIEMPRE**: al soltar, con Esc y cuando otra ventana se
  lleva la captura. Esto último no se enteraba nadie hasta ahora —`Ui::Router` se comía el
  `Cancel`— y es lo que hacía falta para que soltar fuera no deje nunca una tarjeta
  flotando. `List::Update` lo cancela también: una sincronización que termine a mitad cambia
  a qué repositorio apunta cada índice.
- **El clic se avisa al soltar**, no al pulsar: al pulsar todavía no se sabe si es un clic, y
  avisándolo ahí cada arrastre empezaba abriendo el inspector debajo de la tarjeta que se
  estaba levantando. La selección se queda en el pulsar.
- **El orden a mano no viaja a PROYECTO.md** —el formato no tiene ese campo, y serían ciento
  nueve commits por una tarde ordenando— y se escribe con `Repos::SetOrder`, que toca esa
  columna y nada más. Al soltar se renumera la vista entera del uno en adelante, en una
  transacción. Con búsqueda puesta no se ordena: lo que se ve es un trozo.
- **Lo ordenado a mano va primero en todas las vistas.** Mirar el orden solo entre los de la
  misma prioridad no sería una relación de orden, y `std::sort` con una de esas no da un
  resultado raro: da comportamiento indefinido.

**Lo medido**

- Las pruebas pasan de 261 a 266 casos y de 2008 a 2054 aserciones. Las nuevas fijan la
  tabla de muelles nueva, el orden a mano —que va delante de la fecha—, `Reordered` y sus
  bordes, que las cinco primeras vistas son las cinco prioridades, y que guardar una nota no
  pisa el orden ni una sincronización lo toca.
- Filtrar 500 repositorios sigue costando 0,007 ms por pulsación.
- `auditar.ps1`: las once reglas en verde.
- La migración a v3 corrió sobre la caché de verdad, con los 109 repositorios dentro:
  `user_version` a 3 y la columna `orden` a cero en todas partes.

**Lo que falta por comprobar**

- **El arrastre con un ratón de verdad.** La máquina tenía otra aplicación reteniendo el
  foco —el mismo problema que dejó los cuadros de archivo sin probar en la fase 5— así que
  todo se probó con mensajes puestos a mano en la cola de la ventana. Con eso se vio el
  camino entero: levantar la tarjeta, soltarla fuera y verla volver a su sitio, y la tecla 1
  poniendo un repositorio en Enfoque con el contador de la barra lateral subiendo a 1. Lo que
  no se ha visto es el hueco abriéndose entre dos tarjetas mientras una mano mueve el ratón.
- La **nitidez a otras escalas**, el **IME de verdad**, el **panel táctil de precisión** y
  los **cuadros de archivo**, heredados de las fases anteriores y por los mismos motivos.

---

### Fase 5 — Inspector, notas y PROYECTO.md

La primera fase en la que Brújula **escribe**. Hasta aquí todo lo que se veía se podía
volver a descargar; desde aquí hay datos que solo existen porque alguien los escribió, y
eso cambia qué es un fallo grave: ya no es una pantalla en blanco, es un párrafo que
desaparece de un archivo ajeno dentro de un commit que dice «actualizar PROYECTO.md».

**Lo que hay**

- **Inspector** (`views/Inspector`): se abre con clic o con Enter y **la tarjeta se
  transforma en el panel** —posición, tamaño y radio con el muelle estándar, contenido con
  fundido cruzado—; Esc lo cierra devolviéndola a su sitio. Dentro: prioridad y estado con
  menú, siguiente paso editable en línea (`E`), novedades con fecha (`N` añade, `Supr`
  borra), los cinco últimos commits, issues y PR abiertos, el bloque de PROYECTO.md, los
  otros `.md` de la raíz y los botones de abrir en GitHub y en la carpeta local.
- **`projectfile/Proyecto`**, puro y probado: lee y escribe el formato de `CLAUDE.md`,
  **conservando lo que no entiende** —claves inventadas del frontmatter, párrafos, secciones
  enteras— y fusionando novedades por fecha y texto.
- **Esquema v2**: tablas `commits` y `raiz_md`, y dos columnas nuevas en `local`,
  `repo_confirmed` y `push_pending`.
- **Modo repo**: se enciende por repositorio, pasa por una hoja de confirmación una vez por
  repositorio, y escribe `PROYECTO.md` con la API de contenidos. Con conflicto, relee,
  vuelve a fusionar y reintenta; si el archivo ya dice lo mismo, **no commitea**.
- **Copia de seguridad**: exportar e importar todo lo del usuario a un JSON.
- **Ajustes** al pie de la barra lateral: la carpeta donde se clonan los repositorios, el
  modo repo por omisión y las dos copias.
- **El pase 2 trae más**: los cinco últimos commits y la lista de `.md` de la raíz, para
  que abrir el inspector no espere a la red.

**Lo medido**

El pase 2 engorda, y era la decisión que había que comprobar antes de seguir. Tres tandas
de cada, el mismo día y contra los 109 repositorios de la cuenta, forzando el
reenriquecimiento entero —que es el peor caso, no el de un día normal—:

| | pase 1 | pase 2 |
|---|---|---|
| fase 3, medido entonces | 1,3 s | 2,2 s |
| hoy, sin los campos nuevos | 2,6 / 2,4 s | 3,1 / 2,7 s |
| hoy, con los cinco commits y la raíz | 2,2 / 2,3 / 2,4 s | **4,3 / 4,4 / 3,2 s** |

El pase 1 no cambió de consulta y sin embargo subió de 1,3 s a ~2,4 s: la red de hoy es más
lenta que la del día de la fase 3, así que la comparación buena es la del mismo día. Contra
esa, el pase 2 pasa de ~2,9 s a ~4,0 s: **poco más de un segundo**, y solo en una
sincronización que tenga que reenriquecerlo todo. La de un día normal sigue sin hacer ni una
petición de detalle. Se queda en el pase 2.

De ahí salen 303 commits y 126 archivos `.md` de raíz guardados en la caché — o sea, unos
2,8 commits de media por repositorio y `.md` importables en más de la mitad de ellos.

Los tiempos de cada pase se guardan ahora en la tabla de ajustes (`ms_pase1`, `ms_pase2`,
`repos_detalle`). Es lo mismo que la fase 3 decidió para el resto: la fila de estado hace
de registro, porque un archivo de log es justo lo que la regla 3 de `SEGURIDAD.md` evita.

**Lo comprobado contra la cuenta de verdad**

Con `Elimay312/pruebaFable`, que es un repositorio de pruebas:

1. El commit sale con el mensaje exacto: `chore: actualizar PROYECTO.md` (`c21c8bb`), y el
   archivo con el frontmatter y la sección de novedades del formato de `CLAUDE.md`.
2. Editando el archivo **desde fuera** —otra clave en el frontmatter, un párrafo suelto y
   una sección `## Licencia` al final— y guardando después desde Brújula sin sincronizar en
   medio: GitHub contesta 409, se relee, se vuelve a fusionar y el segundo intento
   (`f304796`) deja **lo nuestro actualizado y lo ajeno intacto**, con las dos novedades.
3. Editar el siguiente paso y cerrar la aplicación conserva el cambio: se comprueba leyendo
   SQLite con el proceso ya muerto.

**Lo que se arregló, y venía de la fase 4**

**Una tecla usada como atajo se escribía además como letra.** `E` enfocaba el campo del
siguiente paso y acto seguido metía una «e» dentro; `N` abría una novedad que empezaba por
«n». El motivo es de Windows y no del kit: `TranslateMessage` pone el `WM_CHAR` en la cola
al sacar el mensaje, **antes** de que nadie haya podido decir que la tecla era un atajo, así
que consumir el `WM_KEYDOWN` no lo evita. `Shell::Window` se come ahora el `WM_CHAR` que
sigue a una tecla consumida.

Y el detalle que costó encontrarlo: la marca la tocan **solo las pulsaciones y nunca las
sueltas**. Con un teclado de verdad el `WM_CHAR` suele llegar antes que el `WM_KEYUP`, pero
no siempre — y al mandar los mensajes a mano para probarlo, nunca. Un `WM_KEYUP` colándose
en medio apagaba la marca justo antes de que sirviera.

Esto venía de la fase 4 sin que se viera: su único atajo de una sola letra era `/`, que
enfoca la búsqueda… y se escribía dentro del propio campo que acababa de enfocar.

**Decisiones**

- **Lo que viaja es el elemento, no un `Gfx::Morph`.** Un `Morph` lleva dos capas de
  píxeles, y el inspector tiene un campo de texto, botones y una lista, que son elementos
  con entrada: usarlo obligaría a dibujar el panel dos veces, una como textura para el viaje
  y otra como árbol al aterrizar. En su lugar el kit gana `Element::MorphTo`, que anima
  posición, tamaño y radio del material **solo en elementos sin superficie propia** —con
  textura habría que reasignarla en cada fotograma— y `Element::SetContentOpacity`, que
  cruza el contenido sin tocar el material. Por eso, en el primer fotograma, el panel es la
  tarjeta: misma forma, mismo color, mismo sitio.
- **La lista se estrecha de golpe y las celdas se deslizan.** Es la misma decisión que la
  fase 4 tomó para el paso de lista a cuadrícula, y por el mismo motivo.
- **El inspector no guarda punteros al estado.** `App::State` se reconstruye entero después
  de cada sincronización, así que un puntero a una `App::Entry` apuntaría a memoria liberada
  en cuanto llegara el hilo de trabajo. Recibe una copia de lo que enseña.
- **Y se reengancha por identificador, no por posición.** Al cambiar la prioridad, el
  repositorio puede salirse de la vista que se está mirando; el panel tiene que seguir
  enseñando lo que el usuario acaba de tocar en vez de cerrarse en su cara.
- **El texto estático del panel vive en UNA pizarra.** Un contenedor cuyos hijos tienen
  superficie no puede pintar (regla del orden en z de `ui/Element.h`), y el inspector no
  puede tener superficie propia porque tiene que poder morfear. La pizarra se añade la
  primera y queda debajo de todo lo demás.
- **Los cinco commits son cinco etiquetas y no una con saltos de línea.** `Ui::Text` recorta
  con elipsis midiendo el texto entero, así que cinco renglones en una sola se recortarían
  por un punto cualquiera del bloque en vez de uno por renglón.
- **Guardar es del `OnBlur` y Enter solo suelta el foco.** Con dos caminos de guardado uno
  de los dos se olvida, y el que se olvida siempre es el de perder el foco — que es la mitad
  de las veces que alguien termina de escribir.
- **Primero SQLite y después la red, siempre.** El criterio de aceptación de la fase no
  puede depender de que haya cobertura. Lo que no llega a GitHub se queda marcado en
  `push_pending` y se reintenta al terminar la siguiente sincronización.
- **Un commit que no cambia nada no se hace.** Antes de escribir se compara con el texto que
  hay; si coinciden, se da por bueno. Tapa además el reintento de red de un PUT cuya
  respuesta se perdió: el commit ya existe y al releer sale exactamente esto.
- **El modo repo son DOS columnas.** `repo_mode` es el interruptor y `repo_confirmed` es
  «alguien dijo que sí en ESTE repositorio». Escribir exige las dos, y solo una línea de
  todo el programa enciende la segunda. Con un solo booleano, «modo repo por omisión» sería
  exactamente el interruptor global que la regla 5 de `SEGURIDAD.md` dice que no existe.
- **Y la copia de seguridad no lo importa.** Se exporta, para que la copia diga la verdad de
  cómo estaba la cosa, y al restaurar hay que volver a confirmar repositorio por
  repositorio. Un archivo que encendiera ciento nueve escrituras sería ese mismo interruptor
  entrando por la puerta de atrás.
- **El trabajador pasa a atender una cola.** Sincronizar y escribir comparten la credencial,
  el cliente y la conexión a SQLite, y dos dueños de una credencial son dos vidas que
  sincronizar. Queda además serializado, que es lo que se quiere: un PUT no puede correr a
  la vez que el segundo pase escribiendo la misma fila. La decisión de que el hilo se muera
  se toma bajo el mismo candado que usa quien encola — fuera de él, un trabajo que llegara
  entre la comprobación y el `return` se quedaría en la cola sin nadie que lo recogiera, y
  eso no daría ningún error: solo un commit que nunca sube.
- **La ruta de la API se valida, no se pega.** El nombre del repositorio viene de la
  respuesta de GitHub y acaba dentro de una URL. Con un nombre que no tenga forma de nombre,
  la escritura falla con un aviso en vez de pedir una dirección inventada.
- **`auditar.ps1` gana la regla 11**, que hasta ahora no tenía código que vigilar: el archivo
  está en una constante y vale exactamente `PROYECTO.md`, toda ruta `/repos/` termina en esa
  constante, y el único verbo que llega al cliente REST es `PUT`. Comprobada con dos sondas
  —un `DELETE` y una segunda ruta— antes de darla por buena.

**Lo que falta por comprobar**, y todo por no tener con qué:

- La **nitidez a otras escalas**, heredada de las cuatro fases anteriores: esta máquina
  tiene una sola pantalla al 100 % y `WM_DPICHANGED` sigue sin dispararse. El inspector
  añade un elemento más que anima su tamaño, así que la lista crece en vez de encoger.
- El **IME de verdad** y el **panel táctil de precisión**, también heredados.
- Una credencial **sin permiso de escritura**: la de esta máquina viene de GitHub CLI y es
  ancha, así que el 403 del modo repo no se dispara solo. El camino existe y traduce el
  código a una frase, pero no se ha visto.
- Los **cuadros de archivo** —carpeta de repositorios, exportar, importar— se han escrito y
  compilan, pero no se han podido accionar: probarlos necesita traer la ventana al frente y
  esta máquina tenía otra aplicación reteniendo el foco.

---

### Fase 4 — Vista principal

Ya se ve de qué va la aplicación. Los 109 repositorios de la cuenta entran por la barra
lateral, la lista y la búsqueda, y la ventana los enseña **antes** de hablar con la red.
Se tiran las dos raíces provisionales: `Views::Demo` (fase 1) y `Views::Status` (fase 3).

**Lo que hay**

- **`app/State`, puro y probado**: las nueve vistas de la barra lateral, el filtro, la
  búsqueda sin tildes ni mayúsculas, el orden por último push y las claves estables que
  hacen que una tarjeta se deslice en vez de repintarse en su sitio nuevo.
- **Barra lateral** con dos grupos —prioridad y vistas inteligentes—, contadores, una sola
  píldora de selección que se muda de un grupo al otro, y al pie la cuenta conectada con
  «Sincronizar» y «Cerrar sesión».
- **Lista de tarjetas** con nombre, siguiente paso destacado, punto de actividad, píldora
  de prioridad, lenguaje y «hace X días»; **lista compacta y cuadrícula**, con las celdas
  deslizándose de una disposición a la otra.
- **Búsqueda en vivo** (Ctrl+F o `/`) sobre nombre, dueño, descripción, lenguaje y
  siguiente paso, con las filas que sobreviven deslizándose y las que entran apareciendo
  escalonadas.
- **Estado vacío propio de cada vista**, con una frase y **una** acción.
- **Indicador de sincronización en la barra de título**: un punto que late mientras trabaja
  y el texto de en qué anda o de cuándo fue la última.
- **Teclado**: ↑/↓ y j/k, ←/→ en cuadrícula, Inicio/Fin, Re Pág/Av Pág, Ctrl+F y `/` para
  buscar, Esc para quitar el filtro, Ctrl+G para alternar disposición, Ctrl+R para
  sincronizar y Ctrl+O para abrir en GitHub.
- **La vista elegida se guarda** en los ajustes: se vuelve a abrir donde se dejó.

**Lo que se arregló, y venía de la fase 2**

La vista principal salía **en blanco**: la barra lateral sin una letra, el título de la
vista tampoco, los glifos de los botones de ventana tampoco, y en cambio los materiales,
las píldoras y las tarjetas sí. Lo que quedaba en pantalla era exactamente lo que alguien
había repintado después del último `Host::Layout`.

`ICompositionDrawingSurfaceInterop::Resize` devuelve un hueco del atlas **vacío**, así que
`Element::SetFrame` borraba cada superficie del árbol en cada recolocación y nadie pedía
repintarla: `Element::Relayout` invalida `SurfaceOwner()`, y la raíz no tiene superficie.
Hasta la fase 3 no se notó porque cada refresco cambiaba también el contenido, y un texto
distinto sí invalida; un título que dice siempre lo mismo, no. Dos arreglos: `Surface`
recuerda el tamaño en píxeles y sale sin tocar nada cuando no cambia, y `Element::SetFrame`
invalida cuando cambian el marco o la escala.

Y el segundo, encontrado al revisar el catálogo de F12: `Element::Close` soltaba su
referencia al visual pero no lo sacaba del árbol de composición, así que **la vista vieja se
quedaba dibujada detrás de la nueva** al cambiar de raíz. Se veía poco porque lo que se
cierra suele llevar la superficie cerrada y deja de pintar. Ahora `Close` y el destructor lo
desenganchan. De paso, el catálogo lleva ya su propio `Views::Chrome`: los botones de la
ventana los dibuja el kit desde esta fase, y una raíz sin Chrome dejaba la ventana sin aspa
a la vista —funcionando, porque el hit-test del marco no depende del dibujo—.

**Medido**

Contra la cuenta real, con la caché llena: 109 repositorios, 2 con push esta semana, 24
dormidos, 109 sin clasificar.

| | Objetivo | Medido |
|---|---|---|
| Warnings con `/W4 /permissive-`, Release y Debug | 0 | 0 |
| Pruebas | pasan | **187 casos, 1507 aserciones** (eran 186 y 1504) |
| Auditoría de seguridad | sale 0 | **10 reglas, 0 pendientes** |
| Arrancar hasta la ventana con la lista | < 200 ms | **103 – 113 ms**, mediana 106, en cinco arranques en caliente; **195 ms el primero**, recién recompilado en limpio |
| Filtrar mientras se escribe, con 500 repositorios | instantáneo | **0,006 ms por pulsación** — 2.700 veces por debajo de un fotograma |
| Consultas a SQLite para pintar la primera pantalla | pocas | **2** (`repos` y `local`) |

**Lo que no se ha podido comprobar**

Los 109 repositorios de la cuenta están **todos sin clasificar**, así que la píldora de
prioridad, el límite de Enfoque y la vista «Necesita decisión» solo se han visto con datos
hechos a mano en las pruebas. Siguen pendientes de la fase 1 y la 2 el DPI distinto de
100 %, el IME de verdad y el panel táctil de precisión.


### Fase 3 — GitHub, SQLite y sincronización

Ya hay datos. La cuenta de verdad —109 repositorios personales, 108 privados— entra en la
caché y se vuelve a sincronizar sin repetir lo que no ha cambiado.

**Lo que hay**

- **Núcleo ampliado, y todo probado**: `model/Result` (los errores como valores),
  `model/Utf` (el borde UTF-8 ↔ UTF-16 escrito a mano), `model/Time` (ISO-8601 y todo en
  UTC), `model/Types` y `model/Rules` (clasificación por actividad, límite de Enfoque y
  «Necesita decisión»), `store/` entero, y de `github/` lo que decide: construir la
  consulta, entender la respuesta y la política de reintentos.
- **Credencial**: `gh auth token` con `CreateProcessW` y una tubería, y si no hay, una hoja
  que explica los permisos y recoge el token pegado. La que pega el usuario va al
  Administrador de credenciales con `CredWriteW`; la de GitHub CLI no se guarda.
- **Cliente WinHTTP** con reintentos, lectura de las cabeceras de cuota y cancelación que
  cierra los handles en vuelo.
- **SQLite** en `%LOCALAPPDATA%\Brujula\`, con migraciones por `PRAGMA user_version`, WAL, y
  dos mitades que no se mezclan: lo del servidor y lo del usuario.
- **Sincronización en dos pases** en un hilo director, con seis hilos para el segundo, y el
  aviso al hilo de UI por `PostMessageW` sin carga.
- **Interfaz provisional**: un panel de estado que sustituye a `Views::Demo` como raíz, la
  hoja de bienvenida y los avisos discretos. La fase 4 tira el panel.

**Medido**

Contra la cuenta real: 109 repositorios, 108 privados, 0 archivados, 0 forks. **76 de 109
sin descripción y 7 sin lenguaje principal** — el nulo es el caso normal, no el raro.

| | Objetivo | Medido |
|---|---|---|
| Warnings con `/W4 /permissive-` | 0 | 0 |
| Pruebas | pasan | **175 casos, 1416 aserciones** (eran 86 y 891) |
| Auditoría de seguridad | sale 0 | **10 reglas, 0 pendientes** |
| Primera sincronización de los ~120 | pocos segundos | **5,4 – 6,7 s** los 109, en cuatro arranques |
| — de los cuales, la lista entera en la caché | | **2,9 – 4,2 s**; el detalle va entrando después |
| Segunda sincronización, sin novedades | solo lo que cambió | **2,6 s y CERO peticiones de detalle** |
| Protocolo negociado | HTTP/2 | **HTTP/2** |
| Cerrar la ventana a mitad de sincronizar | sin cuelgue | **0,26 s**, salida 0, cortando a los 0,8 / 2,5 / 4,0 y 5,2 s |
| Coste de cuota por sincronización | poco | 8 puntos de 5000/hora |
| Notas escritas a mano tras sincronizar | siguen | siguen |

La comprobación de que la caché quedó bien:
`select count(*), count(description), count(language), count(enriched_push) from repos`
devuelve **109 / 33 / 102 / 109**.

Y una anécdota que vale como medición: al añadir `tests/auth_test.cpp`, la regla 1 del
auditor lo marcó al instante. El ejemplo de credencial de la prueba empezaba por
`github_pat_`, que es justo lo que esa regla busca. No era un token de verdad, pero la regla
no puede saberlo, y así es como tiene que ser: el ejemplo se cambió por uno sin prefijo real.
La fase 2 ya dejó escrito que `auditar.ps1` no se toca para acallar un aviso.

**Lo que se probó y no valía**

**La consulta de un solo pase que pedía este documento no cumple su propio criterio.** Los
~120 repositorios con todos los campos, 100 por página, tardan **8,3–9,1 s por página**, y
una de las ocho peticiones de la medición devolvió un **HTTP 502**. Dos páginas son
diecisiete segundos.

Bajar el tamaño de página tampoco: la latencia va por repositorio (~85 ms) y no por
petición, así que `first:25` son 2,6 s por página y los 109 siguen siendo once segundos. El
cursor obliga a ir en serie.

Desglose por campo a `first:100`, descontando el arranque de `gh`: metadatos ~1,6 s,
`defaultBranchRef` +2,6 s, contadores de issues y PR +1,9 s, el blob de PROYECTO.md +0,7 s.

De ahí los dos pases. El segundo va por `nodes(ids:)`, que no lleva cursor y por eso se
puede pedir en paralelo: seis peticiones de veinte a la vez frente a tres de cincuenta en
serie son **2,2 s frente a 11,0 s**.

**El ajuste de HTTP/2 hay que comprobarlo midiendo, no leyendo.** WinHTTP limita las
conexiones por servidor, y seis peticiones «en paralelo» estranguladas a dos tardarían el
triple sin dar un solo error. Por eso `Response` lleva un campo con el protocolo que se
negoció de verdad y la sincronización lo escribe en `ajustes`: dice `HTTP/2`.

**Lo que no se pudo comprobar**

- **La credencial pegada a mano.** Esta máquina tiene GitHub CLI autenticado, así que la
  aplicación nunca llega a esa rama por sí sola. Se comprobó que al quitar `gh` del PATH el
  proceso se queda esperando, no sincroniza y no toca la caché; y el tipo `Secret` y la
  comprobación de forma tienen pruebas. Lo que falta por ver es el viaje completo de pegar,
  validar, guardar con `CredWriteW` y volver a leer con `CredReadW`. Las pruebas no lo hacen
  a propósito: escriben en el mismo sitio y con el mismo nombre que la credencial de verdad.
- **Las organizaciones.** Esta cuenta no pertenece a ninguna, así que la consulta
  `organization(login:)` está escrita y sin estrenar.
- **PROYECTO.md.** Ninguno de los 109 repositorios tiene el archivo, así que el camino del
  blob se ha probado con respuestas enlatadas y no contra uno de verdad.
- **Un token sin permiso de Contents.** El camino que repite la tanda sin el blob tiene
  prueba con JSON enlatado, pero no se ha visto con una credencial recortada de verdad.

### Fase 2 — Kit de UI y catálogo

Los ocho componentes que pedía `PROMPTS.md`, la base de entrada y repintado que necesitan,
y una pantalla de catálogo para juzgarlos. Sigue sin haber datos.

**Lo que hay**

- **Núcleo puro ampliado**: `ui/Metrics.h` (rejilla de 4, radios, elevaciones y el
  rectángulo de layout), `shell/Input.h` (eventos ya traducidos y el contador de clics),
  `ui/Edit` (el modelo del campo de texto) y `ui/Virtual` (la aritmética de la lista).
  Todo en `brujula_core`, o sea todo probado.
- **Dieciséis tokens nuevos** en `Theme::Tokens` —velos de control, estados del acento,
  foco, selección, superficies flotantes, velo y sombra— más `OnColor` y `Shade`, que los
  derivan del acento del sistema en vez de fijarlos a mano.
- **Entrada de verdad en `Shell::Window`**: mover, salir, rueda vertical y horizontal,
  botón derecho, captura, `WM_CAPTURECHANGED`, cursor, foco de ventana, menú contextual,
  `WM_CHAR` y la colocación de la ventana del IME.
- **`Ui::Element` + `Ui::Host`**: árbol retenido mínimo con enrutado, hover en cadena,
  captura, foco con Tab, capas modales y un conjunto de repintado diferido.
- **Los ocho componentes**: texto (medida, alineación, elipsis y números tabulares),
  botón en tres formas y botón de icono, píldora de prioridad y punto de actividad, campo
  de texto, lista virtualizada, elemento de barra lateral con selección deslizante, menú
  contextual y aviso discreto, y hoja modal con fondo atenuado.
- **Catálogo con F12, solo en Debug**: dos columnas, la izquierda con los tokens de claro
  y la derecha con los de oscuro, sea cual sea el tema de Windows.

**Medido**

| | Objetivo | Medido |
|---|---|---|
| Warnings con `/W4 /permissive-`, Debug y Release | 0 | 0 |
| Pruebas | pasan | 86 casos, 891 aserciones |
| Auditoría de seguridad | sale 0 | 10 reglas, 1 pendiente de la fase 3 |
| Reciclado de la lista, 500 elementos | < 16,6 ms | **0,01 ms** |
| Superficies vivas con 500 elementos | pocas | **7 a 10 filas**, no 500 |
| Velo de la hoja modal sobre blanco | `rgba(0,0,0,0.40)` | (153,153,153), que es 255 × 0,6 |
| Aviso sobre la columna oscura | 0,94 de `#38383a` sobre `#2c2c2e` | (55,55,57), lo calculado |
| Escribir «Revisión año niño» | sin problemas | 17 unidades, tildes y eñe en una cada una |

El reciclado es el número que decide el criterio de los 60 fps: el movimiento lo lleva
DWM, así que lo único que puede tirar un fotograma es lo que hace el hilo de UI al
reciclar filas, y hace 0,01 ms. Hay mil seiscientas veces más presupuesto del que gasta.

**Decisiones**

Las que condicionan lo que venga después están en `CLAUDE.md`. Aquí, las que solo
importan para entender este código.

**El contador de clics es nuestro.** Windows cuenta hasta dos y manda
`WM_LBUTTONDBLCLK`; el triple clic que selecciona la línea entera no existe. Se cuenta en
`Input::Clicks` con dos umbrales, tiempo y distancia, y el de distancia importa tanto como
el otro: sin él, teclear deprisa y pinchar luego en otro sitio selecciona una palabra que
nadie pidió. El cuarto clic vuelve a uno, como en los navegadores, para poder recolocar el
cursor sin esperar medio segundo.

**El deshacer se agrupa por forma y no por reloj.** Agrupar por tiempo obligaría a pasarle
un `now()` al modelo y a que las pruebas mintieran sobre él. Se agrupa por lo que se
escribe: letras seguidas en el mismo sitio son un paso, y el grupo se cierra al escribir un
espacio, al pegar, al mover el cursor y al perder el foco. Escribir «Revisión año niño» y
deshacer una vez devuelve «niño», que es lo que la mano espera.

**El resto de la rueda se guarda.** Un panel táctil de precisión manda deltas de ocho
unidades, que con filas de 36 DIP son menos de una fila. Truncando cada uno por separado
salen todos cero y el desplazamiento suave no existe. `Ui::Wheel` acumula, y la prueba
comprueba que tres deltas de 40 mueven exactamente lo mismo que una muesca de 120.

**La aparición y la salida del cursor van por la GPU.** El cursor parpadea con una
animación en bucle con `IterationBehavior::Forever` y escalones, no con un `WM_TIMER`: el
bucle de mensajes se queda dormido en `GetMessageW` y así sigue. Con las animaciones del
sistema apagadas se queda encendido, que es lo que pide quien las apaga.

**Lo que no se pudo comprobar**

- **La nitidez a otras escalas**, igual que en la fase 1: esta máquina tiene una sola
  pantalla al 100 % y `WM_DPICHANGED` no llega a dispararse. El kit hereda el riesgo y lo
  agrava, porque ahora hay veinte veces más superficies.
- **El IME de verdad.** No hay ningún método de entrada de Asia oriental instalado aquí.
  Lo que sí está probado es el modelo: `Ui::Editor` mantiene la composición fuera del
  texto y fuera del historial, y hay casos para ello. Lo que falta por ver en pantalla es
  que la ventana de composición caiga donde se le dice.
- **El panel táctil de precisión.** La aritmética está probada con deltas pequeños; el
  panel, no, porque esta máquina no tiene.

---

### Fase 1 — Ventana Mac

La base del proyecto y una ventana que ya se siente como una aplicación de Mac. Sin datos.

**Lo que hay**

- **Compilación**: C++20 con MSVC, CRT estático, `/W4 /permissive- /utf-8 /EHsc
  /await:strict` por objetivo. Cuatro objetivos: `brujula_core` (lo puro), `sqlite3`,
  `brujula.exe` y `brujula_tests.exe`. nlohmann/json, SQLite y doctest por `FetchContent`,
  con la versión fijada y, en SQLite, el SHA-256 del archivo.
- **`preparar.ps1`**: se busca MSVC, CMake y Ninja por su cuenta. Hacía falta: en esta
  máquina ninguno de los tres está en el PATH.
- **Ventana**: Mica, esquinas redondeadas del sistema, barra de título integrada en el
  contenido, arrastrable, doble clic para maximizar, botones dibujados por nosotros con el
  comportamiento nativo y el menú de ajuste de Windows 11 al pasar sobre maximizar.
- **`compositor/`**: escena, dispositivo compartido D3D11 → D2D → Composition, superficies
  reutilizables, los cuatro muelles de `CLAUDE.md` y la transición compartida.
- **Tema** claro/oscuro siguiendo a Windows, con cruce de 250 ms, y respeto a «Mostrar
  animaciones en Windows».
- **Demo temporal**: barra lateral translúcida y una tarjeta que se convierte en panel y
  vuelve, interrumpible a mitad.

**Medido**

| | Objetivo | Medido |
|---|---|---|
| Warnings con `/W4 /permissive-` | 0 | 0 |
| Pruebas | pasan | 21 casos, 115 aserciones |
| Destellos al abrir | ninguno | 295 muestras en 3 s, luminancia máxima 30 |
| Destellos al redimensionar | ninguno | 60 redimensionados, máxima 32 en el borde recién descubierto |
| Auditoría de seguridad | sale 0 | sale 0, con 1 regla marcada pendiente |

- **La Mica es de DWM y está medida**, no dada por buena: el cuerpo da `(32,32,32)` con
  Windows en oscuro y `(241,244,244)` en claro. Ninguno de los dos es un color nuestro —el
  respaldo opaco es `#2c2c2e`— y cambian solos al cambiar el tema del sistema.
- **La barra lateral y la tarjeta salen donde dice la aritmética de los tokens**: velo
  sobre Mica `(26,26,26)`, tarjeta `rgba(44,44,46,0.72)` sobre Mica `(41,41,42)`. Los tres
  valores son distintos, que es lo que se quería comprobar.
- **Los cuatro muelles asientan en 85, 165, 239 y 297 ms** (4·T/(2π·ζ)). `Period` es el
  periodo no amortiguado, no la duración; los vecinos usan 40-50 ms, bastante más seco.
  Queda anotado para volver a afinarlo en la fase 8.
- **El modo sin animaciones está medido en las dos direcciones**: con animaciones, a los
  100 ms del clic el panel todavía va por el camino (el punto de destino da Mica); sin
  ellas, a los 100 ms ya está puesto. Mismo clic, misma espera.
- **La auditoría también está medida en las dos direcciones**: una sonda con 6 violaciones
  plantadas las detecta las 6; las **mismas palabras** dentro de comentarios no disparan
  ninguna regla.

**Lo que se probó y no valía**

- **`DwmExtendFrameIntoClientArea` no basta para que se vea la Mica.** Con el marco
  extendido un píxel, el cuerpo salía `(255,255,255)` en blanco puro. Con el marco
  extendido entero (`-1`), también: ese truco es de la época de Aero y necesita que el
  cliente se pinte de negro para que DWM lo tome por cristal. Lo que hacía falta era
  **`WS_EX_NOREDIRECTIONBITMAP`**: sin superficie de redirección no hay nada blanco que
  tape la Mica.
- **Y el marco extendido entero tenía un segundo problema, peor**: con `-1`, DWM considera
  que toda la ventana es marco y dibuja **encima sus propios botones** de ventana. Se veían
  los seis a la vez, desplazados cinco píxeles porque los suyos van en una franja de 32 y
  los nuestros en una de 48.
- **`UIColorType::Background` no sirve para saber el tema** en una aplicación de
  escritorio: devuelve negro siempre, con Windows en claro y en oscuro. El tema salía
  «oscuro» en los dos casos, y como la máquina estaba en oscuro parecía que funcionaba. Se
  deduce del **texto** (`UIColorType::Foreground`): texto claro significa fondo oscuro.
- **`CreateHostBackdropBrush` no se ha usado**, y no por pereza: la isla ya midió que en
  una aplicación Win32 sin empaquetar se crea sin error y se pinta negro. La barra lateral
  es un velo de color sobre la Mica.
- **Las formas de Composition no se rellenan con una superficie**, solo con color y
  degradados. Por eso el material de la tarjeta (un `ShapeVisual` con geometría de
  rectángulo redondeado) va separado de su contenido (dos `SpriteVisual` con textura). De
  paso sale mejor: las esquinas las redondea el rasterizador de formas, con suavizado, en
  vez de un recorte geométrico, que tiene el borde duro.
- **El auditor daba TODO LIMPIO con una dirección prohibida delante.** Su limpiador de
  comentarios se comía la barra doble de las URL y con ella el resto de la línea, así que
  las dos reglas que vigilan con quién se habla no podían saltar nunca. Lo encontró la
  sonda, no la lectura del código.
- **El doble clic se lo tragaba `DefWindowProc`.** La clase lleva `CS_DBLCLKS` para que el
  doble clic en la barra de título maximice, y eso hace que el segundo clic rápido en el
  contenido llegue como `WM_LBUTTONDBLCLK` y no como `WM_LBUTTONDOWN`.

**Lo que queda sin comprobar**

- **Nitidez a otras escalas y al mover la ventana entre monitores.** Esta máquina tiene una
  sola pantalla a 96 ppp (100 %), así que `WM_DPICHANGED` no llega a dispararse nunca. La
  aritmética de DIP a píxeles sí está probada (100 %, 125 %, 150 % y 175 %), pero eso
  prueba las cuentas, no la pantalla. Hay que mirarlo en un equipo con dos escalas.
- **Sombras suaves en elementos flotantes.** No se han intentado. La isla dejó documentados
  dos caminos que no funcionaron y uno sin probar (`DropShadow` con `Mask`); cuando toque,
  se empieza por ahí.
- **Barra de tareas oculta automáticamente** con la ventana maximizada: el marco propio no
  la compensa todavía, así que no se puede sacar acercando el ratón al borde. Es un clásico
  de las barras de título propias y va a la fase 8.
