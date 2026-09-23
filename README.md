# Utilidades para Windows

Carpeta contenedora de varias utilidades que cambian cómo se usa el escritorio de
Windows 11. **Cada una es un proyecto aparte, con sus reglas y su documentación**,
reunidas en este repositorio para poder clonarlas y probarlas en otro equipo.

> Esta carpeta todavía se llama `dock-mac-en-windows` por herencia del primer proyecto.
> Renómbrala cuando quieras; nada del código depende del nombre.

## Qué hay

| Proyecto | Qué es | Estado |
|---|---|---|
| [`calendario/`](calendario/README.md) | **Agenda**: un calendario en C++ que sale con un atajo en el monitor del ratón; escribes `mañana 5pm dentista` y lo crea, sincronizado con Google Calendar y Tasks. | 1.0.0, con instalador. |
| [`dock/`](dock/README.md) | Un dock estilo macOS: magnificación, efecto genio, uno por pantalla, miniaturas de ventanas. | En uso diario. Falta pulir rendimiento. |
| [`hud/`](hud/README.md) | El aviso de volumen, rehecho: cápsula de cristal abajo y centrada, que se transforma en vez de ir y venir. Las teclas son suyas, así que el recuadro gris de Windows no sale. | Terminado. Funciona en tres pantallas y sigue al dispositivo de salida. |
| [`isla/`](isla/README.md) | Una isla dinámica en el borde superior de la pantalla en la que estés trabajando: qué suena, de quién, cuánto queda, y poder pausarlo. Más pomodoro, batería y volumen —con el número y por qué altavoces sale—, y los recordatorios de Agenda en su propia burbuja. | Funcionando (dos islas: principal y avisos). Falta probarla en otros equipos. |
| [`lanzador/`](lanzador/README.md) | `Alt+Espacio` y escribes: aplicaciones, ficheros (vía Everything), prefijos web, sitios del sistema y cuentas, ordenados por lo que más abres. | Funcionando. Falta usarlo unos días. |
| [`panel-de-control/`](panel-de-control/README.md) | **Panel**: un centro de control estilo macOS con `Ctrl+Alt+A`: Wi-Fi, Bluetooth, luz nocturna, brillo, volumen y un interruptor por cada utilidad de esta carpeta. | 8 de 8 fases, con instalador. Falta la 4b, los monitores externos, para cuando estén las tres pantallas. |
| [`proyectos-github/`](proyectos-github/README.md) | **Brújula**: los ~120 repositorios de GitHub ordenados por en cuál conviene trabajar, con el siguiente paso de cada uno guardado para no tener que reconstruirlo. | Terminado (8 de 8 fases). Sincronización en segundo plano, inspector, revisión semanal y empaquetador. |
| [`quicklook/`](quicklook/README.md) | Vista previa con la barra espaciadora: seleccionas un archivo en el Explorador, pulsas espacio, lo ves. Imágenes, PDF, vídeo y audio. | Funcionando. Falta usarlo unos días. |
| [`rayo-file-manager/`](rayo-file-manager/CLAUDE.md) | **Rayo**: un gestor de archivos en C++ con vista previa, vigilancia de carpetas y orden natural. | 1.0.0, con instalador (8 de 8 fases). Su documentación es `CLAUDE.md`. |
| [`renombrar/`](renombrar/README.md) | Renombrado masivo con vista previa: ves la tabla antes → después de todo el lote, y solo entonces se aplica. Con deshacer. | Funcionando. Falta usarlo unos días en la oficina. |

## Atajos

Sacados del código, no de la memoria. El detalle fino de cada uno está en el README o el
`CLAUDE.md` de su proyecto.

### Globales

| Atajo | App | Qué hace | Se cambia en |
|---|---|---|---|
| `Alt+Espacio` | Lanzador | Muestra o esconde el lanzador. | `atajo` en `lanzador.json` (al reiniciar) |
| `Alt+Shift+C` | Agenda | Abre o cierra el popup, en el monitor del ratón. | `hotkey` en el JSON, o Configuración en caliente |
| `Ctrl+Alt+I` | Isla | Rota brasa → asomada → abierta. Con un aviso esperando, abre su tarjeta. | — |
| `Ctrl+Alt+T` | Isla | Arranca un pomodoro; otra vez, lo cancela. | Duración: `pomodoroMinutos` (25) |
| `Ctrl+Alt+A` | Panel | Abre o cierra el panel, en la pantalla del ratón. | `hotkey` en `panel.json` (al reiniciar) |
| `Ctrl+Alt+H` | HUD | Cierra el HUD. | — |
| Teclas de volumen | HUD | Subir, bajar y silenciar: se las queda para que no salga el aviso de Windows. | Paso: `pasoVolumen` (2 %) |
| `Espacio` | QuickLook | Abre o cierra la vista previa. Solo con el Explorador delante y sin estar escribiendo. | — |
| `Esc` | QuickLook | Cierra la vista previa, con las mismas condiciones. | — |
| *ninguno por defecto* | Dock | Rota entre el dock de siempre y cada perfil. | `atajoPerfil` en `dock.json` (p. ej. `Ctrl+Alt+D`) |

Brújula, Rayo y Renombrar no tienen atajo global.

### Dentro de cada app

**Lanzador.** `Enter` lanza · `Ctrl+Enter` abre la carpeta del fichero · `↑`/`↓` cambian
de fila · `Esc` esconde · `Ctrl+V` pega · `Ctrl+A` selecciona todo. En la caja, lo normal:
`←`/`→` (con `Ctrl`, por palabras; con `Shift`, seleccionando), `Inicio`/`Fin`, `Ctrl+Retroceso`.

