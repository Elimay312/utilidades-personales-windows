# Cambios

El registro va por hitos, que es como se construye: cada uno tiene que compilar y ejecutarse
antes de empezar el siguiente.

Los números que aparecen aquí están medidos, no estimados. El detalle de cada medición está
en el mensaje de su commit.

---

## Sin publicar

### H0 — Las reglas, antes del código

- **`SEGURIDAD.md` propio**, escrito antes de la primera línea. No es el del lanzador con
  otro nombre: los vecinos de esta carpeta tocan ventanas, teclas y procesos, y **este es el
  único que puede destruir algo que no se pueda recuperar**. El criterio se apoya en tres
  cortes propios: *mover no es borrar*, *se ven nombres y nunca contenido*, y *solo lo que
  sueltas y solo después de verlo*.
- **La regla 2 no tiene excepciones, y eso ahorró código.** El primer borrador dejaba que
  `--check` limpiase su carpeta temporal, con la excepción escrita en el documento. Se cayó:
  una regla absoluta que el auditor puede comprobar con un patrón vale más que una regla con
  una excepción que hay que leer. `--check` reutiliza `%TEMP%\renombrar-check` y limpiarla es
  cosa de Windows.
- **Dos comprobaciones positivas en `auditar.ps1`**, que son las que un patrón de prohibición
  no puede hacer porque aquí la API está permitida y lo que se vigila es *dónde* se llama:
  `File.Move` solo puede aparecer en `Aplicar.cs` y nunca con tercer argumento, y `Regla.cs` y
  `Previa.cs` no pueden escribir en el disco. Cuentan **apariciones, no líneas** — la lección
  que el lanzador pagó en su propio H0.
- **El auditor tenía un agujero, y lo encontró romperlo a propósito, no leerlo.** Con el
  primer patrón, la regla 3 buscaba `overwrite:` y `File.Move(a, b, true)` le pasaba por
  delante diciendo `limpio` — el argumento posicional es el mismo overwrite sin escribir la
  palabra. Lo cazaba de rebote la comprobación positiva, pero solo porque el fichero de
  prueba no se llamaba `Aplicar.cs`. Patrón corregido y vuelto a probar dentro y fuera de
  `Aplicar.cs`. **Tres violaciones metidas a mano, tres detectadas**, y la que iba dentro de
  un comentario correctamente ignorada.
- **`InvariantGlobalization` va a `false`, al revés que el dock y la isla.** Aquí se comparan,
  se ordenan y se pasan a mayúsculas **nombres de fichero del usuario**: sin ICU, `Niño` y
  `NIÑO` dejan de ser el mismo fichero para la comparación y el orden alfabético deja de ser
  el del Explorador. Es la primera vez en esta carpeta que el peso de ICU se paga a propósito.
- **Los cinco modos de consola existen desde el primer commit** y devuelven 2 diciendo en qué
  hito llegan, en vez de fingir que pasaron. Una comprobación que aprueba sin mirar es peor
  que no tenerla.
