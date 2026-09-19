# Superficie de riesgo — documento vinculante

Este documento gobierna todo el desarrollo del lanzador. Si una función futura necesita algo
prohibido, **la función se rediseña o se descarta**. No se piden excepciones de palabra: se
enmienda este documento por escrito, con su justificación, y queda en el historial de git.

El objetivo es concreto y medible: **que ningún antivirus tenga un motivo razonable para
marcar este binario**, y que alguien que lea el código entienda en diez minutos por qué cada
API que se llama está donde está.

> **Escrito el 19 de septiembre de 2026, antes de la primera línea de código.** Ese es el
> momento en que sirve para algo: decidir qué no vas a hacer mientras todavía no cuesta nada
> renunciar a ello. Se reescribirá entero al llegar al producto mínimo viable, cuando ya se
> sepa qué hacía falta de verdad y qué me inventé.

**Este documento es del lanzador y solo del lanzador.** El dock y la isla tienen los suyos,
con reglas distintas, porque hacen cosas distintas. Lo que comparten es el método, no la lista.

---

## 1. El criterio

Este proyecto hace las tres cosas que más mal suenan juntas: **recibe pulsaciones de teclado,
indexa tus ficheros y ejecuta programas.** Dicho así es la descripción de un troyano. Que no
lo sea depende de tres cortes, y son el resto de este documento.

### 1.1 Todo pasa delante de ti, ahora

| | Legítimo | Sospechoso |
|---|---|---|
| **Cuándo actúa** | Cuando pulsas el atajo, y mientras la ventana está delante | En segundo plano, en un temporizador, cuando no miras |
| **Qué lanza** | Una entrada que **estás viendo seleccionada** en la lista | Una cadena que se compone sola, o un comando arbitrario |
| **Qué queda** | Lo que elegiste lanzar | Todo lo que escribiste |

El dock se apoya en *"lo empieza el usuario con un gesto"*. La isla no podía, porque ahí no
hay gesto. **Aquí el gesto es todo**: no existe ningún camino en el que este programa abra
nada sin que tú hayas pulsado Enter sobre una fila visible. No hay temporizador que lance, no
hay acción automática, no hay "abrir el primer resultado" sin confirmación. Si alguna vez
aparece un camino así, este documento está roto.

### 1.2 El caso difícil: recibir teclas

Es el sitio donde este proyecto podría convertirse en algo que no quiero, así que la línea se
dibuja a mano.

**Lo que hace el lanzador:** su propia ventana, cuando **tiene el foco**, recibe `WM_CHAR` y
`WM_KEYDOWN`, igual que los recibe el cuadro de búsqueda de cualquier programa. Y
`RegisterHotKey` le pide a Windows que le mande `WM_HOTKEY` cuando se pulse **una**
combinación concreta.

**Lo que sería un keylogger:** `SetWindowsHookEx(WH_KEYBOARD_LL)`, que instala un callback que
ve **todas** las teclas de **todas** las aplicaciones; o `GetAsyncKeyState` en bucle, que
consulta el estado del teclado sin tener el foco. **Las dos están prohibidas** (reglas 2 y 3)
y están en `auditar.ps1`.

La diferencia no es de grado. Un `WM_CHAR` solo llega si la ventana está delante y tú estás
escribiendo en ella; un hook de bajo nivel llega siempre, mires donde mires. **El lanzador no
puede enterarse de nada que escribas fuera de su caja de texto**, y no es una promesa: es que
no existe la API para ello dentro de la lista cerrada de `NativeMethods.txt`.

Y el corolario, que es la regla 11: **lo que escribes no se guarda**. Solo se guarda lo que
lanzaste. Las consultas que no acabaron en Enter no dejan rastro en ningún sitio.

### 1.3 El caso difícil: indexar ficheros

El lanzador lee **nombres y rutas**. Nunca contenido.

Del menú Inicio lee tus propios `.lnk` y resuelve a qué apuntan. De Everything recibe rutas.
En ningún momento se abre un fichero para mirar dentro, ni se busca por contenido, ni se leen
metadatos de documentos. Un índice de nombres de fichero es lo que ya te enseña el Explorador
cuando escribes en su caja de búsqueda; el contenido es otra cosa y está prohibido (regla 12).

**Everything no es una dependencia nuestra ni un servicio que montemos**: es un programa que
tú instalas, que mantiene su índice porque tú se lo pediste, y al que le hacemos una pregunta
por IPC cuando tú escribes. Si no está, el lanzador enseña solo aplicaciones y lo dice. No se
instala, no se arranca, no se configura desde aquí.

