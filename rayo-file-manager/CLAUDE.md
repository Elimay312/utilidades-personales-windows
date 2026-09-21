# CLAUDE.md — Rayo (gestor de archivos para Windows)

> "Rayo" es un nombre de trabajo. Cámbialo aquí y en `CMakeLists.txt` si prefieres otro.

## Visión

Un gestor de archivos para Windows inspirado en Yazi: columnas Miller, navegación 100% por teclado (estilo Vim) y vista previa instantánea. Es una aplicación gráfica nativa, no una app de terminal.

La prioridad absoluta es la **velocidad percibida**: arranque instantáneo, navegación sin esperas y cero consumo en reposo. Ante cualquier duda de diseño, gana la opción que mantenga la UI fluida.

## Stack

- C++20, compilador MSVC (Visual Studio 2022 / Build Tools), solo x64.
- CMake + Ninja.
- Win32 puro para la ventana; DirectX 11 para el render.
- Dear ImGui (backends `imgui_impl_win32` + `imgui_impl_dx11`), descargado con `FetchContent` y versión fijada a un tag concreto.
- WIC (Windows Imaging Component) para decodificar imágenes. **No** usar stb_image.
- Sin Qt, sin Boost, sin .NET. Cualquier dependencia nueva debe justificarse y consultarse antes.

## Comandos

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\rayo.exe
```

Instalación por usuario (copia a `%LOCALAPPDATA%\Programs\Rayo`, acceso directo en el menú
Inicio y "Abrir en Rayo" en el menú contextual; todo en `HKCU`, sin administrador):

```
powershell -ExecutionPolicy Bypass -File install.ps1
powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall
```

Para depurar: `-DCMAKE_BUILD_TYPE=Debug` en otra carpeta (`build-debug`).
Compilar siempre desde "x64 Native Tools Command Prompt for VS 2022" o con el entorno de MSVC cargado.

Después de cada cambio: compilar, corregir **todos** los warnings y errores, y solo entonces dar la tarea por terminada.

## Presupuestos de rendimiento (no negociables)

| Métrica | Objetivo |
|---|---|
| Arranque hasta primer frame visible | < 100 ms |
| Listar carpeta de 10.000 entradas | < 50 ms, sin bloquear la UI |
| Moverse con j/k | respuesta en el mismo frame |
| CPU en reposo | 0 % (no redibujar si nada cambia) |
| Memoria base | < 50 MB |

Si un cambio empeora alguna de estas cifras, avísalo explícitamente.

"Memoria base" hay que leerla como **memoria privada**, no como working set: el working set
cuenta páginas compartidas de DLL de Windows que no son nuestras y se recortan a voluntad
(medido en la fase 6: 67 MB de working set bajan a 5,3 sin que la app note nada). La cifra
privada está hoy en ~54 MB, así que este presupuesto está pendiente de revisar.

## Arquitectura

```
src/
  main.cpp              Punto de entrada (wWinMain), mínimo
  app/                  App: bucle principal, estado global, despacho de comandos; Config
  platform/             Ventana Win32, dispositivo D3D11, DPI, portapapeles
  fs/                   DirectoryReader, ListingCache, DirectoryWatcher, FileOps, rutas
  preview/              Decodificador WIC, vista previa de texto, caché de previews
  ui/                   MillerView, PreviewPane, StatusBar, Theme, Keymap
  core/                 Cola de tareas / pool de hilos, tipos comunes, Diag (log)
third_party/            (solo si algo no puede venir por FetchContent)
```

### Reglas de arquitectura

1. **Separar estado y vista.** El estado (ruta actual, entradas, cursor, selección) vive en `app/`. La UI de ImGui solo lee el estado y emite comandos (`Command::MoveDown`, `Command::EnterDir`...). Nada de lógica de negocio dentro de las llamadas a ImGui.
2. **El hilo de UI nunca toca el disco.** Listar carpetas, decodificar imágenes, leer archivos y operar con archivos ocurre en hilos de trabajo. Los resultados vuelven al hilo de UI por una cola protegida y un `PostMessageW(hwnd, WM_APP_..., ...)` que despierta el bucle.
3. **Resultados obsoletos se descartan.** Cada petición lleva un número de generación. Si el usuario ya navegó a otro sitio, el resultado viejo se ignora.
4. **Bucle guiado por eventos.** Usar `MsgWaitForMultipleObjectsEx` / `WaitMessage`. Redibujar solo ante input, resize, resultado de un hilo o animación activa. Tras cada evento, renderizar 2-3 frames extra para que ImGui se asiente.
5. **Nada de excepciones cruzando límites de hilo ni de API Win32.** Los errores se devuelven como valores (`std::expected` o struct de resultado) y se muestran en la barra de estado.

## Convenciones de Windows

- Definir `UNICODE`, `_UNICODE`, `WIN32_LEAN_AND_MEAN`, `NOMINMAX`.
- Solo APIs `W` (anchas). Rutas como `std::wstring` internamente; convertir a UTF-8 únicamente para pasar texto a ImGui.
- Soportar rutas largas: prefijo `\\?\` para operaciones de disco y `longPathAware` en el manifiesto.
- Manifiesto de la app con DPI **Per-Monitor V2** y `activeCodePage` UTF-8.
- Listado de carpetas: `FindFirstFileExW` con `FindExInfoBasic` y `FIND_FIRST_EX_LARGE_FETCH`.
- Borrar, copiar, mover y renombrar: `IFileOperation` (da Papelera, deshacer y diálogos de conflicto nativos), ejecutado en un hilo propio con COM inicializado en STA.
- Abrir archivos con la app predeterminada: `ShellExecuteExW`.
- CRT estático (`/MT`) para que el .exe no dependa de redistribuibles.

## Estilo de código

- Flags: `/W4 /permissive- /utf-8 /EHsc`; en Release `/O2 /GL` y `/LTCG` al enlazar.
- RAII para todo handle: `HANDLE`, `HFIND`, objetos COM (`Microsoft::WRL::ComPtr`).
- Nombres: `PascalCase` para tipos y funciones, `camelCase` para variables, `m_` para miembros, `k` para constantes.
- Un archivo `.h` + `.cpp` por clase. Funciones cortas. Comentar el *porqué*, no el *qué*.
- No añadir funcionalidades fuera de la fase actual.

## Atajos de teclado (estilo Yazi)

| Tecla | Acción |
|---|---|
| `j` / `k` o flechas | Bajar / subir |
| `h` / `←` | Ir a la carpeta padre |
| `l` / `→` / `Enter` | Entrar en carpeta / abrir archivo |
| `gg` / `G` | Ir al principio / al final |
| `Ctrl+d` / `Ctrl+u` | Media página abajo / arriba |
| `Espacio` | Marcar / desmarcar y avanzar |
| `y` | Copiar (yank) selección |
| `x` | Cortar selección |
| `p` | Pegar |
| `d` | Enviar a la Papelera |
| `D` | Borrar definitivamente (con confirmación) |
| `r` | Renombrar |
| `a` | Crear archivo (terminar en `\` crea carpeta) |
| `/` | Filtrar la carpeta actual |
| `Esc` | Quitar el filtro |
| `.` | Mostrar / ocultar archivos ocultos |
| `~` | Ir a la carpeta de usuario |
| `:` | Ir a una ruta escrita |
| `t` / `1`-`9` / `Ctrl+w` | Nueva pestaña / ir a la pestaña / cerrarla |
| `m` + letra / `'` + letra | Guardar marcador / saltar a él |
| `q` | Salir |

El mapa de teclas vive en `ui/Keymap` como tabla de datos, no como `if` repartidos por el código.
Desde la fase 8 esa tabla se lee de `%APPDATA%\Rayo\config.ini`, sección `[keys]`.

## Tema visual

