# Brújula

Un priorizador de repositorios de GitHub para Windows 11. Se conecta a la cuenta del
autor —unos 120 repositorios, casi todos privados y de trabajo— y ayuda a decidir **en
qué trabajar**: clasifica por actividad, deja asignar prioridades a mano y guarda, de cada
proyecto, su estado, sus novedades y sobre todo **el siguiente paso concreto**. Retomar un
proyecto dormido no debería costar media hora de reconstruir dónde se quedó uno.

La estética es de aplicación de Mac: materiales translúcidos, mucho aire, tipografía
cuidada y movimiento con física de muelle. Debe sentirse como una aplicación de Apple que
resulta que corre en Windows.

> El nombre es de trabajo. Cambiarlo es tocar `CLAUDE.md` y `CMakeLists.txt`.

## Estado

**Fase 5 de 8 terminada.** Ya sirve para lo que existe: al abrirla aparece la lista de los
repositorios de la cuenta —barra lateral con los grupos de prioridad y las vistas
inteligentes, tarjetas con el siguiente paso, búsqueda en vivo—, y al abrir uno, la tarjeta
se transforma en el inspector, donde se le pone prioridad, estado, siguiente paso y
novedades. La sincronización con GitHub ocurre detrás, sin que la ventana la espere. Medido
contra la cuenta del autor —109 repositorios— y con la caché llena, **la ventana está en
pantalla con la lista en 106 ms** y filtrar mientras se escribe cuesta **0,006 ms por
pulsación**.

Lo escrito se queda **en local por omisión**. Repositorio por repositorio, y pasando por una
confirmación que se hace una sola vez en cada uno, se puede activar el **modo repo**: además
de guardarlo aquí, Brújula escribe un `PROYECTO.md` en la raíz de ese repositorio con un
commit `chore: actualizar PROYECTO.md`. Si el archivo cambió por otro lado, lo relee, lo
fusiona y conserva lo que no entiende — claves inventadas, párrafos y secciones enteras.

Lo que ya funciona: la ventana con Mica, la barra de título propia con su indicador de
sincronización, el tema claro/oscuro siguiendo al del sistema, el kit de componentes entero
(F12 abre su catálogo en una compilación de Debug), la credencial —de GitHub CLI si está, y
si no una hoja que explica los permisos y la recoge—, el cliente de la API, la caché en
SQLite, la sincronización en segundo plano, la vista principal, el inspector con sus notas,
el modo repo y la copia de seguridad a un JSON.

Lo que no hay todavía: arrastrar tarjetas entre grupos y la paleta de comandos (fase 6) y la
revisión semanal (fase 7). El plan completo está en [`PROMPTS.md`](PROMPTS.md).

## Compilar y ejecutar

En esta máquina ni `cmake` ni `ninja` están en el PATH: viven dentro de Visual Studio
Build Tools. `preparar.ps1` los busca, monta el entorno de MSVC y compila:

```
powershell -NoProfile -ExecutionPolicy Bypass -File preparar.ps1
build\brujula.exe
build\brujula_tests.exe
```

Si prefieres los comandos de `CLAUDE.md` tal cual, primero deja el entorno en tu consola
—con el punto delante, que es lo que hace que se quede— y después compila a mano:

```
. .\preparar.ps1 -SoloEntorno
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Hace falta Visual Studio Build Tools 2022 con «Desarrollo para el escritorio con C++» y
el componente «CMake para C++», y el SDK de Windows 10.0.26100 o posterior. Si falta algo,
el script lo dice y da el comando para instalarlo.

## Qué lleva dentro

C++20 con MSVC, sin Qt, sin Electron y sin .NET. La ventana es Win32 puro; el material lo
pone DWM; todo lo que se ve lo dibujan Direct2D y DirectWrite sobre superficies de
`Windows.UI.Composition`, y todas las animaciones son muelles que ejecuta el proceso de
DWM, no este hilo.

```
src/
  app/          monta las piezas y las conecta
  shell/        ventana, marco propio, Mica, DPI, tema
  compositor/   escena, dispositivo gráfico, superficies, muelles, materiales, sombras
  ui/           el kit: texto, elementos, entrada, botones, campo, lista, barra, flotantes
  model/        tipos de dominio, reglas, tiempo en UTC, el borde UTF-8 y base64
  projectfile/  leer y escribir PROYECTO.md sin perder lo que no se entiende
  store/        SQLite: esquema, migraciones, repositorios, notas y la copia de seguridad
  github/       credencial, cliente de la API, la sincronización en dos pases y la escritura
  views/        barra de título, barra lateral, lista, tarjetas e inspector
tests/          doctest sobre el núcleo puro
```

`brujula_core` es una biblioteca aparte con lo que no necesita ni Win32 ni WinRT para
decidir —la geometría de la barra de título, la tabla de colores, los muelles, la rejilla,
el contador de clics, el modelo del campo de texto, la aritmética de la lista virtualizada
las reglas de dominio, el esquema de la base, el parser de las respuestas, la política de
reintentos, el estado de la vista principal —qué repositorios entran en cada vista, en qué
orden salen y qué significa que dos textos coincidan— y, desde la fase 5, el lector y el
escritor de `PROYECTO.md` y la copia de seguridad—, y es lo que enlazan las pruebas. La regla para decidir si algo va ahí
es la que este proyecto viene aplicando: *¿se equivocaría esto en silencio, sin que se vea
en pantalla hasta que ya ha decidido mal?* El cursor de un campo de texto y el rango de
filas de una lista, sí. El color de una brocha, no.

## Seguridad

Brújula es la única utilidad de esta carpeta que maneja una credencial y datos de trabajo.
Sus reglas están en [`SEGURIDAD.md`](SEGURIDAD.md) y `auditar.ps1` las comprueba:

```
powershell -NoProfile -ExecutionPolicy Bypass -File auditar.ps1
```

## Documentos

- [`CLAUDE.md`](CLAUDE.md) — cómo se trabaja aquí, y el registro de decisiones.
- [`PROMPTS.md`](PROMPTS.md) — las ocho fases.
- [`SEGURIDAD.md`](SEGURIDAD.md) — qué no va a hacer, y por qué.
- [`CHANGELOG.md`](CHANGELOG.md) — qué cambió en cada fase, con lo medido.