---

## 2. Lo que este programa no va a hacer, nunca

| # | Prohibido | Por qué |
|---|---|---|
| 1 | Driver de kernel (`.sys`), servicio de Windows, tarea programada, o cualquier componente elevado | Es lo que hace que un antivirus marque a un programa de escritorio. Corre siempre como usuario normal, `requestedExecutionLevel` `asInvoker`. |
| 2 | `SetWindowsHookEx` y `SetWinEventHook` | **La regla que este proyecto necesitaba más afilada.** Un hook global ve las teclas de todas las apps, o carga una DLL nuestra dentro de otros procesos. *Ese* es el patrón de keylogger. Para el atajo se usa `RegisterHotKey`, que es otra cosa — ver §3.3. |
| 3 | Leer el teclado fuera de la ventana propia: `GetAsyncKeyState`, `GetKeyboardState`, `keybd_event`, `SendInput`, `GetRawInputData` | El lanzador lee lo que se escribe **en su caja de texto cuando está delante**, y nada más. No hay ninguna razón para consultar el estado del teclado sin foco, así que la prohibición es gratis. |
| 4 | `CreateRemoteThread`, `WriteProcessMemory`, `VirtualAllocEx`, o cualquier código dentro de otro proceso | Inyección. Bandera roja inmediata de EDR. |
| 5 | Reemplazo del shell (`Winlogon\Shell`), IFEO, `AppInit_DLLs`, parcheo de binarios | Persistencia de malware por definición. |
| 6 | Persistencia oculta | El autoarranque va en `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, sale en la pestaña Inicio del Administrador de tareas, y se pregunta antes de escribirlo. Nada de `schtasks`, servicios ni carpeta Startup. |
| 7 | **Cualquier** llamada de red: sin telemetría, sin updater, sin sugerencias de búsqueda, sin favicons descargados | 100% offline, verificable con `netstat` mientras corre. Los prefijos web **construyen una URL y se la entregan al navegador**; ahí acaba nuestro trabajo. El lanzador no abre ningún socket — ver §3.5. |
| 8 | Ofuscación, packers, compresión del ejecutable, cifrado de cadenas, single-file comprimido | Alta entropía y sin secciones legibles es indistinguible de un dropper. Se compila con PDB y secciones normales. |
| 9 | Descargar o generar código en runtime: `Assembly.Load(byte[])`, `Reflection.Emit`, plugins | Ejecutar código no firmado en runtime es comportamiento de loader. Y un lanzador con plugins es un lanzador que ejecuta el código de otro. |
| 10 | **Ejecutar una cadena arbitraria como comando.** Nada de `cmd /c`, `powershell -c`, ni un modo "ejecuta lo que escriba" | Se lanza **una entrada del índice**, que es un objeto con su ruta ya resuelta y que estás viendo en la lista. La diferencia entre eso y un intérprete de comandos es la diferencia entre un lanzador y una shell remota. Ver §4. |
| 11 | **Guardar lo que escribes.** Ni las consultas, ni las teclas, ni lo que se descartó | Lo que se guarda es **lo que lanzaste**: qué, cuántas veces y cuándo fue la última. Una consulta que no acabó en Enter no deja rastro. Ver §3.6. |
| 12 | **Leer el contenido de los ficheros.** Ni buscar dentro, ni leer metadatos de documentos, ni previsualizar | El índice es de **nombres y rutas**. Lo mismo que ya te enseña el Explorador. El contenido de tus documentos no es asunto de un lanzador. |
| 13 | Tocar el navegador: historial, marcadores, perfiles, cookies, credenciales, gestores de contraseñas | Muchos lanzadores indexan los marcadores. Este no. Es la carpeta más sensible del perfil y el beneficio no compensa ni de lejos. |
| 14 | Portapapeles: `OpenClipboard`, `GetClipboardData`, `SetClipboardData` | Ctrl+V funciona en la caja de texto porque **lo hace el control `EDIT` del sistema por dentro**, sin que nosotros llamemos a nada. Nuestro código nunca lee el portapapeles. Ver §3.2. |
| 15 | Tocar ventanas ajenas: enumerarlas, moverlas, cerrarlas, leer sus píxeles | `EnumWindows`, `PrintWindow`, `ShowWindowAsync`, `AttachThreadInput`. **`SetForegroundWindow` solo sobre nuestro propio HWND**, que es la única excepción y está en §3.4 con su comprobación propia en `auditar.ps1`. |
| 16 | Matar procesos: `TerminateProcess`, `TerminateThread`, `EndTask`, `ExitWindowsEx` | Pierde datos sin preguntar. El lanzador abre cosas; no cierra las de nadie. |
| 17 | **Elevar lo que se lanza.** Nada del verbo `runas` ni de pedir UAC al abrir algo | Si algo necesita administrador, lo pedirá él. Un lanzador que eleva por su cuenta es un lanzador que convierte un Enter tuyo en un consentimiento que no diste. |

---

## 3. Lo que sí se hace, y por qué se sostiene

### 3.1 Indexar las aplicaciones

Los dos menús Inicio (`%ProgramData%\Microsoft\Windows\Start Menu\Programs` y el equivalente
en `%AppData%`), leyendo los `.lnk` y resolviendo su destino con `IShellLink`. Y las apps de la
Store enumerando `shell:AppsFolder` con `IShellItem` / `BHID_EnumItems`.

**Por qué se sostiene:**

1. **Es exactamente lo que enumera el menú Inicio** para pintarse a sí mismo. Mismas carpetas,
   misma carpeta virtual, mismos datos.
2. **Son ficheros tuyos, en tu sesión**, y son accesos directos: su contenido *es* una ruta.
3. **Solo se lee el nombre y el destino.** No se abre el ejecutable, no se lee su versión, no
   se mira dentro de nada.

**Los cortes:** no se recorre el disco. No se busca fuera de esas dos carpetas y de
`AppsFolder`. El índice vive en memoria y **no se escribe a disco** — se reconstruye al
arrancar, que cuesta milisegundos.

### 3.2 La caja de texto

Un control `EDIT` hijo (`CreateWindowExW` con la clase `"EDIT"`), subclasado con
`SetWindowSubclass` solo para interceptar las flechas, `Esc` y `Enter` antes de que el control
se los coma.

**Por qué se sostiene:** es el mismo control que usa cualquier cuadro de diálogo de Windows.
El caret, la selección, las teclas muertas y **Ctrl+V los implementa el propio control dentro
del sistema**. Nuestro código recibe el texto con `WM_GETTEXT` cuando cambia; no toca el
portapapeles (regla 14) y no ve ninguna tecla que no le hayas escrito dentro.

**El corte:** la subclase actúa sobre **nuestro control hijo**, identificado por el HWND que
nos devolvió `CreateWindowExW`. `SetWindowSubclass` sobre una ventana de otro proceso no
funcionaría aunque quisiéramos —haría falta inyectar una DLL, que es la regla 4—, pero conviene
decir que tampoco se intenta.

### 3.3 El atajo global

`RegisterHotKey(hwnd, id, MOD_*, VK_*)` con lo que diga `lanzador.json`; por defecto
`Alt+Espacio`.

**No es un hook.** Registra una combinación concreta para **nuestra propia ventana**: el
sistema manda `WM_HOTKEY` a nuestro `WndProc` cuando se pulsa esa combinación y solo esa. No
carga nada en ningún proceso, no ve ninguna otra tecla, y no puede verlas. Es lo contrario de
`SetWindowsHookEx`, que está prohibido en la regla 2, y es como registra su atajo cualquier app.

Si `RegisterHotKey` falla porque otro programa ya tiene esa combinación, **se dice en la
consola**. Un atajo global que falla en silencio es media hora perdida. Ese fue el motivo de
que el atajo sea configurable: para poder salir del choque sin recompilar.

### 3.4 Ponerse delante — la única excepción de la regla 15

`SetForegroundWindow(nuestro_hwnd)` justo después de recibir `WM_HOTKEY`.

Hace falta: una ventana que se muestra desde un proceso que no está en primer plano aparece
**sin foco**, y entonces no recibe lo que escribes. Windows tiene una excepción documentada
para exactamente esto — el proceso que recibe un evento de hotkey queda autorizado a ponerse
delante.

**El corte, y es el que importa:** el argumento es **siempre nuestro propio HWND**. Nunca el de
otra ventana. `auditar.ps1` no se limita a buscar el nombre de la API: comprueba que aparece
solo en `LanzadorWindow.cs` y que lo que se le pasa es el handle propio. Un `grep` a secas aquí
no sirve de puerta, porque la API sí está permitida — lo que está prohibido es a quién se le
aplica.

Al ocultarse, el lanzador **no toca el foco de nadie**: esconde su ventana y Windows devuelve
el foco solo. No hay ningún `SetForegroundWindow` sobre una ventana ajena, ni siquiera para
"devolver" el foco.

### 3.5 Los prefijos web

`ShellExecuteExW` sobre una URL construida con lo que hay en `lanzador.json`, con el término
escapado.

**No hay red en este proceso** (regla 7). Se le entrega una URL al navegador por defecto,
que es lo mismo que hace un enlace en un correo. No se descarga nada, no se piden sugerencias
mientras escribes, no se buscan favicons. `auditar.ps1` comprueba que no aparece ninguna API
de red, y con el lanzador corriendo `Get-NetTCPConnection` filtrado por su PID sale vacío.

**El corte:** la plantilla sale del fichero de configuración, que escribes tú. El término se
escapa antes de sustituirlo, y **el resultado tiene que ser `http:` o `https:`** — si una
plantilla produce otro esquema, no se abre. Sin eso, una plantilla con `file:` o con un
esquema de aplicación convertiría el lanzador en un ejecutor de cualquier cosa, que es la
regla 10 por la puerta de atrás.

### 3.6 El ranking por uso — y por qué aquí sí hay historial

Esto es lo que la isla prohíbe en su regla 12, así que hay que ganárselo por escrito.

`uso.json` guarda dos cosas:

- **`lanzamientos`**: por cada cosa que has abierto desde aquí, cuántas veces y la fecha de la
  última. Sin eso no hay ranking por uso, que es la mitad del encargo.
- **`elecciones`**: qué elegiste la última vez para una consulta concreta. Es lo que hace que
  escribir `br` dé siempre lo mismo.

**Los cortes, que son lo que lo hace defendible:**

1. **Solo lo que lanzaste desde aquí.** No es un registro de lo que usas en el ordenador: es
   un registro de lo que abriste *con este programa*. Lo que abres desde el escritorio, la
   barra de tareas o el dock no aparece, porque no nos enteramos.
2. **Lo que no acabó en Enter no existe.** Las consultas que descartaste no se guardan (regla
   11).
3. **Una fecha por entrada, no una lista.** Se guarda *la última vez*, no un registro de todas
   las veces. De ahí no sale una línea temporal de tu día.
4. **Texto plano, legible y tuyo.** Está en `%LOCALAPPDATA%\Lanzador\uso.json`, se abre con el
   Bloc de notas, y `--olvidar` lo borra entero.
5. **No sale del proceso** (regla 7).

### 3.7 Consultar a Everything

`FindWindowW` sobre la clase `EVERYTHING_TASKBAR_NOTIFICATION` y `SendMessageW` con
`WM_COPYDATA` y la estructura de consulta que documenta el SDK de voidtools. Los resultados
vuelven como otro `WM_COPYDATA` a nuestra ventana.

**Por qué se sostiene:** es el canal que Everything publica **para esto**. Está documentado en
su SDK y es lo que usa su propia línea de comandos. No se toca su proceso, no se lee su
memoria, no se abre su base de datos en disco: se le manda una pregunta por un buzón que él
dejó puesto, y él contesta si quiere.

**Los cortes:** solo se consulta **cuando estás escribiendo**, nunca en segundo plano. Se piden
los primeros resultados, no el índice. Y lo que vuelve **son rutas**: no se abre ninguno de
esos ficheros (regla 12). Si `FindWindowW` devuelve cero, Everything no está: se enseñan solo
aplicaciones y se dice en la lista. No se intenta arrancarlo ni instalarlo.

`WM_COPYDATA` a otro proceso es el único mensaje que este programa manda fuera de sí mismo, y
va a un destinatario que existe únicamente para recibirlo.

### 3.8 Lanzar

`ShellExecuteExW` con el verbo por defecto, sobre la entrada seleccionada: una ruta de fichero,
una app de la Store por su AUMID, un `ms-settings:` o una URL `http(s)`.

**Es lo mismo que hace un doble clic.** Mismo API, mismo alcance, misma resolución de verbo
por parte del shell. Y siempre detrás de un Enter tuyo sobre una fila que estás viendo (§1.1).

**Los cortes:** nunca el verbo `runas` (regla 17). Nunca una cadena que hayas escrito tú tal
cual, solo una entrada del índice con su destino ya resuelto (regla 10). Y el proceso hijo se
lanza y se suelta: no se le espera, no se le vigila, no se le mata (regla 16).

### 3.9 Autoarranque

`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, y nada más. Sale en la pestaña Inicio
del Administrador de tareas, se puede quitar desde ahí, y **se pregunta antes de escribirlo**.

