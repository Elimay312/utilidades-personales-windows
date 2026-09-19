# Cambios

El registro va por hitos, que es como se construye: cada uno tiene que compilar y ejecutarse
antes de empezar el siguiente.

Los números que aparecen aquí están medidos, no estimados. El detalle de cada medición está
en el mensaje de su commit.

---

## Sin publicar

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
