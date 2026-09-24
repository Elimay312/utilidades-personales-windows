# hud

El aviso de volumen de Windows, rehecho. El de serie no cambia desde 2012: una caja gris en la
esquina superior izquierda que aparece de golpe y desaparece de golpe.

Este sale abajo, centrado, es una cápsula acrílica, y **se transforma** en vez de ir y venir:
si sigues pulsando no se cierra para volver a abrirse — la barra vuelve a muellear y el glifo
morphea en el sitio.

Y el aviso de Windows **no sale**, sin tocar nada de nadie: al registrar las teclas, el shell
no llega a verlas. Eso ocupa la §1 de `SEGURIDAD.md` y vale la pena leerlo.

.NET 10, Win32 crudo y `Windows.UI.Composition`, como el resto de la familia
(`..\dock`, `..\isla`, `..\lanzador`, `..\quicklook`).

---

## Estado

**Terminado.** Las teclas de volumen son suyas, el aviso de Windows no sale, el HUD aparece
también — en menos de 150 ms — cuando el volumen lo cambia otra cosa, y funciona en las tres
pantallas, que van a 125%, 100% y 175%. `hud.json` se recarga al guardarlo.

**Sigue al dispositivo de salida predeterminado.** Cambias de altavoces y las teclas mueven
el volumen de los nuevos, no el de los viejos. No es gratis: el endpoint de audio que tienes
abierto **no falla** al cambiar de dispositivo, se queda contestando del anterior —medido:
47 muestras, cero excepciones, 38 % contra el 100 % real—, así que hace falta que COM avise.
Está en `SEGURIDAD.md` §3.2 y contado en el `CHANGELOG`.

```
Hud.exe             # lo normal
Hud.exe --demo      # niveles falsos en bucle, sin tocar el volumen ni las teclas
Hud.exe --check     # logica pura, no abre ventana
```

| Hito | Qué | Estado |
|---|---|---|
| H0 | `SEGURIDAD.md`, `auditar.ps1`, ventana, colocación por monitor y DPI | ✅ |
| H1 | La cápsula: Composition, acrílico, barra, glifos, los cuatro morphs | ✅ |
| H2 | Volumen de verdad: `RegisterHotKey` + `IAudioEndpointVolume` | ✅ |
| H3 | Multi-monitor, DPI mixto, pantalla completa, autoarranque | ✅ |

El plan original tenía dos hitos más, **el brillo y apartar el aviso nativo**, y los dos
murieron midiendo antes de costar una línea de código. Está contado justo debajo.

## En qué pantalla sale

En la del **cursor**, no en la primaria ni en la de la ventana activa. Con un juego a
pantalla completa da igual, porque el juego confina el ratón a su monitor; y en el
escritorio, donde está el ratón suele ser donde estás mirando.

El precio: si dejas el ratón aparcado en otra pantalla, el HUD sale ahí. Mirar la ventana
en primer plano en vez del cursor arreglaría ese caso, pero obligaría a abrir la regla 15 de
`SEGURIDAD.md` — que es absoluta — para leer una ventana ajena, y no compensa.

**Se recoloca al aparecer, no mientras está puesto.** Si cambias el volumen, mueves el ratón a
otra pantalla y vuelves a cambiarlo antes de que se cierre (1,6 s), se queda donde estaba en
vez de teletransportarse a media animación.

La [isla](../isla/README.md) sigue al cursor con la misma idea y una regla distinta, porque
vive permanentemente en el borde: ella **sí** se muda mientras está puesta, con una
histéresis de ~375 ms para no mudarse al rozar un borde de paso. Un aviso que dura 1,6 s
puede permitirse decidir una vez; una que está siempre, no.

## Los dos hitos que murieron midiendo

El plan tenía un hito entero para apartar el aviso nativo de Windows, y era el que justificaba
la única excepción de `SEGURIDAD.md`. No hizo falta, por dos motivos independientes, los dos
medidos:

**No se puede.** El aviso no es una ventana. Una sonda tomó 6111 muestras de la capa de
ventanas en 20 segundos (mediana 3 ms, peor hueco 36 ms) mientras se pulsaban 22 teclas de
volumen: ni una sola ventana top-level apareció, se movió ni se hizo visible. La clase
`NativeHWNDHost` que usan las soluciones de internet ya no existe en Windows 11 26200.