- Fondo `#1e1e1e`, paneles `#252526`, selección `#264f78`, texto `#d4d4d4`, texto atenuado `#808080`, acento `#4fc1ff`.
- Fuente: Segoe UI desde `%WINDIR%\Fonts\segoeui.ttf`, escalada según el DPI del monitor con
  `style.FontScaleDpi`. Encima se fusionan `seguisym.ttf`, `seguiemj.ttf`, `msyh.ttc` (CJK) y
  `malgun.ttf` (hangul) para que cualquier nombre de archivo se vea; el atlas dinámico de ImGui
  1.92 solo rasteriza los glifos que aparecen en pantalla.
- Layout: 3 columnas, 20 % padre / 40 % actual / 40 % vista previa, más una barra de estado inferior de una línea.

## Fases del proyecto

El plan completo está en `PROMPTS.md`. Estado actual:

- [x] Fase 1 — Ventana, DirectX 11, ImGui y bucle por eventos
- [x] Fase 2 — Lector de carpetas asíncrono y columna central
- [x] Fase 3 — Columnas Miller y navegación
- [x] Fase 4 — Vista previa (imágenes, texto, carpetas)
- [x] Fase 5 — Vigilancia de cambios en disco
- [x] Fase 6 — Operaciones de archivos
- [x] Fase 7 — Filtro, ir a ruta, archivos ocultos, barra de estado
- [x] Fase 8 — Pulido: configuración, pestañas, marcadores, medición de arranque

Al terminar una fase: marcarla aquí, anotar decisiones importantes en la sección siguiente y hacer commit.

## Decisiones y notas

### 2026-09-21 — Fase 1

- **Dear ImGui fijado a `v1.91.9b`** por `FetchContent`, compilado como librería estática
  aparte (`imgui`). Motivo: `/W4` solo se aplica a `rayo`; ImGui no compila limpio con `/W4`
  y no queremos parchearlo ni silenciar warnings en nuestro código.
  *(Subido a `v1.92.9b` en la fase 2; el porqué está en las notas de esa fase.)*
- **Manifiesto como fuente de CMake** (`src/rayo.manifest` en las fuentes de `rayo`).
  link.exe lo fusiona con el `trustInfo` que genera por defecto. Comprobado con
  `mt.exe -inputresource:rayo.exe;#1`: PerMonitorV2, longPathAware y UTF-8 quedan embebidos.
  Si algún día hace falta un `.rc` propio, habrá que usar `/MANIFEST:NO` para no duplicar.
- **Bucle por eventos**: `MsgWaitForMultipleObjectsEx(..., QS_ALLINPUT, MWMO_INPUTAVAILABLE)`
  bloquea mientras no haya frames pendientes. `Window::NeedsRedraw()` decide qué mensajes
  merecen frame; se llama *antes* de pasar el mensaje a ImGui, porque si ImGui consume la
  tecla el frame hace falta igual. `WM_APP`..`WM_APP+0xFF` cuentan como evento: ahí entrarán
  los resultados de los hilos de trabajo (`WM_APP_WAKE` en `app/App.h`).
- **Repintado dentro de `WM_SIZE`**: durante el bucle modal de redimensionado el bucle
  principal está bloqueado en `DefWindowProcW`, así que el frame se dibuja desde el propio
  `WM_SIZE`. Con eso se puede usar `DXGI_SCALING_NONE` (sin estirado) y no parpadea.
- **Tamaño inicial encajado en el área de trabajo del monitor.** 1280x800 DIP son 1600x1000
  px al 125 %, que no caben en un 1080p: la barra de estado quedaba fuera de pantalla.
- **Presupuesto de arranque incumplido, y no por nuestro código.** Medido en esta máquina:
  ~245 ms desde el arranque del proceso hasta ventana visible, de los cuales **~210 ms son
  `D3D11CreateDevice`** (carga del driver de la GPU; medido con un programa aparte que solo
  hace esa llamada). Llegar a los <100 ms del presupuesto exige mostrar la ventana antes de
  crear el dispositivo (pintando el fondo con GDI en el primer `WM_PAINT`) y crear D3D
  después. Pendiente para la fase 8.

Medido tras la fase 1: CPU en reposo 0 ms en 20 s, memoria ~48 MB, exe 518 KB sin
dependencias de redistribuibles (`/MT` confirmado con `dumpbin /dependents`).

### 2026-09-21 — Fase 2

- **Dear ImGui subido a `v1.92.9b`.** Desde 1.92 el atlas de fuentes es dinámico: los glifos se
  rasterizan bajo demanda y ya no hay que declarar rangos por adelantado. Era la única forma de
  cumplir "los nombres con tildes, CJK y emoji se ven bien" sin hinchar el atlas ni el arranque.
  `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` ya estaba puesto, así que los `GetGlyphRanges*` ni existen.
  Efectos colaterales: `ImGuiSelectableFlags_SpanAvailWidth` y `GetContentRegionMax()` ya no
  están (se usan un tamaño explícito en el `Selectable` y `GetCursorScreenPos()`), y
  `Theme::LoadFont` se llama **una sola vez**: un cambio de DPI ahora solo toca
  `style.FontScaleDpi`, sin reconstruir el atlas ni invalidar los objetos de D3D.
- **`IMGUI_USE_WCHAR32` es obligatorio para los emoji.** Con `ImWchar` de 16 bits, ImGui sustituye
  todo lo que pase de U+FFFF por U+FFFD *antes* de buscarlo en la fuente: da igual qué fuente
  cargues, el emoji sale como caja. Se vio en pantalla, no en la teoría.
- **Las fuentes se mapean en memoria, no se leen.** Se fusionan `segoeui.ttf` + `seguisym.ttf` +
  `seguiemj.ttf` + `msyh.ttc` (CJK) + `malgun.ttf` (hangul). Leerlas con `AddFontFromFileTTF`
  serían ~35 MB de heap y se cargaría el presupuesto de memoria. Con `CreateFileMappingW` +
  `MapViewOfFile` y `FontDataOwnedByAtlas = false` solo entran en el working set las páginas de
  los glifos que se usan. Medido: añadir las cuatro fuentes de respaldo cuesta **0 ms** de
  arranque (mejor marca con una sola fuente 253 ms, con las cinco 251 ms). Una fuente que no
  esté se salta en silencio. Emoji en monocromo; en color haría falta el loader de FreeType.
- **El camino de despertar el bucle ya estaba hecho en la fase 1** y no hizo falta tocarlo:
  `Window.cpp` ya trataba `WM_APP..WM_APP+0xFF` como evento que merece frame. El hilo de trabajo
  solo hace `PostMessageW(hwnd, WM_APP_WAKE, 0, 0)` con el HWND **copiado por valor**, porque la
  UI pone `m_hwnd` a null en `WM_DESTROY`.
- **`TaskPool` se declara el último miembro de `App`.** Los miembros se destruyen en orden
  inverso, así que el pool hace join antes de que mueran el mutex y el inbox que sus tareas usan.
- **El prefijo `\?\` desactiva la normalización de rutas**, incluido convertir `/` en `\`.
  Cualquier ruta pasa antes por `NormalizePath` (`GetFullPathNameW`), que además resuelve `.`,
  `..` y las rutas relativas. Sin eso, `rayo.exe C:/Windows/System32` fallaba con
  `ERROR_PATH_NOT_FOUND`.
- **El nombre nunca va como etiqueta de `Selectable`** ni por `ImGui::Text`: un fichero llamado
  `a##b.txt` se cortaría y un `%` se interpretaría como formato. Se pinta con `TextUnformatted`
  encima de un `Selectable` vacío. Igual para la ruta de la barra de estado.
- **Ruta inicial por línea de comandos** (`rayo.exe <ruta>`). Va un poco más allá del enunciado,
  pero sin `h`/`l`/`Enter` (fase 3) no había forma de probar el criterio de las 10.000 entradas.

Medido tras la fase 2, con 12.000 entradas cargadas: memoria 48,4 MB, CPU en reposo 0 ms en 10 s,
exe 663 KB, 0 warnings con `/W4 /permissive-`.

