# CLAUDE.md — Brújula (priorizador de repos de GitHub para Windows)

> "Brújula" es un nombre de trabajo. Cámbialo aquí y en `CMakeLists.txt` si prefieres otro.

## Visión

Una app nativa de Windows que conecta con la cuenta de GitHub del autor (unos 120 repos, en su mayoría privados y de trabajo interno) y le ayuda a decidir **en qué trabajar**. Clasifica los repos por actividad automáticamente, permite asignar prioridades a mano y guarda por cada proyecto su estado, sus novedades y, sobre todo, **el siguiente paso concreto**, para que retomar un proyecto dormido no cueste reconstruir dónde se quedó.

La estética es de **app de Mac**: materiales translúcidos, tipografía cuidada, mucho aire, animaciones con física de muelle y transiciones que conectan los estados. Debe sentirse como una app de Apple que resulta que corre en Windows.

Prioridades, en orden:
1. **Seguridad de los datos del trabajo:** el token nunca en texto plano, nada sale del equipo salvo hacia la API de GitHub.
2. **Sensación de calidad:** animaciones fluidas e interrumpibles, cero parpadeos, texto nítido.
3. **Rapidez:** la ventana muestra datos de la caché al instante; la sincronización ocurre en segundo plano.

## Stack

- C++20, MSVC, CMake + Ninja, x64. Windows 11 como objetivo (degradar con elegancia en Windows 10).
- **Ventana:** Win32 puro con Mica como fondo (`DWMWA_SYSTEMBACKDROP_TYPE`), barra de título integrada en el contenido (extender el marco con `DwmExtendFrameIntoClientArea` y gestionar `WM_NCHITTEST`) y esquinas redondeadas del sistema.
- **Animación y composición:** `Windows.UI.Composition` desde Win32 con C++/WinRT (`DesktopWindowTarget`), para animaciones con muelle en la GPU, materiales y sombras.
- **Dibujo:** Direct2D + DirectWrite sobre superficies de composición (`CompositionGraphicsDevice` / `ICompositionDrawingSurfaceInterop`).
- **Red:** WinHTTP (viene con Windows, sin dependencias).
- **JSON:** nlohmann/json por FetchContent.
- **Caché local:** SQLite (amalgamación por FetchContent).
- **Tests:** doctest por FetchContent.
- Sin Qt, sin Electron, sin .NET. Dependencias nuevas: consultar antes.

## Comandos

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\brujula.exe
build\brujula_tests.exe
```

Después de cada cambio: compilar sin warnings y pasar los tests.

## Arquitectura

```
src/
  main.cpp            wWinMain mínimo
  app/                Estado de la app, comandos, navegación entre vistas
  shell/              Ventana, Mica, barra de título propia, DPI, tema claro/oscuro
  compositor/         Árbol de visuales, animaciones, muelles, transiciones compartidas
  ui/                 Kit de componentes propio (texto, botones, listas, campos, paneles)
  views/              Barra lateral, lista de repos, inspector, revisión semanal, paleta de comandos
  github/             Autenticación, cliente GraphQL/REST, sincronización
  store/              SQLite: repos, prioridades, notas, historial
  model/              Tipos de dominio y reglas (clasificación, límite de Enfoque)
  projectfile/        Lectura y escritura de PROYECTO.md
tests/
```

### Reglas de arquitectura

1. **La UI nunca espera a la red.** Al abrir, se pinta todo desde SQLite. La sincronización corre en un hilo de trabajo y publica cambios al hilo de UI con `PostMessageW`; la UI anima los cambios (repos que cambian de grupo se deslizan a su nuevo sitio).
2. **Modelo separado de la vista.** Las reglas (clasificación por actividad, límite de Enfoque) viven en `model/` como funciones puras con tests. La UI solo lee estado y emite comandos.
3. **Toda animación pasa por `compositor/`.** Nada de interpolar a mano en el bucle de mensajes. Las animaciones deben poder interrumpirse: si el usuario actúa a mitad de una, la nueva parte desde el estado actual, sin saltos.
4. **Errores como valores**, mostrados como avisos discretos dentro de la app, nunca como `MessageBox`.

## GitHub: autenticación y datos

- **Token:** orden de preferencia:
  1. Si GitHub CLI está instalado y autenticado, obtenerlo con `gh auth token`.
  2. Si no, pedir un fine-grained personal access token con *Metadata: read* **y *Contents: read***, que es lo que hace falta para leer `PROYECTO.md`; con *Metadata* a secas ese campo vuelve vacío. *Contents: **write*** sigue siendo solo del modo repo (ver abajo). Si la credencial no llega a *Contents: read*, la sincronización lo detecta y repite la tanda sin ese campo: se sincroniza sin `PROYECTO.md` en vez de no sincronizarse.
- **El token se guarda en el Administrador de credenciales de Windows** (`CredWriteW` / `CredReadW`), nunca en archivos, logs ni SQLite.
- **Sincronización con GraphQL, en dos pases.** Pedir todos los campos de golpe, 100 por página, tarda 8,3-9,1 s por página y devuelve algún 502: medido en la fase 3, y son diecisiete segundos para los ~120. Así que:
  1. **Metadatos**, paginado por cursor, 100 por página: nombre, dueño, descripción, privado/archivado, lenguaje principal, `pushedAt`, `updatedAt`, `createdAt`, estrellas y tamaño. ~1,3 s los 109. Con esto ya se puede pintar.
  2. **Detalle**, por `nodes(ids:)` y solo de los repos cuyo `pushedAt` cambió: fecha y mensaje del último commit de la rama principal, número de issues y PRs abiertos, y el contenido de `PROYECTO.md` si existe (`object(expression: "HEAD:PROYECTO.md")`). Sin cursor, así que va en paralelo: seis peticiones de veinte, ~2,2 s.
- Incluir repos personales y de organizaciones a las que el usuario pertenece; qué organizaciones incluir es configurable.
- Sincronización incremental: solo actualizar repos cuyo `pushedAt` cambió. Respetar los límites de la API y mostrar cuándo fue la última sincronización.

## Modos de guardado de notas

Como los repos son de trabajo, **escribir en ellos debe ser una decisión explícita**:

- **Modo local (por defecto):** prioridad, estado, siguiente paso y novedades se guardan solo en SQLite en `%LOCALAPPDATA%\Brujula\`. No se crea ningún commit.
- **Modo repo (opcional, por repo o global):** además se escribe `PROYECTO.md` en la raíz del repo mediante la API de contenidos (commit con mensaje `chore: actualizar PROYECTO.md`). Antes del primer commit en un repo, pedir confirmación.
- Exportar e importar todo a un archivo, para copias de seguridad.

### Formato de PROYECTO.md

```markdown
---
prioridad: enfoque        # enfoque | secundario | algun-dia | archivado
estado: activo            # activo | bloqueado | en-espera | terminado
siguiente_paso: Conectar el lector de carpetas a la columna central
actualizado: 2026-09-21
---

## Novedades

- 2026-09-20 — Terminada la fase 2, falta probar con rutas largas.
```

Parser tolerante: si el archivo no tiene frontmatter o tiene campos desconocidos, no se rompe nada y se conserva el contenido que no entiende.

## Reglas de dominio

- **Actividad automática** (umbrales configurables): *activo* si hubo push en los últimos 14 días; *en pausa* entre 14 y 90 días; *dormido* más de 90 días.
- **Prioridad manual:** Enfoque, Secundario, Algún día, Archivado, o Sin clasificar (repos nuevos).
- **Límite de Enfoque:** máximo 5 (configurable). Al intentar añadir uno más, la app pide elegir cuál baja a Secundario; no se puede saltar.
- **Alertas de desajuste:** un repo en Enfoque dormido más de 14 días, o un repo Archivado con pushes recientes, aparece en la vista "Necesita decisión".
- **Aplazar una pregunta** (30 días, configurable): en la revisión semanal, la P aparta un repo hasta que venza el plazo. Silencia **esa** pregunta y no el repo: si mientras tanto le aparece un desajuste distinto, se vuelve a preguntar. Se compara por día, no por segundo — vence al empezar el día, no a la hora a la que se pidió. Sigue contando en "Necesita decisión", que dice lo que pasa, y sale además en "Pospuestos", que dice lo que decidiste no mirar todavía.

## Lenguaje visual (estilo Mac)

### Principios

- **Contenido primero:** chrome mínimo, sin bordes innecesarios, jerarquía con tipografía y espacio, no con cajas.
- **Materiales:** Mica en la ventana; barra lateral e inspector con un material más translúcido. Sombras suaves y difusas solo en elementos flotantes.
- **Movimiento con significado:** cada animación explica de dónde viene y a dónde va algo. Nada gira ni rebota por decoración.

### Tokens

| Token | Claro | Oscuro |
|---|---|---|
| Texto principal | `#1d1d1f` | `#f5f5f7` |
| Texto secundario | `#6e6e73` | `#a1a1a6` |
| Separador | `rgba(0,0,0,0.08)` | `rgba(255,255,255,0.08)` |
| Superficie tarjeta | `rgba(255,255,255,0.72)` | `rgba(44,44,46,0.72)` |
| Acento | color de acento del sistema (`UISettings::GetColorValue(UIColorType::Accent)`), con `#0a84ff` como respaldo | |
| Enfoque | `#ff9f0a` | `#ff9f0a` |
| Secundario | `#0a84ff` | `#0a84ff` |
| Algún día | `#bf5af2` | `#bf5af2` |
| Archivado | `#8e8e93` | `#8e8e93` |
| Activo / en pausa / dormido | `#30d158` / `#ffd60a` / `#8e8e93` | igual |

- **Tipografía:** Segoe UI Variable (Display para títulos, Text para cuerpo). Escala: 26 / 20 / 15 / 13 / 11 px lógicos. Pesos: regular, semibold. Números tabulares en fechas y contadores.
- **Espaciado:** rejilla de 4 px; espacios habituales 8, 12, 16, 24, 32.
- **Radios:** 6 (controles pequeños), 10 (tarjetas), 14 (paneles), 20 (hojas modales).
- **Iconos:** Segoe Fluent Icons, trazo fino, 16 px.
- El tema sigue al de Windows (claro/oscuro) y cambia en caliente con transición cruzada de 250 ms.

### Movimiento

| Uso | Muelle | Rebote | Asienta en |
|---|---|---|---|
| Interacciones pequeñas (hover, pulsar, marcar) | rígido: amortiguación 0,9, periodo 130 ms | 0,10 | 92 ms |
| Paneles e inspector | estándar: amortiguación 0,85, periodo 60 ms | 0,15 | 45 ms |
| Reordenar y mover tarjetas entre grupos | suave: amortiguación 0,8, periodo 250 ms | 0,20 | 199 ms |
| Hojas modales y revisión semanal | expresivo: amortiguación 0,75, periodo 340 ms | 0,25 | 289 ms |