---

## 4. Descartado, y por qué

| Se quería | Por qué no está |
|---|---|
| Modo comando: escribir algo y que se ejecute tal cual | Regla 10. Es la diferencia entre un lanzador y una shell. Si alguna vez entra, entra con su enmienda y con la confirmación delante |
| Indexar marcadores del navegador | Regla 13. Es la carpeta más sensible del perfil, y ya hay prefijos web para lo mismo |
| Buscar dentro de los ficheros | Regla 12. Everything tampoco lo hace por defecto, y por la misma razón |
| Saltar a una ventana ya abierta en vez de abrir otra | Necesita `EnumWindows` + `SetForegroundWindow` sobre ventanas ajenas, que es la regla 15. Es la función que más me apetece y la que más caro sale: entraría con una enmienda propia, no de tapadillo |
| Sugerencias de búsqueda mientras escribes | Regla 7. Mandaría cada pulsación a un servidor. Es justo lo contrario de este documento |
| Plugins o extensiones | Regla 9. Sería ejecutar el código de otro dentro de nuestro proceso |
| Arrancar Everything si no está corriendo | No es asunto nuestro lanzar el programa de otro sin que lo pidas. Se dice que falta y ya |
| Un índice de ficheros propio, en disco | Para eso está Everything, que ya lo hace mejor. Y un índice propio en disco *sí* sería un inventario de tus ficheros guardado por nosotros |

