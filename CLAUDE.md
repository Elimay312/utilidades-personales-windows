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

## Arquitectura

```
src/
  main.cpp              Punto de entrada (wWinMain), mínimo
  app/                  App: bucle principal, estado global, despacho de comandos
  platform/             Ventana Win32, dispositivo D3D11, DPI, portapapeles
  fs/                   DirectoryReader, ListingCache, DirectoryWatcher, FileOps, rutas
  preview/              Decodificador WIC, vista previa de texto, caché de previews
  ui/                   MillerView, PreviewPane, StatusBar, Theme, Keymap
  core/                 Cola de tareas / pool de hilos, tipos comunes
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
| `.` | Mostrar / ocultar archivos ocultos |
| `~` | Ir a la carpeta de usuario |
| `:` | Ir a una ruta escrita |
| `q` | Salir |

El mapa de teclas vive en `ui/Keymap` como tabla de datos, no como `if` repartidos por el código, para poder hacerlo configurable más adelante.

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
- [ ] Fase 4 — Vista previa (imágenes, texto, carpetas)
- [ ] Fase 5 — Vigilancia de cambios en disco
- [ ] Fase 6 — Operaciones de archivos
- [ ] Fase 7 — Filtro, ir a ruta, archivos ocultos, barra de estado
- [ ] Fase 8 — Pulido: configuración, pestañas, marcadores, medición de arranque

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
