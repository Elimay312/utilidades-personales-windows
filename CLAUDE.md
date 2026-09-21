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
  fs/                   DirectoryReader, DirectoryWatcher, FileOps, utilidades de rutas
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
- Fuente: Segoe UI desde `C:\Windows\Fonts\segoeui.ttf`, escalada según el DPI del monitor.
- Layout: 3 columnas, 20 % padre / 40 % actual / 40 % vista previa, más una barra de estado inferior de una línea.

## Fases del proyecto

El plan completo está en `PROMPTS.md`. Estado actual:

- [x] Fase 1 — Ventana, DirectX 11, ImGui y bucle por eventos
- [ ] Fase 2 — Lector de carpetas asíncrono y columna central
- [ ] Fase 3 — Columnas Miller y navegación
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
