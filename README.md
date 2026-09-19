# Isla

Una isla dinámica para Windows 11. Vive en el borde superior, dice qué está sonando y
deja pausarlo sin ir a buscar la ventana. También cuenta pomodoros, avisa al enchufar y
al desenchufar, y enseña el volumen.

El referente es la isla de macOS, pero con una diferencia que lo cambia todo: **macOS
puede permitirse una pastilla negra permanente porque la muesca ya existe** — es espacio
muerto que regala el hardware. Windows no tiene muesca, y el borde superior está vivo:
ahí están las pestañas del navegador. De ahí salen casi todas las decisiones de diseño
de este proyecto.

Corre 100% en modo usuario, sin elevación, sin red y sin nada instalado en el sistema.
Las reglas que lo garantizan están en [SEGURIDAD.md](SEGURIDAD.md) y se comprueban con
`auditar.ps1`. Lo que ha ido cambiando está en [CHANGELOG.md](CHANGELOG.md).

> Proyecto **independiente del dock**. Repos separados, procesos separados, reglas
> separadas. Si uno revienta, el otro sigue.

---

## Índice

1. [Qué hace](#1-qué-hace)
2. [Compilar y ejecutar](#2-compilar-y-ejecutar)
3. [Configurar](#3-configurar-islajson)
4. [Cómo está hecho](#4-cómo-está-hecho)
5. [Mapa de ficheros](#5-mapa-de-ficheros)
6. [Cosas que se midieron y no son obvias](#6-cosas-que-se-midieron-y-no-son-obvias)
7. [Cómo se prueba esto](#7-cómo-se-prueba-esto)
8. [Lo que falta](#8-lo-que-falta)

---

## 1. Qué hace

La isla tiene cuatro estados y solo enseña uno a la vez.

| Estado | Cuándo | Tamaño | Clics |
|---|---|---|---|
| **Ausente** | no hay ninguna sesión de audio | oculta | — |
| **Brasa** | hay sesión, en reposo | 140 × 5, pegada al borde | **ninguno** |
| **Asomada** | cambió la canción, saltó un aviso, corre un pomodoro | 320 × 56 | ninguno |
| **Abierta** | ratón quieto en el borde, o `Ctrl+Alt+I` | 380 × 180, despegada 10 px | los suyos |

**En reposo no roba ni un clic.** La ventana mide 520 × 260 pero `SetWindowRgn` la recorta
a lo que se está dibujando: recogida son 140 × 5 px, así que las pestañas del navegador
siguen siendo del navegador. Y para abrirse hay que **quedarse** 240 ms en la franja:
cruzar el borde de camino al botón de cerrar no la despierta.

| Gesto | Qué pasa |
|---|---|
| Ratón quieto en el centro del borde superior | Se despega y se convierte en panel |
| Clic en ⏯ / ⏮ / ⏭ | Lo mismo que la tecla de play del teclado |
| Arrastrar la barra de progreso | Salta a esa posición al soltar |
| `Ctrl+Alt+I` | Rota brasa → asomada → abierta |
| `Ctrl+Alt+T` | Arranca un pomodoro. Otra vez lo cancela |

Un botón que la sesión no admite **no se dibuja**: con Brave solo sale play/pausa, porque
declara `IsPreviousEnabled` e `IsNextEnabled` a `false`.

---

## 2. Compilar y ejecutar

Hace falta el **SDK de .NET 10** y Windows 11. La única referencia es
`Microsoft.Windows.CsWin32`, que es un generador de código y no aparece en la salida.

```powershell
# Compilar
dotnet build

# Instalar: publica a %LOCALAPPDATA%\Isla\app
dotnet publish -c Release -o "$env:LOCALAPPDATA\Isla\app"

# Arrancar
& "$env:LOCALAPPDATA\Isla\app\Isla.exe"

# Comprobar que sigue cumpliendo SEGURIDAD.md
powershell -NoProfile -ExecutionPolicy Bypass -File auditar.ps1
```

**Sobre `auditar.ps1`:** el `CLAUDE.md` del dock dice `pwsh -File`, pero en esta máquina
no hay PowerShell 7 instalado. El script está escrito para correr también en Windows
PowerShell 5.1, que es lo que hay.

Para salir todavía no hay interfaz: `Stop-Process -Name Isla`.

### Trazas

Compilada como `WinExe` no tiene consola propia, pero se engancha a la del padre si la
lanzas desde una terminal. Al arrancar imprime la pantalla elegida, las que hay y la
sesión de audio que encuentra:

```
[isla] \\.\DISPLAY2 al 100%, ventana 520x260 en 1020,0
[isla] pantallas: \\.\DISPLAY1, \\.\DISPLAY2, \\.\DISPLAY3
[isla] arrancada en 413 ms
[isla] SpotifyAB.SpotifyMusic_...!Spotify: ただ君に晴れ - ヨルシカ (03:18)
```

---

## 3. Configurar: `isla.json`

Vive en `%LOCALAPPDATA%\Isla\isla.json`. La primera vez se copia del que hay junto al
ejecutable. **Admite comentarios y comas finales.** Se recarga sola al guardarlo.

```jsonc
{
  // Vacio = la pantalla principal. Los nombres salen en la consola al arrancar.
  // Ojo con las barras: en JSON hay que doblarlas.
  "pantalla": "\\\\.\\DISPLAY2",

  "pomodoroMinutos": 25,

  // Windows ya ensena su propio aviso de volumen y NO se puede quitar.
  // Ponlo en false si te sobra verlo dos veces.
  "volumenAsoma": true,

  // Escribe en HKCU\...\Run, que sale en la pestana Inicio del Administrador
  // de tareas. Por defecto no se escribe nada.
  "autoArranque": false
}
```

Un JSON roto **no tumba la isla**: avisa por la consola y sigue con los valores por
defecto. Es un adorno del escritorio; negarse a arrancar por una coma de más sería peor.

Cambiar cualquier cosa **rehace la ventana entera**. Es bruto, pero es una sola ventana y
así mudarse a una pantalla con otro DPI sale gratis en vez de ser un caso especial.

---

## 4. Cómo está hecho

**.NET 10, Win32 crudo y `Windows.UI.Composition`.** Sin WPF, sin WinUI: se crea la
ventana con `CreateWindowEx`, se atiende su propio `WndProc` y se bombea su propio bucle.

### Las tres decisiones que explican el resto

**1. El morph son tres muelles, y lo demás son expresiones.**

La ventana mide 520 × 260 y **nunca se redimensiona**: la pastilla es un visual dentro de
ella. Tres `SpringNaturalMotionAnimation` mueven la caja —`Size`, `CornerRadius` y el
`Offset.Y` del grupo, que es el despegue— y seis `ExpressionAnimation` derivan de su alto
en vivo todo lo demás: el material, el borde, el titular, el contenido y su escala.

Esto se aparta del dock a propósito. El dock embudó su animación por un
`CompositionPropertySet` porque veinte iconos derivan de una sola posición del cursor.
Aquí hay **una caja**, y para una caja las animaciones de movimiento natural son mejores:
arrancan solas desde donde estén y son interrumpibles, que es justo lo que hace falta
cuando entras y sales del hover a medias.

Y nada de esto corre en el hilo de UI, así que el morph no pierde un fotograma aunque el
hilo se quede bloqueado decodificando una carátula.

**2. El titular y la ficha se cruzan.**

La pastilla asomada solo enseña un **titular**: miniatura y una línea. El panel abierto
enseña la ficha entera. Las dos opacidades se cruzan —el titular entra desde 26 px de
alto y se va desde 92, cuando entra la ficha— así que parece que el titular *crece* hasta
convertirse en ficha, y no que una cosa tapa a la otra.

**3. Todo el `async` de WinRT vive en el pool de hilos.**

Los eventos de medios llegan en un hilo cualquiera y los objetos de composición solo se
pueden tocar desde el que tiene la `DispatcherQueue`. En vez de montar un
`SynchronizationContext`, el resultado se empaqueta en un `record` plano y se despierta a
la ventana con un `PostMessage`. El hilo de UI recoge lo último que haya y no espera a
nadie.

### Lo que cuesta

| | |
|---|---|
| Arranque | ~200–400 ms |
| Memoria | ~70 MB de working set, **20 MB privados** |
| CPU en reposo, sin música | **0,00 %** |
| CPU en reposo, con música y la brasa latiendo | ~0,4 – 1,1 % |
| CPU con el panel abierto | ~0,8 % |

Solo hay **un temporizador permanente**, el de 120 ms que mira si el cursor está en la
franja. De él cuelgan también el latido de la brasa, las caducidades de los avisos y, una
vez por segundo, la comprobación de pantalla completa. Los otros dos —el reloj de 1 s y
la onda de 50 ms— solo corren con el panel desplegado.

---

## 5. Mapa de ficheros

| Fichero | Qué es |
|---|---|
| `Program.cs` | Punto de entrada, instancia única, y el vigilante de `isla.json`. |
| `IslaWindow.cs` | La ventana, su `WndProc`, los estados y los avisos. El fichero grande. |
| `IslaVisuals.cs` | El árbol de composición: la caja, el titular, la ficha, la onda. |
| `Medios.cs` | El puente con el canal de medios de Windows. |
| `Audio.cs` | El medidor de pico y el volumen. Solo lectura. |
| `Texto.cs` | DirectWrite. Un formato por tamaño físico y peso. |
| `Config.cs` | `isla.json` y el autoarranque. |
| `NativeMethods.txt` | **La lista cerrada de P/Invokes.** Si no está aquí, no compila. |
| `auditar.ps1` | Comprueba que el código cumple `SEGURIDAD.md`. |

---

## 6. Cosas que se midieron y no son obvias

Están comentadas en el código donde tocan, pero conviene tenerlas a mano.

**`CreateHostBackdropBrush` no muestrea nada en una app Win32 sin empaquetar.** Se crea
sin lanzar excepción y se pinta **negro**. Con un azul (0,90,220) detrás, el panel daba
(11,11,13) — el tinte sobre negro. Y el control descarta que sea cosa de esta app: la
barra del **dock**, misma máquina y mismo azul, da (48,48,48), que es su capa de tinte
blanco de alfa 48 sobre negro. **El dock tampoco tiene acrílico**, aunque sus comentarios
digan que sí. Aquí el "cristal" es alfa a secas, y poco: al 11% se leían los botones de
la ventana de detrás a través del panel.

**`TimelineProperties.Position` no avanza sola.** Solo cambia cuando la app la empuja, y
cada una lo hace a su ritmo. Medida cinco veces con 6 s entre medias, con `Playing` todo
el rato, dio `05:00` las cinco. Por eso el reloj **extrapola** desde la última posición
conocida y la barra la **anima el compositor** hasta el final de la canción.

**El pico de audio vive en una banda estrecha.** Veinte lecturas seguidas dieron 0,043 a
0,066. Normalizar dividiendo por un techo que decae da 1 casi siempre, porque el techo lo
acaba de fijar el propio pico; y estirar la banda techo-suelo tampoco, porque los dos
persiguen al pico. Lo que funciona es una **media lenta como centro** y estirar la
desviación relativa a su alrededor.

**Cada app avisa a su ritmo, y eso cambia el coste.** Spotify empuja la línea de tiempo
cada dos por tres; Brave apenas avisa. Decodificar la carátula o repintar el texto en
cada aviso subía la CPU en reposo de 0,0% a 11,7% — y con Brave no se habría visto nunca.
Por eso la carátula se cachea por canción y el contenido lleva una firma de lo que se
dibuja.

**Cada asignación a una propiedad del compositor cruza a DWM.** El latido y la onda solo
escriben si el cambio supera medio píxel. A ocho lecturas por segundo, escribir siempre
se notaba en el medidor.

**`WS_EX_TRANSPARENT` no deja pasar los clics.** Con el bit puesto y la isla recogida, un
clic a 129 px de alto le llegaba igual y movía la barra de progreso 123 segundos — porque
la región cubría 380 × 190 aunque solo se dibujaran 5 px de alto. **Lo único que de verdad
aparta el ratón es `SetWindowRgn`**, que es lo que el dock tenía escrito en su README
desde el primer día y yo no leí. La región se ajusta ahora a cada estado: al crecer se
pone ya, y al encogerse se espera a que el muelle termine o recortaría la animación de
cierre.

**`WS_EX_TOOLWINDOW` es lo que la mantiene fuera del dock** y de la barra de tareas. Es
la forma documentada que tiene una ventana de decir "no soy una app". El dock tuvo que
aprender a respetarla.

**Destruir la ventana para rehacerla mataba el proceso.** `WM_DESTROY` llamaba a
`PostQuitMessage`, y eso cierra el bucle de mensajes: la ventana nueva nacía bien y el
proceso se cerraba detrás de ella. El dock tuvo el mismo fallo con sus tres ventanas.

---

## 7. Cómo se prueba esto

No hay tests unitarios y es a propósito: casi todo lo que puede romperse aquí es
interacción con Windows y con el compositor, y un test que se burla de Win32 no prueba
nada. Lo que hay son **sondas en el scratchpad**, y tres costumbres que salieron de
equivocarse:

**Pide la señal al sistema, no a un píxel.** La mejor prueba de todo el proyecto fue
clicar el botón de pausa de la isla y preguntarle a Windows el estado de reproducción:
`Playing → Paused → Playing`, con un control clicando un hueco vacío del panel que no lo
cambió. Una sola señal probó cuatro cosas — que el clic llega, que la región no se lo
come, que el hit-test acierta y que la orden llega a la app.

**Toda prueba necesita su control.** Un contador que no cambia no prueba nada hasta que
demuestras que sabe cambiar. El arreglo del dock se dio por bueno porque abrir el Bloc de
notas movía el número y arrancar la isla no.

**Mide contra el propio fondo.** Los umbrales fijos de brillo fallaron cuatro veces
porque el escritorio de detrás cambia. Lo que funciona es restar dos capturas, o comparar
dos filas de la misma captura.

**Y una advertencia práctica:** las sondas que mueven el cursor con `SetCursorPos` se
pelean con el usuario si está delante de la máquina. Si los resultados salen absurdos,
lee `GetCursorPos` entre muestras antes de culpar al código.

---

## 8. Lo que falta

- **Sin sombra.** Dos intentos: un sprite de molde por debajo del panel tapaba el fondo al
  volverlo translúcido, y un `LayerVisual` con `Shadow` pintó la ventana entera de negro.
  El camino cuando toque es `DropShadow` con `Mask` sobre una superficie con la forma.
- **El volumen se sondea a 2 Hz**, o sea hasta medio segundo de retraso frente al aviso de
  Windows. El techo es `IAudioEndpointVolumeCallback`, que avisa por evento.
- **Una sola isla, en una sola pantalla.** Se elige cuál; no hay una por monitor, igual que
  un portátil no repite la muesca en cada pantalla.
- **Sin forma de salir por interfaz.** Hoy es `Stop-Process`.
- **Las notificaciones de otras apps** no van a estar: `UserNotificationListener` exige
  identidad de paquete, y aunque no la exigiera está prohibido por la regla 13.
