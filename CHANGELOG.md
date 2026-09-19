# Historial

Por hitos, con lo que se midió. Los descartes también se anotan: la idea que no se escribe
vuelve sola al cabo de un mes.

---

## M0 — Andamio y SEGURIDAD.md

El documento vinculante se escribió **antes** que el código, que es lo que manda el §7 del
documento del dock.

**La decisión del hito:** abrir `WH_KEYBOARD_LL`, que en el dock está prohibido por la
regla 3. Se evaluaron las tres alternativas antes de decidirlo:

- `RegisterHotKey(VK_SPACE)` sin modificador reserva la barra espaciadora en todo el
  sistema mientras el programa vive. Escribir deja de funcionar en todas partes.
- Registrarla y liberarla según quién esté en primer plano obliga a un temporizador que
  sondea, y **sigue** comiéndose el espacio al renombrar un archivo con F2 o al escribir en
  la caja de búsqueda del propio Explorador. El problema que se quería evitar no se evita.
- Un modificador (`Ctrl+Alt+Espacio`) no es Quick Look. Lo que se extraña de macOS es el
  espacio pelado.

Así que el hook, con seis cortafuegos en el código y su comprobación en `auditar.ps1`. Y la
defensa no descansa en que el hook sea pequeño, sino en lo que el §2 cierra alrededor: sin
red, sin escribir nada del teclado en disco, sin sintetizar entrada, sin ofuscar el
binario.

**`auditar.ps1` cambia de forma respecto al del dock.** Allí basta con buscar
`WH_KEYBOARD_LL` y fallar si aparece. Aquí está permitido, así que el script no comprueba
que *no* esté: comprueba que esté **donde tiene que estar** y que no haga más de lo que
dice el §3.1.

- `WH_KEYBOARD_LL` y `SetWindowsHookEx` solo pueden aparecer en `Hook.cs`.
- Dentro de `Hook.cs`, la única constante `VK_*` permitida además de `VK_SPACE` son las de
  modificador — y están abiertas solo para poder **dejar pasar** `Ctrl+Espacio` y
  compañeros.
- `UnhookWindowsHookEx` tiene que existir.
- Se vigila que `Hook.cs` no engorde: el §5 dice que se queda pequeño, y el script avisa
  pasadas las 90 líneas de código.

Reglas nuevas que el dock no tiene: **12 — no sintetizar entrada** (`SendInput`,
`keybd_event`), que es la otra mitad del perfil de un troyano y aquí había que cerrarla
explícitamente; y **A — no escribir archivos del usuario**, porque un previsualizador que
puede escribir no es un previsualizador.

**Descartado y anotado:** la extensión de shell (preview handler propio). Funcionaría, pero
es una DLL nuestra dentro de `explorer.exe`: un fallo nuestro tira el Explorador del
usuario, y es exactamente lo que la regla 4 existe para evitar. La ventana propia por
encima es más fea de montar y mucho más fácil de defender.

**Medido:** `dotnet build` en 3,9 s, 0 errores y 0 advertencias. `auditar.ps1` sale `TODO
LIMPIO` con 16 reglas comprobadas y 10 entradas en `NativeMethods.txt`. El programa arranca
y se queda en el bucle de mensajes sin hacer nada, que es todo lo que se le pide a este
hito.

**Ficheros:** `QuickLook.csproj`, `app.manifest`, `NativeMethods.txt`, `SEGURIDAD.md`,
`auditar.ps1`, `README.md`, `CLAUDE.md`, `CHANGELOG.md`, `quicklook.json`, `.gitignore`,
`Program.cs`, `HostWindow.cs`.
