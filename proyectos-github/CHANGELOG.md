# Cambios

El proyecto va por fases, no por versiones: cada una tiene que compilar sin warnings y
pasar sus pruebas antes de empezar la siguiente.

Los números que aparecen aquí están medidos, no estimados. Cuando algo no se pudo medir,
lo dice.

---

## Sin publicar

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