**El periodo ES la duración perceptual, y es el único mando con el que se afina.**
Composition define `Period` como el tiempo que tarda el muelle en completar una oscilación,
y Apple define `Spring(duration:bounce:)` con rigidez `(2π/duration)²` y masa 1: las dos
cosas fijan la misma ωn, así que `periodMs` es su `duration` y `dampingRatio` es su
`1 - bounce`. «Asienta en» es la *settling duration*, que Apple dice expresamente que no se
use para afinar porque depende de demasiadas cosas — las fases 1 y 6 la usaron, y por eso
hubo que volver. Se cambian en `compositor/MotionSpec.h`, que es el único sitio donde están
escritos, y las pruebas fijan los cuatro.

**El cruce de color de un estado no sale de ningún muelle:** son 120 ms fijos
(`Motion::kInkMs`), para el hover, el pulsado, el anillo de foco y la píldora de prioridad.
Salía de `SettleMs × 0,6` del muelle rígido, y con la tabla de la fase 6 eso eran 34 ms —dos
fotogramas— o sea un corte. Alargarlo no cuesta nada de lo que aquí importa: un fundido de
color no retrasa ningún clic.

**En Debug, F10 multiplica por cinco todo lo que dura algo** —periodos, fundidos, retardos y
los temporizadores que esperan a que un muelle acabe— para poder mirar una transición
despacio. No se compila en Release, y por eso no está en la tabla de atajos.

- Pulsar un control lo escala a 0,97; soltarlo vuelve con muelle rígido.
- **Transiciones compartidas:** al abrir un repo, su tarjeta se transforma en el inspector (posición, tamaño y radio a la vez, con fundido cruzado del contenido). Al cerrar, vuelve a su sitio.
- Listas: los elementos que entran aparecen con fundido + desplazamiento de 8 px escalonado **45 ms** (techo 180); los que cambian de posición se deslizan, nunca saltan. Por debajo de unos 40 ms el escalonado no se distingue de que entren todas a la vez: cuesta el retardo y no compra el ritmo.
- **Cambiar de lista a cuadrícula no desliza las celdas: funde la columna entera.** Lo que cambia no es dónde está cada tarjeta, es la forma de la rejilla, y deslizar con la celda ya a un tercio de ancho hace que unas pasen por encima de otras.
- **El desplazamiento de la rueda va a un DESTINO, no a una velocidad.** Cada muesca suma su distancia —la que dice `SPI_GETWHEELSCROLLLINES`— sobre el destino y se llega en **200 ms de duración fija**. Sin inercia, a propósito: una rueda no tiene velocidad que medir, tiene muescas. Y sin muelle, que es la única excepción a la regla de arriba: un muelle dentro de un `InteractionTracker` no termina cuando su periodo dice —medido, una muesca dejaba la lista moviéndose un segundo y medio— y aquí lo que hace falta es que termine.
- **Respetar "Mostrar animaciones en Windows"** (`SPI_GETCLIENTAREAANIMATION`): si está desactivado, sustituir movimientos por fundidos cortos.
- Objetivo: 60 fps constantes (y la frecuencia del monitor si es mayor) incluso con los 120 repos visibles.

## Vistas

- **Barra lateral:** grupos de prioridad con contador, y vistas inteligentes: Necesita decisión, Dormidos, Actividad esta semana, Pospuestos, Sin clasificar.
- **Lista principal:** tarjetas con nombre, siguiente paso (lo más visible tras el nombre), indicador de actividad, prioridad, lenguaje y "hace X días". Alternar entre lista compacta y cuadrícula.
- **Inspector (panel derecho):** prioridad, estado, siguiente paso editable, novedades con fecha, últimos commits, issues/PRs abiertos, botones para abrir en GitHub y en la carpeta local.
- **Revisión semanal:** modo a pantalla completa que presenta una tarjeta por repo que necesita decisión; con 1-4 asignas prioridad, E editas el siguiente paso, espacio salta. Cada decisión anima la tarjeta hacia su grupo.
- **Paleta de comandos (Ctrl+K):** buscar repos y ejecutar acciones escribiendo.

## Cómo se abre desde fuera

- `brujula.exe --repo NOMBRE` y `brujula://repo/NOMBRE` abren ese repositorio directamente
  en el inspector. El nombre es `nombre` o `dueño/nombre`, sin distinguir mayúsculas.
- **Solo hay una Brújula a la vez.** Si ya está abierta, la segunda le pasa el nombre por
  `WM_COPYDATA`, la trae al frente y se muere. Dos procesos serían dos conexiones de
  escritura a la misma caché.
- El esquema se registra en `HKCU\Software\Classes\brujula` al arrancar, apuntando al
  ejecutable que se está ejecutando, y se reescribe si la ruta cambió.
- **Lo que llega por ahí es texto de un desconocido** —un esquema propio lo dispara
  cualquier página web— así que pasa por `App::LooksLikeRepoName`, que es una lista blanca
  con pruebas. Solo se usa para buscar en la caché: no abre archivos, no compone rutas y no
  llega a ninguna petición.

## Atajos

| Atajo | Acción |
|---|---|
| Ctrl+K | Paleta de comandos |
| Ctrl+F o / | Buscar |
| ↑ / ↓ o j / k | Mover selección |
| Enter | Abrir inspector |
| Esc | Cerrar inspector o modal |
| 1 / 2 / 3 / 4 | Enfoque / Secundario / Algún día / Archivado |
| E | Editar siguiente paso |
| N | Añadir novedad (con el inspector abierto) |
| Ctrl+R | Sincronizar ahora |
| Ctrl+Shift+R | Empezar revisión semanal |
| P (en la revisión) | Aplazar esa pregunta 30 días |
| Ctrl+O | Abrir el repo en GitHub |
| Ctrl+Z | Deshacer el último cambio (prioridad, orden, ediciones y novedades) |
| Ctrl+G | Alternar entre lista y cuadrícula |
| Botón derecho | Menú de la tarjeta, con todas sus acciones |

## Convenciones de código

- `UNICODE`, `_UNICODE`, `WIN32_LEAN_AND_MEAN`, `NOMINMAX`. Manifiesto con DPI Per-Monitor V2.
- Flags `/W4 /permissive- /utf-8 /EHsc /await:strict` (C++/WinRT). CRT estático si C++/WinRT lo permite; si no, documentar la decisión.
- RAII para handles, `winrt::com_ptr` / `winrt::` para COM y WinRT.
- Cadenas internas en `std::wstring`; UTF-8 solo en el borde con JSON y SQLite.
- Nunca registrar el token ni respuestas completas de la API en el log.

## Fases

El plan completo está en `PROMPTS.md`.

- [x] Fase 1 — Ventana Mac: Mica, barra de título propia, composición y muelles
- [x] Fase 2 — Kit de UI propio y catálogo de componentes
- [x] Fase 3 — GitHub: token seguro, GraphQL, SQLite y sincronización
- [x] Fase 4 — Vista principal: barra lateral, lista y clasificación
- [x] Fase 5 — Inspector, notas y PROYECTO.md
- [x] Fase 6 — Priorizar: arrastrar, límite de Enfoque, atajos y paleta
- [x] Fase 7 — Revisión semanal
- [x] Fase 8 — Pulido final y rendimiento

Al terminar una fase: marcarla aquí, anotar decisiones abajo y hacer commit.

## Decisiones y notas

### Después de la fase 8 — 22 de septiembre de 2026

Usando la revisión semanal con la aplicación delante, y las tres cosas que salieron de ahí
son las que la fase 8 dejó escritas como pendientes de sentir.

**El «toque borroso» era el medio píxel, y llevaba desde la fase 1.** El marco de un
elemento se escribe en DIP, y casi todos salen de centrar algo —una división por dos— o de
una escala que no es 1: a 125 % un renglón de 142 DIP cae en el píxel 177,5. Un visual con
superficie propia colocado en medio píxel se dibuja REMUESTREADO, con todo su texto dentro.
Ahora `Ui::Element::SetFrame` redondea el `Offset` a píxel entero, y va ahí y no en cada
vista porque el desplazamiento de un hijo es relativo al padre: con los dos redondeados, la
suma también lo está, y por ese método basta con hacerlo en el único sitio por el que pasan
todos. **El marco guardado no se toca**: la maquetación y el hit-test siguen hablando en DIP
exactos y medio píxel no cambia dónde cae un clic. Un piso más abajo hace falta lo mismo a
mano cuando se reparte un ancho DENTRO de una textura —`ChipRect` divide el pie entre cuatro
y dejaba tres de los cuatro bordes en fracciones—, porque ahí ya no hay marco que redondear.
Lo que quedaba de la hipótesis del suavizado en gris se mira DESPUÉS de esto: la mitad de lo
que se estaba juzgando era remuestreo.

**En la revisión semanal no se mueve nada, y es la misma lección otra vez.** Una animación
colgando de un visual rasteriza su texto filtrado aunque haya acabado en su valor exacto
—la fase 7 lo vio con la escala, la 8 con `Recede`— y la revisión es la pantalla con la
letra de 26 DIP. Se quitaron las tres: la tarjeta ya no entra deslizándose ni sale volando
hacia su diana, el «no» del límite de Enfoque ya no tiembla, y el resumen ya no aparece con
`Ui::Panel::Appear`, que deja una escala colgada del panel que lleva el texto dentro. Se
quedan animadas la barra de progreso y las barras del resumen, que son material y no llevan
una letra. **Y la tarjeta pasa a ser UNA**: las dos caras alternándose existían solo para
que la que salía volando siguiera viéndose mientras entraba la siguiente.

**El estándar bajó de 200 a 60, en dos pasadas y mirando.** Es el experimento que la fase 8
dejó escrito y sin hacer, y confirma su propia regla: `SettleMs` no lo habría encontrado.
Queda por debajo del rígido y rompe la escala creciente de la tabla, y eso es información:
el inspector aparece donde ya se sabía que iba a aparecer y no hay nada que seguir con la
vista, mientras que lo que sí se sigue —una tarjeta que cambia de grupo, una hoja que cruza
la pantalla— sigue arriba. **Y arrastró un fundido que no era suyo**: `Resolve()` calcula el
fundido como `SettleMs × 0,6`, así que el de la columna entera al cambiar de vista se quedó
en 27 ms —dos fotogramas, o sea el fallo que la fase 8 acababa de arreglar—. Colgaba del
estándar por ser el del medio y no porque acompañara a ese muelle; los tres sitios de la
lista cuelgan ahora del suave. La regla que queda: **al tocar un periodo hay que mirar quién
usa su `FadeMs`**, porque un fundido que no acompaña a ese muelle no tiene por qué seguirlo.

