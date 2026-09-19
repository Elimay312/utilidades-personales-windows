# Cambios

El registro va por hitos, que es como se construye: cada uno tiene que compilar y ejecutarse
antes de empezar el siguiente.

Los números que aparecen aquí están medidos, no estimados. El detalle de cada medición está
en el mensaje de su commit.

---

## Sin publicar

### H2 — Aplicar, y deshacer

- **El orden de las dos pasadas era un fallo, y lo encontró escribir el caso.** Los ficheros
  que pasan por un temporal tienen que moverse **después** de los directos: en una cadena
  `x→y, y→z`, la `x` espera a que `y` suelte su nombre, y la `y` no pasó por temporal porque
  su destino no era de nadie. Con los temporales primero, la cadena fallaba entera. Metido
  otra vez a propósito: `FALLA una cadena x->y, y->z se hace entera`.
- **La ida y vuelta se comprueba sobre disco de verdad**, en una subcarpeta con la hora dentro
  de `%TEMP%\renombrar-check`. Con la hora porque aquí no se borra nada: reutilizar la carpeta
  arrastraría lo de la pasada anterior y la comprobación dejaría de empezar desde lo mismo.
  **Cada fichero lleva su nombre escrito dentro**, porque "los nombres están bien" se cumple
  igual aunque el contenido se haya cruzado, y ese es el fallo que más duele.
- **Los cuatro casos del disco, todos en verde**: el intercambio `a↔b`, el cambio de solo
  mayúsculas (`FOTO.txt` sale en mayúsculas de verdad en el disco), la cadena completa, y un
  destino imposible —una carpeta ocupando el nombre— que para el lote **y devuelve el temporal
  que ya había creado**. Ningún `.renombrar-tmp` sobrevive a ninguno de los cuatro.
- **Medido sobre la carpeta de recibos de prueba**: 6 ficheros a `Recibo_{fecha}_{n:000}`,
  `--deshacer`, y los seis nombres vuelven exactos, `CON.pdf` incluido. El diario se queda sin
  nada que deshacer y el segundo `--deshacer` lo dice en vez de inventarse algo.
- **Se cayó el guardia de la entrada redirigida.** Pedía ejecutarlo a mano si `stdin` venía de
  una tubería, y no protegía de nada: escribir `si` en una tubería es tan deliberado como
  teclearlo, y la tabla se imprime antes en los dos casos. Lo que impide cruzar esa puerta por
  inercia es tener que escribir la palabra, no de dónde viene.
- **Las filas en rojo no paran el lote**: se quedan fuera, se dice cuántas y se renombra el
  resto. Parar por una colisión en una carpeta de 300 ficheros sería obligar a arreglarlo todo
  antes de poder hacer nada.

### H1 — El motor y la vista previa

- **Siete tipos de regla se quedaron en cinco, y hacen más.** El primer diseño tenía
  `Numerar`, `Insertar` y `Fecha` por separado, y cada una necesitaba su posición, su formato
  y su origen. Con fichas dentro de `Plantilla` — `{nombre}`, `{n:000}`, `{fecha:yyyy-MM}` —
  las tres se colapsan en una y encima **se combinan**, que es justo lo que pide un recibo:
  `Recibo_{fecha}_{n:000}`. La caja (MAYÚS/minús/Título) tampoco es un campo: son tres
  valores del enum, y así no hay un campo que solo signifique algo para un tipo.
- **La previa no toca el disco, y por eso `--check` comprueba los seis estados sin crear un
  solo fichero.** Los nombres ya ocupados de la carpeta entran como parámetro en vez de
  preguntarle al disco. Es lo que permite que el caso "el destino ya existe" —el que solo se
  ve con un disco de verdad— tenga su comprobación como todos los demás.
- **Un fallo real, encontrado escribiendo el caso y no leyendo el código.** `1.txt → 2.txt`
  salía `Ok` cuando `2.txt` estaba en el lote pero **no se movía**: el código miraba "está en
  el lote" en vez de "va a quedar libre". Al aplicarlo, Windows habría dado error y el lote se
  habría parado a mitad. Arreglado, y metido otra vez a propósito para ver a `--check`
  detectarlo: `FALLA un destino del lote que NO se mueve sigue estando ocupado`.
- **`SinCambio` va antes que `Invalido`.** Una carpeta con un `CON.pdf` dentro salía entera en
  rojo y `--previa` devolvía 1 aunque el lote no fuese a tocarlo. Si el nombre no cambia no
  hay nada que validar: ya existe en el disco.
- **El orden de las filas es el del Explorador, con `StrCmpLogicalW`.** Medido con la carpeta
  de prueba: `FACTURA acme 2`, `3`, `10` — en orden alfabético el 10 va antes que el 2, y la
  numeración habría salido barajada sin que la tabla pareciera decir nada raro. Una entrada en
  `NativeMethods.txt` sale más barata y es más correcta que escribir el comparador a mano.
- **`--previa` imprime la misma tabla que la ventana**, con los mismos estados y la misma
  llamada a `Previa.Calcular`. No es una versión reducida para depurar: es lo que la hace
  valer como sonda. Devuelve 1 si alguna fila no se podría renombrar.
- **36 comprobaciones en `--check`, todas en verde**, incluidas la ñ subiendo a mayúsculas
  (que es lo que se pierde sin ICU), los nombres reservados con extensión, y las dos caras del
  intercambio `A→B, B→A`.

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