---

## 5. Corolarios de diseño

Cosas que el código hace de una forma concreta **porque este documento existe**, no por gusto:

- **El índice de aplicaciones no se escribe a disco.** Se reconstruye al arrancar. Lo único que
  se guarda es `uso.json`, que es lo que abriste, no lo que tienes.
- **No hay ninguna ruta de código que lance algo sin una fila seleccionada.** Ni un
  temporizador, ni un "abre el primero automáticamente", ni un modo sin confirmación.
- **La consulta vive en el control `EDIT` y en una variable**, y las dos se vacían al ocultar la
  ventana. No hay una lista de consultas anteriores en ningún sitio.
- **`SetForegroundWindow` aparece exactamente una vez en el código**, en `LanzadorWindow.cs`, y
  con el handle propio. Si aparece una segunda, la auditoría falla.
- **Las plantillas de URL se validan antes de abrirse**, y solo pasan `http` y `https`.
- **`NativeMethods.txt` es la lista cerrada de P/Invokes.** Si no está ahí, no se genera y no
  compila. Cada grupo lleva encima un comentario que dice para qué es, y cada entrada tiene que
  poder señalarse a una sección de §3.

---

## 6. Cómo se audita

```powershell
pwsh -File auditar.ps1        # tiene que decir TODO LIMPIO y salir con 0
```