### Fase 8 — 22 de septiembre de 2026

El detalle con todas las mediciones está en `CHANGELOG.md`. Aquí van solo las decisiones
que condicionan lo que venga después.

**`periodMs` y `dampingRatio` SON los dos parámetros de Apple, y las fases 1 y 6 afinaron el
número equivocado.** Composition define `Period` como el tiempo de una oscilación y Apple
define `Spring(duration:bounce:)` con rigidez `(2π/duration)²` y masa 1: las dos fijan la
misma ωn, así que el periodo es la *duración perceptual* y la amortiguación es `1 - bounce`.
`SettleMs` es la *settling duration*, de la que Apple dice expresamente que no se afina
porque «depends on many different factors and can be unpredictable». Afinando esa, la fase 6
dejó los cuatro muelles entre 80 y 195 ms de duración perceptual cuando los tres presets de
iOS están los tres en 500 — y lo que se siente con eso no es rapidez, es un corte. La regla,
para todo lo que venga: **el mando es el periodo**, y «asienta en» es una consecuencia que
solo sirve para saber cuándo se puede tirar un elemento.

**Y lo que más se notaba no era el muelle, era el fundido de la tinta.** `Resolve()` calcula
el fundido como `SettleMs × 0,6`, y eso está bien para el fundido que ACOMPAÑA a un muelle
—el contenido tiene que leerse mientras la forma se asienta— pero el cruce de color de un
hover no acompaña a nada. Con la tabla de la fase 6 eran 34 ms: dos fotogramas. Ahora hay
`Motion::kInkMs` aparte. Lo que hace que esto sea gratis y no un compromiso: **un fundido de
color no retrasa ningún clic**, así que alargarlo no toca el criterio de quien usa esto, que
es «¿estorba para trabajar rápido?».

**La rueda manda destino y no velocidad, y eso deshace una decisión de la fase 2.** Aquella
mandaba un impulso al `InteractionTracker` para que DWM calculara la inercia. Es cierto que
da inercia de verdad y es cierto que dos muescas suman, y las dos cosas están mal para una
rueda: **una rueda no tiene velocidad que medir, tiene muescas**, y cada una es una
distancia que Windows ya define. Lo reportado al usarla fue exacto — «se desliza sobre el
hielo»— y el repositorio que se buscaba quedaba atrás. `Scroller::By` suma sobre `m_target`
y no sobre la posición: partiendo de dónde está, cinco muescas seguidas pierden lo que a las
anteriores les queda por recorrer y se quedan en tres. Un panel táctil de precisión sí
tendría velocidad que pasar, y entonces esto sería otra rama; hoy no llega ese evento, así
que esa rama no tendría a nadie dentro.

**Cambiar la FORMA de la rejilla se funde, no se desliza — y eso corrige la fase 4.**
Aquella dijo «el tamaño de una celda no se anima, y la posición sí», con una razón buena
—animar el tamaño reasigna la textura de cada celda en cada fotograma— y una consecuencia
que no se vio hasta usarlo: entre lista y cuadrícula la celda pasa a un TERCIO de ancho, así
que con el ancho ya puesto y la posición viajando, las que van a la segunda y la tercera
columna cruzan POR ENCIMA de las de la primera, y como las tarjetas son translúcidas se leen
tres textos superpuestos. Medido: **1,4 segundos** de tarjetas cruzándose, con el estado
final correcto y el camino pareciendo que la vista se ha roto. Ahora se recoloca de golpe y
lo que se anima es la columna apareciendo: **una sola animación de opacidad en vez de una
por celda**, y queda igual al estado final antes de los 200 ms. La regla que queda: cuando
lo que cambia es la FORMA de un contenedor y no el sitio de sus hijos, se funde.

**La rueda va con duración fija, y es la única excepción a «muelles para todo lo
interrumpible».** No es una preferencia: es que un muelle dentro de un `InteractionTracker`
no termina cuando su periodo dice. Medido contando cada cuántos milisegundos cambian los
píxeles de la columna, una sola muesca dejaba la lista moviéndose **1500 ms** con un muelle
de periodo 180 que debería asentar en 115; bajando el periodo a 60 se quedaba en 1000, así
que ahí dentro no corre solo lo que se le pasa. Con 200 ms escritos, acaba en 200 — y una
ráfaga de cinco muescas, 190 ms después de la última. Lo que se pierde es retomar la
velocidad al reapuntar, y a esa escala no se nota: 200 ms son menos que el hueco entre dos
muescas de una mano.

**Escribir la identidad no basta: hay que PARAR la animación.** `Views::Main::Recede`
devolvía la lista de 0,94 a 1,0 con un muelle, y eso dejaba una animación de escala colgando
del visual — lo que la fase 7 ya midió que basta para que el texto se rasterice filtrado. Era
además una animación que **no mira nadie**, porque lo que vuelve está en opacidad cero hasta
que el fundido lo trae. De ahí `Ui::Element::ResetScale`, que para la escala Y el
`CenterPoint` —`ScaleTo` lo ata con una expresión, que es otra animación— y escribe la
identidad. Es la regla de la fase 6 («parar antes de escribir») aplicada a una propiedad
que se creía a salvo por acabar en 1,0 exacto.

**El modo lento de depuración es global y mutable, y es lo correcto aquí.**
`Motion::TimeScale()` lo miran `Motion::Animator`, `Gfx::Scroller` y **los dos temporizadores
que esperan a que un muelle acabe** para esconder lo que viajó. Ese último es el que importa:
sin él, el modo lento haría desaparecer el inspector a mitad de su viaje, que es exactamente
el fotograma que se estaba yendo a mirar. Pasarlo por parámetro obligaría a llevarlo encima
a dos vistas y al scroller sin ningún otro motivo para conocerse.

**El arranque no puede bajar de 300 ms en frío, y eso está medido, no supuesto.**
`D3D11CreateDevice` tarda **172 ms** en esta máquina y es casi todo el arranque. Con un banco
aparte: WARP tarda 20, precargar `d3d11.dll` y `dxgi.dll` no cambia nada (162) y darle el
adaptador ya elegido tampoco (162). O sea que **no es el cargador ni la enumeración de
adaptadores: es el controlador de la tarjeta inicializándose**, y no se le puede pedir que
tarde menos. Lo que sí se hizo: `Gfx::Device::BeginCreate()` lo lanza en un hilo en la
primera línea de `Init` y el resto de la función le quita al camino crítico la ventana, la
escena, el tema y **abrir SQLite y leer la caché entera**, que estaban todos detrás del
dispositivo sin necesitarlo. De 269 ms de media a 246, con la espera al dispositivo bajando
de 172 a 120-146. La única manera de bajar más es que el primer fotograma no necesite
Direct2D — o sea, enseñar la ventana antes de tener texto, que es dejar de cumplir «primer
fotograma con datos». Queda escrito para quien lo quiera decidir.

**`ms_arranque` y `ms_arranque_gpu` van a la tabla de ajustes**, como la fase 3 hizo con los
pases de la sincronización y la 5 con sus milisegundos. Van los dos juntos porque solo
juntos dicen algo: un arranque que sube sin que suba el segundo es culpa nuestra, y uno que
sube con él es el controlador. La hoja de «Acerca de» los enseña, que es donde se miran sin
abrir SQLite.

**Lo que llega por `brujula://` es texto de un desconocido.** Un esquema propio lo dispara
cualquier página web que alguien abra sin mirar; el navegador pregunta y hay gente que dice
que sí. Por eso el parser es puro, vive en `brujula_core` y tiene pruebas con
`../../Windows/System32` dentro, y por eso es **lista blanca y no lista negra**: una lista
negra hay que acertarla entera y una blanca solo hay que acertarla una vez. Se vuelve a
validar al recibir el `WM_COPYDATA`, porque por ahí entra cualquiera que sepa el nombre de
nuestra clase de ventana — es la misma regla que la fase 5 le puso a la ruta de la API.

**Una sola instancia, y el traspaso por `WM_COPYDATA`.** Dos procesos serían dos conexiones
de escritura a la misma caché y dos hilos sincronizando la misma cuenta. `WM_COPYDATA` y no
un mensaje propio con un puntero dentro: ese puntero sería de otro proceso y aquí no apunta
a nada — es el mismo motivo por el que `Github::Sync` publica un aviso vacío desde la fase 3,
visto desde el otro lado.

**La versión vive en `src/app/Version.h` y la incluye el `.rc`.** rc.exe sabe incluir
cabeceras mientras no tengan C++ dentro, así que el número se escribe una vez. Con dos
copias no falla nada el día que se suba una y no la otra: simplemente las propiedades del
archivo dicen una cosa y la aplicación otra, y nadie se entera hasta que hace falta saber qué
versión tiene alguien delante.

**La auditoría del modo lento encontró dos fallos, y los dos eran del mismo tipo: algo que
aparece o desaparece sin transición.** Se miraron nueve transiciones contando los píxeles
que cambian entre fotogramas consecutivos, que es lo que dibuja la forma del movimiento: un
cero en medio con números a los lados es un hueco, y un pico después de una racha de ceros
es algo que apareció de golpe.

- **Al cerrar el inspector, el panel desaparecía de golpe.** La fase 5 lo dejó solo
  encogiendo hasta la tarjeta, con la idea de que al final ES la tarjeta y esconderlo no se
  notaría. No es verdad: su material es el velo del panel, no el de una tarjeta, así que
  cuando el temporizador lo escondía había un pico del tamaño del panel entero justo en ese
  fotograma. Ahora se apaga mientras encoge, y lo que queda debajo es la tarjeta de verdad.
- **Al cambiar de vista, la columna se quedaba casi vacía mientras se rellenaba.** El
  escalonado de entrada es para unas pocas filas que llegan; con la lista entera cambiando,
  a los 130 ms había CUATRO tarjetas de diez y no estaba llena hasta pasados casi
  quinientos. Ahora `Ui::List::Update` distingue los dos casos con un criterio que significa
  algo —**se fueron todas las que había y no se quedó ninguna**, o sea que nada se ha
  movido— y entonces funde la columna entera como entre lista y cuadrícula. Las dos mitades
  del criterio hacen falta: sin «se fueron todas», una lista que todavía no tiene filas
  vivas contaría como cambio de pantalla y se fundiría por encima de su propia entrada.

