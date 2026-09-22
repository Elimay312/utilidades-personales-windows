# Agenda

Calendario nativo para Windows 11, escrito en C++ y Win32 puro. Se abre con un atajo global
en un popup compacto sobre la barra de tareas, entiende lenguaje natural (`mañana 5pm
dentista`) y, al hacer clic en el calendario, se expande con animación a una app completa con
vistas de día, semana y mes. Sincroniza con Google Calendar y Google Tasks.

Las decisiones de producto, el stack y el sistema de diseño están en [CLAUDE.md](CLAUDE.md).

## Estado

**En desarrollo, fase 2.** Agenda se queda residente en la bandeja, el atajo abre un popup con
fondo acrylic en la esquina inferior derecha del monitor de trabajo, y el popup ya muestra el
mes, los eventos del día y el campo de texto. El panel se adapta al monitor: su alto es el
42 % del área de trabajo, entre 380 y 560 DIP, y todo lo de dentro escala con él. Los eventos son **datos de ejemplo en
memoria**: todavía no hay SQLite, ni parser, ni sincronización, y pulsar Enter no hace nada.

| Tema oscuro | Tema claro |
|---|---|
| ![El popup de Agenda en tema oscuro](docs/img/popup.png) | ![El popup de Agenda en tema claro](docs/img/popup-claro.png) |

Agenda sigue el tema de las aplicaciones de Windows, que lee del registro al abrir el popup.

## Requisitos

- Windows 10 1809 o superior, o Windows 11 (x64). El fondo acrylic necesita Windows 11 22H2;
  en versiones anteriores el panel se pinta opaco. Aunque esté disponible, Windows dibuja un
  color plano en vez del material cuando el efecto no se puede calcular: transparencia
  desactivada, ahorro de energía, sesión remota o según el adaptador de vídeo. Eso lo decide
  DWM y le pasa a cualquier ventana, no solo a Agenda.
- Visual Studio 2022 con la carga de trabajo *Desarrollo para el escritorio con C++* (MSVC
  v143 y el SDK de Windows 10/11).
