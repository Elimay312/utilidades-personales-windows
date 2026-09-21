# Prompts por fases para Claude Code — Brújula

## Cómo usarlos

- `CLAUDE.md` en la raíz del proyecto. Una fase por sesión, commit al terminar y `/clear` antes de la siguiente.
- Empieza en modo plan (`Shift+Tab`) en las fases 1, 2, 3 y 5, que son las que fijan la base técnica.
- Las fases 1 y 2 no tocan GitHub: primero se consigue que la app se *sienta* bien, luego se le ponen datos. Si la base visual no convence, es mucho más barato corregirla ahí.
- Guía visual: el archivo de Figma "Utilidades Windows — animaciones" tiene el tono de materiales y movimiento que buscamos.

---

## Fase 1 — Ventana Mac

```
Lee CLAUDE.md. Crea la base del proyecto y una ventana que ya se sienta como una app de Mac. Todavía sin datos.

1. CMakeLists.txt con C++20, flags de CLAUDE.md, C++/WinRT, nlohmann/json, SQLite y doctest por FetchContent. Objetivos brujula.exe y brujula_tests.exe.
2. Manifiesto con DPI Per-Monitor V2.
3. Ventana Win32 con Mica (DWMWA_SYSTEMBACKDROP_TYPE), esquinas redondeadas del sistema y barra de título integrada en el contenido: arrastrable, con doble clic para maximizar, botones de ventana dibujados por nosotros pero con el comportamiento nativo (incluido el menú de ajuste de Windows 11 al pasar sobre maximizar, vía WM_NCHITTEST devolviendo HTMAXBUTTON).
4. compositor/: DesktopWindowTarget con Windows.UI.Composition, dispositivo Direct2D/DirectWrite y superficies de dibujo. Utilidad para crear animaciones de muelle a partir de los cuatro muelles definidos en CLAUDE.md.
5. Tema claro/oscuro siguiendo a Windows, con cambio en caliente y transición cruzada.
6. Respetar SPI_GETCLIENTAREAANIMATION (sin movimiento → solo fundidos).
7. Demo temporal: una barra lateral translúcida y una tarjeta de ejemplo. Al hacer clic en la tarjeta, se transforma en un panel lateral derecho (posición, tamaño y radio animados a la vez, contenido con fundido cruzado); Esc la devuelve. La animación debe poder interrumpirse haciendo clic a mitad.

Criterios de aceptación:
- La ventana aparece con Mica sin destellos blancos al abrir ni al redimensionar.
- La transformación tarjeta → panel es fluida y, si se interrumpe, vuelve sin saltos.
- Se ve nítida con escalados distintos y al mover la ventana entre monitores.
- Compila sin warnings.
```

---

## Fase 2 — Kit de UI y catálogo

```
Lee CLAUDE.md. Construye ui/, el kit de componentes propio, y una pantalla de catálogo para revisarlos (se abre con F12 solo en Debug).

Componentes, todos con los tokens y muelles de CLAUDE.md, estados hover/pulsado/foco y soporte de teclado:
1. Texto con DirectWrite: estilos de la escala tipográfica, recorte con elipsis, números tabulares.
2. Botón (primario, secundario, sin fondo) y botón de icono con Segoe Fluent Icons.
3. Etiqueta/píldora de prioridad y punto de actividad.
4. Campo de texto de una línea: cursor, selección con ratón y teclado, copiar/pegar, deshacer, tildes y ñ (WM_CHAR con teclas muertas), IME básico. Anillo de foco animado.
5. Lista virtualizada con scroll suave con inercia (rueda y panel táctil de precisión), elementos que entran con fundido escalonado y que cambian de posición deslizándose.
6. Elemento de barra lateral con contador y selección animada (el fondo de selección se desliza entre elementos).
7. Menú contextual y aviso discreto (toast interno) con material translúcido y sombra suave.
8. Hoja modal centrada con fondo atenuado.

Criterios de aceptación:
- El catálogo muestra cada componente en claro y oscuro.
- El scroll de una lista de 500 elementos va a 60 fps.
- Escribir "Revisión año niño" en el campo de texto funciona sin problemas.
- Compila sin warnings.
```

---

## Fase 3 — GitHub, SQLite y sincronización

```
Lee CLAUDE.md. Implementa github/, store/ y model/. Propón primero el diseño (modo plan).

1. Autenticación: intentar `gh auth token`; si no hay, hoja modal de bienvenida que explica qué permisos hacen falta (Metadata: read; Contents: read and write solo para el modo repo), con enlace para crear el token, y campo para pegarlo. Validar el token con una petición y guardarlo en el Administrador de credenciales (CredWriteW). Opción para cerrar sesión que lo borra.
2. Cliente WinHTTP para GraphQL y REST, con reintentos, respeto de los límites de la API y cabeceras de rate limit.
3. Consulta GraphQL paginada que trae los repos personales y de las organizaciones elegidas con los campos listados en CLAUDE.md, incluido el contenido de PROYECTO.md si existe.
4. store/: esquema SQLite para repos, datos de prioridad/notas (locales), novedades con fecha y estado de sincronización. Migraciones versionadas.
5. Sincronización incremental en un hilo de trabajo que publica cambios al hilo de UI.
6. model/: clasificación por actividad con umbrales configurables, reglas del límite de Enfoque y detección de "Necesita decisión", como funciones puras con tests.
7. Nunca escribir el token ni respuestas completas en el log.

Criterios de aceptación:
- Con una cuenta real, los ~120 repos se sincronizan en pocos segundos y quedan en SQLite.
- La segunda sincronización solo actualiza los repos que cambiaron.
- Tests de model/ pasan.
- Compila sin warnings.
```

---

## Fase 4 — Vista principal