**Lo que queda medido y sin decidir: el morfeo de apertura del inspector.** Es la única
transición que sigue siendo larga —2,1 s de cambio medible, con el panel visiblemente a
medias todavía a los 530 ms— y es también la que enseña el problema de `SettleMs` en
crudo. Cerrarlo tarda 240 ms porque el fundido tapa la cola del muelle; abrirlo no la tapa,
así que se ve entera. Bajar `kStandard` a la mitad lo deja en 1,3 s, así que el mando
funciona; cuál es el número bueno es algo que hay que sentir con la aplicación delante, y
por eso no se ha tocado a ciegas por tercera vez.

**Falta comprobar tres cosas**, las tres heredadas y ninguna nueva: la nitidez a otras
escalas —`WM_DPICHANGED` sigue sin poder dispararse: una sola pantalla al 100 %—, el IME de
verdad y el panel táctil de precisión. **Los cuadros de archivo salen de la lista**: el de
guardar la copia de seguridad se abrió y se pintó entero durante esta fase, por accidente
—un clic que se fue de renglón en el menú de ajustes—, que es como acabaron
comprobándose después de tres fases esperando una máquina que dejara enfocar la ventana. Y el texto «un toque
borroso» de la fase 7 sigue pendiente de decidir: la hipótesis viva sigue siendo el suavizado
en gris, y lo que hay que probar son unos `IDWriteRenderingParams` propios — pero esta fase
encontró y quitó una causa real de borrosidad que no estaba en aquella lista (la escala
residual de `Recede`), así que conviene volver a mirarlo antes de tocar el suavizado.

### Fase 7 — 21 de septiembre de 2026

El detalle con todas las mediciones está en `CHANGELOG.md`. Aquí van solo las decisiones
que condicionan lo que venga después.

**La revisión NO es una capa flotante del Host.** Es un hijo de `Views::Main` que ocupa la
ventana entera, por lo mismo que `Views::DragCard`: las capas se cierran todas juntas
cuando la ventana pierde el foco —`onDeactivate` llama a `PopAllLayers`— y una revisión a
medias que desaparece por mirar el navegador un momento perdería por dónde iba. Va por
encima de la lista y del inspector y por DEBAJO de `Views::Chrome`, que es lo único que no
puede taparse: sin la barra de título no hay por dónde cerrar la ventana.

**Quién entra en la pila es una función pura con prueba: `App::ReviewQueue`.** Primero los
desajustes de «Necesita decisión» y después los «Sin clasificar». Los tres casos que pide
este documento son DOS listas y no tres, porque «Enfoque sin actividad» ya ES uno de los
desajustes (`Model::Mismatch::FocusDormant`) y pedirlo aparte enseñaría la misma tarjeta dos
veces. Devuelve identificadores y no posiciones, como el inspector de la fase 5 y por el
mismo motivo: el estado se reconstruye entero después de cada guardado.

**La revisión es un MODO y se queda con el teclado entero — menos mientras se escribe.**
`Views::Main::OnKey` le pasa todo mientras corre, y ella consume todo salvo las teclas con
Alt (Alt+F4 y Alt+Espacio son de Windows). Pero con el campo del siguiente paso abierto,
todo lo que el campo no quiera se devuelve **SIN consumir**: `Shell::Window` se come el
`WM_CHAR` de cualquier tecla consumida —lo que desde la fase 5 evita que un atajo de una
letra se escriba dentro del campo que acaba de abrir— así que consumirlas aquí es un campo
de texto en el que no se puede escribir. Pasó exactamente eso.

**El campo de texto vive FUERA de la tarjeta.** La tarjeta sale volando; un campo dentro se
iría volando con lo escrito a medias. Se coloca justo encima del renglón del siguiente paso
—las dos posiciones salen de `CardView::StepRect`, que es una sola función— y mientras está
puesto la tarjeta NO pinta ese renglón: el campo es un pozo translúcido y lo de debajo se
leía a través.

**Nada de la tarjeta se anima con escala, y no es una simplificación.** La escala de salida
hacia la diana dejaba la cara encogida al reutilizarla dos decisiones después: escribir una
propiedad que tuvo una animación encima no la deja escrita —la lección de la fase 6, en su
tercera visita—. Se quitó entera en vez de pelearla: lo que una tarjeta que sale tiene que
decir es hacia dónde va, y eso lo dice el viaje.

**`App::ApplyPriority` ahora devuelve si pudo.** Es el único llamador que lo mira: la
revisión necesita saber si la tarjeta sale volando o se queda temblando. Los otros cuatro
caminos lo ven en la pantalla y no preguntan. Cuando la hoja del límite de Enfoque acaba
eligiendo, es ella quien avisa a la revisión (`Review::Accepted`) — y por identificador, no
«la de delante», porque entre la pregunta y la respuesta puede haber pasado cualquier cosa.

**El recordatorio es un globo del área de notificación, no una toast de WinRT.** Una
aplicación sin empaquetar necesita un AppUserModelID registrado en un acceso directo del
menú Inicio para que el sistema le acepte una toast, y un servidor COM registrado para
enterarse del clic. Eso es un instalador, y Brújula es un .exe que se copia. El icono se
añade para enseñar el globo y se quita en cuanto el globo se va: esto no es una aplicación
de bandeja. **Solo suena con la aplicación abierta**, que es la limitación honesta; la
alternativa es una tarea del Programador de tareas que arranque `brujula.exe --revision`, y
ese argumento es de la fase 8.

**El reloj del recordatorio mira la hora cada cinco minutos y solo si hay uno puesto.** Un
temporizador de una semana se lo come una suspensión del equipo sin avisar, y para un aviso
semanal llegar cinco minutos tarde no significa nada. Y el día en que sonó se guarda en la
tabla de ajustes con el día **LOCAL**, no con `Model::FormatDay`, que da el UTC: un aviso
del lunes a las nueve de la noche se marcaba como del martes y, al cruzar la medianoche
UTC, volvía a sonar esa misma noche.

**`Ui::Element::ScaleTo` es nuevo y es del kit.** La lista se aleja al 0,94 mientras llega
la pila; un fundido a secas no dice «esto se ha ido detrás», dice «esto se ha apagado». No
toca el marco, así que el hit-test y la maquetación siguen hablando del tamaño de verdad.

**`Gfx::Morph` sigue sin llamadores.** La fase 5 lo reservó para esta fase «para una pila de
tarjetas que no reciben entrada mientras vuelan», y no hizo falta: las tarjetas de la
revisión son dueñas de su superficie y se mueven con `SlideTo`, que es un desplazamiento en
la GPU y no dos capas de píxeles cruzándose. Queda para quien lo necesite o para borrarlo.

**Para la fase 8: el texto se ve «un toque borroso», y no es de esta fase.** Se persiguieron
tres hipótesis y las tres están descartadas con medidas, no a ojo:

- *¿Remuestreo por una escala?* No. El borde de la tarjeta pasa de fondo a tarjeta en UN
  píxel —sin degradado— y mide 620 px justos. Aun así se le quitó la animación de escala,
  que tenía otro fallo de verdad.
- *¿La sombra?* No. Quitando `CreateShadow` de la tarjeta el texto sale exactamente igual.
- *¿Está temblando?* No. Tres capturas del área de cliente separadas 350 ms y una cuarta a
  los dos segundos, con la revisión abierta y sin tocar nada, salen **idénticas píxel a
  píxel**. En pantalla no se mueve nada.

Lo que queda, y es lo único que las medidas sostienen, es el **suavizado en gris**: la fase 1
lo eligió porque ClearType no existe sobre una superficie con alfa premultiplicado
(`Gfx::Surface` pone `D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE`), y canta aquí más que en ningún
otro sitio porque la revisión es la primera pantalla con texto de 26 DIP. Lo que hay que
probar en el pulido: unos `IDWriteRenderingParams` propios con más contraste mejorado y otra
gamma, que endurecen el gris sin tocar la decisión de la fase 1 — y juzgarlo con la
aplicación delante, no con una captura.

*Y dos avisos para quien vuelva a medir esto:* PowerShell es DPI-unaware, así que
`GetDpiForWindow` y `GetClientRect` desde ahí devuelven lo del sistema y no lo de la ventana
—daban 120 ppp donde la ventana está a 96— y hay que llamar antes a
`SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)`. Y comparar tamaños de letra distintos
ampliados no dice nada: dos textos del MISMO estilo, uno dentro de la tarjeta y otro fuera,
es la única comparación que separa un problema de la tarjeta de uno de toda la aplicación.

**Aplazar guarda QUÉ se aplazó, no solo hasta cuándo.** Son dos columnas —`pospuesto_hasta`
y `pospuesto_por`— y la segunda es la que importa: aplazar silencia una PREGUNTA y no un
repositorio. Si alguien aparca «está en Enfoque y parado» y durante ese mes ese mismo
repositorio empieza a recibir pushes estando archivado —el desajuste más informativo de los
tres, según la propia `Model::Review`— la pregunta ya no es la misma y se hace igual. Con
solo la fecha, ese aviso se perdería sin que nadie se enterara, que es exactamente la clase
de fallo que este proyecto persigue. El código lo dice en una línea: `local.snoozeFor !=
asking`.

**`Mismatch` se mudó a `model/Types.h`, y `None` significa dos cosas.** Se muda porque
`Local` lo guarda y `Rules.h` ya incluye `Types.h`; es la misma mudanza que la fase 3 le hizo
a `Priority` y `Activity`. Y dentro de la pila de la revisión, un repositorio SIN desajuste
está ahí por estar sin clasificar, así que «la pregunta que se hizo» es exactamente un
`Mismatch` con `None` queriendo decir «¿y esto qué es?». Reusarlo evita una segunda
enumeración que diría lo mismo con otras palabras — pero hay que saberlo, y por eso está
escrito en los dos sitios.

**El plazo vence por DÍA y no por segundo.** `Model::DayNumber` y no `DaysBetween`: quien
aplaza algo un mes no espera que reaparezca el día que vence a la hora exacta en que lo
aplazó, en medio de otra cosa. En UTC, como `FormatDay` y como las novedades — dos ideas de
«qué día es hoy» dentro de la misma caché es una de ellas equivocándose, que es justo el
fallo del recordatorio de más arriba.

**Un aplazado sigue contando en «Necesita decisión».** No se le quita, y no es un descuido:
esa vista dice lo que PASA con los datos y esconderlo sería que la barra lateral mintiera. Lo
que se añade es una vista aparte, «Pospuestos», que dice lo que decidiste no mirar todavía.
Un repositorio puede estar en las dos. Y para que las dos verdades no se lean como un fallo,
cuando la pila sale vacía por aplazamientos la aplicación lo DICE: «los N que quedaban están
pospuestos».

**Aplazar cuenta como decidido en la barra de progreso y aparte en el resumen.** Decidir no
decidir todavía es una decisión y mueve la barra; pero no entra en ningún grupo, así que el
resumen lo dice por separado — sin eso pondría «106 decididos» encima de unas barras que
suman 104.

