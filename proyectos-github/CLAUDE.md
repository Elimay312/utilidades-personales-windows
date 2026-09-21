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

| Uso | Muelle |
|---|---|
| Interacciones pequeñas (hover, pulsar, marcar) | rígido: amortiguación 0,9, periodo 120 ms |
| Paneles e inspector | estándar: amortiguación 0,85, periodo 220 ms |
| Reordenar y mover tarjetas entre grupos | suave: amortiguación 0,8, periodo 300 ms |
| Hojas modales y revisión semanal | expresivo: amortiguación 0,75, periodo 350 ms |

- Pulsar un control lo escala a 0,97; soltarlo vuelve con muelle rígido.
- **Transiciones compartidas:** al abrir un repo, su tarjeta se transforma en el inspector (posición, tamaño y radio a la vez, con fundido cruzado del contenido). Al cerrar, vuelve a su sitio.
- Listas: los elementos que entran aparecen con fundido + desplazamiento de 8 px escalonado 20 ms; los que cambian de posición se deslizan, nunca saltan.
- **Respetar "Mostrar animaciones en Windows"** (`SPI_GETCLIENTAREAANIMATION`): si está desactivado, sustituir movimientos por fundidos cortos.
- Objetivo: 60 fps constantes (y la frecuencia del monitor si es mayor) incluso con los 120 repos visibles.

## Vistas

- **Barra lateral:** grupos de prioridad con contador, y vistas inteligentes: Necesita decisión, Dormidos, Actividad esta semana, Sin clasificar.
- **Lista principal:** tarjetas con nombre, siguiente paso (lo más visible tras el nombre), indicador de actividad, prioridad, lenguaje y "hace X días". Alternar entre lista compacta y cuadrícula.
- **Inspector (panel derecho):** prioridad, estado, siguiente paso editable, novedades con fecha, últimos commits, issues/PRs abiertos, botones para abrir en GitHub y en la carpeta local.
- **Revisión semanal:** modo a pantalla completa que presenta una tarjeta por repo que necesita decisión; con 1-4 asignas prioridad, E editas el siguiente paso, espacio salta. Cada decisión anima la tarjeta hacia su grupo.
- **Paleta de comandos (Ctrl+K):** buscar repos y ejecutar acciones escribiendo.

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
| N | Añadir novedad |
| Ctrl+R | Sincronizar ahora |
| Ctrl+Shift+R | Empezar revisión semanal |
| Ctrl+O | Abrir el repo en GitHub |

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
- [ ] Fase 4 — Vista principal: barra lateral, lista y clasificación
- [ ] Fase 5 — Inspector, notas y PROYECTO.md
- [ ] Fase 6 — Priorizar: arrastrar, límite de Enfoque, atajos y paleta
- [ ] Fase 7 — Revisión semanal
- [ ] Fase 8 — Pulido final y rendimiento

Al terminar una fase: marcarla aquí, anotar decisiones abajo y hacer commit.

## Decisiones y notas

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
