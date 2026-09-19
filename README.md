# Dock

Un dock estilo macOS para Windows 11. Se esconde solo, magnifica los iconos al pasar por
encima, se traga las ventanas con efecto genio al minimizarlas, y hay uno por pantalla con
sus propias apps.

El referente visual es MyDockFinder. **La arquitectura no**: MyDockFinder carga un driver
de kernel y por eso Defender lo marca. Este corre 100% en modo usuario, sin elevación, sin
red y sin nada instalado en el sistema. Las reglas que lo garantizan están en
[SEGURIDAD.md](SEGURIDAD.md) y se comprueban con `auditar.ps1`.

---

## Índice

1. [Qué hace](#1-qué-hace)
2. [Compilar y ejecutar](#2-compilar-y-ejecutar)
3. [Configurar](#3-configurar-dockjson)
4. [Cómo está hecho](#4-cómo-está-hecho)
5. [Mapa de ficheros](#5-mapa-de-ficheros)
6. [Añadir una función](#6-añadir-una-función)
7. [Cómo se prueba esto](#7-cómo-se-prueba-esto)
8. [Lo que falta](#8-lo-que-falta)

---

## 1. Qué hace

| Gesto | Qué pasa |
|---|---|
| Pasar por encima | El icono crece y sale su nombre. La curva empuja a los vecinos. |
| Clic | Tres estados, como la barra de tareas: si no está abierta la lanza; si lo está la trae al frente; si ya estaba delante la minimiza con efecto genio. |
| Clic en una carpeta | Se despliega en rejilla, 5 por fila. Se puede entrar en subcarpetas y volver. |
| Clic derecho | Menú: los documentos recientes de esa app, quitarla del dock, y salir. |
| Clic central | Una instancia nueva, aunque ya haya ventana. |
| Rueda | La lista de ventanas de esa app, con miniatura de la elegida y una ✕ para cerrarla. |
| Arrastrar un icono | Reordena. Sacándolo del dock, lo quita. |
| Soltar un fichero encima | Sobre un icono, lo abre con esa app. En el hueco de la derecha, lo añade al dock. |
| `Ctrl+Alt+…` | Rota entre perfiles de dock, si los hay configurados. |

Y por su cuenta: se esconde **solo cuando una ventana lo tapa de verdad**, se aparta del
todo si hay algo a pantalla completa, y reserva su hueco en el escritorio declarándose
AppBar.

---

## 2. Compilar y ejecutar

Hace falta el **SDK de .NET 10** y Windows 11 (build 26100 o superior). No hay más
dependencias: la única referencia del proyecto es `Microsoft.Windows.CsWin32`, que es un
generador de código y no aparece en la salida.

```powershell
# Compilar
dotnet build

# Autocomprobaciones: matemáticas de las curvas + extracción real de iconos.
# No abre ninguna ventana.
dotnet run -- --check

# Instalar: publica a %LOCALAPPDATA%\Dock\app
dotnet publish -c Release -o "$env:LOCALAPPDATA\Dock\app"

# Arrancar
& "$env:LOCALAPPDATA\Dock\app\Dock.exe"

# Comprobar que sigue cumpliendo SEGURIDAD.md
pwsh -File auditar.ps1
```

**Por qué se publica a `%LOCALAPPDATA%` y no se ejecuta desde `bin\`:** la configuración
vivía junto al ejecutable, que durante el desarrollo es la carpeta de compilación. Un
`dotnet clean` se llevaba por delante `dock.json` y `dock.local.json` con todo lo que
hubieras reordenado. Ahora la config vive en `%LOCALAPPDATA%\Dock\` y el binario en
`%LOCALAPPDATA%\Dock\app\`.

Para salir: clic derecho sobre el dock → *Salir del dock*.

### Trazas

El dock escribe en la consola si lo lanzas desde una terminal (`AttachConsole`). Con
`DOCK_HOVER_LOG=1` añade la traza de posición del ratón, que es lo que se usa para calibrar
las pruebas.

```powershell
$env:DOCK_HOVER_LOG = "1"
& "$env:LOCALAPPDATA\Dock\app\Dock.exe"
```

---

## 3. Configurar: `dock.json`

Vive en `%LOCALAPPDATA%\Dock\dock.json`. La primera vez se copia del que hay junto al
ejecutable. **Admite comentarios y comas finales.** Se recarga solo al guardarlo: no hace
falta reiniciar el dock.

### Lo básico

```jsonc
{
  "iconSize": 48,        // tamaño del icono en reposo, en unidades logicas (96 ppp)
  "iconSpacing": 16,     // separacion entre iconos
  "magnification": 1.3,  // cuanto crece bajo el cursor. 1.0 la apaga del todo
  "autoHide": true,      // si se esconde solo
  "autoStart": true,     // HKCU\...\Run, visible en el Administrador de tareas
  "trash": true,         // la papelera al final

  "apps": [
    { "name": "Explorador", "target": "C:/Windows/explorer.exe" },
    { "separator": true },
    { "name": "Paint", "target": "shell:AppsFolder/Microsoft.Paint_8wekyb3d8bbwe!App" }
  ]
}
```

**Usa siempre barras normales** (`/`). El dock las convierte solo, así no hay que escapar
barras invertidas en JSON. La excepción son las URLs, que se quedan como están.

### Qué puede ser un `target`

| Tipo | Ejemplo |
|---|---|
| Ejecutable | `"C:/Windows/System32/notepad.exe"` |
| Carpeta | `"C:/Users/tu/Escritorio/renpy"` — se despliega en rejilla al clicarla |
| Script o documento | `"C:/scripts/backup.ps1"` |
| App de la Store | `"shell:AppsFolder/<AppUserModelID>"` |
| Dirección o protocolo | `"https://claude.ai"`, `"ms-settings:display"` |
| Carpeta virtual | `"shell:RecycleBinFolder"` |

Con `"arguments"` se le pasan parámetros:

```jsonc
{ "name": "Copia", "target": "C:/Windows/System32/cmd.exe",
  "arguments": "/c C:\\scripts\\backup.cmd" }
```

El icono de una dirección es **el de la app que la va a abrir**, que es lo que enseña el
propio Windows.

### Apps distintas por pantalla

La clave es el nombre de dispositivo del monitor, que sale en la consola al arrancar:
`[dock] \\.\DISPLAY2 al 100%, 7 iconos`.

```jsonc
"pantallas": {
  "\\\\.\\DISPLAY2": {
    "apps": [ { "name": "Explorador", "target": "C:/Windows/explorer.exe" } ]
  }
}
```

**No hace falta escribirlo para tener docks distintos**: arrastrar un icono a una pantalla,
o sacarlo de ella, ya afecta solo a esa. El bloque es para partir de una lista base
distinta. Una pantalla que declara su lista **no hereda** lo que arrastraste en otra.

### Perfiles

```jsonc
"atajoPerfil": "Ctrl+Alt+D",
"perfiles": {
  "trabajo": { "apps": [ /* ... */ ] },
  "juegos":  { "apps": [ /* ... */ ] }
}
```

El atajo rota entre **el dock de siempre** y cada perfil, en ese orden, y vuelve a empezar.
Cada perfil recuerda su propio orden y sus propios añadidos; un perfil tampoco hereda lo que
arrastraste estando en otro. Un perfil puede llevar dentro su propio bloque `pantallas`.

### `dock.local.json`

Lo escribe el dock, no tú. Guarda lo que cambias arrastrando: el orden, lo añadido y lo
quitado. La clave de cada bloque es `perfil|pantalla`, o solo `pantalla` cuando no hay
perfil activo.

`dock.json` es tuyo y el dock **no lo toca nunca**. Si quieres empezar de cero, borra
`dock.local.json`.

---

## 4. Cómo está hecho

**.NET 10, Win32 crudo y `Windows.UI.Composition`.** Sin WPF, sin WinUI, sin WinForms: se
crea la ventana con `CreateWindowEx`, se atiende su propio `WndProc` y se bombea su propio
bucle de mensajes.

### Las tres decisiones que explican el resto

**1. La animación no corre en nuestro hilo.** Todo el movimiento del dock son
`ExpressionAnimation` sobre un `CompositionPropertySet` compartido: el hilo de UI solo
escribe la posición del ratón en una propiedad y el compositor —que vive en el proceso de
DWM— recalcula. Si el hilo de UI se bloquea extrayendo un icono, la animación sigue fluida.

La contrapartida: **las expresiones tienen un límite de longitud** que se alcanza antes de
lo que parece. Por eso `DockExpressions.cs` precalcula los subtérminos compartidos en vez de
repetirlos por icono.

**2. El dock nunca roba el foco.** `WS_EX_NOACTIVATE` no basta: hay que responder
`MA_NOACTIVATE` a `WM_MOUSEACTIVATE`. De ahí sale que el menú del clic derecho esté dibujado
a mano con el compositor en vez de usar `TrackPopupMenu`, que exige foco para cerrarse bien.

**3. La región de la ventana es lo que decide qué es del dock.** La ventana ocupa el ancho
entero de la pantalla, pero `SetWindowRgn` la recorta a la barra más lo que se esté
dibujando encima. Y esto es importante: **`HTTRANSPARENT` no atraviesa procesos**. Se midió
poniendo Paint debajo del hueco — devolviendo `HTTRANSPARENT` el clic no le llegaba igual.
Lo único que de verdad deja pasar el ratón es la región.

La región también **recorta el dibujo**, así que tiene que cubrir todo lo que se pinte: la
barra, el icono magnificado, la etiqueta, y el menú cuando está abierto.

### El inventario de ventanas

Saber qué apps están abiertas alimenta el puntito, la lista de la rueda y el autoocultar.
Se hace **una sola pasada para todos los docks** —enumerar procesos y ventanas no depende de
qué iconos tenga cada pantalla— y se dispara con los avisos del shell
(`RegisterShellHookWindow`), no con un temporizador. Queda una red de seguridad cada diez
segundos para el caso que los avisos no cubren: una ventana que nace sin título, se titula
después, y nadie activa nunca.

### Cosas que se midieron y no son obvias

Están comentadas en el código donde tocan, pero conviene tenerlas a mano:

- **La rueda no puede cambiar la ventana en primer plano.** `SetForegroundWindow` desde un
  evento de rueda no surte efecto, y `SetWindowPos(HWND_TOP)` sobre una ventana ajena
  devuelve `TRUE` y no mueve nada. Es a propósito: la rueda se enruta a la ventana bajo el
  puntero justamente para *no* activarla. Por eso la rueda enseña una lista y es el clic
  quien abre.
- **La barra de tareas ya no se revela** porque la franja del borde inferior es del dock. No
  hizo falta tocar ningún ajuste del sistema. `ABM_SETAUTOHIDEBAREX` devuelve 0: la barra ya
  tiene ese hueco.
- **Las apps UWP no son dueñas de su ventana.** Al minimizarlas, Windows les saca la
  `CoreWindow` de dentro del `ApplicationFrameWindow` y el cruce con su icono se pierde. Por
  eso se recuerda el PID resuelto.
- **El Explorador aparece como abierto siempre** si no excluyes la ventana del escritorio
  (`GetShellWindow`), que tiene título y pasa todos los filtros.
- **La ruta del `.exe` no vale como AppUserModelID.** Para las listas de saltos hay que
  leerlo de una ventana viva de la app.

---

## 5. Mapa de ficheros

| Fichero | Qué es |
|---|---|
| `Program.cs` | Punto de entrada. `OleInitialize` lo primero, crea un dock por pantalla, bucle de mensajes. |
| `DockWindow.cs` | La ventana, su `WndProc` y toda la interacción. Es el fichero grande. |
| `DockVisuals.cs` | El árbol de visuals: barra, iconos, puntos, etiquetas. Un `Compositor` para todo el proceso. |
| `DockExpressions.cs` | Las expresiones de animación, en forma cerrada. |
| `Magnify.cs` / `GenieCurve.cs` | Las matemáticas. Cada una con su autocomprobación ejecutable. |
| `GenieOverlay.cs` | La ventana del efecto genio: 40 franjas deformadas. |
| `StackOverlay.cs` / `StackItems.cs` | La rejilla de una carpeta. |
| `PreviewOverlay.cs` | La miniatura de la ventana elegida. |
| `DockMenu.cs` | El menú del clic derecho y la lista de ventanas, dibujados en el compositor. |
| `Labels.cs` | Texto con DirectWrite. Un formato por escala de pantalla. |
| `Icons.cs` | Extrae iconos del shell. Caché en memoria compartida entre docks. |
| `Config.cs` | `dock.json`, `dock.local.json`, perfiles y pantallas. |
| `Running.cs` | El inventario de ventanas. |
| `WindowActions.cs` | Las cuatro cosas que se le hacen a una ventana ajena. |
| `WindowCapture.cs` | `PrintWindow`. Lo usan el genio y las miniaturas. |
| `AppBar.cs` | El registro como barra de herramientas de escritorio. |
| `DropTarget.cs` | `IDropTarget`: recibir ficheros arrastrados. |
| `JumpList.cs` | Documentos recientes de una app. |
| `Profiles.cs` | El atajo que rota perfiles. |
| `AutoStart.cs` | La entrada de `HKCU\...\Run`. |
| `NativeMethods.txt` | **La lista cerrada de P/Invokes.** Si no está aquí, no se genera y no compila. |
| `auditar.ps1` | Comprueba que el código cumple `SEGURIDAD.md`. |

---

## 6. Añadir una función

1. **Lee [SEGURIDAD.md](SEGURIDAD.md) primero.** Si la función necesita algo prohibido, o se
   rediseña o se enmienda el documento — por escrito y antes de tocar el código.
2. **¿Necesita un P/Invoke nuevo?** Añádelo a `NativeMethods.txt`. Ahí es donde un auditor
   mira, así que agrúpalo bajo un comentario que diga para qué es.
3. **¿Toca una ventana ajena?** Tiene que salir de un gesto del usuario. No existe ningún
   camino desde un temporizador, y así debe seguir.
4. **¿Dibuja algo encima de la barra?** La región tiene que llegar hasta ahí o se recorta.
   Ver `ApplyRegion`.
5. **Mídelo.** Ver abajo.
6. **`pwsh -File auditar.ps1`** antes de commitear.

### Convenciones

- **Comentarios en español, código en inglés.** Los comentarios explican *por qué*, no
  *qué*: lo que se midió, lo que se probó y no funcionó, la trampa que hay debajo.
- **Un commit por cosa**, con el mensaje contando qué se midió.
- **Nada de abstracciones especulativas.** No hay interfaces con una implementación ni
  fábricas de un producto.
- Los atajos deliberados se marcan con un comentario `ponytail:` que dice cuál es el techo.

---

## 7. Cómo se prueba esto

No hay tests unitarios y es a propósito: casi todo lo que puede romperse aquí es
interacción con Windows, y un test que se burla de Win32 no prueba nada. Lo que hay:

**Autocomprobaciones ejecutables** (`--check`), para lo que sí es lógica pura: las curvas de
magnificación y del genio, el premultiplicado de alfa, y la extracción real de los iconos
que tengas configurados.

**Sondas que miden el comportamiento de verdad.** Mover el ratón con `SetCursorPos`,
inyectar clics con `mouse_event`, leer el resultado con `WindowFromPoint`, `GetWindowRect` o
un diff de capturas de pantalla. Van en el scratchpad, no en el repo.

**Y un aviso que sale de la experiencia de construir esto:** en este proyecto el **método de
prueba se equivocó más veces que el código**. Ejemplos reales:

- Un clic sintético en el borde izquierdo de la barra parecía demostrar que los clics habían
  dejado de llegar. La coordenada estaba un píxel fuera, y al poner el cursor ahí el layout
  se desplazaba lo justo para excluirla.
- Un diff de píxeles que medía la etiqueta dio un resultado absurdo porque había un vídeo
  reproduciéndose detrás.
- `WindowFromPoint` ignora `HTTRANSPARENT`, así que decía que el dock recogía clics que en
  realidad dejaba pasar.

**Antes de creerte que algo está roto, comprueba que la sonda mide lo que crees.** Y cuando
arregles algo, mete el fallo a propósito otra vez y comprueba que la prueba lo detecta: si
no lo detecta, la prueba no vale.

---

## 8. Lo que falta

- **Rendimiento.** Los tres números medidos: arranque **1029 ms** (objetivo < 1000),
  memoria **121 MB** con tres pantallas (objetivo < 60), CPU en reposo **~3%** (objetivo 0).
  Pendiente: caché de iconos a disco, `PublishReadyToRun`, y averiguar de dónde sale ese 3%
  — no es el inventario ni el compositor, y con los temporizadores apagados no baja.
- **Una rejilla con todas las miniaturas a la vez**, en vez de la de la ventana elegida.
- **Empaquetar como MSIX**, si alguna vez hace falta identidad de paquete.
- Las listas de saltos solo funcionan con apps que declaran su AppUserModelID en la ventana.
  El Explorador va por una excepción escrita a mano.
