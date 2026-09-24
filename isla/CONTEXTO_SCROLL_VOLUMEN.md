# Tarea: hacer preciso el control de volumen con scroll en la isla

## Contexto (léelo completo antes de tocar código)

Tú generaste esta app conmigo: es una "isla" estilo Dynamic Island para Windows, escrita en C#. Una de sus funciones es que, al poner el cursor sobre la isla y hacer scroll, sube o baja el volumen del sistema.

**El problema:** con el touchpad del portátil, un deslizamiento pequeño con dos dedos sube o baja el volumen de golpe (a veces casi todo el rango). Con eso es imposible hacer ajustes finos.

**Mi equipo:**
- Windows 11, Lenovo LOQ 15ARP9 (Ryzen 7 7435HS, RTX 4060).
- Touchpad Precision TouchPad (PTP) de Microsoft, superficie Mylar, 75 x 120 mm. Ya usa el driver de Microsoft; el problema no es el driver.

**Causa probable (verifícala en el código, no la asumas):**
1. Una rueda de mouse envía un evento por notch con delta de ±120. El touchpad PTP envía muchos eventos seguidos con deltas pequeños (ej. 3, 8, 15, 40). Si el código aplica un paso fijo de volumen por evento (tipo `if (delta > 0) volumen += 2`), un gesto corto dispara decenas de pasos.
2. Si el volumen se cambia simulando teclas (`VK_VOLUME_UP` / `VK_VOLUME_DOWN` con `keybd_event`, `SendInput` o similar), cada pulsación son pasos fijos del 2% y nunca habrá control fino.

## Qué quiero (estado final)

1. El cambio de volumen es **proporcional al delta real** del scroll, no un paso fijo por evento. Fórmula base:
   `volumen += delta / 120.0 * PorcentajePorNotch * factor`
   acumulando decimales internamente y enviando al sistema el valor redondeado a 1%.
2. Se distingue rueda física de touchpad (heurística: `Math.Abs(delta) % 120 != 0` → touchpad) y cada uno tiene su propia sensibilidad configurable. Valores iniciales: `PorcentajePorNotch = 2.0`, sensibilidad touchpad `= 0.5`.
3. El volumen se fija directamente con la API de audio de Windows (Core Audio), por ejemplo con NAudio: `MMDeviceEnumerator` → `GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia)` → `AudioEndpointVolume.MasterVolumeLevelScalar`. No simular teclas de volumen.
4. Si pasan ~400 ms sin scroll, el valor interno se resincroniza leyendo el volumen real del sistema (por si cambió desde otro lado, como las teclas del teclado).
5. La lógica vive en **una clase reutilizable** (ej. `ScrollVolumeController` o un nombre más general si encaja mejor con el proyecto) para poder usarla después en otros controles con scroll (brillo, progreso de canción, etc.). No quiero lógica de scroll duplicada en cada handler.
6. El volumen siempre queda entre 0 y 100.

**Opcional, solo después de que lo anterior funcione y yo lo apruebe:** detectar fin de gesto por tiempo (sin eventos durante ~80 ms) y añadir una inercia suave al final, tipo macOS.

## Cómo quiero que trabajes

**Paso 1 — Investigar y reportar (sin editar nada):**
- Revisa el proyecto y dime: qué framework de UI usa (WPF, WinForms, WinUI 3 u otro), en qué archivo(s) y método(s) se maneja el scroll sobre la isla, cómo se obtiene el delta (evento del framework, hook global `WH_MOUSE_LL`, etc.) y cómo se cambia el volumen actualmente.
- Confirma o descarta las dos causas probables de arriba.
- Propón el plan concreto: qué archivos vas a tocar y qué vas a crear.
- **Detente aquí y espera mi aprobación.**

**Paso 2 — Implementar lo aprobado:**
- Crea la clase reutilizable y conecta el handler de scroll de la isla a ella.
- Deja la sensibilidad en constantes o propiedades fáciles de encontrar y ajustar.
- Compila el proyecto y corrige errores de compilación.
- Al terminar, resume: qué cambiaste, en qué archivos y dónde ajusto la sensibilidad.

## Restricciones

- Solo haz los cambios relacionados con esta tarea. No refactorices, renombres ni "mejores" otras partes de la app.
- No cambies la apariencia, animaciones ni otras funciones de la isla.
- **Detente y pregúntame antes de:** agregar cualquier paquete NuGet (incluido NAudio, si no está ya), borrar archivos, cambiar el target framework o la configuración del proyecto.
- Si un comando o compilación falla dos veces seguidas por la misma causa, detente y explícame el problema en vez de seguir intentando.
- Si algo del código real contradice este documento, dímelo en lugar de forzar mi suposición.

## Criterios de aceptación

- Un deslizamiento corto con dos dedos en el touchpad sobre la isla cambia el volumen solo unos pocos por ciento.
- Un deslizamiento largo recorre un rango amplio de forma continua, sin saltos.
- Un notch de rueda de mouse cambia el volumen en `PorcentajePorNotch` (2% por defecto).
- Cambiar el volumen con las teclas del teclado y luego hacer scroll parte del valor real, sin saltar.
- El volumen nunca sale de 0–100 y el proyecto compila sin errores ni advertencias nuevas.