Mira **solo código**, saltando comentarios y bloques `/* */`. Hace falta: este documento
nombra todas las APIs prohibidas para explicar por qué lo están, y los fuentes llevan
comentarios del estilo *"aquí no hay ningún hook y no lo va a haber"*. Un `grep` a secas se
encuentra a sí mismo y la auditoría nunca sale limpia, con lo que deja de servir de puerta.

A mano, las que un script no puede comprobar:

```powershell
# Sin red: con el lanzador corriendo, no debe aparecer ninguna conexion suya.
Get-NetTCPConnection | Where-Object { $_.OwningProcess -eq (Get-Process Lanzador).Id }

# Lo que se guarda: abrelo y leelo. Tiene que haber solo lo que lanzaste.
notepad "$env:LOCALAPPDATA\Lanzador\uso.json"

# Y que --olvidar de verdad lo deja vacio.
# Y un escaneo de Defender sobre el binario publicado.
```

Y entrada por entrada: cada línea de `NativeMethods.txt` tiene que poder señalarse a una
sección de §3. Si una no se puede, o sobra la entrada o falta una sección.

---

## 7. Cómo se enmienda

1. Se edita **este fichero** antes de escribir el código, explicando qué se abre, **con qué
   cortes**, y por qué sigue siendo defendible.
2. Se añade la regla nueva a `auditar.ps1` si la enmienda cierra algo, o se relaja la
   existente si abre algo.
3. Va en su propio commit, **antes** del commit que usa la API.

Nunca al revés. Un documento que se actualiza después de escribir el código no es una regla,
es un parte de daños.