### Fase 7, segunda pasada — 22 de septiembre de 2026

Con el Brave cerrado, la máquina por fin dejó traer la ventana al frente y mandarle entrada
de verdad (`SendInput`). Se repitió todo con teclado y ratón reales y salieron **dos fallos
más, los dos heredados**, además de confirmar que lo que parecía roto no lo estaba.

**Ctrl+K no abría la paleta desde la lista, y venía de la fase 6.** `Ui::List::OnKey` trata
la `J` y la `K` como «abajo» y «arriba» al estilo vim **sin mirar los modificadores**, y la
lista tiene el foco nada más arrancar. Así que Ctrl+K subía la selección, se comía la tecla
y `Views::Main` no llegaba a verla nunca: la paleta solo se abría con el foco en otro sitio.
La regla es la que las teclas 1-4 de la vista principal ya cumplían y la que `Ui::Field`
cumple en sus seis letras — **una letra con Control es de otro**— y ahora la cumple también
la lista, y la revisión con su `E` y su `P`.

**El nombre de la aplicación salía como «BrÃºjula», y venía de la fase 1.** `rc.exe` lee un
`.rc` sin BOM con la página de códigos del sistema, así que el UTF-8 de `brujula.rc` se
compilaba como CP1252 y la mojibake acababa en las propiedades del archivo y —donde se ve de
verdad— en la cabecera de la notificación del recordatorio. Se arregla con
`#pragma code_page(65001)` dentro del archivo, que es la solución que se lee; un BOM también
vale y lo pierde cualquiera al guardar con otra herramienta. No da ningún error al compilar,
y por eso llevaba seis fases ahí.

**Y lo que NO estaba roto**, aunque lo pareciera con el arnés de pruebas: Ctrl+Mayús+R, la
paleta, el globo. Dos falsos positivos con la misma causa y conviene saberla: la estructura
`INPUT` de `SendInput` mide **40 bytes en x64** y sin la parte del ratón en la unión sale de
32, con lo que la llamada devuelve cero y no llega ni una tecla — sin error visible. El
segundo: entre dos ejecuciones del script la ventana pierde la activación y `onDeactivate`
se lleva por delante menús, hojas y la paleta, así que una cadena de clics tiene que ir en
una sola ejecución.

**Comprobado con entrada real**: Ctrl+Mayús+R abre la revisión; E abre el campo y lo escrito
se guarda; Espacio salta; 1 clasifica; P aplaza; Esc vuelve a la lista; Ctrl+K abre la
paleta y su acción abre la revisión; y **el globo del recordatorio, pulsado con el ratón,
trae la ventana al frente y abre la revisión**.

**Falta comprobar cuatro cosas**, las cuatro heredadas: la nitidez a otras escalas
—`WM_DPICHANGED` no se puede disparar aquí: una sola pantalla al 100 %, medido con
`GetDpiForWindow` desde un proceso DPI-aware—, el IME de verdad, el panel táctil de
precisión y los tres cuadros de archivo.

### Fase 6 — 21 de septiembre de 2026

El detalle con todas las mediciones está en `CHANGELOG.md`. Aquí van solo las decisiones
que condicionan lo que venga después.

**Esta es la primera fase que se probó con la aplicación EN USO, y de ahí salieron tres
arreglos que no estaban en el guión.** Las cinco anteriores se juzgaron con capturas; esta
se usó, y en cuanto alguien hizo clic deprisa aparecieron un panel congelado a medio viaje,
las tarjetas en blanco y unas animaciones que sobre el papel eran correctas y en la mano se
sentían lentas. Los tres se arreglaron aquí y los tres venían de fases anteriores.

**Escribir una propiedad que tiene una animación encima NO para la animación.** Es la causa
del panel que se quedaba clavado a medio crecer al volver a pulsar una tarjeta:
`Element::SetFrame` paraba `Offset` antes de escribirlo pero no `Size`, así que el tamaño
escrito se perdía —la animación seguía mandando— y acto seguido `Material::SetSize` paraba
esa MISMA animación por su otro extremo, porque `Animator::SizeTogether` la arranca en el
visual y en la geometría a la vez. El visual se quedaba con el último valor animado y ahí se
quedaba para siempre. La regla, para todo lo que venga: **parar antes de escribir**, que es
lo que `Gfx::Material` ya hacía en sus tres métodos y `Element` hacía a medias.

**Reservar una textura la VACÍA, y la lista tenía el mismo fallo que la fase 4 encontró un
piso más abajo.** `Ui::List::OnArrange` recolocaba las celdas vivas —reservando su textura
al ancho nuevo— y no las repintaba: abrir el inspector estrecha la columna, y eso dejaba
TODAS las tarjetas en blanco menos la que tuviera el ratón encima, que se repintaba por el
hover. Lo reportado fue exactamente eso: «desaparecen los proyectos y tengo que pasarles el
mouse para que aparezcan». Ahora repinta `PlaceRow`, que es por donde pasan los cinco sitios
que recolocan, y no cada uno de ellos por su cuenta: dos ya se habían olvidado.

**El clic se avisa al SOLTAR desde que las tarjetas se arrastran.** Al pulsar todavía no se
sabe si eso es un clic; avisándolo ahí, cada arrastre empezaba abriendo el inspector —que
estrecha la columna y recoloca las tarjetas— justo debajo de la que se estaba levantando. La
selección sí se queda en el pulsar: es lo que ilumina la tarjeta bajo el dedo y de lo que
tira el arrastre.

**Lo que viaja mientras se arrastra es una COPIA, y cuelga de `Views::Main`.** La ventanilla
de la lista se recorta a sí misma, así que una celda arrastrada hacia la barra lateral
desaparecería al cruzar el borde. `Views::DragCard` es un elemento aparte —con sombra, a
escala 1,03 y pintado con el mismo `PaintCard`— y NO es una capa flotante del Host: las
capas se cierran todas juntas cuando la ventana pierde el foco, y una tarjeta levantada que
desaparece mientras alguien la sujeta dejaría un puntero colgando en la vista.

**Todo lo que cambia una prioridad pasa por `Application::ApplyPriority`.** Son cinco
caminos —el menú del inspector, las teclas 1-4, arrastrar, el menú contextual y la paleta—
y con la comprobación del límite copiada en cada uno, el sexto Enfoque entra por el camino
que se olvidó. Por eso el criterio «no es posible tener más en Enfoque que el límite» es una
función con pruebas (`Model::PlanFocus`) llamada desde un solo sitio.

**El arrastre termina SIEMPRE, y por eso avisa una sola llamada.** `Ui::List::OnDragEnd` se
llama al soltar, al pulsar Esc y cuando otra ventana se lleva la captura —que hasta ahora no
se enteraba nadie: `Ui::Router` se comía el `Cancel` sin contárselo al que tenía la captura—.
Y `List::Update` cancela el arrastre en curso: una sincronización que termine a mitad
cambiaría a qué repositorio apunta cada índice, y soltar después escribiría la prioridad del
que no es sin dar el menor error.

**El orden a mano es una columna de `local` y NO viaja a PROYECTO.md.** El formato de este
documento no tiene ese campo, y un commit por cada tarjeta arrastrada serían ciento nueve
commits por una tarde ordenando. Se escribe con `Repos::SetOrder`, que toca esa columna y
nada más —como `push_pending` y por lo mismo—, y al soltar se renumera la vista ENTERA del
uno en adelante dentro de una transacción: con números sueltos habría que inventar huecos
entre medias y un día no cabría ninguno.

**Y lo ordenado a mano va primero en TODAS las vistas.** No es un descuido: una comparación
que mirase el orden solo entre los de la misma prioridad no sería una relación de orden —A
antes que B por orden, B antes que C por fecha, C antes que A por fecha— y `std::sort` con
una de esas no da un resultado raro, da comportamiento indefinido. Con búsqueda puesta no se
ordena: lo que se ve es un trozo, y «entre estas dos» no dice nada de los que el filtro dejó
fuera.

**Deshacer es una pila de vueltas atrás, no de cosas que pasaron.** Guardar el `Model::Local`
de antes y volver a guardarlo sirve para la prioridad, el estado, el siguiente paso, la
carpeta y el modo repo con el mismo código; las novedades y el orden traen la suya. Lo que
va en pareja —bajar uno de Enfoque para subir otro— se apunta como UNA entrada, porque con
dos el primer Ctrl+Z dejaría seis en Enfoque durante un rato, que es el estado que no puede
existir. Y deshacer no se apunta a sí mismo: si no, Ctrl+Z dos veces mece el mismo cambio
para siempre.

**Ctrl+K se mira DESPUÉS del enrutador y Ctrl+R antes.** Los atajos globales de `App` se
miran antes que nadie, y ahí Ctrl+K abriría una segunda paleta encima de la primera. Ctrl+Z
también va por la vista: con el foco en un campo de texto, el Ctrl+Z es del historial del
campo, que es lo que espera quien está escribiendo una frase.

**El temblor del límite de Enfoque es lo único de la aplicación animado con fotogramas
clave.** La regla de la fase 1 dice muelles para todo lo interrumpible, y un temblor no se
interrumpe: o se ve entero o no ha dicho nada. Con las animaciones del sistema apagadas no
tiembla nada y el aviso lo da la hoja.

**Se añadió una vista que no está en este documento: `Views::Palette`.** La paleta no sabe
hacer nada de lo que ofrece —recibe las acciones ya montadas por `App`— y filtra con el
MISMO `App::Terms` que la búsqueda de la lista: dos maneras de buscar dentro de la misma
aplicación serían dos ideas distintas de qué significa encontrar algo. La única acción que
no hace lo que dice es «empezar la revisión semanal», que avisa de que llega en la fase 7:
una acción que no aparece se busca; una que avisa, no.

**Falta comprobar cinco cosas**, cuatro heredadas y una nueva: la nitidez a otras escalas
—`WM_DPICHANGED` sigue sin dispararse—, el IME de verdad, el panel táctil de precisión, los
tres cuadros de archivo, y **el arrastre con un ratón de verdad**: la máquina tenía otra
aplicación reteniendo el foco, así que todo lo de esta fase se probó con mensajes puestos a
mano en la cola de la ventana. Con eso se vio funcionar el camino entero —levantar, soltar
fuera y volver a su sitio— y el cambio de prioridad con la tecla 1, contador de la barra
lateral incluido; lo que no se ha visto es el hueco abriéndose entre dos tarjetas mientras
una mano mueve el ratón.

### Fase 5 — 21 de septiembre de 2026

El detalle con todas las mediciones está en `CHANGELOG.md`. Aquí van solo las decisiones
que condicionan lo que venga después.