| Métrica | Objetivo | Medido |
|---|---|---|
| Listar 12.000 entradas | < 50 ms para 10.000 | 49 ms (≈41 ms extrapolado a 10.000) |
| Memoria con 12.000 entradas | < 50 MB | 48,4 MB |
| CPU en reposo | 0 % | 0 ms en 10 s |
| Arranque hasta ventana visible | < 100 ms | 251 ms (mejor marca), sin cambio respecto a la fase 1 |

- **El orden natural es el que cuesta, no enumerar.** De esos 49 ms, **45 son el `std::sort` con
  `StrCmpLogicalW`** y solo 4 son `FindFirstFileExW`. Un `std::sort` ordinal normal tarda 5 ms:
  `StrCmpLogicalW` es ~9 veces más lento. Por encima de unas 14.000 entradas se sale del
  presupuesto. Como el orden vive en un hilo de trabajo, la UI nunca se bloquea y no compensa
  escribir un comparador natural propio todavía; queda anotado en `fs/DirectoryReader.cpp`.
- **El presupuesto de arranque sigue incumplido y sigue sin ser culpa nuestra**: los ~210 ms de
  `D3D11CreateDevice` de la fase 1 no han cambiado. La fase 2 no lo empeora de forma medible,
  aunque la dispersión entre ejecuciones en esta máquina (250-410 ms) es demasiado ancha para
  afinar más. Sigue pendiente para la fase 8.
- **Sin verificar**: el cambio de DPI al arrastrar entre monitores con escalados distintos (no hay
  segundo monitor en esta máquina). El camino inicial sí se ve bien.

### 2026-09-21 — Fase 3

- **La ruta sustituye al número de generación.** Cada `DirectoryListing` ya llevaba su `path`, y
  ahora hay dos columnas cargando a la vez, así que un solo contador global no llegaba. Un
  resultado se aplica a la columna cuya ruta coincide exactamente; si el usuario ya se fue, no
  coincide ninguna y el listado solo engorda la caché. Es más fuerte que la generación (descarta
  por *contenido equivocado*, no por *antigüedad*) y quita un miembro. El criterio de "teclear muy
  rápido nunca muestra la carpeta equivocada" sale de aquí, no del orden de llegada.
- **La caché LRU es la que quita el parpadeo, no un "seed" de la columna anterior.** Todo listado
  que llega se guarda por ruta; al navegar, cada columna se sirve al instante de la caché y a la
  vez se relanza la lectura. Como al bajar el padre ya estaba en pantalla (y por tanto en caché) y
  al subir la carpeta actual también, ir y volver no tiene ni un frame vacío. La única lista vacía
  posible es la primera visita a una carpeta nueva, que no se puede evitar.
- **`EntryList` es un `shared_ptr<const vector<DirectoryEntry>>`.** Cambiar de carpeta mueve un
  puntero; copiar 12.000 entradas por navegación habría costado ~2 ms y el doble de memoria. Como
  efecto secundario, una columna sobrevive a que su listado sea desalojado de la caché.
- **La caché tiene dos topes: 32 carpetas y 50.000 entradas.** Solo con el de 32, treinta y dos
  carpetas de 12.000 entradas serían ~54 MB y el presupuesto entero son 50.
- **El cursor se sigue por nombre, no por índice.** Cada columna guarda el nombre que debe quedar
  seleccionado; al aplicar un listado se busca ese nombre. Así un refresco con archivos nuevos o
  borrados no mueve la selección, y la memoria de cursor por carpeta (`ruta -> nombre`) usa el
  mismo mecanismo. Bajar anota en la memoria del padre por dónde se bajó, así que `h` no necesita
  ningún caso especial: restaura como cualquier otra vuelta a una carpeta ya visitada.
- **La lista de unidades es la ruta vacía.** `ReadDirectory("")` fabrica el listado con
  `GetLogicalDriveStringsW`, así que caché, memoria de cursor, columnas y navegación funcionan sin
  tocar nada. El nombre de la entrada es `"C:"` (lo que unen `JoinPath` y la memoria) y la etiqueta
  del volumen va solo en `nameUtf8`, que es lo único que se pinta. `JoinPath("", "C:")` da `"C:\"`
  y no `"C:"`, que para Win32 significaría *el directorio actual de esa unidad*.
  `SetThreadErrorMode(SEM_FAILCRITICALERRORS)` alrededor de `GetVolumeInformationW`: sin eso una
  unidad extraíble vacía saca el diálogo "Inserte un disco" desde un hilo de trabajo.
- **`~` es un atajo de carácter, no de tecla.** `ImGuiKey_GraveAccent` es VK_OEM_3, que en un
  teclado español es la `ñ`: `Shift+ñ` habría disparado "ir a casa". Mirando
  `io.InputQueueCharacters` el atajo funciona en cualquier distribución. La tabla `kCharBindings`
  ya sirve para `/`, `.` y `:` de la fase 7.
- **`Binding` lleva ahora un campo `repeat`.** Mantener `j` pulsado baja de forma continua, pero
  mantener `l` no debe bucear tres carpetas por los 3 frames que se pintan por evento.
- **Peticiones deduplicadas por ruta (`m_inFlight`).** Entrar y salir a lo bruto encolaba la misma
  lectura una y otra vez por delante de la carpeta a la que el usuario acababa de llegar. Probado
  con 30 vueltas `l`/`h` sin pausa: acaba en la carpeta correcta, con el cursor donde tocaba.
- **Abrir archivo: `ShellExecuteExW` en un hilo del pool**, con COM en STA (sus extensiones lo
  usan) y `SEE_MASK_NOASYNC` porque el hilo vuelve al pool y deja de bombear mensajes. Sin `hwnd`
  padre: los diálogos del shell son de otro hilo y no queremos que deshabiliten la ventana desde
  fuera del hilo de UI. Verificado: un `.txt` sin asociación saca el "Elegir una aplicación"
  nativo. Los errores van a la barra de estado por un segundo buzón (`m_messages`).
- **`tests/path_check.cpp`**: asserts sobre `ParentPath` / `JoinPath` / `LastComponent`, que son la
  única lógica de la fase que no se ve en pantalla hasta que ya te ha llevado a otro sitio (raíz de
  unidad, raíz virtual, recursos de red). `cmake --build build` lo compila; se ejecuta con
  `build\rayo_path_check.exe` y no imprime nada si todo va bien.
- **`fs/FileOps` sigue sin existir.** Abrir con la app predeterminada son 15 líneas dentro de
  `App::Open`; el archivo se creará en la fase 6, cuando haya copiar, mover y borrar que meter.

Medido tras la fase 3: memoria 43,8 MB recién abierto y 46,1 MB tras pasear por `C:\Windows` y
volver varias veces (caché incluida), CPU en reposo 0 ms en 8 s, 0 warnings con `/W4 /permissive-`.
Sin cambios en el arranque: los ~210 ms de `D3D11CreateDevice` siguen ahí y siguen pendientes para
la fase 8.

- **ponytail: sin dedupe por tiempo.** Volver a una carpeta siempre relanza la lectura, aunque se
  acabe de leer hace medio segundo. Con la caché sirviendo al instante no se nota, y la fase 5
  (vigilante de disco) sustituye este refresco por completo. Anotado en `App::Request`.
- **Sin verificar**: unidades de red y recursos UNC (no hay ninguno en esta máquina); el camino de
  `ParentPath` para `\\servidor\recurso` solo está cubierto por los asserts.

### 2026-09-21 — Fase 4

- **La preview de una carpeta es una tercera `Pane`, no código nuevo.** `SetPane(m_preview, ...)`
  la sirve de `ListingCache` al instante, la relee por detrás y `DrainResults` la aplica por el
  mismo bucle que las otras dos columnas (`for (Pane* pane : {&m_current, &m_parent,
  &m_preview})`). El descarte por ruta de la fase 3 vale igual. Se dibuja con
  `MillerView::DrawEntries(rows, -1, dummy)`: cursor −1 = ninguna fila resaltada, porque es una
  vista y no una columna navegable.
