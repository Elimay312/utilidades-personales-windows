# Changelog

## H3 — bordes (a medias)

**Pantalla completa: verificado, y de rebote.** Las capturas de H2 se tomaron con VALORANT en
primer plano a pantalla completa, y la cápsula se dibujó encima sin robarle el foco ni sacarlo
de su modo. No hace falta ninguna lógica de "apartarse", que es lo que hacen el dock y la
isla: un aviso de volumen tiene que verse justo cuando estás jugando.

**Autoarranque: verificado.** Con `autoArranque: true` escribe en `HKCU\...\Run` la ruta del
exe publicado (`%LOCALAPPDATA%\Hudpp\Hud.exe`, no `bin\`); con `false` borra la entrada. El
registro queda limpio.

**Fuga arreglada, encontrada leyendo.** Cada salto a una pantalla con otra escala rehace los
visuals enteros, pero `Dispose` solo soltaba el `DesktopWindowTarget`: las cinco superficies
de D2D de los glifos se quedaban. Con tres pantallas a tres escalas eso pasa varias veces por
minuto. Ahora se sueltan el pincel y su superficie.

**Sin fuga en el camino normal.** Cuatro minutos de `--demo`, unas 220 apariciones con sus
morphs: la memoria privada sube de 14 a 19 MB en los primeros 70 s y ahí se queda (+1 MB en
los 165 s siguientes), y los handles bajan de 336 a 327. Eso es el GC asentándose, no una
fuga por ciclo — una fuga crece recta y no se para. Con los 80 s del primer intento no se
podía distinguir una cosa de la otra.

**Multi-monitor: sin medir todavía.** Las tres pantallas van a 125%, 100% y 175%, que es el
banco de pruebas ideal, pero la sonda no puede llevar el cursor a las otras dos: VALORANT
tenía el ratón confinado a `1,1..2559,1079` con `ClipCursor`, y `SetCursorPos` devolvía `True`
mientras el cursor se quedaba clavado en x=2558. Dos intentos midieron el fondo de tres sitios
vacíos antes de darse cuenta. La sonda ahora **se declara no concluyente** si detecta el
confinamiento, en vez de inventarse un fallo.

Trazas nuevas: el HUD imprime una línea cada vez que se mueve de pantalla, con la escala y el
rectángulo. Solo cuando de verdad cambia de sitio, no en cada aparición.

## H2 — el volumen de verdad

Las tres teclas son del HUD (`RegisterHotKey`, sin modificadores y **sin `MOD_NOREPEAT`**,
que al mantenerlas pulsadas tienen que repetir), y el nivel se pone con
`IAudioEndpointVolume` sobre `eRender`. Eso es lo que hace que el aviso gris de Windows no
salga: el shell no llega a ver la tecla.

**El paso se alinea a la rejilla en vez de sumar.** Desde un 37% con paso 5 se va a 40, no a
42: si otra app te dejó el volumen en un valor raro, la primera pulsación lo cuadra. El
`--check` recorre 1212 caminos — seis tamaños de paso por 101 puntos de partida, subiendo y
bajando — y comprueba que desde cualquier sitio se llega al 0 y al 100 sin atascarse. Es lo
que caza el clasico "con 99 y paso 2 nunca llegas al 100".

Tocar el volumen quita el silencio, como hace Windows. Bajar a cero no: eso es bajar a cero.

**El HUD sale también cuando lo cambia otro** — el mezclador, una app, el mando de unos
auriculares — con un sondeo de 250 ms. `ponytail: sondeo en vez de
IAudioEndpointVolumeCallback`; la isla ya sondea audio ocho veces por segundo sin coste
medible, y el callback entrega en un hilo ajeno y habría que marshalarlo.

Si las teclas no se pueden registrar porque otro programa las tiene, **el HUD arranca igual**
en modo solo-reflejo y lo dice en la consola. Es el único caso en que se verían dos avisos.

Medido de punta a punta sin inyectar teclas, cambiando el volumen por la misma API pública
que usaría el mezclador: en reposo no se ve nada; al poner 25% el HUD sale solo con una onda
y la barra a un cuarto; al poner 80%, tres ondas y la barra casi llena; y el volumen queda
como estaba. La sonda también se estrelló una vez — violacion de segmento — por declarar un
hueco de más en la vtable de `IMMDeviceEnumerator`: `GetDefaultAudioEndpoint` es el segundo
método, no el tercero.

Las tres teclas van como constantes a la vista (`0xAD`, `0xAE`, `0xAF`) en vez de traerse el
enum `VIRTUAL_KEY` entero: tres números dicen "solo estas tres" mejor que 250 nombres
generados, y es justo lo que mira un auditor.

## H1 — la cápsula

Ya se ve. Cristal oscuro redondeado abajo y centrado, glifo de altavoz, barra con relleno, y
los cuatro morphs corriendo sobre **tres escalares** de un `CompositionPropertySet` (`V`, `A`,
`S`): el hilo de UI no escribe nada más y el resto lo interpola DWM.

`--demo` recorre niveles falsos en bucle para afinar los muelles sin tocar audio. Los cinco
glifos son de Segoe Fluent Icons por DirectWrite sobre superficie propia, y se transforman
entre ellos — el que sale se encoge, el que entra llega grande y asienta con muelle.

**Sin region, y eso es un hallazgo.** Los cuatro vecinos usan `SetWindowRgn` para dejar pasar
los clics, porque la isla midió que `WS_EX_TRANSPARENT` no bastaba. Aquí se volvió a medir con
**`WS_EX_LAYERED` añadido** y entonces sí funciona: `WindowFromPoint` sobre el centro de la
cápsula devuelve la ventana de detrás, exactamente igual que con el HUD cerrado. Y el árbol de
Composition se sigue pintando. Eso quita `CreateRoundRectRgn`, `SetWindowRgn`, la función
`AplicarRegion` y la obligación de acordarse de que la región cubra el squash — la región
también recorta el dibujo.

Medido, no mirado: la primera lectura a ojo de una captura dijo que la cápsula estaba 16 px
baja y medía 298x86. Medida de verdad sobre la captura del rectángulo exacto de la ventana:
columna central **y 24..91, 68 px**, y el perfil por filas va de 51..276 en y=24 a 25..302 en
y=48 y vuelve simétrico — una pastilla de 280x68 en (24,24), como estaba diseñada. Los tramos
cortos en y=54 y y=60 son la barra blanca partiendo el tramo oscuro, justo donde toca.

Dos sondas se invalidaron solas y se dicen para que no se repitan: la de diferencia entre dos
capturas no valía porque había un reproductor detrás y cambiaba el 96% de los píxeles; y la del
umbral de oscuridad dejó de medir la cápsula en cuanto se le añadió la capa de brillo.

El acrílico lleva tres capas, la receta del dock más un velo: backdrop del sistema, velo oscuro
para que la barra blanca se lea sobre cualquier fondo, y brillo claro encima. Solo con el velo
la cápsula salía casi negra y parecía opaca.

## El brillo sale del alcance

Con el volumen el HUD queda limpio: capturamos la tecla y el aviso de Windows no sale. Con el
brillo no se puede — las teclas Fn van por ACPI, no hay nada que capturar, y Windows enseña el
suyo igual. Enseñar el brillo significaba ver dos indicadores, que es lo que este proyecto
existe para evitar.

Se cae el módulo de brillo, se cae H3, y se cae con ellos `System.Management`: **vuelve a haber
una sola dependencia**. `auditar.ps1` gana un guardia de alcance que falla si reaparece
`WmiMonitor`, `System.Management`, `ManagementObject` o `root\WMI`, porque un alcance que no se
comprueba se vuelve a ensanchar solo.

Arreglado de paso un fallo de la propia puerta: `Codigo` sabe saltar `//` y `/* */` pero no
`<!-- -->`, así que explicar en el `.csproj` por qué **no** está `System.Management` hacía
saltar la regla que comprueba que no está. Ahora salta los comentarios XML, y verificado que
sigue atrapando un `PackageReference` de verdad. El `auditar.ps1` de los vecinos tiene el mismo
agujero sin disparar.

La clase de ventana pasa de `HudVolumenBrillo` a `HudVolumen`, que es lo que es.

## H4 retirado antes de escribirse

El hito que justificaba la única excepción del documento no existe. Dos motivos
independientes, los dos medidos antes de escribir una línea de `FlyoutNativo.cs`:

- **El aviso no es una ventana.** 6111 muestras en 20 s (mediana 3 ms, peor hueco 36 ms) con 22
  teclas de volumen pulsadas: cero ventanas top-level nuevas, movidas o hechas visibles. La
  clase `NativeHWNDHost` de las soluciones que circulan ya no existe en Windows 11 26200.
- **Y no hacía falta.** `RegisterHotKey` consume la tecla, así que el shell no la ve y no
  enseña nada; cambiar el volumen por `IAudioEndpointVolume` tampoco dispara aviso. Medido: el
  recuadro gris no apareció ni una vez, y el volumen no se movió — la segunda mitad de la
  prueba, la que demuestra que la tecla se la tragó el registro.

Retirado: la excepción de `SEGURIDAD.md` §1, la regla 17 de `auditar.ps1`, el fichero
`FlyoutNativo.cs` que nunca llegó a existir y la clave `ocultarFlyoutNativo` de `hud.json`. La
regla 15 vuelve a ser absoluta y ahora veta también `FindWindow`, que es el centinela: es el
primer paso de cualquier intento de reabrir la grieta.

Queda dicho en voz alta lo que no se resuelve: **el aviso de brillo de Windows sigue saliendo**,
porque sus teclas van por ACPI y no hay nada que capturar.

También medido de paso: es un portátil (chasis 10, batería) con panel AUO interno de 101
niveles de brillo legibles sin elevación, así que H3 sí existe. Y las tres teclas de volumen se
registran sin que ningún otro programa las tenga cogidas, que el plan daba por hecho.

## H0 — andamio y SEGURIDAD.md

Lo primero es el documento, no el código.

- **`SEGURIDAD.md`, escrito antes de la primera línea.** 16 reglas, heredadas del dock y de la
  isla, con una diferencia: la regla 15 está abierta por una grieta de una sola operación
  —mover el host del aviso nativo con `SetWindowPos`— razonada entera en la §1.
- **`auditar.ps1` con una regla 17 propia**, que confina esa grieta a `FlyoutNativo.cs`. Probada
  metiendo el fallo a propósito: una sonda con `FindWindowW`, `WH_KEYBOARD_LL` y `EnumWindows`
  hace saltar las reglas 17, 4 y 15 y devuelve código 1. Sin la sonda, `TODO LIMPIO`.
  ASCII puro y sin sintaxis de PowerShell 7: en esta máquina solo hay 5.1.
- **Ventana Win32** `WS_POPUP` + `NOACTIVATE | TOOLWINDOW | TOPMOST`, con `MA_NOACTIVATE`,
  `WM_NCACTIVATE` forzado y `hwndInsertAfter` en `WM_WINDOWPOSCHANGING`. Se crea pero **no se
  enseña**: todavía no hay nada que pintar. Sin `WS_EX_TRANSPARENT` — la isla midió que no deja
  pasar los clics entre procesos, así que eso lo resolverá la región en H1.
- **Colocación por monitor del cursor y DPI**, con `PerMonitorV2` en el manifest desde el primer
  commit. `--check` la comprueba a 100% y 150%, en un monitor secundario a la derecha, en uno a
  la izquierda con coordenadas negativas, arriba y abajo, y en 800x600.
- **Ctrl+Alt+H para salir.** Sin icono de bandeja todavía; el cierre limpio hace falta porque es
  lo que devolverá el aviso nativo a su sitio.
- **Una sola dependencia** (`CsWin32`). La segunda, `System.Management`, entra en H3 con el
  brillo y está anotada en `Hud.csproj` con el porqué.

Medido: compila con 0 advertencias, `--check` pasa 6 comprobaciones, la auditoría sale limpia.