**Esta es la primera fase que ESCRIBE, y eso cambia qué es un fallo grave.** Hasta aquí,
todo lo que se veía se podía volver a descargar. Desde aquí hay datos que existen solo
porque alguien los escribió, y el peor fallo posible ya no es una pantalla en blanco: es un
párrafo que desaparece del archivo de otro dentro de un commit que dice «actualizar
PROYECTO.md». De ahí sale casi todo lo demás de esta lista.

**Primero SQLite y después la red, siempre.** Toda edición se guarda en la caché antes de
intentar ningún commit. El criterio de aceptación de la fase —editar el siguiente paso y
cerrar la aplicación conserva el cambio— no puede depender de que haya cobertura ni de que
el modo repo esté encendido. Lo que no llega a GitHub se queda marcado en `push_pending` y
se reintenta al terminar la siguiente sincronización.

**El modo repo son DOS columnas y no una.** `repo_mode` es el interruptor; `repo_confirmed`
es «alguien dijo que sí en ESTE repositorio». Escribir exige las dos, y solo una línea de
todo el programa enciende la segunda: la que contesta la hoja de confirmación. Con un solo
booleano, «modo repo por omisión» sería exactamente el interruptor global que la regla 5 de
`SEGURIDAD.md` dice que no existe. Por lo mismo, **la copia de seguridad exporta el modo
repo pero no lo importa**: un archivo que encendiera ciento nueve escrituras sería ese
interruptor entrando por la puerta de atrás.

**Un commit que no cambia nada no se hace.** Antes de escribir se compara lo que se iba a
subir con lo que hay; si coinciden, se da por bueno sin commitear. Tapa además el reintento
de red de un PUT cuya respuesta se perdió: el commit ya existe, y al releer sale justo esto
en vez de un segundo commit idéntico.

**El parser de PROYECTO.md conserva lo que no entiende, y eso es el requisito, no un
adorno.** Las claves inventadas del frontmatter vuelven a escribirse en crudo, y los
párrafos y las secciones de alrededor de las novedades también. Incluso un valor que no
reconocemos —`prioridad: urgentísimo`— se guarda tal cual para poder devolverlo: sin eso, el
`optional` sale vacío y la línea desaparece al escribir. Comprobado contra un repositorio de
verdad editando el archivo desde fuera y volviendo a guardar desde Brújula.

**Lo que viaja en la transición compartida es el ELEMENTO, no un `Gfx::Morph`.** Un `Morph`
lleva dos capas de píxeles; el inspector tiene un campo de texto, botones y una lista, que
son elementos con entrada. Usarlo obligaría a dibujar el panel dos veces. En su lugar el kit
gana `Element::MorphTo` —posición, tamaño y radio del material con un muelle, **solo en
elementos sin superficie propia**— y `Element::SetContentOpacity`, que cruza el contenido
sin tocar el material. Por eso en el primer fotograma el panel ES la tarjeta: misma forma,
mismo color y mismo sitio. `Gfx::Morph` se queda sin llamadores y NO se borra: la fase 7 lo
quiere para la revisión semanal.

**Una tecla usada como atajo se escribía además como letra, y venía de la fase 4.**
`TranslateMessage` pone el `WM_CHAR` en la cola al sacar el mensaje, antes de que nadie haya
podido decir que la tecla era un atajo, así que consumir el `WM_KEYDOWN` no lo evita.
`Shell::Window` se come ahora ese `WM_CHAR`. El detalle que costó encontrarlo: la marca la
tocan **solo las pulsaciones y nunca las sueltas**, porque un `WM_KEYUP` colándose en medio
la apagaba justo antes de que sirviera. La fase 4 lo tenía sin saberlo: su único atajo de
una letra era `/`, que enfoca la búsqueda y se escribía dentro del campo que acababa de
enfocar.

**El inspector no guarda punteros al estado, y se reengancha por identificador.**
`App::State` se reconstruye entero después de cada sincronización, así que un puntero a una
`App::Entry` apuntaría a memoria liberada en cuanto llegara el hilo de trabajo. Y por
posición tampoco: al cambiar la prioridad, el repositorio puede salirse de la vista que se
está mirando, y el panel tiene que seguir enseñando lo que el usuario acaba de tocar.

**Guardar es del `OnBlur`; Enter solo suelta el foco.** Con dos caminos de guardado, uno de
los dos se olvida — y el que se olvida siempre es el de perder el foco, que es la mitad de
las veces que alguien termina de escribir.

**El trabajador de GitHub pasa a atender una cola.** Sincronizar y escribir comparten la
credencial, el cliente y la conexión a SQLite; dos dueños de una credencial son dos vidas
que sincronizar. Queda además serializado, que es lo que se quiere: un PUT no puede correr a
la vez que el segundo pase escribiendo la misma fila. La decisión de que el hilo se muera se
toma **bajo el mismo candado** que usa quien encola: fuera de él, un trabajo que llegara
entre la comprobación y el `return` se quedaría ahí para siempre sin dar ningún error — solo
un commit que nunca sube.

**Los cinco commits y los `.md` de la raíz se piden en el PASE 2 y no al abrir el
inspector.** Es la regla 1 de arquitectura: la interfaz no espera a la red. Medido el mismo
día contra la cuenta real, el pase 2 pasa de ~2,9 s a ~4,0 s reenriqueciendo los 109 — poco
más de un segundo, y solo en el peor caso, porque la sincronización de un día normal no hace
ni una petición de detalle. **Y ese número ahora se puede volver a mirar**: `ms_pase1`,
`ms_pase2` y `repos_detalle` se guardan en la tabla de ajustes, que es el mismo papel de
registro que la fase 3 le dio a esa tabla.

**La ruta de la API se valida, no se pega.** El nombre del repositorio viene de la respuesta
de GitHub y acaba dentro de una URL. Con un nombre que no tenga forma de nombre, la
escritura falla con un aviso en vez de pedir una dirección inventada. Y `auditar.ps1` gana
la **regla 11**, que hasta ahora no tenía código que vigilar: el archivo está en una
constante y vale exactamente `PROYECTO.md`, toda ruta `/repos/` termina en esa constante, y
el único verbo que llega al cliente REST es `PUT`. Comprobada con dos sondas antes de darla
por buena.

**Se añadió una vista que no está en este documento: el botón de ajustes del pie.** La
carpeta donde se clonan los repositorios, el modo repo por omisión y las dos copias de
seguridad no tenían dónde vivir, y una hoja de Ajustes entera es más pantalla de la que esta
fase pedía. Es un `Ui::Menu`, que ya existía desde la fase 2.

**Falta comprobar cinco cosas**, cuatro heredadas y una nueva: la nitidez a otras escalas
—`WM_DPICHANGED` sigue sin dispararse, y ahora hay un elemento más que anima su tamaño—, el
IME de verdad, el panel táctil de precisión, una credencial sin permiso de escritura (la de
esta máquina viene de GitHub CLI y es ancha, así que el 403 del modo repo no se dispara
solo) y los tres cuadros de archivo, que compilan pero no se han podido accionar: probarlos
necesita traer la ventana al frente, y esta máquina tenía otra aplicación reteniendo el foco.

### Fase 4 — 21 de septiembre de 2026

El detalle con todas las mediciones está en `CHANGELOG.md`. Aquí van solo las decisiones
que condicionan lo que venga después.

**Reservar una textura la VACÍA, y eso era un fallo dormido desde la fase 2.** La vista
principal salió en blanco: la barra lateral sin una sola letra, el título de la vista
tampoco, los glifos de los botones de ventana tampoco — y sin embargo los materiales, las
píldoras y las tarjetas de la lista, sí. Lo que quedaba en pantalla era exactamente lo que
alguien había repintado *después* del último `Host::Layout`, y nada más.
`ICompositionDrawingSurfaceInterop::Resize` devuelve un hueco del atlas con los píxeles de
quien estuviera antes —por eso `Surface::Draw` empieza siempre por un `Clear`—, así que
`Element::SetFrame` estaba borrando cada superficie del árbol en cada recolocación y nadie
pedía repintarla. `Element::Relayout` invalida, sí, pero invalida `SurfaceOwner()`, y la
raíz no tiene superficie: devolvía nulo y no se repintaba nada.

*Por qué no se vio antes:* hasta la fase 3, cada refresco de la pantalla cambiaba también
el contenido —`Views::Status::Show` reescribía sus etiquetas cada vez— y un `SetText`
distinto invalida. Un título que dice siempre lo mismo no invalida nunca, y la fase 4 está
llena de ellos. Son dos arreglos y los dos hacen falta: `Surface::Resize` sale sin tocar
nada cuando el tamaño **físico** no cambia, y `Element::SetFrame` invalida cuando el marco
o la escala sí cambiaron. El segundo es el que salva el cambio de monitor, donde el marco
en DIP es el mismo y la textura no.

**Cerrar un elemento no lo quitaba de la pantalla, y es el mismo tipo de fallo.**
`Element::Close` soltaba su referencia al visual, pero el contenedor del padre tiene la
suya: el visual se quedaba en el árbol de composición y se seguía dibujando. Al cambiar de
raíz —F12, el catálogo— la vista vieja se quedaba detrás de la nueva. Casi no se ve porque
lo que se cierra suele llevar su superficie cerrada y deja de pintar nada; un contenedor
sin superficie propia no deja rastro. Ahora `Close` y el destructor sacan el visual del
árbol. La fase 5 cambia de vista constantemente y esto le habría tocado a ella.

*Y de ahí sale una consecuencia:* los botones de la ventana se dibujan dentro del kit
—`Views::Chrome`— y no fuera como los dibujaba `Views::Demo`, así que **toda raíz necesita
un Chrome**. El catálogo lleva el suyo, con su propio título. Siguen funcionando aunque no
se dibujen, porque el hit-test del marco no depende del dibujo, y por eso el fallo no se
nota hasta que alguien busca el aspa y no está.

**La lista se actualiza por CLAVE y su índice es siempre el de la pantalla.** `Reorder`
—que la fase 2 dejó preparado— se ha quitado. Barajar posiciones manteniendo el orden de
los datos suena más barato hasta que se mira el teclado: con las posiciones barajadas, «el
siguiente» de la flecha abajo es el siguiente del array y no el de debajo, y la selección
va dando saltos. `List::Update(keys)` resuelve además lo que `Reorder` no podía —quién
entra y quién sale— que es justo lo que pide filtrar en vivo. Y la selección sigue a su
clave: el repositorio elegido sigue elegido después de sincronizar aunque cambie de sitio.

**El tamaño de una celda no se anima, y la posición sí.** Al pasar de lista a cuadrícula
las celdas cambian de tamaño de golpe y se deslizan a su sitio con el muelle suave.
Animar el tamaño obligaría a reasignar la textura de cada celda en cada fotograma, que es
justo lo que la fase 1 midió que no había que hacer. Lo que se lee en pantalla es «la
rejilla ha fluido», y es la misma decisión que ya estaba escrita para `SlideTo`.

