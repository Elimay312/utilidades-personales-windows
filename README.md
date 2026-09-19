# hud

El aviso de volumen y brillo de Windows, rehecho. El de serie no cambia desde 2012: una caja
gris en la esquina superior izquierda que aparece de golpe y desaparece de golpe.

Este sale abajo, centrado, es una cápsula acrílica, y **se transforma** en vez de ir y venir:
si pulsas brillo mientras está enseñando el volumen, no se cierra para volver a abrirse —
cambia de icono, de color y de nivel sin moverse del sitio.

.NET 10, Win32 crudo y `Windows.UI.Composition`, como el resto de la familia
(`..\dock`, `..\isla`, `..\lanzador`, `..\quicklook`).

---

## Estado

**H0 — andamio.** Compila, arranca, se coloca donde debe y sale con Ctrl+Alt+H. Todavía no
dibuja nada ni toca el volumen: la ventana se crea pero no se enseña.

| Hito | Qué | Estado |
|---|---|---|
| H0 | `SEGURIDAD.md`, `auditar.ps1`, ventana, colocación por monitor y DPI | ✅ |
| H1 | La cápsula: Composition, acrílico, barra, glifos, los cinco morphs | |
| H2 | Volumen de verdad: `RegisterHotKey` + `IAudioEndpointVolume` | |
| H3 | Brillo de la pantalla interna: WMI, solo lectura | |
| H4 | ~~Apartar el flyout nativo~~ — **no hace falta y no se puede**, ver abajo | ⚪ |
| H5 | Multi-monitor, DPI mixto, pantalla completa, autoarranque | |

## Por qué no hay H4

El plan tenía un hito entero para apartar el aviso nativo de Windows, y era el que justificaba
la única excepción de `SEGURIDAD.md`. No existe, por dos motivos independientes, los dos
medidos:

**No se puede.** El aviso no es una ventana. Una sonda tomó 6111 muestras de la capa de
ventanas en 20 segundos (mediana 3 ms, peor hueco 36 ms) mientras se pulsaban 22 teclas de
volumen: ni una sola ventana top-level apareció, se movió ni se hizo visible. La clase
`NativeHWNDHost` que usan las soluciones de internet ya no existe en Windows 11 26200.

**No hace falta.** El aviso sale porque el shell recibe la tecla. `RegisterHotKey` la consume,
así que el shell no la ve, y cambiar el volumen por `IAudioEndpointVolume` no dispara ningún
aviso. Medido con una sonda que solo registra las tres teclas: **el recuadro gris no apareció
ni una vez**, y el volumen tampoco se movió — que es la otra mitad de la prueba.

Lo que **sigue saliendo** es el aviso de brillo, porque sus teclas van por ACPI y no hay nada
que capturar. Eso no tiene solución permitida, y no la tiene prohibida que funcione tampoco.

## Cómo se construye

```powershell
dotnet build                                            # 0 errores, 0 advertencias
dotnet run -- --check                                   # logica pura, no abre ventana
powershell -File auditar.ps1                            # tiene que decir TODO LIMPIO
dotnet publish -c Release -o "$env:LOCALAPPDATA\Hud\app"
& "$env:LOCALAPPDATA\Hud\app\Hud.exe"                   # nunca desde bin\
```

> Los vecinos documentan `pwsh -File auditar.ps1`. En esta máquina no hay PowerShell 7
> instalado, así que el script está escrito para correr igual en Windows PowerShell 5.1:
> ASCII puro, sin operadores de PS7.

## Lo que hay que leer antes de tocar código

**[SEGURIDAD.md](SEGURIDAD.md), y en particular su §1.** Cuenta una excepción que se abrió
con todo el papeleo hecho y se cerró el mismo día al medirla. Es la sección que explica por
qué este programa no toca ninguna ventana ajena — y por qué no le hace falta.

## Ajustes

`%LOCALAPPDATA%\Hud\hud.json`. Admite comentarios y comas finales.

| Clave | Por defecto | Qué hace |
|---|---|---|
| `pasoVolumen` | `2` | Cuánto sube o baja por pulsación, en %. Windows usa 2 y no deja cambiarlo; aquí sí |
| `msAutoocultar` | `1600` | Cuánto se queda en pantalla tras la última pulsación |
| `posicion` | `"abajo"` | `"abajo"` como macOS, o `"arriba"` |
| `autoArranque` | `false` | `HKCU\...\Run`, visible en la pestaña Inicio del Administrador de tareas |

## Atajos

| | |
|---|---|
| Subir / bajar / silenciar | Las teclas de volumen del teclado (desde H2) |
| Ctrl+Alt+H | Salir |

Ctrl+Alt+H es hoy la única forma limpia de cerrarlo, y el cierre limpio importa: es lo que
devuelve el aviso nativo a su sitio.

## Decisiones que explican el resto

1. **Nada de hooks de teclado.** `SetWindowsHookEx` es la forma fácil de saber que has pulsado
   una tecla de volumen, y está prohibida (regla 3 y 4). Se usa `RegisterHotKey` sobre tres
   teclas concretas, que **consume** la pulsación — por eso el volumen lo pone el HUD, y por
   eso el paso es configurable.
2. **El brillo se observa, no se escribe.** Las teclas Fn de brillo van por ACPI y no llegan
   como tecla; Windows ya cambia el brillo solo. El HUD se entera por
   `WmiMonitorBrightnessEvent` y lo dibuja. `WmiSetBrightness` no está en el código, y
   `auditar.ps1` comprueba que sigue sin estar.
3. **La animación no corre en nuestro hilo.** Todo es `ExpressionAnimation` sobre un
   `CompositionPropertySet`, como el dock: el hilo de UI solo escribe escalares y el resto lo
   hace DWM. Ojo con el límite de longitud de las expresiones, que el dock alcanzó dos veces.
