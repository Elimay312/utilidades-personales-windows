# Cambios

El proyecto va por fases, no por versiones: cada una tiene que compilar sin warnings y
pasar sus pruebas antes de empezar la siguiente.

Los números que aparecen aquí están medidos, no estimados. Cuando algo no se pudo medir,
lo dice.

---

## Sin publicar

### Fase 1 — Ventana Mac

La base del proyecto y una ventana que ya se siente como una aplicación de Mac. Sin datos.

**Lo que hay**

- **Compilación**: C++20 con MSVC, CRT estático, `/W4 /permissive- /utf-8 /EHsc
  /await:strict` por objetivo. Cuatro objetivos: `brujula_core` (lo puro), `sqlite3`,
  `brujula.exe` y `brujula_tests.exe`. nlohmann/json, SQLite y doctest por `FetchContent`,
  con la versión fijada y, en SQLite, el SHA-256 del archivo.
- **`preparar.ps1`**: se busca MSVC, CMake y Ninja por su cuenta. Hacía falta: en esta
  máquina ninguno de los tres está en el PATH.
- **Ventana**: Mica, esquinas redondeadas del sistema, barra de título integrada en el
  contenido, arrastrable, doble clic para maximizar, botones dibujados por nosotros con el
  comportamiento nativo y el menú de ajuste de Windows 11 al pasar sobre maximizar.
- **`compositor/`**: escena, dispositivo compartido D3D11 → D2D → Composition, superficies
  reutilizables, los cuatro muelles de `CLAUDE.md` y la transición compartida.
- **Tema** claro/oscuro siguiendo a Windows, con cruce de 250 ms, y respeto a «Mostrar
  animaciones en Windows».
- **Demo temporal**: barra lateral translúcida y una tarjeta que se convierte en panel y
  vuelve, interrumpible a mitad.

**Medido**

| | Objetivo | Medido |
|---|---|---|
| Warnings con `/W4 /permissive-` | 0 | 0 |
| Pruebas | pasan | 21 casos, 115 aserciones |
| Destellos al abrir | ninguno | 295 muestras en 3 s, luminancia máxima 30 |
| Destellos al redimensionar | ninguno | 60 redimensionados, máxima 32 en el borde recién descubierto |
| Auditoría de seguridad | sale 0 | sale 0, con 1 regla marcada pendiente |

- **La Mica es de DWM y está medida**, no dada por buena: el cuerpo da `(32,32,32)` con
  Windows en oscuro y `(241,244,244)` en claro. Ninguno de los dos es un color nuestro —el
  respaldo opaco es `#2c2c2e`— y cambian solos al cambiar el tema del sistema.
- **La barra lateral y la tarjeta salen donde dice la aritmética de los tokens**: velo
  sobre Mica `(26,26,26)`, tarjeta `rgba(44,44,46,0.72)` sobre Mica `(41,41,42)`. Los tres
  valores son distintos, que es lo que se quería comprobar.
- **Los cuatro muelles asientan en 85, 165, 239 y 297 ms** (4·T/(2π·ζ)). `Period` es el
  periodo no amortiguado, no la duración; los vecinos usan 40-50 ms, bastante más seco.
  Queda anotado para volver a afinarlo en la fase 8.
- **El modo sin animaciones está medido en las dos direcciones**: con animaciones, a los
  100 ms del clic el panel todavía va por el camino (el punto de destino da Mica); sin
  ellas, a los 100 ms ya está puesto. Mismo clic, misma espera.
- **La auditoría también está medida en las dos direcciones**: una sonda con 6 violaciones
  plantadas las detecta las 6; las **mismas palabras** dentro de comentarios no disparan
  ninguna regla.

**Lo que se probó y no valía**

- **`DwmExtendFrameIntoClientArea` no basta para que se vea la Mica.** Con el marco
  extendido un píxel, el cuerpo salía `(255,255,255)` en blanco puro. Con el marco
  extendido entero (`-1`), también: ese truco es de la época de Aero y necesita que el
  cliente se pinte de negro para que DWM lo tome por cristal. Lo que hacía falta era
  **`WS_EX_NOREDIRECTIONBITMAP`**: sin superficie de redirección no hay nada blanco que
  tape la Mica.
- **Y el marco extendido entero tenía un segundo problema, peor**: con `-1`, DWM considera
  que toda la ventana es marco y dibuja **encima sus propios botones** de ventana. Se veían
  los seis a la vez, desplazados cinco píxeles porque los suyos van en una franja de 32 y
  los nuestros en una de 48.
- **`UIColorType::Background` no sirve para saber el tema** en una aplicación de
  escritorio: devuelve negro siempre, con Windows en claro y en oscuro. El tema salía
  «oscuro» en los dos casos, y como la máquina estaba en oscuro parecía que funcionaba. Se
  deduce del **texto** (`UIColorType::Foreground`): texto claro significa fondo oscuro.
- **`CreateHostBackdropBrush` no se ha usado**, y no por pereza: la isla ya midió que en
  una aplicación Win32 sin empaquetar se crea sin error y se pinta negro. La barra lateral
  es un velo de color sobre la Mica.
- **Las formas de Composition no se rellenan con una superficie**, solo con color y
  degradados. Por eso el material de la tarjeta (un `ShapeVisual` con geometría de
  rectángulo redondeado) va separado de su contenido (dos `SpriteVisual` con textura). De
  paso sale mejor: las esquinas las redondea el rasterizador de formas, con suavizado, en
  vez de un recorte geométrico, que tiene el borde duro.
- **El auditor daba TODO LIMPIO con una dirección prohibida delante.** Su limpiador de
  comentarios se comía la barra doble de las URL y con ella el resto de la línea, así que
  las dos reglas que vigilan con quién se habla no podían saltar nunca. Lo encontró la
  sonda, no la lectura del código.
- **El doble clic se lo tragaba `DefWindowProc`.** La clase lleva `CS_DBLCLKS` para que el
  doble clic en la barra de título maximice, y eso hace que el segundo clic rápido en el
  contenido llegue como `WM_LBUTTONDBLCLK` y no como `WM_LBUTTONDOWN`.

**Lo que queda sin comprobar**

- **Nitidez a otras escalas y al mover la ventana entre monitores.** Esta máquina tiene una
  sola pantalla a 96 ppp (100 %), así que `WM_DPICHANGED` no llega a dispararse nunca. La
  aritmética de DIP a píxeles sí está probada (100 %, 125 %, 150 % y 175 %), pero eso
  prueba las cuentas, no la pantalla. Hay que mirarlo en un equipo con dos escalas.
- **Sombras suaves en elementos flotantes.** No se han intentado. La isla dejó documentados
  dos caminos que no funcionaron y uno sin probar (`DropShadow` con `Mask`); cuando toque,
  se empieza por ahí.
- **Barra de tareas oculta automáticamente** con la ventana maximizada: el marco propio no
  la compensa todavía, así que no se puede sacar acercando el ratón al borde. Es un clásico
  de las barras de título propias y va a la fase 8.