**Un contenedor cuyos hijos tienen superficie no puede pintar, y de ahí salen dos piezas
del kit.** La regla estaba escrita en `ui/Element.h` desde la fase 2 y la columna de la
fase 4 es su caso extremo: lleva una lista, un campo y unos botones, todos dueños de
superficie, y necesita además un título, un contador y las frases del estado vacío.
`Ui::Slate` es un contenedor con superficie y sin pintura propia, para que ese texto tenga
dónde caerse; `Ui::Rule` es un separador hecho MATERIAL y no píxeles, que además cruza de
tema en la GPU. La fase 5 tendrá el mismo problema en el inspector.

**El ajuste de línea va en la maquetación, no en el formato.** Los formatos de `Ui::Text`
nacen con `NO_WRAP` y están cacheados y compartidos: ponerle ajuste de línea a uno se lo
pone a todas las etiquetas de la aplicación, incluidos los nombres de repositorio, que
tienen que recortarse con elipsis. Es la tercera propiedad que va en la maquetación por el
mismo motivo, después de la alineación y de los números tabulares.

**Los contadores de la barra lateral NO bajan al buscar.** Dicen cuántos hay en esa vista,
no cuántos quedan del filtro. Es lo único que permite ver que lo que buscas está en otro
grupo, que es la mitad de las veces que uno busca.

**Se añadió una vista que no está en este documento: «Todos».** Sin ella la búsqueda solo
puede mirar dentro del grupo elegido, y buscar un repositorio que no sabes dónde
clasificaste es exactamente para lo que se busca. Es también la vista por omisión del
primer arranque, cuando los 109 están sin clasificar y cualquier otra saldría vacía.

**«/» se le pregunta a la distribución de teclado.** En un teclado estadounidense es
`VK_OEM_2` a secas y en uno español es Mayús+7. `VkKeyScanW(L'/')` devuelve las dos cosas
—tecla y estado de mayúsculas— para la distribución activa. Escrita a mano, la tecla
habría funcionado en la máquina de quien la escribió y en ninguna otra.

**Un repositorio con `gone_at` solo aparece en «Todos», y marcado.** No se esconde del
todo porque sus notas siguen existiendo y hay que poder llegar a ellas; no sale en las
demás porque una lista que existe para decidir no puede tener dentro cosas sobre las que
ya no se puede decidir.

**«Sin clasificar» no lleva píldora.** No es una prioridad, es la falta de una. Ponerle
etiqueta significa escribir «Sin clasificar» ciento nueve veces en la primera pantalla que
ve el usuario —la más ancha de todas, además, comiéndole el sitio al nombre— para decir
exactamente nada. Lo que hay que mirar de un vistazo es cuáles SÍ tienen una.

**El estado se recarga entero después de sincronizar.** Dos consultas y unos pocos
milisegundos con 109 repositorios, y a cambio no hay un camino por el que la pantalla y
SQLite acaben diciendo cosas distintas. La animación no se pierde por eso: la identidad la
llevan las claves, así que lo que solo cambió de sitio se desliza.

**La cuenta se lee de un solo sitio.** `Github::Progress` ya la trae, sacada de la tabla de
ajustes al crearse la sincronización. Leerla otra vez desde `App` obligaba a repetir el
nombre de la clave en dos archivos — y se repitió mal («login» en vez de «cuenta»), con el
resultado de un «Sin cuenta conectada» permanente y ningún error por ninguna parte.

**Falta comprobar cuatro cosas**, tres heredadas y una nueva: la nitidez a otras escalas
—`WM_DPICHANGED` sigue sin dispararse en esta máquina, y ahora hay además el camino nuevo
de invalidar al cambiar de escala—, el IME de verdad, el panel táctil de precisión, y la
lista con repositorios repartidos por los cinco grupos: los 109 de la cuenta están todos
sin clasificar, así que la píldora, el límite de Enfoque y «Necesita decisión» solo se han
visto con datos hechos a mano en las pruebas.

### Fase 3 — 21 de septiembre de 2026

El detalle con todas las mediciones está en `CHANGELOG.md`. Aquí van solo las decisiones
que condicionan lo que venga después.

**La consulta de un solo pase no cumplía el criterio de aceptación de este documento.** Con
todos los campos y 100 por página son 8,3-9,1 s por página y un 502 de cada ocho peticiones:
diecisiete segundos para los ~120. Y reducir la página no ayuda, porque la latencia va por
repositorio (~85 ms) y el cursor obliga a ir en serie. La línea de arriba ya está reescrita
con los dos pases; lo que hay que llevarse de aquí es **por qué el segundo se puede
paralelizar y el primero no**: `nodes(ids:)` no tiene cursor. Cualquier consulta futura que
haya que pedir en paralelo tiene que ir por identificadores, no por páginas.

**Y lo incremental salió de ahí, no se diseñó aparte.** Como el pase 2 ya recibe una lista
de identificadores, restringirla a los que cambiaron es la misma línea de SQL. La segunda
sincronización del día hace el pase 1 y **cero** peticiones de detalle.

**`enriched_push` es una columna, no una comparación.** Guarda el `pushedAt` que venía en la
respuesta del pase 2 —no el del pase 1— y se escribe *después* de que el detalle haya
entrado en SQLite. Las dos cosas importan: con el del pase 1, un push que caiga entre los
dos pases obliga a volver a pedirlo; escribiéndola antes, una sincronización cortada a mitad
daría por enriquecido lo que no lo está y no volvería a pedirlo jamás.

**La sincronización no borra filas de `repos`.** Un repositorio que deja de aparecer se
marca con `gone_at`. Con `ON DELETE CASCADE`, un 502 en la segunda página se llevaría por
delante el siguiente paso y las novedades escritas a mano. Las claves ajenas van **sin**
cascada al borrar y **con** `ON UPDATE CASCADE`, que es lo contrario de lo que uno escribe
por inercia y es justo lo que hace falta: borrar tiene que fallar, y renumerar tiene que
arrastrar las notas.

**Y por eso `repos` tiene dos claves.** Pidiendo `nodes(ids:)` con un identificador viejo, la
API contesta con un aviso: *«The id … is deprecated. Update your cache to use the
next_global_id»*. GitHub está migrando los identificadores globales. Si algún día cambian,
reconocer la fila solo por el id convertiría los 109 repositorios en 109 nuevos y dejaría las
notas colgando de identificadores que ya no existen — sin un solo error por ningún lado. La
escritura empareja por id y, si no lo encuentra, por `name_with_owner`.

**El aviso del hilo de trabajo no lleva carga, y eso ya estaba decidido.** `Github::Sync`
publica `WM_APP+2` vacío y el hilo de UI relee, exactamente como `Shell::ThemeWatcher` desde
la fase 1. Lo que la fase 3 añade es el reuso del truco de `Ui::Painter`: un `atomic_flag`
evita que seis trozos terminando a la vez publiquen seis mensajes.

**Cancelar no es poner una bandera.** Una bandera mirada entre peticiones no saca a un hilo
de un `WinHttpReceiveResponse` que está esperando: haría falta agotar los treinta segundos
del tiempo de espera. Lo que lo saca es **cerrarle el handle desde otro hilo**, y por eso
`Http::Session` lleva un registro de las peticiones en vuelo. Cada una se apunta con un
número propio y creciente y no solo con su handle, porque si WinHTTP reutiliza el valor de
uno recién cerrado para otra petición, su dueño cerraría el de otro. Medido: cerrar la
ventana a mitad de sincronizar tarda 0,26 s, cortando en cuatro momentos distintos.

**El límite por conexiones habría anulado el pase 2 en silencio.** WinHTTP limita las
conexiones por servidor; seis peticiones estranguladas a dos tardan el triple y no dan ni un
error, solo tiempo. Se ponen las dos defensas —HTTP/2, que multiplexa las seis sobre una
conexión, y `MAX_CONNS_PER_SERVER` explícito por si no se negocia— y, como esto no se puede
comprobar leyendo, `Response` lleva el protocolo que se negoció de verdad y la sincronización
lo escribe en `ajustes`. Dice `HTTP/2`.

**nlohmann lanza excepciones por omisión, y eso aquí es un `std::terminate`.** El JSON se lee
en el hilo de trabajo, y una excepción que se escape de ahí no es un error que se enseñe: es
la ventana desapareciendo de la pantalla. Todo pasa por
`json::parse(…, nullptr, /*allow_exceptions*/ false)` y `is_discarded()`.

**Los nulos son el camino normal, no el raro.** 76 de los 109 repositorios no tienen
descripción y 7 no tienen lenguaje. Un `dump()` sobre un nulo devuelve las cuatro letras
`null`, y esa cadena acabaría impresa en 76 tarjetas como si fuera la descripción. Todos los
lectores del parser tratan «no está la clave» y «la clave vale null» igual.

**El tipo `Error` no puede llevar el cuerpo de una respuesta, y es a propósito.** La regla 3
de `SEGURIDAD.md` dice que no se registran las respuestas de la API. La manera de que eso
siga siendo verdad dentro de tres fases no es acordarse: es que no haya dónde meterlo.
`detail` es una frase que redactamos nosotros; lo que sí viaja es el `X-GitHub-Request-Id`,
que identifica el intercambio sin contener nada de dentro.

**La credencial es un tipo, no una cadena.** `Github::Secret` no tiene `c_str()`, no tiene
accesor al valor y copiarlo es un error de compilación; la única salida es la cabecera
`Authorization` ya montada. Lo que **no** promete, y hay que decirlo: borrar de memoria una
cadena que ya se copió no es posible del todo. Se quitan las copias que sabemos que existen
—el búfer de la tubería, la cabecera, el valor al morir— y se reserva capacidad fija para que
no se reubique. Es una mitigación, no una garantía.

**Y había una fuga en código de la fase 2.** `Ui::Field` guarda el texto en `Ui::Editor`, que
lleva historial de deshacer: cada paso conserva lo que se insertó, así que una credencial
pegada quedaba en dos copias. De ahí `Ui::Editor::SetHistoryEnabled` y `Ui::Field::SetSecret`.
Quince líneas, y convierten una promesa en algo que se puede probar.

**`Ui::Priority` y `Ui::Activity` se mudaron a `model/`.** La fase 2 las estrenó en
`ui/Controls.h` porque todavía no había dominio. Ahora la verdad está en `model/Types.h` y
`Controls.h` las recibe con un `using`: ningún llamador cambió, y no hay dos enumeraciones
para un concepto esperando a separarse.

