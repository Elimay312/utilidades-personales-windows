# Cambios

El registro va por hitos, que es como se construye: cada uno tiene que compilar y ejecutarse
antes de empezar el siguiente.

Los números que aparecen aquí están medidos, no estimados. El detalle de cada medición está
en el mensaje de su commit.

---

## Sin publicar

### H8 — Memoria, arranque, y las cosas que faltaban

Cuatro arreglos que salieron de mirar el programa terminado. El primero se diagnosticó
antes de tocar nada, y menos mal, porque **la primera hipótesis era falsa**.

**La memoria: 385 → 180 MB, y no era lo que parecía.**

Con 15 consultas el proceso pasaba de 59 a 385 MB. Instrumenté el proceso para que dijera
el reparto —montón administrado, conjunto de trabajo, iconos guardados, hilos— en vez de
adivinar, y salieron tres causas distintas:

- **Los iconos se extraían a 256×256 y se dibujan a 32.** 256 KB por icono, 122 iconos,
  **27 MB** de los que se tiraba el 97%. A 64 px son 16 KB: **26,9 → 1,9 MB**.
- **Un hilo STA por icono.** El comentario `ponytail:` que vino copiado del dock decía
  *"si algún día los iconos se extraen en caliente, un único hilo STA con cola"*. Desde H7
  se extraen en caliente. **52 → 13 hilos.**
- **Se pedían los iconos de cada pulsación.** Escribir `micro` extraía los de `m`, `mi`,
  `mic`, `micr` y `micro`: cuarenta para usar ocho. Ahora con rebote de 110 ms.
- **Y una hipótesis que resultó falsa**: culpé a las superficies de Composition, una por
  repintado. Las reutilicé y la memoria bajó de 192 a 190 MB — nada. El experimento que lo
  decidió fue medir **sin pedir iconos**: +30 MB frente a +107 MB. El coste es el shell
  cargando el manejador de iconos de cada aplicación que ves, y esos no se descargan.
  **No es una fuga**: medido en tres rondas seguidas se estanca en ~180 MB. Queda marcado
  con su `ponytail:` y el camino de subida escrito (extraer fuera del proceso).

**El atajo estaba muerto el primer segundo: 1200 → 179 ms.** `Main` construía el índice
*antes* de crear la ventana. Con autoarranque eso cae en el inicio de sesión, que es cuando
más lento va todo: pulsabas `Alt+Espacio` y no pasaba nada. Ahora la ventana y el atajo van
primero y el índice se construye en un hilo STA aparte — **260 entradas, las mismas**, que
es lo que había que comprobar. Y si ya estabas escribiendo cuando llega, la lista se
re-evalúa sola.

**`SendMessage` a Everything con límite de tiempo.** Vuelve en 0,3–0,8 ms, pero es una
llamada síncrona a otro proceso: si Everything se colgaba, nuestra ventana se colgaba con
él y no había salida. Un segundo y `SMTO_ABORTIFHUNG`.

**Ratón, refresco del índice y `Ctrl+Enter`:**

- Pasar el ratón por encima selecciona y el clic lanza. Comprobado que la cuarta fila se
  resalta con el cursor encima, escalado de DPI incluido.
- El índice se reconstruye al asomarse si tiene más de 5 minutos, en segundo plano. Antes
  instalabas algo y no aparecía hasta reiniciar. Al asomarse y no con un temporizador: un
  temporizador despertaría el proceso cada pocos minutos para nada.
- `Ctrl+Enter` abre la carpeta que contiene el fichero. **Sin un solo P/Invoke nuevo**:
  Control se sigue por sus propios mensajes, que ya llegan a la caja de texto, así que no
  hace falta `GetAsyncKeyState` ni rozar la regla 3. Y se le pasa al shell la ruta de la
  carpeta, no un `explorer /select`, que sería componer un comando (regla 10).

### H7 — Iconos, y tres promesas que el código no cumplía

- **Iconos de verdad**, copiados del extractor del dock con sus tres trampas ya pagadas: el
  hilo **STA** (desde un hilo del pool, `GetImage` devuelve el icono genérico sin fallar ni
  avisar), `SIIGBF_ICONONLY`, y volver a pedir a 48 px cuando el fichero no da para 256 —
  si no, los iconos viejos salen como sellos diminutos dentro del lienzo.
- **Se cargan en segundo plano y la lista se repinta cuando llegan.** El hueco ya estaba
  reservado desde H4b, así que no hay salto al aparecer.
- **`SIIGBF_ICONONLY` es la regla 12, no una opción de calidad.** Sin esa bandera `GetImage`
  devuelve la **miniatura**, que es el contenido del documento dibujado. Va con su enmienda
  (`SEGURIDAD.md §3.11`) y `auditar.ps1` lo comprueba en las dos direcciones.