- **Sin tablas de extensiones.** `LoadPreview` prueba WIC (`CreateDecoderFromFilename` devuelve
  `COMPONENTNOTFOUND` rápido y sin leer más que la cabecera), luego mira el contenido, y si no,
  metadatos. Cubre png/jpg/gif/bmp/tiff/webp **y heic/avif/jxl en cuanto el códec esté
  instalado**, sin lista que se desincronice, y acierta con un `.jpg` mal nombrado. A cambio se
  sondea con WIC todo archivo que no es imagen; es leer su cabecera en un hilo de trabajo.
- **El retardo de 60 ms es el timeout de `MsgWaitForMultipleObjectsEx`, no un `SetTimer`.** Seis
  líneas en `App::Run`, sin tocar `Window`, sin otro caso en `NeedsRedraw` y sin `KillTimer` que
  olvidar. En reposo `m_previewDue` es 0 y la espera vuelve a `INFINITE`: el 0 % de CPU se
  conserva (medido: 0 ms en 12 s con una foto en pantalla). La resolución de `GetTickCount64`
  (~15,6 ms) deja el retardo real entre ~45 y ~76 ms.
- **`UpdatePreview()` se llama una vez por frame y ya está.** Cubre los tres motivos por los que
  cambia lo que hay bajo el cursor (moverlo, navegar, y un listado que llega por detrás) sin
  ganchos repartidos, porque `WM_APP_WAKE` ya provoca frame.
- **Lo que hace fluido recorrer fotos no es el retardo, es servir la caché sin él.** Un acierto de
  `PreviewCache` se pinta en el mismo frame; solo espera lo que cuesta un hilo. Ir y volver sobre
  las mismas veinte fotos no vuelve a decodificar ni una.
- **Generación para archivos, ruta para carpetas.** `m_previewGen` (atómica) sube en cada cambio
  de selección: el hilo la mira *antes* de abrir nada (la única cancelación posible, ni WIC ni
  `ReadFile` se abortan a medias) y `DrainResults` antes de mostrar. Un resultado tardío se
  guarda igual en la caché — un decode pagado no se tira — pero no se pinta. Las carpetas siguen
  con la coincidencia de ruta de la fase 3; duplicar el mecanismo no aportaba nada.
- **`32bppBGRA`, alfa recto y no premultiplicado.** El blend del backend DX11 es
  `SRC_ALPHA/INV_SRC_ALPHA` (`imgui_impl_dx11.cpp:541`); con `PBGRA` los PNG transparentes salen
  con halo. Verificado en pantalla con un círculo sobre fondo oscuro.
- **La orientación EXIF se lee antes de escalar.** Si transpone (5..8), el tamaño de destino del
  scaler lleva ancho y alto intercambiados. Escalar primero y rotar después evita rotar la imagen
  a resolución completa. Con JPEG el scaler tira por debajo del escalado por DCT del
  decodificador, así que ni siquiera se decodifica entera.
- **El texto se recorta a 200 líneas de 2000 caracteres, y no es cosmético.** `ImGui::TextEx` sí
  se salta las líneas por encima del clip rect, pero llama a `CalcTextSize` sobre ellas para
  calcular el ancho (`imgui_widgets.cpp:219`): O(texto) por frame. 200 líneas son ~12 KB y no se
  notan; 64 KB en crudo costarían 1-2 ms cada frame, todos los frames.
- **`targetPx` se redondea a múltiplos de 256** para que redimensionar la ventana pixel a pixel no
  esté rehaciendo la imagen. Si el panel crece por encima de lo decodificado y el original daba
  para más (`downscaled`), se rearma el plazo y se vuelve a decodificar. Verificado maximizando
  sobre una foto de 2400x1600: se rehace y se ve nítida.
- **`ImTextureRef(void*)` vive tras `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`**, que está puesto desde la
  fase 1, así que el cast a `ImTextureID` (un `ImU64`) va a mano en `PreviewPane`.
- **`SetThreadErrorMode(SEM_FAILCRITICALERRORS)` ahora también en `ReadDirectory`.** Estaba solo
  en `ReadDrives`: desde esta fase basta pasar el cursor por encima de una unidad extraíble vacía
  para leerla, sin entrar, y el diálogo "Inserte un disco" saldría desde un hilo de trabajo.
- **Los errores de preview no van a la barra de estado.** Un archivo que no se puede leer cae en
  la vista de metadatos, que sí funciona (`GetFileAttributesExW` no necesita permiso de lectura).
  Es menos código y menos ruido que un mensaje por cada fila por la que pasas.
- **`tests/preview_check.cpp`**: asserts sobre `DecodeTextPreview`, que es lo único de la fase que
  no se ve en pantalla hasta que ya ha decidido mal. Cubre los tres BOM, NUL → binario, UTF-8
  inválido, la secuencia partida por el corte de 64 KB, y los dos topes de recorte.

Medido tras la fase 4, sobre una carpeta de 300 fotos de 2400x1600:

| Métrica | Objetivo | Medido |
|---|---|---|
| Memoria recorriendo 290 fotos, 3 pasadas | acotada | 50,1 / 49,7 / 50,0 MB de working set — plana |
| CPU en reposo con una imagen en pantalla | 0 % | 0 ms en 12 s |
| Latencia de la vista previa (tecla → imagen) | — | 50-66 ms, un pico de 130 |
| Arranque hasta ventana visible | < 100 ms | 211 ms mejor, 231 mediana (sin cambio) |
| Warnings con `/W4 /permissive-` | 0 | 0 |

- **El working set no se mueve, pero el *commit* sube a ~150-160 MB** desde los ~60 del arranque,
  y ahí se queda pasada la décima foto (comprobado hasta 131 seguidas y con tres pasadas enteras
  de 290). El tope de la caché cuenta 64 MB de texturas; el resto es que D3D mantiene su propia
  copia y que el asignador no devuelve páginas. Acotado y estable, que es el criterio, pero
  conviene saberlo: `kMaxBytes` en `PreviewCache.cpp` es la palanca.
- **La primera decodificación de la sesión puede tardar bastante más** (se vio medio segundo la
  primera vez): `windowscodecs.dll` no está en las importaciones del exe —`dumpbin /dependents`
  lo confirma—, lo carga COM en el primer `CoCreateInstance`. No se ha optimizado: pagarlo al
  arrancar iría contra el presupuesto de arranque, que ya está incumplido.
- **Las líneas largas se cortan, no se envuelven.** Un JSON minificado enseña los primeros ~80
  caracteres y nada más. Es lo que hace Yazi y lo que pide el enunciado ("las primeras líneas"),
  pero es la limitación más visible de la fase.
- **Los textos heredados en Latin-1 sin BOM caen en la vista de metadatos**: sin BOM solo se
  acepta UTF-8 válido. Anotado en `DecodeTextPreview`; la salida es probar `CP_ACP` antes de
  rendirse.
- **Sin verificar**: HEIC/AVIF (no hay códec instalado en esta máquina), orientaciones EXIF 5 y 7
  (las transpuestas raras; la 6 sí, en pantalla), unidades extraíbles y de red. exe 705 KB, sin
  dependencias nuevas de redistribuibles.

### 2026-09-21 — Fase 5

- **El vigilante no dice *qué* cambió, solo *qué carpeta*.** La respuesta a cualquier aviso es
  releer la carpeta entera, así que descodificar `FILE_NOTIFY_INFORMATION` no aportaba nada: el
  buffer no se lee nunca. Como efecto, que se desborde deja de ser un caso especial (llega con 0
  bytes, y eso también significa "algo cambió"), y 1 KB de buffer sobra.
- **Un solo hilo para todas las carpetas, con APCs en vez de cerrojos.** El hilo duerme en
  `SleepEx(INFINITE, TRUE)` y despierta solo para las APCs con las que la UI le pasa trabajo y
  para las rutinas de terminación de `ReadDirectoryChangesW`. Todo el estado del vigilante (la
  lista de carpetas, los handles) lo tocan únicamente cosas que corren en ese hilo y de una en
  una: cero mutex, cero eventos que administrar. Un hilo por carpeta habría obligado a hacer join
  desde el hilo de UI en cada navegación.