**El catálogo y `Views::Demo` siguen ahí, y la raíz ya no es la demo.** `Views::Status` es la
raíz provisional de esta fase y la fase 4 la tira. `Views::Demo` no se borra porque todavía
dibuja los botones de la ventana y es la única prueba viva de la transición compartida.

**Faltan cuatro cosas por comprobar**, y las cuatro por no tener con qué: la credencial
pegada a mano de principio a fin —esta máquina tiene GitHub CLI autenticado y la aplicación
nunca llega a esa rama sola—, las organizaciones, un `PROYECTO.md` de verdad (ninguno de los
109 repositorios lo tiene) y una credencial recortada sin permiso de Contents.

### Fase 2 — 21 de septiembre de 2026

El detalle con todas las mediciones está en `CHANGELOG.md`. Aquí van solo las decisiones
que condicionan lo que venga después.

**Hay un árbol de elementos, y hay que usarlo.** `ui/Element` y `ui/Host` son la base: un
elemento tiene marco en DIP, opcionalmente un material y un `Gfx::Layer`, y recibe la
entrada ya en coordenadas locales. La fase 4 escribe la vista principal componiendo
piezas, no colocando visuales a mano como hace `Views::Demo`.

**No todo elemento tiene textura, y eso no es una optimización.** Un `Gfx::Layer` son dos
superficies de Direct2D; quinientas filas serían mil, y la fase 1 ya midió que cincuenta
repintados dejaban 105 MB. Un elemento o es *dueño de superficie* —tiene `Gfx::Layer`— o
se pinta dentro de la del ancestro más cercano que la tenga. Un menú es una superficie;
sus opciones no. Una fila de lista sí, pero solo hay entre siete y diez vivas de las 500.

*Consecuencia:* un contenedor cuyos hijos sean dueños de superficie no debe pintar
contenido propio. El orden dentro de un elemento es material abajo, hijos en medio y
contenido arriba, así que el texto del padre quedaría por encima de sus hijos.

**El hover y el pulsado no repintan.** El hover anima el color del material y el pulsado
anima la escala, las dos propiedades de la GPU. Repintar para aclarar un fondo sería
reconstruir una superficie mientras el ratón cruza una lista. Solo repinta lo que cambia
píxeles: habilitar, deshabilitar, cambiar el texto.

**El repintado se difiere y se reúne.** `Element::Invalidate` no pinta: apunta el elemento
y publica un solo `WM_APP+1`. Como el mensaje cae después del que se está tratando, todos
los cambios de estado de un mismo evento de entrada se funden en un repintado. Sigue sin
haber tic por fotograma, y no debe haberlo.

**Todo elemento se desapunta al morir.** El destructor de `Element` se quita del conjunto
de repintado y del enrutador. Una fila reciclada y un menú cerrado se destruyen mientras
los dos todavía les apuntan; sin eso, es la caída más probable de todo el kit.

**La capa flotante es hermana del contenido, no hija.** Las sombras no las recorta el clip
implícito del tamaño, pero **sí** las recorta un `Visual.Clip` explícito, y `Gfx::Morph`
pone uno en su raíz. Nada que lleve sombra puede colgar de un `Morph`. De ahí que el Host
tenga dos raíces.

**La sombra suave, que la fase 1 dejó sin resolver, es `LayerVisual::Shadow`.** Con
`SourcePolicy` en `InheritFromVisualContent`, la sombra sale del alfa aplanado del
subárbol. Esto deja intacta la regla de la fase 1: el material sigue siendo un
`ShapeVisual` con su geometría y su brocha, y las esquinas suavizadas del rasterizador son
también el borde de la sombra. No hizo falta ni nine-grid ni máscara a mano.

*Y el límite:* un `LayerVisual` aplana su subárbol en una superficie fuera de pantalla
cada vez que cambia. **Solo para lo que flota** —menú, aviso y hoja—. En una lista de 500
filas sería el final del criterio de los 60 fps.

**Los números tabulares no caben en un formato.** `'tnum'` es propiedad de un rango de una
maquetación, no de un `IDWriteTextFormat`. Y la alineación tampoco: `Text::Format` devuelve
un objeto compartido y cacheado, así que un `SetTextAlignment` ahí centraría todas las
etiquetas de la aplicación. Las dos van en la maquetación, y por eso existen `Ui::Run` y
`Ui::Text::Layout`.

**El desplazamiento es un `InteractionTracker`.** La inercia la calcula DWM y el contenido
va atado con una expresión. Lo que decide que sea esto y no un muelle reapuntado es que
avisa en cada fotograma con la posición: sin esa lectura, una lista virtualizada no sabe
qué filas materializar. La rueda le manda velocidad, no destino, y por eso dos muescas
seguidas llegan más lejos que el doble de una.

**El campo de texto es puro por dentro.** `Ui::Editor` —cadena, cursor, selección e
historial— no llama a Windows ni sabe de píxeles, y está en `brujula_core` con pruebas.
`Ui::Field` solo pone dónde cae el cursor y de dónde salen las teclas. La composición del
IME vive fuera del texto y fuera del historial, así que quien lea `Text()` no tiene que
saber que existe un IME.

**El IME se limita a colocar su ventana.** `DefWindowProc` ya convierte la cadena
confirmada en mensajes `WM_CHAR`, que es por donde entra todo lo demás; lo único nuestro es
`ImmSetCompositionWindow` en el cursor. No hay una segunda ruta de texto que mantener.

**Las tildes y la eñe no necesitaron nada.** `TranslateMessage` ya compone la tecla muerta:
el acento y luego la «a» llegan como un solo `WM_CHAR`. Lo que sí hizo falta fue filtrar
los caracteres de control, porque Ctrl+V manda un 0x16 por `WM_CHAR` además de su
`WM_KEYDOWN`, y sin el filtro se escribe un carácter invisible.

**`auditar.ps1` no se toca para acallar un falso positivo.** La regla 4 marcaba
`"Windows.UI.Composition.LayerVisual"` porque el «.Co» se lee como un dominio. En vez de
relajar la regla se quitó la cadena: ahora se pregunta por las interfaces `ILayerVisual2` e
`IDropShadow2`, que además es la pregunta de verdad.

**El catálogo se compila solo en Debug.** `$<$<CONFIG:Debug>:...>` en las fuentes y
`BRUJULA_CATALOGO` en las definiciones. La ruta del archivo va **absoluta**: una ruta
relativa dentro de una expresión de generador se resuelve contra el directorio de
compilación y no contra el de fuentes, y CMake no avisa.

**El tema baja por el árbol, no se lee de un global.** `Ui::Paint` lleva los tokens por
puntero y `Element::ApplyTheme` recorre un subárbol, así que un contenedor puede
sustituirlos sobrescribiendo `Substitute`. Es lo que permite que el catálogo enseñe cada
componente en claro y en oscuro a la vez sin ninguna máquina añadida, y lo que la fase 7
necesitará si la revisión semanal quiere su propia paleta.

**Falta comprobar tres cosas**, y las tres por falta de máquina: la nitidez a otras
escalas —sigue sin dispararse `WM_DPICHANGED`, y ahora hay veinte veces más superficies—,
el IME con un método de entrada de verdad, y el panel táctil de precisión.

### Fase 1 — 21 de septiembre de 2026

El detalle con todas las mediciones está en `CHANGELOG.md`. Aquí van solo las decisiones
que condicionan lo que venga después.

**La Mica necesita `WS_EX_NOREDIRECTIONBITMAP`, no solo el marco extendido.** Con una
ventana normal, el área de cliente tiene superficie de redirección y sale blanca opaca
(medido: `(255,255,255)` con Windows en oscuro), así que tapa la Mica pase lo que pase con
`DwmExtendFrameIntoClientArea`. Este documento pedía el marco extendido y se mantiene,
pero con un margen de **un píxel arriba** —para que DWM siga dibujando la línea de borde
que `WM_NCCALCSIZE` se lleva— y no con `-1`: con `-1`, DWM considera que toda la ventana es
marco y dibuja encima sus propios botones de ventana, que se ven a la vez que los nuestros.

*Consecuencia para las fases siguientes:* **no puede haber controles hijos HWND.** Sin
superficie de redirección no se pintan. El campo de texto de la fase 2 y todo lo demás se
dibujan con Direct2D, que es lo que este documento ya pedía.

**El tema se deduce del texto del sistema, no del fondo.** `UIColorType::Background`
devuelve negro siempre en una aplicación de escritorio, en claro y en oscuro. Se usa
`UIColorType::Foreground`: texto claro significa fondo oscuro.

**Un token nuevo en la tabla: el velo de la barra lateral.** El «material más translúcido»
de la barra lateral y el inspector no puede ser acrílico, porque `CreateHostBackdropBrush`
se pinta negro en una aplicación Win32 sin empaquetar (medido en la isla). Es un color
sobre la Mica: `rgba(255,255,255,0.35)` en claro y `rgba(0,0,0,0.20)` en oscuro.

**El texto va con suavizado en gris, no con ClearType.** No es una elección: ClearType no
existe sobre una superficie con alfa premultiplicado, y todas las nuestras lo son porque
debajo está la Mica y no un color opaco.

**El material y el contenido de una tarjeta van separados.** Las formas de Composition se
rellenan con color y degradados, no con superficies, así que el rectángulo redondeado es un
`ShapeVisual` y el contenido son `SpriteVisual` encima. Sale mejor de todos modos: las
esquinas las suaviza el rasterizador de formas, mientras que un recorte geométrico tiene el
borde duro.

**El layout se escribe en DIP y la escala va en la raíz.** Un cambio de DPI es una
escritura de propiedad, no rehacer el árbol de visuales. Las superficies sí se reservan al
tamaño físico, que es lo que mantiene el texto nítido. Los vecinos rehacen el árbol entero
porque tienen los píxeles horneados en los visuals; aquí no hace falta.

**Los muelles interrumpibles no admiten fotogramas clave.** Una animación de fotogramas
clave interrumpida reempieza desde su valor inicial, y eso es un salto. Todo lo que pueda
interrumpirse —posición, tamaño y radio— va con `SpringXNaturalMotionAnimation`, que retoma
valor y velocidad. Los fotogramas clave se quedan para lo que no se interrumpe: opacidad y
color.

**`Period` no es la duración.** Los cuatro muelles de la tabla de arriba asientan en 85,
165, 239 y 297 ms. Los vecinos usan periodos de 40-50 ms, bastante más secos; volver a
afinarlo es trabajo de la fase 8, no de ahora.

**Falta comprobar la nitidez a otras escalas.** Esta máquina tiene una sola pantalla al
100 %, así que `WM_DPICHANGED` no llega a dispararse. La aritmética está probada; la
pantalla no.
