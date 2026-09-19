# Cambios

El registro va por hitos, que es como se construye: cada uno tiene que compilar y ejecutarse
antes de empezar el siguiente.

Los números que aparecen aquí están medidos, no estimados. El detalle de cada medición está
en el mensaje de su commit.

---

## Sin publicar

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