- **El handle solo se cierra en la rutina de terminación.** `CancelIoEx` no espera: hasta que
  llega la terminación la E/S sigue viva y el kernel puede estar escribiendo en el `OVERLAPPED`.
  Al cancelar, el `Entry` sale de la lista y pasa a ser propiedad de la E/S pendiente; muere en
  `Completion`. `Stop` no sale mientras quede uno vivo, y por eso hace falta la marca `abandoned`
  y no basta con mirar `ERROR_OPERATION_ABORTED`: si la lectura ya había terminado, `CancelIoEx`
  falla con `ERROR_NOT_FOUND` y la terminación llega con éxito, no cancelada.
- **El criterio del cursor ya estaba escrito en la fase 3.** `ApplyListing` busca el nombre
  guardado y, si no está, deja el cursor en la misma posición numérica. El refresco del vigilante
  pasa por `Request` como cualquier otro, así que hereda eso, el descarte por ruta y el dedupe de
  peticiones en vuelo. La fase 5 no añadió ni una línea de lógica de cursor.
- **El plazo de 100 ms es el mismo mecanismo que el de la vista previa**: un `m_refreshDue` más
  en el `timeout` de `MsgWaitForMultipleObjectsEx`. Ventana fija, no deslizante: copiar mil
  archivos refresca cada ~100 ms en vez de no refrescar hasta que la copia termine.
- **Se despierta la UI con el primer aviso de cada tanda, no con todos.** El `PostMessageW` solo
  sale si la ruta no estaba ya en el buzón. Sin eso, copiar mil archivos serían mil despertares y
  tres mil frames.
- **Solo se vigilan las dos columnas navegables.** La de la vista previa cambia con cada `j`/`k`
  y abrir y cerrar un handle por pulsación no compensa para algo que se mira de pasada. En la
  raíz virtual (lista de unidades) no se vigila nada: meter y sacar un USB no se ve solo.
