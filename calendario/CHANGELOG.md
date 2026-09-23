# Changelog

Todos los cambios relevantes de Agenda se documentan aquí.

El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el versionado
sigue [SemVer](https://semver.org/lang/es/).

## [Unreleased]

### Añadido

- **Solo este o toda la serie.** Arrastrar, editar o borrar un evento que se repite pregunta
  cuál, en la cápsula de abajo, con **Solo este** y **Toda la serie** (flechas, `Enter`, `Esc`
  o el ratón). *Solo este* aparta esa repetición de la serie igual que Google —en su día nuevo,
  con sus cambios— y la sube como cambio de esa ocurrencia; la serie sigue igual. El panel
  abierto sobre una repetición enseña su fecha y no la de la primera. Captura `app-repeticion`.
- **Repeticiones mensuales por posición y por día del mes.** «El primer martes», «el último
  viernes», «el día 15» y «el último día del mes», que son las que ofrece el menú de repetir de
  Google, ya aparecen en todos sus días; antes se quedaban en el primero. También las anuales
  con mes («el cuarto jueves de noviembre») y el viernes 13 de toda la vida (`BYDAY` y
  `BYMONTHDAY` a la vez).

### Cambiado

- **Varios monitores: el popup se abre donde está el ratón.** Antes salía siempre en el monitor
  principal. Ahora, sin `--monitor`, cada apertura (atajo o bandeja) va a la esquina del
  monitor que tiene el ratón, con su escala; la configuración también. `--monitor=N` y
  `AGENDA_DEV_MONITOR` siguen fijando uno.

- **Una hora sin a. m. ni p. m. se decide mejor, y si hay duda se pregunta.** `4:05` ya no es
  de madrugada por escribirse con dos puntos: va por la misma regla que `a las 4`. Hoy vale la
  mitad del día que todavía no ha pasado (`hoy a las 5` a las diez son las 17:00). Si las dos
  siguen por delante, o el día es otro, la vista previa pregunta **a. m. / p. m.** con la más
  probable marcada, y se cambia con un clic o con `↑` `↓`. `04:05`, `16:05`, `4pm` y `de la
  tarde` no preguntan.

### Corregido

- **Una repetición movida en Google salía dos veces.** Mover o editar una sola ocurrencia de
  una serie en la web la dejaba en su día nuevo y también en el viejo, y una borrada allí
  seguía aquí. Ahora Agenda guarda esas ocurrencias apartadas con su serie y el día que
  tenían, y la serie se salta ese día en la lista, en la semana, en los puntos del mes y en los
  recordatorios. También se leen los `EXDATE` de la regla, que antes se tiraban. Esquema v4
  (dos columnas en `events`); la migración vuelve a bajar los calendarios una vez para traer
  las excepciones que ya existían.
- **Un evento creado a última hora nunca avisaba.** Con un recordatorio de 30 minutos, algo a
  las 16:05 apuntado a las 16:00 ya había perdido su aviso al nacer. Ahora, si todos sus
  recordatorios pasaron antes de escribirlo, avisa una vez al empezar.
- **Alt+F4 sobre el popup lo dejaba inservible.** Destruía la ventana y el atajo y la bandeja no
  tenían nada que enseñar hasta reiniciar Agenda. Ahora lo oculta, como el atajo.

## [1.0.0] - 2026-09-22

### Añadido

- Fase 7: **ventana de configuración**, con el mismo sistema de diseño que el resto: atajo
  global (se graba pulsándolo y dice en rojo si otra aplicación lo tiene), idioma, tema,
  iniciar con Windows, calendario por defecto, duración por defecto de un evento y cuenta de
  Google. Se abre desde la bandeja o con **Ctrl+,**; todo se aplica al momento y se guarda en
  `%LOCALAPPDATA%\Agenda\config.local.json`, fusionado para no tocar las credenciales. Es una
  ventana normal, con barra de título del color del panel, y se destruye al cerrarla: en la
  bandeja no cuesta nada.
- Fase 7: **interfaz en español o en inglés.** Cada texto lleva sus dos versiones juntas en el
  sitio donde se dibuja (`T(L"Sin eventos", L"No events")`), incluidos los nombres de mes y de
  día, la vista previa, los avisos, el menú de la bandeja y las notificaciones. El parser sigue
  entendiendo los dos idiomas a la vez.
- Fase 7: **notificaciones nativas de recordatorio.** Agenda lee los recordatorios de Google
  —los del evento o, si no tiene, los del calendario; solo los de tipo notificación— y avisa en
  su minuto con un toast de Windows en el escenario *reminder*: se queda hasta que se responde,
  con posponer y descartar resueltos por el propio Windows, y un clic abre el popup en ese día.
  Sin acceso en el menú Inicio (una build sin instalar) cae al globo de la bandeja. Tras una
  suspensión solo avisa de lo que venció en el último cuarto de hora.
- Fase 7: **accesibilidad.** Proveedores de UI Automation para el campo de texto (con el patrón
  Value, que también escribe), el mes (un Calendar de 6 × 7 con Grid, GridItem y
  SelectionItem), la lista del día, y en la app las pestañas, los eventos, los calendarios, las
  tareas sin fecha y el panel de detalle. Los cambios de foco y de día se anuncian, y los avisos
  se leen con `UiaRaiseNotificationEvent`. El módulo es uno solo (`src/ui/accessibility.*`): cada
  ventana describe lo que tiene en pantalla como una lista plana y los proveedores la vuelven a
  pedir en cada llamada, sin un árbol de objetos que mantener sincronizado.
- Fase 7: **todo con el teclado.** `Tab` recorre zonas —en el popup, campo, mes y tarjetas; en
  la app, campo, eventos, panel de detalle, calendarios y tareas— con un anillo de foco que solo
  aparece cuando se usa el teclado. Las flechas eligen, `Espacio` marca, `Enter` abre, y el
  arrastre tiene su versión: `Alt+↑↓` mueve el evento un cuarto de hora, `Alt+←→` un día y
  `Ctrl+↑↓` estira su final. El panel de detalle incluye el selector de calendario, la
  repetición y el botón de borrar en su orden de tabulación. La tabla completa está en el README.
- Fase 7: **alto contraste.** Con un tema de contraste de Windows, Agenda pinta con los colores
  del sistema, sin transparencias ni tintes, y bordea tarjetas y bloques. Cambia en caliente.
  `--theme=contrast` lo renderiza en las capturas.
- Fase 7: **la app expandida se mueve** arrastrando la franja de su título, también entre
  monitores con distinta escala: conserva su tamaño en DIP, se redibuja al DPI nuevo y, al
  contraerse, cae en la esquina del monitor donde está.
- Fase 7: **instalador nativo**, `Instalar-Agenda.exe`: un solo archivo con Agenda dentro, por
  usuario y sin administrador, en `%LOCALAPPDATA%\Programs\Agenda`, con acceso en el menú Inicio
  (que lleva el AppUserModelID de los toasts), arranque con Windows marcado por defecto, entrada
  en *Aplicaciones instaladas* y desinstalador que conserva los datos salvo que se pida lo
  contrario. `--silent` para las dos cosas. `empaquetar.ps1` lo genera.
- Fase 7: capturas `configuracion`, `configuracion-claro`, `popup-contraste` y
  `app-semana-contraste`.
- **Los recordatorios van a la Isla** cuando está corriendo, como la isla de Xiaomi: asoman,
  se recogen en una burbuja con el día del evento en el color del calendario (el campo
  `insignia` del buzón) y al abrirlos ofrecen Terminado, posponer 5 o
  10 minutos y Abrir, que trae la app con el evento abierto (`src/app/isla.*`). Sin la isla, o
  con su buzón lleno, sale el toast de siempre. Un aviso sin responder se retira al terminar su
  evento.

### Cambiado

- **Esquema v3**, con permiso y solo con columnas nuevas: `events.reminders` (NULL es «los del
  calendario») y `calendars.reminders`. La migración vacía los `syncToken` para que la siguiente
  pasada baje todo una vez y rellene los recordatorios. El calendario local avisa diez minutos
  antes.
- **El tema ya no es solo el de Windows**: la configuración puede fijar oscuro o claro. El alto
  contraste manda sobre todo.
- **La duración de un evento sin duración** sale de la configuración (60 minutos por defecto).
- **En reposo, Agenda ocupa lo que ocupa un popup**: el swap chain tiene el tamaño del popup y
  crece al de la app una sola vez, al empezar la expansión y antes de su primer fotograma; al
  ocultarse vuelve al del popup y devuelve sus páginas a Windows. Medido con la versión
  instalada: del atajo al primer fotograma compuesto, 17 ms de mediana y 23 ms de p95; en
  reposo, 0,9 MB de memoria privada en uso, 2,3 MB de *working set* y 54,5 MB de bytes
  privados.
- En la app, **Esc quita también la selección** antes de contraer, y `↑↓` recorren los eventos
  cuando hay uno seleccionado (sin selección siguen desplazando las horas).

### Corregido

- `config.example.json` volvió al repositorio: se había borrado sin querer en `ea578ac`.
- La duración por defecto que se guardaba desde la configuración no se habría vuelto a leer:
  se escribía como entero con signo y se leía solo si era sin signo. Lo encontró su test.
- Con la escala de su propio monitor cambiada, Windows no avisa a la ventana del popup ni le
  cambia el DPI (a una ventana con barra de título del mismo proceso sí). Agenda compara ahora
  el DPI del monitor al cambiar los ajustes o las pantallas y se reescala sola.
- **Cerrar el popup dejaba un fantasma**: el material y el borde redondeado los dibuja DWM
  detrás de la ventana, fuera del visual que se funde, así que durante los 120 ms del cierre
  quedaba un panel gris entero con el contenido apagándose dentro. Ahora el cierre quita el
  material y el borde, pinta el panel opaco y se funde todo a la vez.

## [0.6.0] - 2026-09-22

### Añadido

- Fase 6a: **el popup se expande a la app completa.** Un clic en el mes o **Ctrl+Enter** hace
  crecer la misma ventana hasta el 80 % del área de trabajo, centrada, con un muelle de
  rigidez 300 y amortiguación 30 que anima a la vez posición, tamaño y radio (de 14 a 8 DIP).
  **Esc** o el botón de contraer hacen el camino inverso, también a mitad de la expansión: el
  muelle da la vuelta desde donde esté, con la velocidad que llevaba. Con «reducir animaciones»
  de Windows es un salto directo.
- Fase 6a: **no es un corte a otra ventana, el contenido se reorganiza.** La barra lateral mide
  exactamente lo que el popup y empieza en su misma esquina, así que el mes del popup *es* el
  mini mes de la app y no se mueve ni un píxel mientras la ventana crece. La lista del día se
  desvanece en el primer 30 % del camino, la cápsula de texto viaja primero a la derecha y
  luego arriba —en línea recta cruzaría la rejilla— y el resto de la app entra con el fundido
  y los 8 DIP de subida de siempre.
- Fase 6a: **sin parpadeo al crecer.** El swap chain tiene desde el principio el tamaño de la
  app (y un 2 % más, para el rebote del muelle), así que cada fotograma es un `SetWindowPos` y
  nunca un `ResizeBuffers`. El reloj es un hilo que espera a `DwmFlush` y avisa una vez por
  fotograma compuesto, sin acumular fotogramas si la interfaz va lenta. Comprobado a 125 %:
  llega al tamaño final en unos 320 ms y se asienta hacia los 520 ms.
- Fase 6a: **vistas de día, semana y mes.** Línea de tiempo por horas con los solapes uno al
  lado del otro, franja de día entero, una **línea roja de ahora** que se mueve en el cambio de
  cada minuto, y rueda del ratón para desplazar las horas. La barra lateral añade la lista de
  calendarios con su interruptor y la bandeja de tareas sin fecha. Atajos `D`, `S`, `M`, `T`,
  flechas y `Ctrl+K`.
- Fase 6a: **las repeticiones se despliegan**, en el popup y en la app, como CLAUDE.md dejó
  dicho para cuando hubiera vistas de semana y mes: FREQ diaria, semanal, mensual y anual con
  INTERVAL, BYDAY, COUNT y UNTIL. Lo que no se sabe leer se queda en su primer día, como antes.
- Fase 6a: capturas `app-dia`, `app-semana`, `app-mes` y `app-transicion`, en los dos temas.
- Fase 6b: **en la línea de tiempo se arrastra.** Sobre un hueco crea un evento con ese rango y
  abre su detalle con el título seleccionado; sobre un evento lo mueve, también de día; sobre
  su borde inferior cambia la duración. Todo ajusta a cuartos de hora, se ve mientras se
  arrastra y se escribe al soltar, sin esperar a nadie.
- Fase 6b: **una tarea de la bandeja arrastrada a una hora se convierte en un bloque de
  tiempo**: un evento de una hora con su título, y la tarea se retira. «Deshacer» devuelve la
  tarea tal como era.
- Fase 6b: **panel de detalle** a la derecha, que entra con los 160 ms de siempre mientras la
  vista principal se estrecha: título, fecha, horas, calendario, ubicación, notas y
  repetición. Fecha y horas aceptan «25/09» y «17:30» y también lo que entiende el campo de
  arriba («mañana», «5pm»); lo que no se entiende se pone en rojo y no se guarda.
- Fase 6b: **Supr borra con confirmación y deshacer.** El evento desaparece al instante y se
  borra de verdad cuando se va el aviso; deshacer solo lo vuelve a enseñar, así que no se
  pierde nada de lo que Google tiene de él.
- Fase 6b: **cambiar un evento de calendario** usa el `POST .../move` de Google, que es la
  única forma de moverlo sin perder invitados ni historial; la fila recuerda el calendario de
  origen en `moved_from` hasta que el movimiento sube.
- Fase 6b: mover un evento que se repite mueve la serie entera, y una serie semanal de un solo
  día pasa a repetirse el día al que se llevó.
- Fase 6b: capturas `app-detalle`, `app-arrastre` y `app-borrar`.

### Corregido

- **Una edición podía perderse si se hacía mientras subía la anterior.** `pending_ops.id` es un
  rowid sin `AUTOINCREMENT`, y cambiar una operación por otra borraba primero la vieja e
  insertaba después la nueva, que heredaba su número. La pasada que estaba enviando la vieja
  borraba al terminar la nueva por ese número. Pasaba con dos cambios seguidos en el detalle,
  y también —desde la fase 4— con marcar y desmarcar una tarea o con deshacer justo mientras
  subía la creación, que podía dejar el evento en Google. Ahora la nueva entra primero y las
  viejas salen después, así que su número siempre es mayor que cualquiera en vuelo.
- **Las repeticiones creadas con el parser iban a Google sin su `RRULE:`**, que las rechaza.
- **Un movimiento ya no reenvía la regla de repetición.** Agenda guarda la RRULE y no las
  EXDATE, así que mandarla con cada cambio habría devuelto las repeticiones que alguien borró
  en la web. Ahora solo va cuando se editó la repetición, y la ubicación solo cuando hay una o
  cuando se vació a propósito: la operación de la cola dice qué cambió
  (`update+location+recurrence`).

### Cambiado

- **Esquema v2**, solo con columnas nuevas: `calendars.hidden` (el interruptor de la barra
  lateral; no puede ser `visible`, que cada pasada de sincronización vuelve a poner a 1),
  `events.location` y `events.moved_from` (las dos para la fase 6b). Una caché v1 se migra
  sola al abrir.
- **Un clic en un día del popup abre la app en ese día**, en vez de solo seleccionarlo. Las
  flechas siguen moviendo el día sin abrir nada.
- Lo nuevo **ya no cae en un calendario oculto**: si el calendario por defecto está apagado en
  la barra lateral, va al siguiente que se vea.

- Fase 5: **sincronización bidireccional con Google Calendar y Google Tasks.** Lo creado en el
  popup aparece en la web, lo creado en la web aparece en el popup en la siguiente pasada, y la
  interfaz no espera a la red en ningún momento: todo ocurre en un hilo propio y el popup sigue
  leyendo la caché local. El esquema de SQLite **no cambió**: la fase 4 ya había reservado
  `remote_id`, `etag`, `sync_state` y `pending_ops`, y al repasar hueco por hueco no faltaba
  ninguno.
- Fase 5: **OAuth 2.0 con PKCE y redirect a `127.0.0.1`**, que es el flujo que Google documenta
  para una aplicación de escritorio y el único que no necesita un secreto que sea de verdad
  secreto —el `client_secret` de una app de escritorio está en el ejecutable de todo el que la
  tenga—. Lo que protege el intercambio es el verificador, que no sale del proceso. El oyente
  es un socket a secas con el puerto que dé el sistema, y no la API de servidor HTTP, que
  querría una reserva de URL y por tanto un administrador para recibir un GET. El refresh token
  se cifra con **DPAPI** en `%LOCALAPPDATA%\Agenda\token.bin`; el access token no se escribe en
  ningún sitio. **Antes de abrir el navegador se avisa y se espera confirmación**, en un
  TaskDialog y en el hilo de la interfaz, que es el único que tiene una ventana con la que
  preguntar.
- Fase 5: **el orden dentro de una pasada no es arbitrario: primero se sube.** Al revés, una
  bajada machacaría una edición local que todavía no ha salido, y la cola que la sostenía se
  tiraría por vieja.
- Fase 5: **el color de cada evento es el de su calendario de Google**, y eso no costó una sola
  línea nueva de dibujo: `ItemsForDay` ya leía el color con un JOIN contra `calendars`, así que
  bastó con que la sincronización llenara esa tabla. Solo se bajan los calendarios que estén
  marcados en Google Calendar web: quien escondió uno allí lo escondió a propósito.
- Fase 5: **el calendario por defecto se elige en un submenú de la bandeja**, y vive en la
  columna `is_primary`, que ya existía. Esa columna pasa a significar «aquí cae lo que se crea»
  en vez de «es el primary de Google»: se siembra con el primary y la mueve el menú. Una
  columna que ya estaba en lugar de inventar un almacén de ajustes para una elección que se
  hace una vez en la vida.
- Fase 5: **sin conexión no se pierde nada.** Lo escrito se guarda en la caché igual que
  siempre y espera en la cola; el popup enseña un punto de 2 DIP a la izquierda de las flechas
  y nada más, porque no hay nada que el usuario pueda hacer al respecto. Al volver la red la
  cola se vacía sola, y cerrar Agenda por el medio no la pierde: vive en SQLite. Hay una vista
  `--render-snapshot=popup-sin-conexion` para juzgar el punto sin desenchufar nada.
- Fase 5: **deshacer algo que ya está en Google deja lápida.** Los cinco segundos del aviso son
  de sobra para que la creación haya subido, y borrar la fila entonces dejaría el evento en el
  móvil para siempre y además lo traería de vuelta en la siguiente pasada. Una fila con
  `remote_id` se convierte en lápida con un borrado encolado detrás; una que nunca se envió se
  sigue yendo entera, como en la fase 4.
- Fase 5: **una creación de evento se puede enviar dos veces sin duplicarse.** Agenda le da a
  Google su propio `uid` como identificador —el hexadecimal sin guiones cae entero dentro del
  alfabeto base32hex que Google exige—, así que un reintento contesta 409, que significa «ya
  estaba». Google Tasks no admite identificador del cliente y por eso ahí esa red no existe.
- Fase 5: `docs/google-setup.md`, los pasos para crear las credenciales. Incluye lo que muerde
  de verdad: un proyecto de Google Cloud en modo *Prueba* caduca el refresh token a los siete
  días, así que hay que publicarlo.

### Corregido

- **Perder el permiso de Google ya no es silencioso.** Si el proyecto de Google Cloud se queda
  en modo *Prueba*, el token de actualización muere a los siete días. Hasta ahora Agenda se
  limitaba a registrarlo y olvidar la cuenta: seguía escribiendo en la caché, nada subía, y nada
  lo decía. Ahora sale un globo en la bandeja y el menú vuelve a ofrecer «Conectar con
  Google…». Es deliberadamente distinto del punto de sin conexión: sin red se arregla solo y no
  hay nada que hacer; sin permiso se queda roto hasta que una persona pulse algo.
- **Al conectar quedaban dos calendarios marcados como «por defecto»** de cada tipo —el marcador
  local sembrado en la fase 4 y el que acababa de bajar—, y cuál ganaba dependía del orden
  alfabético de los identificadores. Apagar el marcador local colgaba de haber movido filas
  hacia el calendario elegido, y quien conecta sin haber creado nada antes no mueve ninguna.
  Mudar lo local y retirar el marcador son una sola cosa, no dos. Visto en una cuenta de verdad,
  no en una prueba.

- El `due` de una tarea de Google es **una fecha disfrazada de instante**: la API solo guarda el
  día y siempre lo devuelve como medianoche UTC. Convertirlo al huso local movería el día a
  cualquiera al oeste de Londres. Los eventos convierten huso, las tareas no, y las dos reglas
  viven una al lado de la otra en `src/sync/map.cpp` con sus pruebas.
- El `end.date` de un evento de todo el día es **exclusivo**: Google dice que un evento de un
  día termina mañana. Sin restarle el día, todos los eventos de una jornada medirían dos en la
  rejilla del mes.

- Fase 4: **Enter crea, y lo creado sobrevive a cerrar la aplicación.** Todo va a SQLite, en
  `%LOCALAPPDATA%\Agenda\agenda.db`, en modo WAL. El esquema tiene cinco tablas —`calendars`,
  `events`, `tasks`, `sync_state` y `pending_ops`— con migraciones versionadas en
  `PRAGMA user_version`, que es transaccional: si una migración se cae por la mitad, el número
  no sube y el siguiente arranque vuelve a intentarlo desde el mismo sitio. Una caché escrita
  por una versión más nueva no se toca y da error, en vez de convertirse hacia atrás a ciegas.
- Fase 4: `src/data/` como biblioteca estática (`agenda_data`), por el mismo motivo que
  `agenda_nlp`: aquí dentro no entra ni Direct2D ni una ventana, así que los tests la prueban
  directa contra una base en memoria. `db.{h,cpp}` envuelve SQLite en lo justo —un handle que
  se cierra solo, una sentencia que se finaliza sola y la conversión UTF-8 ↔ `wstring` en un
  único sitio—, `schema.{h,cpp}` lleva las migraciones y `store.{h,cpp}` el repositorio.
- Fase 4: **las lecturas van en el hilo de interfaz y las escrituras en uno de trabajo.** El
  popup tiene que estar en pantalla en menos de 100 ms y sin spinners, y las dos consultas que
  lo llenan son un índice y unas pocas filas. Las escrituras se encolan; el trabajador avisa
  con un `WM_APP+2` **sin carga** y la interfaz relee, porque un puntero reservado en un hilo
  habría que liberarlo en el otro.
- Fase 4: **la interfaz es optimista.** El `uid` lo genera la ventana antes de encolar nada,
  así que la tarjeta está en pantalla desde el primer fotograma y nunca hay que esperar al
  `rowid`. Si el trabajador falla, devuelve el `uid`, la fila se quita y se dice por qué. Cada
  escritura y su fila de `pending_ops` van **en la misma transacción**: una creación guardada
  sin su operación sería un evento que jamás llega a Google, sin un solo error por ningún lado.
- Fase 4: **Ctrl+Z deshace durante cinco segundos**, mientras el aviso «Creado · Deshacer» está
  puesto, y **devuelve la frase al campo de texto** —deshacer un error de tecleo solo sirve si
  el error vuelve para corregirlo—. La oferta termina con el aviso, que es la única forma de
  que las dos cosas no puedan discrepar. Nada se ha enviado todavía, así que deshacer borra la
  fila y su operación juntas y no deja lápida.
- Fase 4: **las tareas llevan casilla y se tachan con animación.** El tachado es un rectángulo
  de 1 DIP que crece, y no `IDWriteTextLayout::SetStrikethrough`, que es todo o nada y por eso
  no se puede animar. La casilla se dibuja con dos trazos, como el chevron, para no depender
  de un glifo. La tarea marcada se queda en la lista: tachada, no desaparecida.
- Fase 4: la lista del día mezcla eventos y tareas y los ordena como la forma del día —día
  entero, luego la hora, y las tareas sin hora al final—; los puntos del mes salen de datos
  reales y un evento de varios días los pone en todos ellos. Una tarea **sin fecha** se guarda
  con `due_day` nulo y se enseña en el día de hoy.
- Fase 4: vista de captura nueva, `--render-snapshot=popup-creado`: el panel en el instante
  siguiente a Enter, con el aviso puesto y la tarjeta a medio subir. Ese momento dura ciento
  sesenta milisegundos y no había otra forma de mirarlo con calma.
- Fase 4: `tests/test_store.cpp`, con la mitad del criterio de aceptación que no necesita
  ventana: crear, soltar el archivo como lo suelta cerrar la aplicación, volver a abrirlo y
  encontrarlo. Más las migraciones, la caché de versión superior, el evento de dos días, el
  orden del día, la tarea sin fecha, marcar y desmarcar, y que deshacer se lleve la operación
  encolada.

### Cambiado

- Fase 4: la **regla de monitores** de `CLAUDE.md` daba por hecho el puesto de tres pantallas y
  dejaba una fase sin verificar cuando no está montado. Ahora dice las dos situaciones: con las
  tres, todo al monitor 3 como siempre; con una sola, se usa la que haya y se dice en el
  resumen. La razón de la regla no cambia —no dejar ventanas encima de lo que el usuario mira—
  pero ya no impide probar el criterio de aceptación con la aplicación delante.
- Fase 4: **el tiempo se guarda como reloj de pared local** —un día y un minuto de ese día—
  y no como instante UTC. Es lo que ya llevaba el código, es la única pregunta que hace la
  interfaz y es lo que guarda Google Calendar. `updated_at` es la excepción y sí es UTC,
  porque es metadato de conflicto y no una hora de la agenda.
- Fase 4: `src/ui/sample_data.h` deja de ser lo que pinta la aplicación y pasa a ser lo que
  pinta **solo la captura**. `--render-snapshot` ya congelaba el día y la hora para que el PNG
  cambiara cuando cambia el diseño y no cuando pasa el tiempo; enchufarlo a la base del usuario
  habría hecho que la herramienta con la que se revisa el diseño dependiera de lo que alguien
  escribiera esa mañana.
- Fase 4: el aviso y la vista previa comparten rectángulo y **la lista les cede una tarjeta**
  mientras uno de los dos está puesto. La primera versión dejaba el aviso encima de la tarjeta
  recién creada, que es justo la que había que mirar; se vio en la captura de `popup-creado`.
- Fase 4: una regla de repetición **se guarda y no se despliega**. El RRULE va a su columna
  —es literalmente lo que quiere Google en la fase 5— y el evento sale en su primer día. El
  motor de ocurrencias es de la fase 6, que es la que tiene vistas donde se note.
- Fase 4: `CLAUDE.md` pedía `std::expected`, que es de C++23, y el proyecto es C++20. `src/data/`
  devuelve `bool` y deja el motivo en `Db::error()` y en el log. Anotado allí.

### Corregido

- Fase 4: **algo creado podía seguir sin verse.** Una tarea sin fecha escrita mientras se
  miraba otro día se guardaba en el día de hoy, pero el popup se quedaba donde estaba: el
  aviso decía «Creado» encima de un día donde no había aparecido nada. Ahora el popup salta
  siempre al día donde cayó, y para una tarea sin fecha ese día es hoy. Encontrado usando la
  aplicación, no leyendo el código.
- Fase 4: la vista previa prometía «Dentista» y la tarjeta creaba «dentista». `nlp::Capitalised`
  sale del anónimo y la usan las dos, porque una vista previa que no dice exactamente lo que
  va a pasar deja de merecer que se lea.
- Fase 4: la descarga de la amalgamación de SQLite moría con «SSL certificate verification
  failed» mientras los clones de git funcionaban, así que parecía un problema de red y no lo
  era: el `cmake` del PATH no trae almacén de certificados. `CMakeLists.txt` le pasa el de Git
  para Windows por `CMAKE_TLS_CAINFO`, que FetchContent reenvía al sub-build, y lo normaliza a
  barras hacia delante porque `%ProgramFiles%` trae `C:\Program Files` y ese `\P` aborta el
  script generado.

### Añadido

- Fase 3: parser de lenguaje natural en `src/nlp/`, compilado como biblioteca estática
  (`agenda_nlp`) sin nada de interfaz dentro. Entiende español e inglés a la vez, sin detectar
  idioma: fechas relativas (`hoy`, `mañana`, `pasado mañana`, días de la semana, `próximo
  lunes`, `el 25`, `en 3 días`), horas (`5pm`, `17:00`, `17h`, `a las 5`, `5 de la tarde`,
  `mediodía`), duraciones (`por 2h`, `30 min`, `de 3 a 5`), recurrencia básica traducida a
  RRULE (`cada lunes`, `todos los días`) y los prefijos `t:`, `!` y `e:`. El «ahora» entra
  como parámetro, así que los tests no dependen del reloj de la máquina.
- Fase 3: las reglas de ambigüedad de CLAUDE.md, tal cual. Una hora sin `am`/`pm` cae entre
  las 8:00 y las 20:00; si la hora ya pasó y no se escribió fecha, se usa la del día
  siguiente, pero una fecha explícita se respeta aunque su hora quede atrás. Con hora sale un
  evento de 60 minutos, sin hora una tarea, y el prefijo manda sobre las dos cosas.
- Fase 3: 55 tests de Catch2 para el parser, con los ocho casos que CLAUDE.md nombra y los
  límites que duelen: acentos y su ausencia, mayúsculas, `25:00` y `13pm`, `el 31` en un mes
  de treinta, el 25 de diciembre saltando de año, y «mañana» escrito a las 23:59.
- Fase 3: vista previa en vivo. Los tokens reconocidos se pintan en color de acento dentro de
  la cápsula, y encima aparece una tarjeta que dice qué se va a crear —`📅 Mañana ·
  17:00–18:00 · Dentista`, `☑ Tarea · Lunes · Pagar luz` o `☑ Tarea sin fecha`—. La tarjeta se
  superpone al final de la lista de eventos mientras se escribe, en vez de reservar una fila
  fija, para que la rejilla del mes no se mueva al empezar a teclear. **Enter sigue sin hacer
  nada: todavía no se guarda.**
- Fase 3: `--text=...` precarga el campo de texto en `--render-snapshot`, que es la única
  forma de revisar la vista previa en un PNG.
- Fase 2: el panel se adapta al monitor. Su alto es el 42 % del área de trabajo, recortado
  entre 380 y 560 DIP, y el ancho sale de la proporción 340:420 del diseño. Todo lo de dentro
  escala uno a uno con él —letras, círculos de los días, tarjetas y espacios—, así que un panel
  más grande significa contenido más grande y no el mismo contenido flotando en más hueco. Las
  medidas se calculan una vez en `MakeLayout` y las leen igual el dibujo y la detección de
  clics; a 340×420 salen exactamente los números del sistema de diseño.
- Fase 2: `--panel=WxH` fuerza el tamaño del panel en DIP, en la app y en las capturas, para
  poder juzgar en una pantalla un tamaño que esa pantalla no produciría.
- Fase 2: sistema de diseño en `src/ui/theme.{h,cpp}` con los tokens de CLAUDE.md (colores,
  tipografía, radios y rejilla de 4 DIP) y un tema claro derivado. La app sigue el tema del
  sistema leyendo `AppsUseLightTheme` del registro, solo lectura, y lo relee cada vez que se
  abre el popup.
- Fase 2: el popup ya no está vacío. Título del mes en 15 semibold, fila `L M X J V S D`
  empezando en lunes, rejilla fija de seis semanas con el día de hoy en círculo de acento, el
  seleccionado en anillo y un punto de 4 DIP bajo los días con eventos, lista de eventos del
  día seleccionado y campo de texto en cápsula con el marcador `mañana 5pm dentista…`.
- Fase 2: componentes de dibujo reutilizables. `src/ui/paint.{h,cpp}` trae los formatos de
  DirectWrite (Segoe UI Variable Text con respaldo a Segoe UI), texto con elipsis, rectángulos
  redondeados y círculos; `src/ui/components.{h,cpp}` trae la rejilla del mes, la tarjeta de
  evento y el campo de texto.
- Fase 2: campo de texto editable en `src/ui/text_input.{h,cpp}`, sin Direct2D ni Win32, con
  cursor, selección, movimiento por código de punto (un emoji no se parte por la mitad) y
  filtrado de caracteres de control. La ventana le conecta `WM_CHAR`, el portapapeles
  (`Ctrl+C`, `Ctrl+X`, `Ctrl+V` y `Ctrl+A`) y el IME (`WM_IME_*`), que dibuja la composición
  subrayada en el cursor y coloca ahí la ventana de candidatos.
- Fase 2: estados de hover y foco con transición de 100 ms, y deslizamiento de 160 ms al
  cambiar de mes con las flechas `‹ ›`. Los mueve un temporizador de 16 ms que solo corre
  mientras algo está en movimiento, y respetan la opción de reducir animaciones de Windows.
- Fase 2: navegación con teclado. Con el campo vacío las cuatro flechas mueven el día; con
  texto, `←` y `→` mueven el cursor (con `Shift` seleccionan) y `↑` y `↓` siguen moviendo el
  día una semana. El día seleccionado viaja con el mes, así que la lista siempre muestra un
  día que está en pantalla.
- Fase 2: `--theme=dark|light` para ver cualquiera de los dos temas sin tocar la configuración
  de Windows, tanto en la app como en `--render-snapshot`. Las capturas fijan el 22 de
  septiembre de 2026 como «hoy» para que el PNG solo cambie cuando cambie el diseño.
- Fase 2: datos de ejemplo en memoria (`src/ui/sample_data.h`), indexados por día del mes para
  que cualquier mes se vea poblado. **No se toca SQLite todavía.**
- Fase 2: `src/core/dates.h` con la aritmética del calendario sobre `<chrono>`: rejilla que
  empieza en lunes, seis semanas fijas, meses recortados al último día que existe (31 de enero
  más un mes es 28 de febrero) y nombres de mes en español independientes del locale.
- Fase 2: pruebas de Catch2 para las fechas, el campo de texto y la geometría del panel, que es
  la que comparten el dibujo y la detección de clics.
- Fase 1: atajo global con `RegisterHotKey`, configurable con la clave `hotkey` de
  `config.json` y `Alt+Shift+C` por defecto. Si ya está ocupado, se registra el error, aparece
  un globo de aviso en la bandeja y la app sigue viva para poder abrirla desde el icono.
- Fase 1: ventana popup de 340×420 DIP (`WS_POPUP` sin bitmap de redirección) compuesta con
  DirectComposition sobre un swap chain con alfa premultiplicado, con fondo acrylic
  (`DWMWA_SYSTEMBACKDROP_TYPE`), esquinas redondeadas y panel opaco de respaldo en Windows 10.
  Se precrea oculta al arrancar y se coloca en la esquina inferior derecha del área de trabajo
  del monitor objetivo, escalada con `GetDpiForWindow`.
- Fase 1: apertura de 160 ms y cierre de 120 ms con opacidad y desplazamiento de 8 DIP,
  ajustables con `popup.openMs` y `popup.closeMs`, que respetan la opción de reducir
  animaciones de Windows. Se cierra con Esc, al perder el foco o con el propio atajo.
- Fase 1: icono en la bandeja con menú de **Abrir** y **Salir**, y `assets/agenda.ico`, que es
  también el icono del ejecutable.
- Fase 1: `--render-snapshot=popup --out=<archivo.png>`, que renderiza la vista fuera de
  pantalla con Direct2D sobre un bitmap WIC. No necesita monitor ni instancia libre, así que
  funciona con la app abierta.
- Fase 0: esqueleto del proyecto con CMake 3.28, presets `debug` y `release` para MSVC x64 y
  manifiesto de vcpkg (nlohmann-json, sqlite3 y catch2), con respaldo por `FetchContent`
  cuando no hay vcpkg en la máquina.
- `src/core`: logging a `%LOCALAPPDATA%\Agenda\logs\agenda-AAAAMMDD.log` en UTF-8 y con copia
  a la salida del depurador, y lectura de configuración que fusiona `config.json` con el
  `config.local.json` opcional.
- `src/app/monitors`: `FindMonitorByDisplayNumber(n)`, que resuelve `\\.\DISPLAYn` con
  `EnumDisplayMonitors` y `MONITORINFOEXW` en vez del índice de enumeración, y el parseo de
  `--monitor=N` y `AGENDA_DEV_MONITOR` (3 por defecto en Debug). Si el monitor no existe, se
  registra el error y el proceso sale con código 2.
- `wWinMain` con manifiesto DPI Per-Monitor v2 e instancia única por mutex con nombre.
- Pruebas de Catch2 para el parseo de argumentos, del atajo y de la geometría del popup.
- README.md, CHANGELOG.md, `.gitignore` (build, secretos y `config.local.json`) y
  `.editorconfig`.

### Corregido

- El panel se dibujaba a 96 ppp dentro de una ventana dimensionada al DPI del monitor, así que
  en una pantalla escalada ocupaba solo una esquina y el resto de la ventana se veía como un
  rectángulo gris vacío. El contexto de Direct2D no hereda el DPI del bitmap de destino, de modo
  que ahora se le fija con `SetDpi` en cada cuadro. No se notaba en el monitor de desarrollo,
  que está al 100 %, ni en las capturas, que se renderizan siempre a 96 ppp.

### Cambiado

- Las transiciones simples (abrir, cerrar, fundidos) usan animaciones de DirectComposition, que
  interpola la GPU, en lugar del motor propio de springs sobre temporizador vsync. Ese motor se
  escribirá en la fase 6, que es la que lo necesita para la expansión del popup a la app.
  CLAUDE.md queda actualizado.
