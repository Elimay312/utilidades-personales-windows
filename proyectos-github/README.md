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

**Fase 3 de 8 terminada.** Ya hay datos: al abrirla se conecta con GitHub, trae los
repositorios de la cuenta y los guarda. Medido contra la cuenta del autor —109
repositorios—, la primera sincronización tarda entre **5,4 y 6,7 segundos** y la segunda
**2,6**, porque solo vuelve a pedir el detalle de lo que ha cambiado.

Lo que ya funciona: la ventana con Mica, la barra de título propia, el tema claro/oscuro
siguiendo al del sistema, el kit de componentes entero (F12 abre su catálogo en una
compilación de Debug), y ahora la credencial —de GitHub CLI si está, y si no una hoja que
explica los permisos y la recoge—, el cliente de la API, la caché en SQLite y la
sincronización en segundo plano.

Lo que no hay todavía: la vista principal. De momento se ve un panel de estado provisional
con la cuenta, el número de repositorios y cuándo se sincronizó; las prioridades, la lista,
el inspector y la revisión semanal son las fases 4 a 7. El plan completo está en
[`PROMPTS.md`](PROMPTS.md).

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
  model/        tipos de dominio, reglas, tiempo en UTC y el borde UTF-8
  store/        SQLite: esquema, migraciones, repositorios y notas
  github/       credencial, cliente de la API y la sincronización en dos pases
  views/        la demo de la fase 1 y el panel de estado (los dos temporales)
tests/          doctest sobre el núcleo puro
```

`brujula_core` es una biblioteca aparte con lo que no necesita ni Win32 ni WinRT para
decidir —la geometría de la barra de título, la tabla de colores, los muelles, la rejilla,
el contador de clics, el modelo del campo de texto, la aritmética de la lista virtualizada
y, desde la fase 3, las reglas de dominio, el esquema de la base, el parser de las
respuestas y la política de reintentos—, y es lo que enlazan las pruebas. La regla para decidir si algo va ahí
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