- **Tres cosas que los documentos prometían y el código no hacía**, encontradas repasando
  `SEGURIDAD.md` contra el código línea a línea:
  1. **El autoarranque no existía.** `autoArranque` estaba en el JSON desde H4 y no lo
     aplicaba nadie. Comprobado escribiendo y borrando de verdad en `HKCU\...\Run`.
  2. **"Si Everything no está, se dice en la lista"** (§3.7) no se decía en ningún sitio que
     el usuario pudiera ver: el aviso salía por consola. Y la primera versión tampoco valía
     — se añadía al final, pero las 8 aplicaciones ya llenaban el cupo y la fila quedaba
     recortada fuera de la ventana. Ahora se le **reserva** el sitio antes de buscar.
  3. **`lanzador.json` decía "se recarga sola al guardarlo"** y era mentira. Corregido el
     comentario en vez de implementarlo: el atajo habría que volver a registrarlo y no se ha
     echado de menos todavía.
- **Comprobación final completa**: `dotnet publish` limpio, **ninguna conexión TCP ni UDP**
  con el lanzador corriendo, 59 MB de memoria, y **Defender no encuentra nada** en lo
  publicado — que era el objetivo declarado de `SEGURIDAD.md`.

### H6 — Prefijos web, sitios del sistema y calculadora

- **18 sitios del sistema en el índice**, y ni un P/Invoke nuevo: son `ms-settings:` y
  `shell:`, que `ShellExecuteEx` ya sabía abrir. `papelera` → 343 puntos, `descargas` → 391.
- **`bloquear` sí; `apagar` y `reiniciar` no.** Va con su enmienda escrita antes del código
  (`SEGURIDAD.md §3.10`): `LockWorkStation` es lo mismo que `Win+L`, no pierde nada y se
  deshace con tu contraseña. Apagar y reiniciar siguen prohibidos por la regla 16, y no por
  tecnicismo: **Enter sobre una coincidencia difusa no es sitio para perder trabajo.**
- **Los prefijos web validan el esquema.** La plantilla sale de `lanzador.json`, que lo
  escribes tú, así que una con `file:` o con un esquema de aplicación convertiría el fichero
  de configuración en una forma de abrir cualquier cosa. Solo pasan `http` y `https`, y el
  término va escapado. Comprobado con las tres plantillas.
- **Calculadora con parser propio de descenso recursivo**, sin dependencias. Precedencia,
  paréntesis, unario, potencia asociativa por la derecha (`2^3^2` = 512, no 64), y la coma
  decimal de aquí (`1,5+1,5` = 3). `1234*0,15` → **185,1**.
- **Dos cosas que la calculadora no hace, y están escritas en `SEGURIDAD.md §4` para que no
  parezcan olvidos**: el `%`, porque no se sabe si quien lo escribe quiere un porcentaje o un
  módulo; y copiar el resultado, porque es la regla 14 — con la enmienda que haría falta ya
  redactada, por si algún día se quiere.
- **Un número suelto no es una cuenta.** Sin esa regla, escribir `5` sacaba una fila con un
  5. Hace falta al menos un operador y al menos un dígito.
- **21 comprobaciones nuevas**, incluidas las nueve de "esto NO es una cuenta": `1/0`,
  `(2+3`, `2+3)`, `a+b`, `10%3`.

### H5 — Los ficheros, por IPC con Everything

- **El riesgo que el plan marcaba como el primero a medir, resuelto a favor.** Everything
  instala un **servicio** que indexa con permisos y una **aplicación** que corre como tú;
  la ventana de IPC es de la aplicación, así que nuestro `SendMessage` no lo bloquea UIPI.
  Cinco consultas, cinco respuestas, ningún error. Si algún día corriera elevado, el
  programa lo dice en vez de quedarse mudo.
- **Medido, y la distinción importa**: la ida y vuelta completa es de **62–124 ms**, pero
  `SendMessage` devuelve en **0,3–0,8 ms**. O sea: Everything trabaja por su cuenta y
  nuestra ventana no se bloquea. Escribir no da tirones; los ficheros llegan un poco
  después que las aplicaciones, que salen en 0,5 ms.
- **Las constantes salen del header, no de memoria.** `ipc/everything_ipc.h` del
  Everything-SDK oficial, con el reparto de bytes copiado en un comentario al lado del
  código que lo usa.
- **Número de serie en la respuesta.** El header deja elegir el `dwData` con el que
  Everything contesta, así que lleva un contador en los 16 bits bajos: una respuesta de una
  consulta que ya no es la de ahora se tira sin mirarla. Sin eso, escribir rápido hace
  parpadear la lista con resultados viejos.
- **Rebote de 60 ms y mínimo de 3 letras.** Sin rebote, escribir "documento" serían nueve
  preguntas y ocho respuestas tiradas; con menos de tres letras Everything devolvería medio
  disco.