```
Lee CLAUDE.md. Construye la vista principal con datos reales.

1. Barra lateral: grupos de prioridad con contadores y vistas inteligentes (Necesita decisión, Dormidos, Actividad esta semana, Sin clasificar).
2. Lista de repos con tarjetas: nombre, siguiente paso destacado, punto de actividad, prioridad, lenguaje, "hace X días". Alternar lista compacta / cuadrícula con transición animada entre ambas disposiciones.
3. Búsqueda en vivo (Ctrl+F o /) que filtra con animación.
4. Al abrir, pintar desde SQLite al instante y sincronizar en segundo plano; indicador discreto de sincronización en la barra de título. Los repos que cambian de grupo tras sincronizar se deslizan a su sitio.
5. Estado vacío cuidado para cada vista (texto amable y una acción).
6. Navegación con teclado: ↑/↓ o j/k, Enter abre (el inspector llega en la fase 5; por ahora solo selecciona).

Criterios de aceptación:
- Con la caché llena, la ventana muestra la lista en menos de 200 ms.
- Filtrar entre 120 repos es instantáneo.
- Compila sin warnings.
```

---

## Fase 5 — Inspector, notas y PROYECTO.md

```
Lee CLAUDE.md. Implementa el inspector y projectfile/. Propón primero el diseño (modo plan).

1. Enter o clic abre el inspector: la tarjeta se transforma en el panel derecho (transición compartida de la fase 1). Esc lo cierra devolviéndola a su sitio.
2. Contenido: prioridad y estado editables, siguiente paso editable en línea (E), novedades con fecha (N añade una), últimos 5 commits, issues y PRs abiertos, botones "Abrir en GitHub" y "Abrir carpeta local" (si el usuario configuró la ruta donde clona sus repos).
3. projectfile/: parser y escritor de PROYECTO.md según CLAUDE.md, tolerante y conservando el contenido que no entiende. Tests con archivos malformados.
4. Modo local por defecto. Modo repo activable por repo o global; antes del primer commit en cada repo, hoja modal de confirmación. Escritura por la API de contenidos con el sha actual; si hay conflicto, recargar y reintentar sin perder la edición.
5. Importación: si un repo tiene otros .md en la raíz (distintos de README, CHANGELOG y LICENSE), mostrarlos en el inspector con la opción de copiar su contenido a las novedades.
6. Exportar e importar toda la base local a un archivo JSON.

Criterios de aceptación:
- Editar el siguiente paso y cerrar la app conserva el cambio.
- En modo repo, el commit aparece en GitHub con el formato correcto.
- Compila sin warnings y los tests pasan.
```

---

## Fase 6 — Priorizar

```
Lee CLAUDE.md. Añade las interacciones de priorización.

1. Arrastrar tarjetas entre grupos de la barra lateral y reordenar dentro de un grupo: la tarjeta se levanta (escala 1,03 y sombra), las demás se apartan con muelle suave y al soltar cae en su sitio.
2. Teclas 1-4 cambian la prioridad del repo seleccionado con la misma animación hacia su grupo.
3. Límite de Enfoque: si se supera, la tarjeta se detiene con un pequeño temblor horizontal y aparece una hoja modal con los repos en Enfoque para elegir cuál baja a Secundario.
4. Menú contextual en cada tarjeta con todas las acciones.
5. Paleta de comandos (Ctrl+K): buscar repos, cambiar prioridad, sincronizar, abrir en GitHub, empezar la revisión. Aparece desde arriba con muelle estándar y fondo atenuado.
6. Deshacer (Ctrl+Z) para cambios de prioridad y ediciones.

Criterios de aceptación:
- Arrastrar es fluido y nunca deja una tarjeta "flotando" si sueltas fuera.
- No es posible tener más repos en Enfoque que el límite.
- Compila sin warnings.
```

---

## Fase 7 — Revisión semanal

```
Lee CLAUDE.md. Implementa la revisión semanal (Ctrl+Shift+R).

1. La ventana pasa a un modo a pantalla completa (dentro de la app) con una transición expresiva: la lista se aleja y aparece una pila de tarjetas.
2. Cada tarjeta es un repo que necesita decisión (Necesita decisión, Sin clasificar y Enfoque sin actividad), con su siguiente paso, actividad reciente y novedades.
3. 1-4 asignan prioridad y la tarjeta sale volando hacia el color de su grupo; E edita el siguiente paso sin salir; Espacio la salta al final; Esc termina.
4. Barra de progreso fina arriba y, al terminar, un resumen animado: cuántos repos se revisaron y cómo quedó cada grupo.
5. Recordatorio opcional: una notificación de Windows el día y hora que el usuario elija, que abre directamente la revisión.

Criterios de aceptación:
- Revisar 20 repos lleva un par de minutos y se hace solo con el teclado.
- Compila sin warnings.
```

---

## Fase 8 — Pulido final

```
Lee CLAUDE.md. Fase de pulido:

1. Auditoría de animaciones: revisar todas las transiciones con el modo lento de depuración (multiplicar duraciones ×5 con una tecla en Debug) y corregir saltos, parpadeos y elementos que aparecen sin transición.
2. Revisar el modo sin animaciones del sistema.
3. Medir el arranque hasta el primer frame con datos y bajar de 300 ms en frío.
4. Integración con el resto de utilidades: esquema de URL brujula://repo/NOMBRE y argumento de línea de comandos `brujula.exe --repo NOMBRE` para que el lanzador abra un repo directamente en el inspector.
5. Revisar fugas de memoria y handles tras una hora de uso con sincronizaciones periódicas.
6. Icono de la app y pantalla "Acerca de" con la versión.

Criterios de aceptación:
- Ninguna transición con saltos en el modo lento.
- Sin fugas tras una hora de uso.
```