- CMake 3.28 o superior.
- [vcpkg](https://vcpkg.io) con la variable de entorno `VCPKG_ROOT` apuntando a su carpeta.
  Si no está instalado, CMake descarga nlohmann-json y Catch2 con `FetchContent` y el build
  funciona igual; solo hace falta Git y conexión.

## Compilación

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Lo mismo con `release`. Los binarios quedan en `build\debug\Agenda.exe` y
`build\release\Agenda.exe`.

### Ejecución

```
build\debug\Agenda.exe --monitor=3
```

Al arrancar no se ve nada: Agenda deja el icono en la bandeja y espera el atajo. Con el
clic izquierdo en el icono se abre el popup; con el derecho aparece un menú con **Abrir** y
**Salir**. Si otra aplicación ya usa el atajo, Agenda lo registra en el log, avisa con un
globo en la bandeja y sigue funcionando: se abre desde el icono.

### Cómo se usa el popup

- **Alt+Shift+C** abre y cierra el popup. También se cierra con Esc o al hacer clic fuera.
- Abre siempre en el día de hoy, con el campo de texto enfocado y el cursor esperando.
- **Ratón:** clic en un día para seleccionarlo, clic en `‹` y `›` para cambiar de mes, clic en
  el campo para poner el cursor donde se pinchó. Los días y las flechas se iluminan al pasar
  por encima.
- **Teclado:** con el campo vacío, las cuatro flechas mueven el día seleccionado, `←` y `→` de
  uno en uno y `↑` y `↓` de semana en semana. Con texto escrito, `←` y `→` mueven el cursor
  (con `Shift` seleccionan) e `Inicio` y `Fin` van a los extremos de la línea, mientras `↑` y
  `↓` siguen moviendo el día. `Ctrl+A`, `Ctrl+C`, `Ctrl+X` y `Ctrl+V` hacen lo de siempre.
- Al cambiar de mes, el día seleccionado se mueve con él, así que la lista de abajo siempre
  muestra un día que está en pantalla. Si el día no existe en el mes nuevo, se recorta al
  último que sí (31 de enero más un mes es 28 de febrero).
- La lista muestra hasta dos eventos del día; si hay más, el segundo lleva un `+N` a la
  derecha. Un día sin eventos dice «Sin eventos».
- **Enter todavía no hace nada.** El parser de lenguaje natural llega en la fase 3.

### Opciones de línea de comandos

- `--monitor=N` fija el monitor por su número de Windows, es decir el dispositivo
  `\\.\DISPLAYN`. El orden de enumeración **no** es ese número.
- `AGENDA_DEV_MONITOR=N` hace lo mismo, y `--monitor` tiene prioridad.
- En builds Debug el valor por defecto es 3, que es el monitor de desarrollo. En Release, sin
  argumento ni variable, se usa el monitor primario.
- `--theme=dark` o `--theme=light` fuerza un tema sin tocar la configuración de Windows. Sin
  el argumento, Agenda sigue al sistema.
- `--panel=WxH` fuerza el tamaño del panel en DIP, por ejemplo `--panel=453x560`, en vez de
  calcularlo desde el monitor. Sirve para juzgar en una pantalla un tamaño que esa pantalla no
  produciría, y vale tanto para la app como para `--render-snapshot`.
- Si el monitor indicado no está conectado, se registra el error y el proceso termina con
  código **2**. No hay fallback silencioso a otro monitor. Si ya hay otra instancia
  ejecutándose, termina con código **1**.

### Capturas de las vistas

```
build\debug\Agenda.exe --render-snapshot=popup --theme=dark  --out=docs\img\popup.png
build\debug\Agenda.exe --render-snapshot=popup --theme=light --out=docs\img\popup-claro.png
```

Renderiza la vista fuera de pantalla con Direct2D sobre un bitmap WIC, guarda el PNG y sale.
Así se revisa el diseño: **no con capturas del escritorio**. No necesita monitor ni que la
instancia esté libre, así que funciona con la app abierta. Si falta `--out`, escribe
`shot.png`. Como el acrylic no existe fuera de pantalla, el PNG lleva detrás un gris neutro
que hace su papel, más claro u oscuro según el tema. La captura fija el 22 de septiembre de
2026 como «hoy» y el panel en su tamaño base de 340×420, para que el PNG solo cambie cuando
cambie el diseño y no cuando cambie el monitor.

Ojo: por eso mismo la captura se renderiza siempre a 96 ppp y **no sirve para revisar el
escalado**. Los fallos de DPI solo se ven con la app abierta en un monitor escalado.

Es un ejecutable de subsistema Windows, así que no devuelve el control a la consola: en
PowerShell conviene lanzarlo con `Start-Process ... -Wait` si hace falta esperar al archivo.

### Datos en disco

- Log: `%LOCALAPPDATA%\Agenda\logs\agenda-AAAAMMDD.log` (UTF-8, un archivo por día).
- Configuración: `config.json` y, opcionalmente, `config.local.json`, primero junto al
  ejecutable y después en `%LOCALAPPDATA%\Agenda`. Se fusionan en ese orden, así que el
  archivo local manda. Los dos pueden faltar. **`config.local.json` guarda los secretos y
  nunca se sube al repositorio.**

| Clave | Por defecto | Qué hace |
|---|---|---|
| `hotkey` | `"Alt+Shift+C"` | Atajo global. Combina `Ctrl`, `Alt`, `Shift` y `Win` con una letra, un dígito, `F1`–`F24`, `Space`, `Enter`, `Tab` o `Esc`. |
| `popup.openMs` | `160` | Duración de la animación de apertura, en milisegundos. |
| `popup.closeMs` | `120` | Duración de la de cierre. |

```json
{
  "hotkey": "Ctrl+Alt+Space",
  "popup": { "openMs": 160, "closeMs": 120 }
}
```

Si Windows tiene desactivadas las animaciones (Configuración → Accesibilidad → Efectos
visuales), el popup aparece y desaparece sin animación.

## Estructura del proyecto

```
src/
  app/      entrada (wWinMain), atajo global, bandeja, monitores y argumentos
  ui/       ventana popup, render D2D, composición, animación y capturas
  nlp/      parser de lenguaje natural, sin dependencias de interfaz
  data/     SQLite, modelos y repositorios
  sync/     OAuth y clientes de Google Calendar y Tasks
  core/     logging, configuración, rutas y utilidades
tests/      pruebas con Catch2
assets/     manifiesto (DPI Per-Monitor v2) e iconos
docs/       capturas y decisiones de arquitectura
```

## Hoja de ruta

| Fase | Qué entrega | Estado |
|---|---|---|
| 0 | Esqueleto que compila, documentación, logging, configuración y regla de monitores | Hecha |
| 1 | Ventana popup con fondo acrylic, atajo global e icono en la bandeja | Hecha |
| 2 | Sistema de diseño y vista de mes compacta con datos de ejemplo | Hecha |
| 3 | Parser de lenguaje natural con vista previa en vivo | Pendiente |
| 4 | Almacenamiento en SQLite: eventos, tareas y caché local | Pendiente |
| 5 | Sincronización con Google Calendar y Google Tasks (OAuth) | Pendiente |
| 6 | Expansión animada a la app completa con vistas de día, semana y mes | Pendiente |
| 7 | Pulido, rendimiento, empaquetado y arranque con Windows | Pendiente |

El detalle de cada fase vive en el plan de fases del proyecto; las fases 3 a 7 pueden ajustarse
sobre la marcha.