- **`LongPath` sale de `DirectoryReader.cpp` a la cabecera.** Era el prefijo `\\?\` privado de
  `MakeSearchPattern`; ahora `CreateFileW` del vigilante usa el mismo y las rutas largas siguen
  funcionando en los dos sitios.
- **`tests/watch_check.cpp`**: crea una carpeta temporal y comprueba lo único que no se ve en
  pantalla: que avisa con la ruta de la carpeta, que dejar de vigilar corta los avisos de verdad
  (no un flag que los ignore) y que 200 ciclos de vigilar/desvigilar no suben el recuento de
  handles del proceso. El propio destructor es parte de la prueba: si dejara una E/S viva, se
  colgaría ahí.

Medido tras la fase 5:

| Métrica | Objetivo | Medido |
|---|---|---|
| Crear, borrar y renombrar desde fuera, hasta verse | < 500 ms | visible a los 400 ms (las tres a la vez) |
| CPU en reposo, dos carpetas tranquilas vigiladas | 0 % | 0-15,6 ms en 15 s — igual que la fase 4 |
| 60 idas y venidas `h`/`l` seguidas | sin fugas | handles 573 → 575, hilos 67 → 67 |
| Warnings con `/W4 /permissive-` | 0 | 0 |

- **El suelo de CPU en reposo (~15,6 ms cada 15 s, un tick del reloj) ya estaba en la fase 4.**
  Medido con las dos versiones a la vez sobre la misma carpeta: idénticas. El vigilante dormido
  en `SleepEx` no cuesta nada.
- **Con una carpeta que no para, sí se nota: ~250 ms de CPU cada 15 s frente a ~40.** Medido con
  `%TEMP%` (6.091 entradas) como columna padre, que recibe escrituras todo el rato. Es
  proporcional al trajín real del disco, no un bucle: cada aviso agrupado cuesta una relectura
  con su `std::sort`. Si molestara, la salida es un mínimo entre refrescos de la misma carpeta,
  no subir `kWatchDebounceMs`. Anotado en `App.cpp`.
- **Borrar la carpeta en la que estás no cierra la columna**: el listado viejo se queda en
  pantalla y el error va a la barra de estado. La columna padre sí se entera y la quita de su
  lista. No se ha hecho nada más porque "a dónde ir" es una decisión de la fase 7 (`:` para ir a
  una ruta).
- **Sin verificar**: unidades de red y recursos UNC (no hay ninguno en esta máquina).
  `ReadDirectoryChangesW` no funciona sobre algunos sistemas de archivos remotos; el código lo
  trata como "sin vigilancia" y la app se comporta como en la fase 4, pero no se ha podido
  probar. Tampoco los volúmenes extraíbles.

### 2026-09-21 — Fase 7

- **Cada columna tiene ahora una `view` al lado de `entries`.** El filtro y los ocultos no
  tocan el disco ni la caché: `RebuildView` deja en `view` lo que se pinta, y el cursor, el
  contador y `Selected()` se mueven sobre eso. Si no sobra nada que esconder, `view` es el
  mismo `shared_ptr` que `entries` y no se copia ni una entrada. Como consecuencia, quitar el
  filtro no relee nada y la caché sigue guardando la carpeta entera.
- **El cursor por nombre de la fase 3 vale igual para el filtro.** `PlaceCursor` sale de
  `ApplyListing` y ahora lo llaman los dos caminos: cuando llega un listado y cuando cambia
  el filtro. Estrechar el filtro deja el cursor donde estaba mientras lo que había siga
  pasando, y Esc lo devuelve a su sitio en la lista entera (visto: `5/47` al quitar el filtro
  con el cursor sobre `Canción.mp3`). La fase 7 no añadió lógica de cursor, solo movió la que
  ya había.
- **`FindNLSStringEx` es el filtro entero.** Con `LINGUISTIC_IGNORECASE |
  LINGUISTIC_IGNOREDIACRITIC` la búsqueda de subcadena del sistema ya ignora mayúsculas y
  tildes: "cancion" encuentra `Canción.mp3` y "KERNEL32" encuentra `kernel32.dll`, sin
  normalizar a mano ni guardar una copia "plana" de cada nombre. **10.000 nombres en 6,2 ms**
  (`rayo_filter_check`), así que una tecla del filtro cabe de sobra en un frame.
- **Ocultos = `HIDDEN` o `SYSTEM`**, que es lo que ya decía `DirectoryEntry::IsHidden` desde
  la fase 2 (MillerView los pinta en gris desde entonces). Esta fase solo decide si se ven.
- **El autocompletado de `:` no toca el disco.** Tab es `ImGuiInputTextFlags_CallbackCompletion`,
  el mecanismo del propio widget, y ahí dentro solo se puede mirar `ListingCache`. Si la
  carpeta no está, se pide por el camino normal (`Request`, ya fuera de ImGui) y el Tab
  siguiente completa: al escribir `C:\Wind`, el primer Tab no hace nada visible y el segundo
  deja `C:\Windows\`. La carpeta actual y sus vecinas están siempre en caché, que es el caso
  normal.
- **Completa con el prefijo común y no cicla.** Ciclar obliga a guardar la lista de
  candidatos y por dónde iba; el prefijo no guarda nada, y con una sola coincidencia completa
  el nombre entero y le añade la barra para seguir bajando. `Doc` con `Documentos` y
  `Documentales` delante deja `Document`.
- **`NormalizePath` quita ahora la barra final** (menos en la raíz de una unidad, donde `C:`
  significaría el directorio actual de esa unidad). Escribir una ruta con barra al final
  creaba una segunda clave de caché para la misma carpeta, y con `:` eso pasa a ser lo normal.
  Se arregla en la función por la que pasan todas las rutas, no en quien la llama.
- **El espacio libre viaja dentro de `DirectoryListing`.** `GetDiskFreeSpaceExW` se llama en
  el mismo hilo de trabajo y dentro del mismo `SetThreadErrorMode` que el listado, así que no
  hay buzón nuevo ni hilo nuevo: son cuatro líneas. Se aplica solo cuando el listado es el de
  la columna actual, y `Navigate` lo pone a cero para no enseñar el de la unidad anterior.
- **Los mensajes caducan a los 5 s**, con el mismo `timeout` de `MsgWaitForMultipleObjectsEx`
  que ya usaban la vista previa y el vigilante. Al caducar vuelve a `INFINITE`: el 0 % en
  reposo se conserva.
- **`FormatBytes` y `FormatTime` bajan a `fs/DirectoryReader`.** Los tenían por separado la
  lista (`StrFormatByteSizeW` a mano) y la vista previa (`GetDateFormatEx` + `GetTimeFormatEx`
  a mano), y la barra de estado habría sido el tercero.
- **El bloque de la derecha no se pinta si no cabe**, igual que los tamaños de la lista: con
  una ruta larga desaparece en vez de montarse encima. Y mientras hay un campo abierto
  tampoco se pinta: el campo se come lo que queda de línea a propósito.

Medido tras la fase 7:

| Métrica | Objetivo | Medido |
|---|---|---|
| Filtro sobre 10.000 nombres | instantáneo | 6,2 ms (una sola pasada, en el hilo de UI) |
| CPU en reposo | 0 % | 31,3 ms en 80 s — 2 ticks del reloj, igual que antes |
| Memoria recién abierto | < 50 MB | 46,4 MB de working set (52,7 privada) |
| Warnings con `/W4 /permissive-` | 0 | 0 |

- **Lo que cuesta el filtro no es comparar, es copiar.** Los 6,2 ms de 10.000 nombres son
  `FindNLSStringEx`; encima va una copia de las entradas que pasan (~3 ms para 12.000). Las
  dos cosas ocurren en el hilo de UI y por tecla, y juntas siguen cabiendo en un frame. Si
  algún día molestara, la salida es un vector de índices, a cambio de tocar todo lo que hoy
  recibe un vector plano de entradas. Anotado en `App::RebuildView`.
- **Con los ocultos escondidos se copia siempre**, porque casi toda carpeta de Windows tiene
  algún `desktop.ini`. El atajo de "no sobra nada, comparto el puntero" solo salta con `.`
  activado y sin filtro.
- **La columna padre puede quedarse sin resaltar** si se entra en una carpeta oculta con los
  ocultos escondidos (con `:`, por ejemplo): el nombre no está en su `view` y el cursor se
  queda donde estaba. Se ve raro pero no engaña: lo que hay en pantalla es lo que hay.
- **El exe pasa de 768 a 788 KB**, sin dependencias nuevas: el filtro, las fechas y el
  espacio libre son kernel32 y shlwapi, que ya se enlazaban.
- **Sin verificar**: unidades de red y recursos UNC (ni el espacio libre ni el completado de
  `\\servidor\...`, que no tiene listado en caché de donde tirar); y el filtro sobre nombres
  en árabe o hebreo, donde `FindNLSStringEx` hace más cosas de las que se han probado.

### 2026-09-21 — Fase 6

- **`fs/FileOps` es una sola función.** `RunFileOp(op, sources, dest, name)` cubre copiar,
  mover, Papelera, borrar, renombrar y crear, porque `IFileOperation` es la misma secuencia
  en los seis casos: `SetOperationFlags`, encolar y `PerformOperations`. Se encola **elemento
  a elemento** (`CopyItem` y no `CopyItems`): los diálogos son idénticos y ahorra construir
  un `IShellItemArray`.
- **La Papelera es un flag, y el borrado definitivo son dos.** `FOF_ALLOWUNDO |
  FOFX_RECYCLEONDELETE` para `d`; para `D`, quitar `FOF_ALLOWUNDO` (eso lo hace definitivo) y
  añadir `FOF_NOCONFIRMATION`, porque el popup de ImGui ya ha preguntado y el shell
  preguntaría otra vez. Verificado en pantalla: `d` deja el archivo restaurable desde la
  Papelera con su ruta de origen; `D` lo borra y no aparece por ninguna parte.
- **`FOF_*` está en `shellapi.h`, no en `shobjidl`.** `WIN32_LEAN_AND_MEAN` lo deja fuera de
  `Windows.h` y el error es un `C2065` que no sugiere el include. Los `FOFX_*` sí entran por
  `shlobj.h`.
- **Sin `SetOwnerWindow`, por lo mismo que `ShellExecuteExW` en la fase 3.** Comprobado con
  el diálogo de conflicto ("Reemplazar u omitir archivos") delante:
  `IsWindowEnabled(ventana principal)` sigue devolviendo `TRUE`. Con ventana padre el shell
  la deshabilitaría desde otro hilo, que es justo lo que rompería "la UI sigue respondiendo".
  El precio es que el diálogo sale como ventana independiente, no centrado sobre Rayo.
- **Las rutas van sin el prefijo `\\?\`**: las APIs del shell no lo aceptan. Lo que cubre las
  rutas largas aquí es el `longPathAware` del manifiesto, no `LongPath()`.
- **Las marcas son rutas completas y globales.** Se puede marcar en tres carpetas y pegar en
  la cuarta. Se pintan en la columna actual **y** en la padre sin código extra, porque
  `DrawEntries` ya recibe la carpeta de cada columna. Se vacían al lanzar cualquier
  operación: `y`/`x` ya se llevaron las rutas al portapapeles, y `d`/`D`/`r` las dejarían
  apuntando a lo que ya no existe.
- **La marca se pinta *después* del `Selectable` y translúcida** (`Theme::kMarked`, el acento
  al 22 %). Al revés, la selección opaca taparía la marca de la fila bajo el cursor y no se
  vería que está marcada.
- **`SetKeyboardFocusHere` no activa el widget en ese frame.** La petición de navegación se
  resuelve al final del frame y el `InputText` se activa en el siguiente, así que armar
  `CallbackAlways` solo en el frame del foco no preselecciona nada: **se vio en pantalla**,
  con el cursor al final y sin selección. La preselección sigue armada hasta que el callback
  corre de verdad (solo corre con el campo activo) y se desarma a sí misma desde dentro.
- **El campo lleva `FramePadding` a cero.** Con el padding normal el `InputText` es más alto
  que una fila y el resto de la lista se desplaza al renombrar.
- **Renombrar y crear dejan el cursor donde estaba el archivo, no donde estaba el cursor.**
  `Follow()` anota el nombre nuevo en `m_current.select` y en la memoria de cursor *antes* de
  lanzar la operación, así que el refresco lo encuentra por el mecanismo de la fase 3. Sin
  eso, renombrar `archivo1.txt` a `zzz.txt` dejaba el cursor en la posición 2 y el siguiente
  `x` cortaba el archivo equivocado — pasó durante las pruebas.
- **El refresco no depende solo del vigilante.** `Report` admite una carpeta "sucia" que
  entra por el mismo buzón que los avisos de `ReadDirectoryChangesW`, así que hereda el
  descarte de caché y el agrupado de 100 ms. Son tres líneas y garantizan el criterio
  "tras cada operación la vista se actualiza" también donde el vigilante no funciona.
- **`BuildUi` no ejecuta nada.** El popup y el campo de texto solo marcan la decisión
  (`m_answer`, `m_edit.result`) y `CommitEdits()`, que se llama justo después de `BuildUi`,
  es quien opera. Mantiene la regla de "nada de lógica de negocio dentro de las llamadas a
  ImGui" sin inventar comandos que ninguna tecla emite.
- **El portapapeles se lee "copiado: 2", no "2 copiados".** El resultado de la última
  operación se pinta al lado y con la misma forma la barra decía "1 copiados 1 copiado".
- **`tests/fileops_check.cpp`**: `StemLength` (lo que se preselecciona al renombrar) y
  `SplitNewName` (la barra final que convierte `a` en "crear carpeta"). Son las dos únicas
  reglas de la fase que no se ven en pantalla hasta que ya han decidido mal.

Medido tras la fase 6:

| Métrica | Objetivo | Medido |
|---|---|---|
| Copiar 600 MB (40 archivos) sin congelar | UI viva | `Responding=True`, `j`/`k` responden durante la copia |
| `d` a la Papelera y restaurable | sí | verificado en la Papelera, con ruta de origen |
| Vista actualizada tras cada operación | sí | inmediata en las siete operaciones |
| CPU en reposo (recién abierto / tras operar) | 0 % | 15,6 ms / 0,0 ms en 15 s |
| Memoria recién abierto | < 50 MB | 46,3 MB de working set (53,8 privada) |
| Warnings con `/W4 /permissive-` | 0 | 0 |

- **La primera operación sube el working set 20 MB, pero de memoria de verdad son 2,6.**
  `IFileOperation` carga el motor de copia del shell dentro de nuestro proceso y ahí se
  queda. Medido con una sola operación sobre un archivo: working set 47,7 → 67,2 MB, pero
  **memoria privada 53,8 → 56,4**. La diferencia son páginas compartidas de DLL del shell,
  ya residentes para el Explorador. Tampoco es proporcional al trabajo: 10 operaciones más
  solo suman 2,7 MB de working set, 7 hilos y 30 handles (el pool del shell asentándose).
- **El presupuesto de 50 MB está medido sobre la cifra equivocada.** `SetProcessWorkingSetSize
  (GetCurrentProcess(), -1, -1)` deja el working set en **5,3 MB** con la app entera
  funcionando, y seguir usándola solo lo sube a 6,9; la memoria privada no se mueve. Es
  decir: el working set aquí es casi todo recortable y no mide lo que el presupuesto quería
  medir. La cifra honesta es la privada, y esa ya está en **53,8 MB antes de operar** — o sea
  que el presupuesto habría que revisarlo por su cuenta, no por culpa de esta fase. Si algún
  día molesta el número, recortar el working set tras cada operación es una línea, pero es
  maquillaje: baja el número sin liberar RAM y la operación siguiente vuelve a traer las
  páginas.
- **Sacar las operaciones a un proceso auxiliar no sale a cuenta por memoria.** Medido con un
  exe aparte que hace lo mismo (COM en STA + `IFileOperation` + una operación real):
  **87-93 ms** de arranque a salida, contra los **~5 ms** que cuesta hoy dentro del proceso a
  partir de la segunda operación. Son ~85 ms de sobrecoste en cada `d`, contra un presupuesto
  que exige el mismo frame para moverse. Y habría que inventar un formato de serialización en
  los dos sentidos (la línea de comandos no vale: tope de 32K y comillas dentro de los
  nombres), no ahorraría ni un hilo (alguien tiene que esperar al proceso) y los diálogos del
  shell dejarían de ser ventanas de Rayo. Un auxiliar *persistente* es peor: mata los 85 ms
  pero los 19 MB solo se mudan de proceso.
  **Lo que sí compraría es aislamiento**: el motor de copia carga extensiones de terceros
  (nube, antivirus, compresores) dentro de nuestro proceso, y hoy una que se cuelgue se lleva
  el gestor de archivos. Ese, y no la memoria, sería el motivo para hacerlo.
- **El exe pasa de 705 a 768 KB.** 63 KB es `imgui_stdlib.cpp` más `FileOps` y `EditField`.
  Sigue sin dependencias nuevas de redistribuibles: `shell32` y `ole32` ya se enlazaban desde
  la fase 3.
- **Sin verificar**: unidades de red y recursos UNC; rutas de más de `MAX_PATH` a través del
  shell; el deshacer del shell (Ctrl+Z en el Explorador tras una operación hecha desde Rayo);
  y qué pasa si se cierra Rayo con una copia grande a medias — el pool hace join, así que el
  cierre esperaría a que termine.

### 2026-09-21 — Fase 8

- **`config.ini` y no `config.toml`: `GetPrivateProfileStringW` es el parser entero.** Escribir
  uno de TOML o de JSON serían cien líneas para leer veinte claves, y el enunciado dejaba
  elegir "lo más simple sin dependencias". Lo más simple ya venía con Windows.
  `GetPrivateProfileSectionW` devuelve una sección entera como `clave=valor\0...\0\0`, que es
  justo lo que hacen falta para colores, atajos y marcadores. **Medido: 0,6 ms** leer las cinco
  secciones al arrancar.
- **El archivo se crea en UTF-16 con BOM a mano, y eso no es cosmético.** Las funciones `W` del
  perfil solo escriben Unicode si el archivo *ya* lo es; si lo crea `WritePrivateProfileStringW`
  sale en ANSI y un marcador a `C:\Users\Canción\音楽` se pierde sin ruido. Es lo que comprueba
  `tests/config_check.cpp`, porque es lo único de la fase que no se ve en pantalla hasta que ya
  ha decidido mal.
- **El config por defecto lo escriben las mismas tablas que usa el programa.** Los colores salen
  de `Theme::Colors()` (nombre + puntero) y los atajos de `Keymap::Defaults()`, que reconstruye
  cada línea con `ImGui::GetKeyName`. Así no hay dos listas de hexadecimales ni dos listas de
  teclas que desincronizar, y la sección `[keys]` del archivo es exactamente la tabla de fábrica.
- **`[keys]` sustituye a la tabla entera, no la parcha.** Borrar una línea quita ese atajo, que
  es lo que espera cualquiera que edite el archivo. Las líneas que no se entienden se cuentan y
  se dicen en la barra ("config: 1 atajo sin entender") en vez de desaparecer.
- **Los dígitos de las pestañas son teclas físicas, no caracteres, y por un motivo concreto:**
  ImGui llama `"1"` a la tecla 1. Un atajo de carácter `'1'` escrito en el config vuelve como
  tecla al releerse, así que la app se comportaría distinto con config que sin él. Moverlos a la
  tabla de teclas hace que los dos caminos coincidan. `/`, `.`, `:`, `~` y `'` siguen siendo
  caracteres porque ImGui llama a esas teclas `Slash`, `Period`... y no chocan.
  **Se vio en pantalla**: `1` no cambiaba de pestaña hasta arreglarlo.
