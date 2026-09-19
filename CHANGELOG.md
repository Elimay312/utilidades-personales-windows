# Cambios

El registro va por hitos, que es como se construye: cada uno tiene que compilar y ejecutarse
antes de empezar el siguiente.

Los números que aparecen aquí están medidos, no estimados. El detalle de cada medición está
en el mensaje de su commit.

---

## Sin publicar

### H3 — El ranking por uso

- **Decaimiento exponencial con semivida de 30 días**, y las veces **saturan a las 10**. Sin
  saturar, algo abierto trescientas veces sepultaría todo lo demás para siempre y el lanzador
  dejaría de aprender. Comprobado: 300 veces no puntúa más que 10.
- **La elección fijada manda.** Si para `br` elegiste Brave, `br` da Brave siempre, aunque
  puntúe peor de texto. Diez líneas, y es lo que hace que parezca que te lee la mente.
- **Cerrado el caso que H2 dejó abierto.** `adm` daba `Administrative Tools` (164) por delante
  de `Administrador de tareas` (161), por 3 puntos de longitud. Con el Administrador abierto
  4 veces: **161 → 209**, y adelanta. No hizo falta tocar ni un peso del algoritmo.
- **Otro caso de prueba mal escrito, no el código.** Comprobaba que con la consulta `b` ganase
  `Br`, pero ganaba `Brave` — y con razón: el refuerzo normal de haberlo abierto. La
  comprobación ahora mira el **mecanismo** (que el bono de fijado no se escape a otra
  consulta) en vez del resultado, que cambiaría al afinar los pesos.
- **Las comprobaciones usan fechas fijas y un `Uso` construido a mano.** Si leyeran el
  `uso.json` de verdad, dirían una cosa distinta cada día.

### H2 — El algoritmo

- **Dos pasadas.** Un filtro O(m) que descarta lo que ni siquiera contiene las letras en
  orden, y programación dinámica O(n·m) sobre los supervivientes. La DP premia empezar
  palabra, `camelCase`, letras seguidas y el prefijo entero; penaliza huecos y longitud.
  **Medido: 0,2–0,5 ms por consulta sobre 241 entradas**, con el objetivo en 10.
- **Un solo núcleo para puntuar y para explicar.** `--buscar` enseña en qué letras cayó la
  consulta y cuánto puso cada concepto, y sale de la *misma* función que puntúa, con la
  matriz guardada. Dos implementaciones del mismo algoritmo acaban divergiendo, y entonces
  el desglose explica algo que no pasó.
- **El fallo de H2 lo encontró `--check`, no leer el código.** `<InvariantGlobalization>` a
  `true` —copiado de la isla— hace que `string.Normalize` **devuelva la cadena tal cual,
  sin lanzar ni avisar**. Los acentos dejaban de quitarse en silencio y `configuracion` no
  encontraba `Configuración`. Peor: la sonda que escribí para depurarlo *no* tenía el flag,
  así que decía que todo estaba bien. Quitado, con el porqué en el csproj para que nadie lo
  reponga; comprobado que no añade ni un fichero a la salida (8 antes, 8 después).
- **Un caso de prueba estaba mal escrito, no el algoritmo.** Para `vsc`, `Vs Code Cosa`
  gana a `Visual Studio Code` (108 a 62) y **está bien que gane**: textualmente es mejor
  coincidencia. Lo que hace que en la vida real salga el que quieres no es el texto, es el
  ranking por uso. Queda escrito junto al caso para no volver a "arreglarlo".
- **El filtro barato se comprueba contra la puntuación**, con 4000 consultas al azar y
  semilla fija. Si se desincronizan, el filtro empieza a esconder resultados buenos y eso
  no se ve mirando la pantalla.

### H1 — El índice de aplicaciones

- **Hacen falta las dos fuentes, y eso se midió en vez de suponerlo.** `shell:AppsFolder` da
  205 entradas y los menús Inicio 148; el solape es casi total, pero **36 de las 148 no están
  en `AppsFolder`** — Administrador de tareas, Editor del registro, Panel de control, Símbolo
  del sistema, y los lanzadores de un par de juegos. Ninguna sustituye a la otra. **Índice
  final: 241 aplicaciones.**
- **`IShellLink` no hizo falta y ya no va a estar.** Un `.lnk` se le pasa al shell tal cual.
  Menos código, una entrada menos en `NativeMethods.txt` y un corte más en `SEGURIDAD.md §3.1`.
- **`AppsFolder` cuesta ~800 ms y no es culpa de COM.** Medido: la segunda pasada en el mismo
  proceso cuesta lo mismo, así que no es el arranque de COM sino los ~3,8 ms por app que
  cuesta preguntarle el nombre al repositorio de paquetes. Pedir los items de 64 en 64 en vez
  de uno a uno bajó de 1591 a ~900 ms. Se paga una vez al arrancar y en segundo plano, así que
  se queda, marcado con `ponytail:` y con el camino de subida escrito.
- **La basura se midió antes de filtrarla**: 13 de 241 entradas son desinstaladores, `.chm` y
  `.url` de documentación. Es un 5% y el ranking por uso las entierra solas, así que **no hay
  filtro**. Si al usarlo molestan, entonces se filtra y se sabrá cuáles.

### H0 — Las reglas, antes del código

- **`SEGURIDAD.md` propio**, escrito antes de la primera línea. No es el de la isla con otro
  nombre: aquí el criterio tenía que sostener las tres cosas que peor suenan juntas —
  **recibir teclas, indexar ficheros y ejecutar programas**. Se apoya en que *todo pasa
  delante de ti, ahora*: no existe ningún camino que abra nada sin un Enter tuyo sobre una
  fila visible.
- **Dos reglas más afiladas que en los vecinos y una que va al revés.** Los hooks globales y
  leer el teclado sin foco pasan de "prohibido porque no hace falta" a ser *la* línea del
  proyecto: lo permitido es `WM_CHAR` en la ventana propia y `RegisterHotKey`, y la diferencia
  con `WH_KEYBOARD_LL` no es de grado. Y al revés que la isla, **aquí sí se guarda un
  historial** — con cinco cortes escritos, entre ellos que lo que no acabó en Enter no existe.
- **`SetForegroundWindow` es la única excepción de la regla 15**, y no se puede auditar con un
  `grep`: la API está permitida, lo prohibido es a quién se le aplica. `auditar.ps1` comprueba
  que aparece **una vez**, en `LanzadorWindow.cs`, y **con el handle propio**.
- **`auditar.ps1` con 17 reglas propias**, medido en las dos direcciones: vacío da
  `TODO LIMPIO`; con una sonda de 10 violaciones detecta las 10; y los **mismos nombres**
  dentro de comentarios y de bloques `/* */` no saltan ninguno.
- **El primer fallo lo encontró la sonda, no la lectura.** La comprobación de
  `SetForegroundWindow` contaba *líneas* en vez de *apariciones*, así que dos llamadas en la
  misma línea colaban la segunda. Con la corrección, los cuatro casos malos (handle ajeno, dos
  en la misma línea, dos en líneas distintas, fuera del fichero) se detectan.
- **Andamio que compila**: .NET 10, `WinExe`, x64, `asInvoker`, una sola dependencia
  (`CsWin32`, generador). `dotnet build` con 0 errores y 0 advertencias.
- **Los modos de consola no piden el mutex**, para poder correr con el lanzador ya arrancado.
  Los que aún no existen devuelven 2 y lo dicen, en vez de fingir que pasaron.