**Agenda, popup.** `Enter` crea lo que dice la vista previa · `Esc` esconde · con el campo
vacío, `←`/`→` cambian de día y `↑`/`↓` de semana · `Ctrl+Enter` abre la app en ese día ·
`Ctrl+,` Configuración · `Ctrl+Z` deshace mientras sale el aviso · `Tab` pasa campo → mes →
tarjetas; en el mes `RePág`/`AvPág` cambian de mes, en las tarjetas `Espacio` marca la tarea.

**Agenda, app expandida.** `D`/`S`/`M` vista día, semana o mes · `T` hoy · `←`/`→` periodo
anterior o siguiente · `Ctrl+K` al campo · `Supr` borra el evento (pregunta antes) · `Ctrl+Z`
deshace · sobre un evento, `Alt+↑↓` lo mueve ±15 min, `Alt+←→` ±1 día y `Ctrl+↑↓` cambia su
final ±15 min · `Esc` va soltando capas hasta contraer. En el detalle, `Enter` guarda el campo
y `Shift+Enter` es salto de línea en las notas.

**QuickLook.** `Shift` + rueda sobre el panel: archivo anterior o siguiente de la selección.

**Brújula.** `Ctrl+K` paleta de comandos · `Ctrl+F` o `/` buscar · `Ctrl+R` sincronizar ·
`Ctrl+Shift+R` revisión semanal · `J`/`K` o flechas para moverse · `Enter` abre el inspector ·
`1`–`4` prioridad (Enfoque, Secundario, Algún día, Archivado) · `E` siguiente paso y `N`
novedad, con el inspector abierto · `Ctrl+G` lista o cuadrícula · `Ctrl+Z` deshace ·
`Ctrl+O` abre el repo en GitHub · `Esc` va soltando capas. En la revisión: `1`–`4` decide,
`E` edita, `P` aplaza, `Espacio` salta, `Esc` sale.

**Rayo.** Estilo vim, y se pueden cambiar en `%APPDATA%\Rayo\config.ini`, sección `[keys]`.
`J`/`K` o `↓`/`↑` · `L`, `→` o `Enter` entra o abre · `H` o `←` sube · `gg`/`Inicio` y
`G`/`Fin` · `Ctrl+D`/`Ctrl+U` media página · `Espacio` marca · `Y` copia, `X` corta, `P`
pega · `D` a la Papelera, `Shift+D` borra del todo (pregunta) · `R` renombra · `A` crea
(acabado en `\`, carpeta) · `/` filtra y `Esc` quita el filtro · `:` ir a ruta, con `Tab`
autocompleta · `~` carpeta de usuario · `.` ocultos · `T` pestaña nueva, `Ctrl+W` la cierra,
`1`–`9` salta a una · `M`+letra guarda marcador, `'`+letra va a él · `Q` sale.

**Renombrar.** Ninguno: solo lo que traen de serie los controles de Windows.

**Dock, HUD e Isla.** Nada dentro: no cogen el foco.

## Tenerlos al día en otro equipo

```powershell
winget install --id Git.Git -e                                                   # si el equipo no tiene git
git clone https://github.com/Elimay312/utilidades-personales-windows.git; cd utilidades-personales-windows
powershell -NoProfile -ExecutionPolicy Bypass -File actualizar.ps1 -Todo       # equipo nuevo: instala todo
powershell -NoProfile -ExecutionPolicy Bypass -File actualizar.ps1 -Programar  # y desde entonces, solo al iniciar sesión
```

Hace `git pull` y reinstala desde el fuente solo lo que cambió y ya está instalado en ese
equipo. Lo que falta para compilar (.NET 10, Build Tools de C++) lo instala él. El
actualizador vive **fuera** de las apps a propósito: sus `SEGURIDAD.md` prohíben cualquier
llamada de red, updater incluido, y eso no cambia. Registro en `%LOCALAPPDATA%\Utilidades\actualizar.log`.

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

### La segunda vez sí hizo falta hablar

Los recordatorios de **Agenda** tenían que salir en la **isla**, como en la de Xiaomi, y ahí
el sistema no tiene nada que preguntar: el recordatorio solo lo sabe Agenda. Así que se hizo
la interfaz, y la más pequeña que funciona: **una tubería con nombre y una línea de JSON**.
Agenda deja un aviso —título, línea, color, botones— y la isla le contesta qué botón se
pulsó. Sin biblioteca compartida, sin que ninguno cargue código del otro.

La regla de arriba se sigue cumpliendo: **cada uno funciona solo.** Si la isla no está,
Agenda saca su notificación de Windows; si Agenda no está, a la isla no le llega nada. Y como
la isla era la que abría una puerta, la enmienda de sus reglas se escribió y se commiteó
antes que el código: [`isla/SEGURIDAD.md` §3.7](isla/SEGURIDAD.md).

### La tercera vez bastó una firma

Mover el volumen desde el deslizador del **Panel** sacaba a la vez la cápsula del **HUD** y el
aviso de la **isla**, a cada paso del arrastre. Tampoco hizo falta interfaz. Windows ya deja que
quien cambia el volumen lo firme con un GUID, y lo entrega en el aviso a todos los que escuchan.
Así que el Panel firma sus cambios, y el HUD y la isla, al ver esa firma, guardan el nivel sin
enseñarlo. Si el Panel no está, no hay firma y todo sigue igual.

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