- **El buzón se busca en cada consulta**, no una vez al arrancar: Everything puede abrirse
  después que el lanzador, y cachear el handle dejaría los ficheros muertos hasta reiniciar.
- **Los ficheros puntúan 40 por debajo de las aplicaciones.** Con `seguridad`: primero
  "Seguridad de Windows" y detrás los cinco `SEGURIDAD.md`, cada uno con su ruta para poder
  distinguirlos.
- **El reparto de bytes tiene comprobación propia**, que es el código de más riesgo del
  proyecto: una respuesta armada a mano, más los casos en que la respuesta miente sobre su
  propio tamaño (un `numitems` imposible, un offset fuera del sobre, un sobre vacío, un
  puntero nulo). Un campo mal alineado no da un error, da basura.
- **La auditoría vigila `FindWindow` como vigila `SetForegroundWindow`**: la API está
  permitida, lo que se comprueba es que se use una sola vez, en `Everything.cs`, y sobre la
  clase del buzón.

### H4b — El diseño, a estilo Spotlight

- **Hueco de icono a la izquierda y la caja de búsqueda alineada con la columna de
  nombres.** Los iconos llegan en H7, pero el hueco se reserva ya: así el texto no se
  mueve cuando aparezcan, y mientras tanto se marca con un cuadrado apenas visible para
  que la sangría se lea como intencionada y no como un margen mal puesto.
- **Lo que dibuja Composition tapa a las ventanas hijas.** Pinté la franja con un
  `SpriteVisual` y desapareció lo que se escribía: el `EDIT` quedó debajo. El fondo de la
  franja lo pone ahora un `STATIC` hermano creado **antes** que la caja, que es lo que lo
  deja por debajo en el orden Z.
- **Un `EDIT` de una línea no centra su texto en vertical** si el control es mucho más
  alto que la letra: se pega arriba. Por eso el control mide solo lo que el texto y va
  centrado a mano, y el color de borde a borde lo pone el hermano de detrás.
- La franja pasa de 56 a 64 px y las filas de 44 a 48, con el texto de la caja a 22.

### H4 — La ventana

- **Funciona de punta a punta**: `Alt+Espacio` → escribes → flechas → Enter abre la
  aplicación, y el lanzamiento queda en `uso.json`. Comprobado con la Calculadora.
- **Medido: 70 ms el primer asomo, 28,8 ms los siguientes.** La ventana se crea al
  arrancar y solo se enseña, que era la premisa.
- **El fallo que solo aparece con varias pantallas.** La ventana se dimensionaba con el
  DPI de la pantalla **principal** y se asoma en la del cursor: en el monitor al 125% salía
  un 20% pequeña. Ahora se reescala —ancho, fuente de la caja y escala de Composition— en
  cada asomo. No se atiende `WM_DPICHANGED` a propósito: esta ventana no se arrastra y se
  esconde al perder el foco, así que el único momento en que cambia de pantalla es justo
  antes de asomarse.
- **Y la sonda volvió a mentir, por tercera vez.** Medí 528x339 donde el programa decía
  825x530: PowerShell no es *per-monitor DPI aware* y ve coordenadas virtualizadas. Desde
  entonces la sonda llama a `SetProcessDpiAwarenessContext` antes de medir nada.
- **`DwmExtendFrameIntoClientArea` fuera, y está medido por qué.** Con el marco extendido a
  toda la ventana, lo que pinta GDI queda con alfa cero y DWM lo mezcla: el `EDIT` salía
  `#7F7F7F` en vez del color que se le daba. Sin él, sale `#2B2B2B`, que es el pedido.
- **El acrílico se comprobó en vez de darlo por bueno**: con `DWMSBT_TRANSIENTWINDOW` el
  cuerpo mide `#545454`; con `DWMSBT_NONE`, `#121212` (el escritorio). DWM lo está pintando.
- **Sin `SetWindowSubclass`.** Las teclas de navegación se cazan en nuestro propio bucle de
  mensajes antes de despachar. Menos código y una entrada menos en `NativeMethods.txt`.
- **Una sola superficie para toda la lista**, no dos por fila. Un `BeginDraw` y una subida
  de píxeles por pulsación en vez de dieciséis objetos nuevos.
- **La auditoría saltó y tenía razón a medias.** Contaba la entrada de `NativeMethods.txt`
  como una segunda llamada a `SetForegroundWindow`. Una declaración no es una llamada: la
  regla de "a quién se le aplica" ahora mira solo los `.cs`, y **a cambio** se exige que la
  declaración exista y esté una sola vez, que antes no se comprobaba.
- **Traza con `LANZADOR_LOG=1`**, como el `DOCK_HOVER_LOG` del dock. Va a la consola y solo
  si la pides: no es un registro (regla 11). Es lo que localizó que `EN_CHANGE` sí llegaba.

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