- **`Home` y `End` son atajos nuevos porque un comando sin atajo de fábrica no se puede nombrar
  en el config.** `MoveTop` solo salía de `gg`, que está escrito a mano en `Poll`.
- **Una pestaña es su carpeta y nada más.** El cursor ya lo recuerda `m_cursorMemory` por ruta
  desde la fase 3, así que cambiar de pestaña lo restaura solo: `std::vector<std::wstring>` y un
  índice, sin estado por pestaña que mantener. `Navigate` escribe la ruta en la pestaña activa y
  con eso basta. `Ctrl+w` sobre la última no hace nada: cerrar la última sería salir, y para eso
  está `q`.
- **`m` y `'` dejan a `Poll` esperando la letra.** `State::pending` guarda el comando y la tecla
  siguiente llega por `InputQueueCharacters`; Escape o cualquier cosa que no sea letra o dígito
  cancela. Es el mismo mecanismo que la primera `g` de `gg`, un campo más.
- **La geometría se guarda con `GetWindowRect` y solo se recurre a `WINDOWPLACEMENT` si está
  maximizada** (que es el único caso en el que `GetWindowRect` no dice el tamaño al que hay que
  volver). Al restaurar, `MonitorFromRect(..., MONITOR_DEFAULTTONULL)`: con el monitor secundario
  desenchufado la ventana se abriría fuera de toda pantalla y no habría forma de traerla.
- **`Placement()` se cachea en `WM_DESTROY`.** Cerrar con la X destruye la ventana *antes* de que
  el bucle salga, así que preguntar por su posición al guardar ya no devolvería nada. Se guarda
  al final de `Run`, no en el destructor, por lo mismo.
- **La ventana se muestra antes de crear el dispositivo D3D, y el fondo lo pinta GDI.** Es lo que
  la fase 1 ya había anotado como única salida al presupuesto de arranque. Son tres cosas:
  `hbrBackground` propio y `FillRect` en el `WM_PAINT` mientras `m_gdiBackground`, `UpdateWindow`
  tras `ShowWindow` (sin bucle de mensajes todavía, el `WM_PAINT` hay que provocarlo), y
  `EndGdiBackground()` tras el primer `Present`.
