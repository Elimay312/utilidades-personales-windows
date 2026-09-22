# Cambios

El proyecto va por fases, no por versiones: cada una tiene que compilar sin warnings y
pasar sus pruebas antes de empezar la siguiente.

Los números que aparecen aquí están medidos, no estimados. Cuando algo no se pudo medir,
lo dice.

---

## Sin publicar

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

**Medido**

- Los 268 tests pasan, dos nuevos sobre `App::ReviewQueue`.
- Compila sin warnings con `/W4 /permissive-`.
- `auditar.ps1`: las once reglas limpias.
- Probado contra la cuenta real de 109 repositorios: la pila entera, el límite de Enfoque
  con su hoja y su relevo, editar el siguiente paso, saltar, el resumen y la vuelta a la
  lista. La base de datos se respaldó antes y se restauró después.

**Lo que no se ha podido comprobar**, cinco cosas, cuatro heredadas y una nueva: la nitidez
a otras escalas —`WM_DPICHANGED` sigue sin dispararse: esta máquina tiene una sola pantalla
al 100 %, comprobado con `GetDpiForWindow` desde un proceso DPI-aware—, el IME de verdad, el
panel táctil de precisión, los tres cuadros de archivo, y **el globo del recordatorio
pulsado con el ratón**: el aviso se vio salir y el camino entero se recorrió mandando su
mensaje a la cola de la ventana, pero esta máquina tiene otra aplicación reteniendo el
primer plano y no se pudo hacer clic en la notificación de verdad.

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
