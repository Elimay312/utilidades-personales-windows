# Utilidades para Windows

Carpeta contenedora de varias utilidades que cambian cómo se usa el escritorio de
Windows 11. **Cada una es un proyecto aparte, con sus reglas y su documentación**,
reunidas en este repositorio para poder clonarlas y probarlas en otro equipo.

> Esta carpeta todavía se llama `dock-mac-en-windows` por herencia del primer proyecto.
> Renómbrala cuando quieras; nada del código depende del nombre.

## Qué hay

| Proyecto | Qué es | Estado |
|---|---|---|
| [`dock/`](dock/README.md) | Un dock estilo macOS: magnificación, efecto genio, uno por pantalla, miniaturas de ventanas. | En uso diario. Falta pulir rendimiento. |
| [`hud/`](hud/README.md) | El aviso de volumen, rehecho: cápsula de cristal abajo y centrada, que se transforma en vez de ir y venir. Las teclas son suyas, así que el recuadro gris de Windows no sale. | Funcionando en tres pantallas. Falta usarlo unos días. |
| [`isla/`](isla/README.md) | Una isla dinámica en el borde superior de la pantalla en la que estés trabajando: qué suena, de quién, cuánto queda, y poder pausarlo. Más pomodoro, batería y volumen —con el número y por qué altavoces sale. | Funcionando. Falta probarla en otros equipos. |
| [`lanzador/`](lanzador/README.md) | `Alt+Espacio` y escribes: aplicaciones, ficheros (vía Everything), prefijos web, sitios del sistema y cuentas, ordenados por lo que más abres. | Funcionando. Falta usarlo unos días. |
| [`proyectos-github/`](proyectos-github/README.md) | **Brújula**: los ~120 repositorios de GitHub ordenados por en cuál conviene trabajar, con el siguiente paso de cada uno guardado para no tener que reconstruirlo. | En construcción: fase 1 de 8. Hay ventana, todavía no hay datos. |
| [`quicklook/`](quicklook/README.md) | Vista previa con la barra espaciadora: seleccionas un archivo en el Explorador, pulsas espacio, lo ves. Imágenes, PDF, vídeo y audio. | Funcionando. Falta usarlo unos días. |
| [`rayo-file-manager/`](rayo-file-manager/CLAUDE.md) | **Rayo**: un gestor de archivos en C++ con vista previa, vigilancia de carpetas y orden natural. | Funcionando. Le falta README propio; por ahora su documentación es `CLAUDE.md`. |
| [`renombrar/`](renombrar/README.md) | Renombrado masivo con vista previa: ves la tabla antes → después de todo el lote, y solo entonces se aplica. Con deshacer. | Funcionando. Falta usarlo unos días en la oficina. |

## Qué podría haber

Ideas, no compromisos. Cada una sería su propia carpeta y su propio repo.

- **Gestor de ventanas** — colocar ventanas en zonas, y que cada app se abra donde le toca.
- **Atajos a utilidades** — abrir cosas concretas del sistema sin buscarlas. Aunque el dock
  ya ancla `ms-settings:` y `shell:`, así que a lo mejor esto ya está hecho.

## Por qué cada uno va por separado

Son utilidades distintas y **fallar juntas no tiene sentido**: si el gestor de ventanas
revienta, el dock tiene que seguir. Repos separados, procesos separados, configuraciones
separadas.

Si algún día dos necesitan hablarse, se conectan **entonces** y con la interfaz más
pequeña que funcione. Adelantar una biblioteca compartida antes de tener el segundo
proyecto es adivinar qué comparten, y casi siempre se acierta mal.

### Lo que pasó la primera vez que dos se rozaron

El HUD y la isla enseñan los dos el volumen, y a los dos les faltaba lo mismo: decir **de
qué dispositivo** estaban hablando. Parecía el caso de libro de «estos dos tienen que
hablarse».

**No hizo falta ninguna interfaz.** No hay mensaje entre ventanas, ni tubería, ni fichero
compartido, ni uno depende de que el otro esté corriendo. Los dos le preguntan al sistema,
que es la fuente de verdad de las dos, y por eso coinciden sin coordinarse: mismo dato,
mismo instante. Si desinstalas uno, el otro sigue entero.

Lo que sí viajó de un proyecto al otro fue **una medición**: que cambiar el dispositivo de
salida *no* invalida el endpoint de audio que tienes abierto —no falla, se queda
contestando del anterior—. El HUD lo midió con una sonda, la isla tenía exactamente el
mismo fallo latente escrito desde su primer día, y arreglarlo allí costó leer el
`CHANGELOG` del vecino. Eso es lo que estos proyectos comparten de verdad: no código, sino
lo que a cada uno le costó averiguar.

## Sobre las reglas de seguridad

**`dock/SEGURIDAD.md` es del dock y solo del dock.** No es una norma de la casa.

Lo que sí merece la pena copiar es el *método*, porque funcionó:

1. **Se escribe antes de empezar el proyecto**, no después. Sirve para decidir qué no vas
   a hacer mientras todavía no cuesta nada renunciar a ello.
2. **Se rediseña al llegar al mínimo viable**, cuando ya sabes qué necesitabas de verdad y
   qué te inventaste. El del dock nació como una tabla y cuatro enmiendas encadenadas; al
   llegar al producto se reescribió entero por temas, y solo entonces se entendía.
3. **Lleva su propio script de auditoría** que se ejecuta y devuelve 0 o 1. Una lista de
   prohibiciones que nadie comprueba es una carta de intenciones.

Cada utilidad nueva necesita el suyo, con sus propias reglas: un gestor de ventanas mueve
ventanas ajenas y un lanzador lee el menú Inicio — no se parecen en nada a lo que hace un
dock, y copiar sus reglas tal cual sería o quedarse corto o estorbar.

El del lanzador lo demostró: hizo falta **abrir** tres cosas que los vecinos tienen cerradas
(guardar un historial de uso, ponerse en primer plano, bloquear la sesión) y **cerrar** una
que ellos no necesitaban nombrar — que los iconos se piden con `SIIGBF_ICONONLY`, porque sin
esa bandera lo que llega es la miniatura, o sea el contenido de tus documentos.

Y el del hud demostró la otra mitad, que es más rara de ver: abrió una excepción con todo el
papeleo hecho — apartar el aviso nativo de Windows, con su justificación y su regla propia en
la auditoría — y **al ir a medirla resultó que sobraba**. Escribirla obligó a medirla, y
medirla se llevó por delante dos hitos del plan antes de que costaran una línea de código. El
documento no frenó el proyecto: lo hizo más pequeño.
