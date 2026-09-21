# Changelog

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/); versionado
según [SemVer](https://semver.org/lang/es/).

Las decisiones de diseño y las mediciones detrás de cada punto están en `CLAUDE.md`, en
"Decisiones y notas".

## 1.0.0 — 2026-09-21

Primera versión completa: las ocho fases del plan, de la ventana vacía al gestor usable.
Un solo `.exe` de 840 KB, sin redistribuibles (`/MT`) y portable: el instalador solo lo
copia y le pone un acceso directo.

### Añadido

- **Ventana y render.** Win32 puro + DirectX 11 + Dear ImGui 1.92, con bucle guiado por
  eventos (`MsgWaitForMultipleObjectsEx`): no se dibuja nada si nada cambia. Manifiesto con
  DPI Per-Monitor V2, rutas largas y UTF-8.
- **Listado de carpetas asíncrono.** `FindFirstFileExW` en un hilo de trabajo; el hilo de UI
  nunca toca el disco. Orden natural (`archivo2` antes que `archivo10`). Fuentes de Windows
  mapeadas en memoria (Segoe UI + símbolos + emoji + CJK + hangul): cualquier nombre de
  archivo se ve.
- **Columnas Miller y navegación por teclado** estilo Vim (`j`/`k`/`h`/`l`, `gg`/`G`,
  `Ctrl+d`/`Ctrl+u`), caché LRU de listados para que ir y volver no parpadee, y cursor que
  sigue al nombre y no al índice. La raíz virtual lista las unidades.
- **Vista previa** de imágenes (WIC, con orientación EXIF y escalado al tamaño del panel),
  texto, carpetas y metadatos, con caché propia y 60 ms de espera: pasar de largo con `j`
  no decodifica nada.
- **Vigilancia del disco** con `ReadDirectoryChangesW` desde un solo hilo con APCs: lo que
  cambia fuera de Rayo aparece en menos de medio segundo, agrupando ráfagas cada 100 ms.
- **Operaciones de archivos** con `IFileOperation`: copiar, cortar, pegar, Papelera (`d`),
  borrado definitivo con confirmación (`D`), renombrar (`r`) y crear (`a`, terminar en `\`
  hace carpeta). Marcas con `Espacio`, globales entre carpetas. Diálogos de conflicto y
  deshacer nativos del shell; la UI sigue respondiendo durante una copia grande.
- **Filtro** (`/`) que ignora mayúsculas y tildes, **ir a una ruta** (`:`) con autocompletado
  por Tab, **archivos ocultos** (`.`) y **barra de estado** con ruta, posición, tamaño,
  fecha y espacio libre.
- **Configuración** en `%APPDATA%\Rayo\config.ini` (colores, atajos de teclado y opciones);
  se crea sola con los valores por defecto. **Pestañas** (`t`, `1`-`9`, `Ctrl+w`),
  **marcadores** (`m` + letra, `'` + letra) y memoria de la última carpeta y de la posición
  y tamaño de la ventana.
- **Icono propio** embebido en el `.exe` (Explorador, barra de tareas, alt-tab y barra de
  título) e información de versión.
- **`install.ps1`**: instalación por usuario en `%LOCALAPPDATA%\Programs\Rayo` con acceso
  directo en el menú Inicio y **"Abrir en Rayo"** en el menú contextual de carpetas, del
  fondo de una carpeta y de las unidades. Desinstala con `-Uninstall`, sin borrar la
  configuración. Todo en `HKCU`: no pide administrador. En Windows 11 el verbo vive en
  "Mostrar más opciones" (Shift+F10); el menú corto solo admite apps empaquetadas.
- **Rayo abre en la carpeta de usuario** (`%USERPROFILE%`), que es donde están Descargas,
  Escritorio y Documentos. Con `startPath=last` en el config vuelve a abrir donde lo
  dejaste, y con una ruta fija, ahí. Una ruta en la línea de comandos manda sobre todo.
- **Medición de arranque** por etapas en `%APPDATA%\Rayo\rayo.log`. En Debug, al mismo
  archivo van el informe de fugas del CRT y el de la capa de depuración de D3D.

### Rendimiento

- Ventana visible **45 ms** después de lanzar el proceso: se muestra y pinta su fondo antes
  de crear el dispositivo D3D, y el listado de la carpeta se lanza antes también.
- Listar 12.000 entradas: 49 ms, en un hilo de trabajo.
- Filtrar 10.000 nombres: 6,2 ms.
- CPU en reposo: 0 %. Memoria recién abierto: 46 MB de working set.

### Limitaciones conocidas

- **El primer frame de DirectX llega a los 215 ms**, no a los 100 del presupuesto: 158 de
  esos milisegundos son `D3D11CreateDevice` cargando el driver de la GPU. El resto del
  arranque suma 26 ms y ya no queda nada que diferir.
- **El presupuesto de memoria (50 MB) está medido sobre la cifra equivocada.** La memoria
  privada real ronda los 52 MB; el working set es casi todo recortable. Pendiente de
  revisar el presupuesto, no el programa.
- Las líneas largas en la vista previa de texto se cortan, no se envuelven.
- Sin verificar: unidades de red y recursos UNC, volúmenes extraíbles, HEIC/AVIF (falta el
  códec en la máquina de desarrollo) y el cambio de DPI entre monitores con escalados
  distintos.