**No hace falta.** El aviso sale porque el shell recibe la tecla. `RegisterHotKey` la consume,
así que el shell no la ve, y cambiar el volumen por `IAudioEndpointVolume` no dispara ningún
aviso. Medido con una sonda que solo registra las tres teclas: **el recuadro gris no apareció
ni una vez**, y el volumen tampoco se movió — que es la otra mitad de la prueba.

**Y por eso tampoco hay brillo.** Sus teclas van por ACPI, no se pueden capturar, y el aviso de
Windows saldría igual: dos indicadores, que es lo que este proyecto existe para evitar. Antes
de que se cayera, el brillo era la mitad del alcance y la única razón para una segunda
dependencia. Ahora el HUD hace una cosa y la hace entera.

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

`%LOCALAPPDATA%\Hud\hud.json`. Admite comentarios y comas finales, y **se recarga sola al
guardarla**: no hace falta reiniciar para cambiar el paso, la posición o el autoarranque.

| Clave | Por defecto | Qué hace |
|---|---|---|
| `pasoVolumen` | `2` | Cuánto sube o baja por pulsación, en %. Windows usa 2 y no deja cambiarlo; aquí sí. La primera pulsación alinea a la rejilla: desde un 37% con paso 5 se va a 40, no a 42 |
| `msAutoocultar` | `1600` | Cuánto se queda en pantalla tras la última pulsación |
| `posicion` | `"abajo"` | `"abajo"` como macOS, o `"arriba"` |
| `autoArranque` | `false` | `HKCU\...\Run`, visible en la pestaña Inicio del Administrador de tareas |

## Atajos

| | |
|---|---|
| Subir / bajar / silenciar | Las teclas de volumen del teclado — sin aviso de Windows |
| Ctrl+Alt+H | Salir |

Ctrl+Alt+H es hoy la única forma limpia de cerrarlo: no hay icono de bandeja. Mientras el HUD
no corre, las teclas de volumen vuelven a ser de Windows, con su recuadro gris y todo.

## Los cuatro morphs

Es la razón de ser del proyecto, así que van escritos:

1. **Entrada y salida.** La cápsula no aparece: crece desde una línea fina, con muelle
   (damping 0.8, periodo 55 ms), deslizando hacia arriba y apareciendo. Al irse, lo contrario.
2. **El relleno de la barra.** `ExpressionAnimation` sobre un escalar al que se escribe con
   muelle. Nunca un keyframe lineal: la barra tiene que sentir inercia.
3. **Silencio.** El relleno colapsa a cero con muelle, el glifo del altavoz morphea a
   silenciado, y la cápsula se destiñe. No es un icono que se cambia: es el mismo que pierde
   las ondas.
4. **El tope.** Subir al 100% o bajar al 0% no cambia nada, así que sin esto no habría
   respuesta ninguna: la cápsula da un squash de 1.03 a 1.0. Resuelve gratis el único agujero
   de UX que tenía el diseño.

Y la regla que los gobierna: **si el HUD ya está en pantalla, no se cierra para volver a
abrirse.** Todo pasa en el sitio.

## Decisiones que explican el resto

1. **Nada de hooks de teclado.** `SetWindowsHookEx` es la forma fácil de saber que has pulsado
   una tecla de volumen, y está prohibida (regla 3 y 4). Se usa `RegisterHotKey` sobre tres
   teclas concretas, que **consume** la pulsación — por eso el volumen lo pone el HUD, y por
   eso el paso es configurable.
2. **El HUD no se come los clics.** Ni siquiera encima de la cápsula. Los vecinos usan
   `SetWindowRgn` porque la isla midió que `WS_EX_TRANSPARENT` a secas no basta; aquí se
   volvió a medir **con `WS_EX_LAYERED` y `WS_EX_NOREDIRECTIONBITMAP` puestos** y entonces sí funciona: sin region, sin
   perder el dibujo de Composition y sin que el marco se vuelva negro opaco al cerrar la tapa o entrar a UAC.
3. **El alcance lo vigila la auditoría.** El brillo se cayó midiendo, y con él WMI y la segunda
   dependencia. `auditar.ps1` falla si reaparece `WmiMonitor`, `System.Management` o
   `ManagementObject`: un alcance que no se comprueba se vuelve a ensanchar solo.
4. **La animación no corre en nuestro hilo.** Todo es `ExpressionAnimation` sobre un
   `CompositionPropertySet`, como el dock: el hilo de UI solo escribe escalares y el resto lo
   hace DWM. Ojo con el límite de longitud de las expresiones, que el dock alcanzó dos veces.