- **El listado de la carpeta se lanza antes que D3D.** Los ~160 ms del driver son tiempo en el
  que un hilo de trabajo puede estar leyendo: al llegar al primer frame se hace `DrainResults` y
  la carpeta ya está. El primer frame es el primer frame útil, no uno vacío.
- **El log va a `%APPDATA%\Rayo\rayo.log` y no a `OutputDebugString`.** Una app sin consola no
  tiene dónde imprimir y el depurador no siempre está enganchado. `Diag::Open` además redirige
  ahí el informe de fugas del CRT (`_CrtSetReportFile`), que se emite al final del proceso,
  cuando ya no hay nadie escuchando. El archivo se trunca solo pasados 64 KB.

Medido tras la fase 8 (8 arranques en caliente, mediana):

| Etapa | ms |
|---|---|
| proceso → `wWinMain` (cargador de Windows) | 9,4 |
| `ImGui::CreateContext` | 1,0 |
| leer el config | 0,6 |
| crear la ventana | 5,5 |
| **ventana visible, con su fondo pintado** | 29,1 → **45 ms acumulados** |
| lanzar hilos y pedir el listado | 0,2 |
| `D3D11CreateDevice` + swap chain | 158,5 |
| backends de ImGui + fuentes | 0,9 |
| primer frame + `Present` | 8,7 |
| **total hasta el primer frame de D3D** | **215 ms** |

- **El presupuesto de <100 ms se cumple para "ventana visible" (45 ms) y no para "primer frame de
  D3D" (215 ms).** De esos 215, **158 son `D3D11CreateDevice`**: cargar el driver de la GPU, que
  ya en la fase 1 se midió aparte y no es código nuestro. Todo lo demás junto son 26 ms. No queda
  nada que diferir: el config son 0,6 ms, las fuentes 0,9 (mapeadas, fase 2) y COM no se toca en
  el hilo de UI. Lo único que quedaba por hacer —enseñar la ventana antes del driver— está hecho,
  y es lo que cambia la sensación de arranque: la ventana aparece en 45 ms y se llena a los 215.
- **La fase encontró una fuga de verdad, y era de orden de destrucción.** `PreviewCache` guarda
  `ComPtr<ID3D11ShaderResourceView>` y es miembro de `App`: se destruía *después* de
  `m_gfx.Destroy()`, es decir, después del dispositivo. El destructor ahora para el pool primero
  (nadie más puede empujar texturas al buzón), vacía la caché y el buzón, y solo entonces suelta
  el dispositivo. `ID3D11DeviceContext::ClearState()` + `Flush()` antes de soltarlo por lo mismo:
  el contexto retiene lo último que se le ató.
- **La capa de depuración de D3D no está instalada en esta máquina** (`Graphics Tools` es una
  característica opcional de Windows y consultarla pide elevación), así que
  `ReportLiveDeviceObjects` no puede decir nada: el código lo detecta y lo escribe en el log. La
  comprobación que sí funciona sin nada opcional es **la cuenta del último `Release` del
  dispositivo: 0 = no queda nadie agarrado**. Medido 0 en todas las ejecuciones.
- **El informe de fugas del CRT se validó metiendo una fuga a propósito** (`new int[7]`): salió en
  el log como "Detected memory leaks! ... 28 bytes long". Sin ella, el log no dice nada. Es decir,
  el silencio significa "no hay fugas" y no "el mecanismo no está puesto".
- **Ojo al medir CPU en reposo: el puntero del ratón encima de la ventana la despierta.** Medir
  daba ~250 ms cada 20 s hasta que se contaron los mensajes: **45 `WM_MOUSEMOVE` en 20 s**, tres
  frames cada uno. Con el puntero fuera, **0 ms en 20 s**. No es de esta fase (`NeedsRedraw`
  despierta con el ratón desde la fase 1) pero invalida cualquier medida hecha con el ratón
  encima.

| Métrica | Objetivo | Medido |
|---|---|---|
| Ventana visible desde el arranque del proceso | < 100 ms | **45 ms** |
| Primer frame de D3D | < 100 ms | 215 ms (158 son el driver) |
| CPU en reposo, puntero fuera de la ventana | 0 % | 0 ms en 20 s |
| Memoria recién abierto | < 50 MB | 45,9 MB working set (52,5 privada) |
| Fugas al cerrar en Debug | ninguna | 0 del CRT; refcount del dispositivo 0 |
| Warnings con `/W4 /permissive-` | 0 | 0 (Release y Debug) |

- **El exe pasa de 788 a 830 KB**, sin dependencias nuevas: el config es kernel32 y el log
  también.
- **Icono y versión: `src/rayo.rc`, y el manifiesto sigue yendo suelto.** La nota de la fase 1
  avisaba de que un `.rc` propio podía duplicar el manifiesto; no pasa mientras el `.rc` no
  lleve un `RT_MANIFEST`, y así link.exe lo sigue fundiendo con el `trustInfo` que genera él.
  Comprobado tras añadirlo con `mt.exe -inputresource:rayo.exe;#1`: sigue habiendo uno solo,
  con PerMonitorV2, longPathAware y UTF-8.
- **El icono pequeño se carga aparte y al tamaño exacto** (`LoadImageW` con `SM_CXSMICON`). Si
  `hIconSm` se deja nulo, Windows encoge el de 32 y la barra de título se ve emborronada.
  `LR_SHARED` los deja en manos del sistema: nada que destruir.
- **`src/rayo.ico` está generado, no dibujado**: rayo del acento (`#4fc1ff`) sobre cuadrado
  redondeado oscuro, en 16/24/32/48/64/128/256, con las imágenes guardadas como PNG dentro del
  `.ico` (Windows 10 los lee a cualquier tamaño, así que no hay que armar DIB a mano). El
  polígono del rayo se ajusta por su propia caja y no por la del dibujo: escalarlo por la caja
  entera dejaba medio cuadrado vacío y a 16 px no se leía.
- **`CHANGELOG.md`** resume la versión 1.0.0 por bloques; el porqué de cada decisión se queda
  aquí.
- **Se abre en la carpeta de usuario, no en la última de la sesión anterior.** Ahí están
  Descargas, Escritorio y Documentos, que es donde se trabaja; subir a la raíz de la unidad
  es una tecla (`h`). La última carpeta se sigue guardando y se recupera con
  `startPath=last` en el config, o se fija una ruta concreta. La línea de comandos (y por
  tanto el menú contextual) manda sobre las dos.
- **"Abrir en Rayo" son tres claves del registro, no una.** El Explorador trata como cosas
  distintas la carpeta seleccionada (`Directory`, ruta en `%1`), el fondo de la carpeta
  abierta (`Directory\Background`, donde la ruta es `%V`) y la raíz de una unidad (`Drive`,
  que no entra en `Directory`). Todo bajo `HKCU\Software\Classes`: sin administrador y sin
  tocar nada de los demás usuarios.
- **Hay que avisar al shell con `SHChangeNotify(SHCNE_ASSOCCHANGED)`.** Sin eso el verbo no
  aparece —ni desaparece al desinstalar— hasta reiniciar el Explorador. Se vio en pantalla:
  antes de añadir el aviso, el menú no lo mostraba.
- **En el menú corto de Windows 11 no sale, y no es un fallo de caché.** Comprobado tras
  reiniciar el Explorador: un verbo estático clásico se queda en "Mostrar más opciones"
  (Shift+F10). Estar en el menú corto exige una app empaquetada (MSIX) con un
  `IExplorerCommand`, que es otro proyecto. En el clásico aparece con su icono, verificado
  en pantalla, y la ruta llega bien: `rayo.exe "C:\Users\elima\Downloads"` abre ahí.
- **Sin verificar**: `ReportLiveDeviceObjects` de verdad (falta `Graphics Tools`; se activa con
  `dism /online /add-capability /capabilityname:Tools.Graphics.DirectX~~~~0.0.1.0`); restaurar la
  ventana en un monitor con otro DPI o desenchufado (solo hay uno en esta máquina; el camino de
  descarte está escrito pero no probado); y qué hace el config si dos instancias de Rayo se
  cierran a la vez — gana la última que escriba.
