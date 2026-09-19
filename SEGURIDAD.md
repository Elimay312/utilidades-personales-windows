# Superficie de riesgo — documento vinculante

Este documento gobierna todo el desarrollo de `renombrar`. Si una función futura necesita
algo prohibido, **la función se rediseña o se descarta**. No se piden excepciones de palabra:
se enmienda este documento por escrito, con su justificación, y queda en el historial de git.

El objetivo es concreto y medible: **que ningún fichero del usuario se pierda, se pise o se
lea**, y que alguien que lea el código entienda en diez minutos por qué cada API que se llama
está donde está.

> **Escrito el 19 de septiembre de 2026, antes de la primera línea de código.** Ese es el
> momento en que sirve para algo: decidir qué no vas a hacer mientras todavía no cuesta nada
> renunciar a ello. Se reescribirá entero al llegar al producto mínimo viable, cuando ya se
> sepa qué hacía falta de verdad y qué me inventé.

**Este documento es de `renombrar` y solo de `renombrar`.** El dock, la isla y el lanzador
tienen los suyos, con reglas distintas, porque hacen cosas distintas. Lo que comparten es el
método, no la lista.

---

## 1. El criterio

Los vecinos de esta carpeta tocan ventanas, teclas y procesos. **Este toca el disco del
usuario, y es el único que puede destruir algo que no se pueda recuperar.** Un dock que se
cuelga se reinicia; un renombrado masivo mal hecho sobre la carpeta de recibos de la oficina
no se reinicia.

Así que el criterio no es el de los otros. Aquí son tres cortes.

### 1.1 Mover no es borrar, y aquí solo se mueve

**No existe ninguna llamada de borrado en este programa, ni la va a haber.** Ni
`File.Delete`, ni `Directory.Delete`, ni papelera, ni `IFileOperation`. La única operación
sobre el disco es `File.Move` de un nombre a otro nombre de la misma carpeta.

Esto no es una promesa de comportamiento: es que **la API no está**, y `auditar.ps1` falla si
aparece. La consecuencia es la que importa: por mal que salga un lote, los ficheros siguen
existiendo, con otro nombre, en la misma carpeta. Siempre hay algo que deshacer.

Y por el mismo motivo **nunca se mueve encima de algo que ya existe**: `File.Move` se llama
sin `overwrite`, y la vista previa marca el destino ocupado *antes* de dejarte aplicar. Un
`overwrite: true` en este programa es la única forma de perder un fichero, así que está
prohibido por escrito y auditado (regla 3).

### 1.2 Se ven nombres, nunca contenido

Lo que este programa necesita de un fichero es **su nombre, su extensión y sus fechas**. Nada
más. No abre ni un solo fichero del usuario: no hay `FileStream`, no hay `ReadAllBytes`, no
hay `StreamReader` sobre nada que no sean sus propios `renombrar.json` y `ultimo-lote.json`.

Es la línea que separa "una utilidad de escritorio" de "un programa que lee tus recibos". Y
es la que hace que la versión con OCR y lectura de PDF **no se haya hecho todavía**: entraría
justo por aquí, y cuando entre tendrá que enmendar esta sección explicando qué abre, cuándo y
qué hace con lo que lee.

### 1.3 Solo lo que sueltas, y solo después de verlo

| | Legítimo | Sospechoso |
|---|---|---|
| **Sobre qué actúa** | Los ficheros que soltaste con el ratón | Lo que el programa encuentre por su cuenta |
| **Hasta dónde** | La carpeta soltada, un nivel | Recursivo, subcarpetas, otras unidades |
| **Cuándo** | Cuando pulsas Aplicar, con la tabla delante | En un temporizador, al arrancar, en segundo plano |
| **Qué queda** | Un diario del último lote, para deshacerlo | Un índice de lo que tienes |

**No hay ningún camino en el que este programa renombre nada sin que hayas visto antes la
fila exacta que va a cambiar.** No hay acción automática, no hay carpeta vigilada, no hay
"aplicar al soltar". Si alguna vez aparece un camino así, este documento está roto.

Y el programa **no recorre el disco**: enumera los ficheros de las rutas que le soltaste, sin
bajar a subcarpetas. No hay `AllDirectories`, no hay `GetLogicalDrives`, no hay índice.

---

## 2. Lo que este programa no va a hacer, nunca

El número es el artículo. `auditar.ps1` comprueba todos los que se pueden comprobar con un
patrón sobre el código.

| # | Prohibido | Por qué |
|---|---|---|
| 1 | Driver, servicio, tarea programada, elevación | Corre como tú y solo puede lo que tú puedas |
| 2 | **Borrar cualquier cosa**, incluida la papelera | §1.1. Es la regla que hace que todo sea recuperable |
| 3 | **Sobrescribir**: `overwrite`, copiar encima, truncar | §1.1. Es la única forma de perder un fichero |
| 4 | Abrir el contenido de un fichero del usuario | §1.2. Aquí solo se ven nombres |
| 5 | Recorrer el disco: recursión, unidades, índice | §1.3. Solo lo que sueltas, un nivel |
| 6 | Renombrar fuera de las rutas soltadas | El destino vive en la misma carpeta que el origen. Siempre |
| 7 | Tocar rutas del sistema | `%WINDIR%`, `Program Files`, la raíz de una unidad: se rechazan aunque las sueltes |
| 8 | Red, de cualquier forma | Nada de lo que hace necesita salir de esta máquina |
| 9 | Portapapeles | Pegar en una caja de texto lo hace el control `EDIT` del sistema, sin API |
| 10 | Hooks globales y leer el teclado sin foco | Las teclas llegan a la ventana propia o no llegan |
| 11 | Ejecutar nada: procesos, shell, `ShellExecute` | Un renombrador no lanza programas. No hay excepción |
| 12 | Ofuscación, packers, compresión, código en runtime | El binario tiene que ser legible para un analizador estático |
| 13 | Registro, autoarranque, persistencia | No se arranca solo. Se abre cuando hace falta y se cierra |
| 14 | Tocar ventanas o procesos ajenos | La única ventana que toca es la suya |
| 15 | Cambiar atributos, permisos o ACLs | Se cambia el nombre. Nada más del fichero cambia |

---

## 3. Lo que sí se hace, y por qué se sostiene

### 3.1 Recibir lo que sueltas (`IDropTarget`)

La única entrada. Es **puramente receptivo**: solo ve lo que el usuario suelta sobre esta
ventana, con su gesto, exactamente igual que el dock (que apoya en esto su enmienda 2). No es
el portapapeles, que sigue prohibido por la regla 9.

De cada ruta soltada se saca: nombre, extensión, fecha de creación y fecha de modificación.
Si es una carpeta, sus ficheros directos. **Sin bajar un nivel más** (regla 5).

Antes de aceptar nada se comprueba la carpeta contra la regla 7: una lista de rutas de
sistema que se rechazan aunque las sueltes a propósito, porque nadie renombra
`C:\Windows\System32` por gusto y quien lo intente se ha equivocado de ventana.

### 3.2 La vista previa

Es la razón de ser del programa, así que también es una medida de seguridad y no solo una
comodidad. La tabla tiene que marcar, antes de que el botón Aplicar se pueda pulsar:

- dos ficheros que acabarían con el mismo nombre (**colisión**),
- un destino que ya existe en la carpeta (**ya existe**),
- un nombre que Windows no acepta (**inválido**): `\ / : * ? " < > |`, vacío, punto o espacio
  final, o un nombre reservado (`CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9`, `LPT1`–`LPT9`,
  también con extensión: `CON.txt` está igual de prohibido),
- una ruta que se pasa de 260 caracteres cuando la original no lo hacía (**largo**).

Y las filas van **en el orden del Explorador**, no en el alfabético: `StrCmpLogicalW`, que es
una comparación de cadenas de `shlwapi` y no toca el disco. No es cosmético — ese orden es el
que la ficha `{n}` usa para repartir la numeración, y renumerar cien facturas en orden
alfabético las deja barajadas sin que la tabla parezca decir nada raro.

**Un estado que la previa no marque es exactamente el fallo que este programa existe para
evitar.** Por eso van en `--check` con su caso, y por eso la costumbre de la casa —meter el
fallo a propósito y comprobar que la prueba lo detecta— se aplica aquí antes que en ningún
otro sitio.

### 3.3 Aplicar

`File.Move(origen, destino)`, sin tercer argumento, dentro de la misma carpeta. Nada más.

Dos pasadas con nombres temporales **solo** cuando un destino coincide con un origen del
mismo lote (el caso `A→B, B→A`, y el de cambiar solo mayúsculas, que es el mismo caso). El
temporal se llama `<nombre>.renombrar-tmp` y vive lo que dura el lote.

Si una fila falla a mitad, se para, **no se revierte a ciegas** y se escribe el diario con lo
que sí se hizo. Deshacer es una decisión del usuario con la lista delante, no una reacción
automática.

### 3.4 El diario, y por qué aquí sí se guarda algo

`%LOCALAPPDATA%\Renombrar\ultimo-lote.json`: fecha, carpeta y la lista de pares
`viejo → nuevo` del último lote. Un fichero, sobrescrito cada vez.

Es lo único que este programa recuerda de ti, y guarda **nombres de ficheros que acabas de
ver en pantalla**, no un historial de lo que tienes. Existe por una razón concreta: sin él,
"deshacer" sería reescribir a mano lo que la máquina acaba de hacer. Se puede borrar a mano
en cualquier momento y el programa sigue funcionando; simplemente no tendrá nada que deshacer.

### 3.5 Las reglas y los presets

`renombrar.json`, junto al ejecutable: cadenas de reglas con nombre. Son **datos**, no código:
un enum, unas cadenas y unos números que alimentan un `switch`. No se evalúa nada, no se
compila nada, no se lanza nada (reglas 11 y 12).

La única regla que interpreta texto del usuario es la de expresiones regulares, y lo hace con
`System.Text.RegularExpressions` **con timeout**: una expresión mal escrita tiene que fallar,
no colgar la ventana mientras piensa.

---

## 4. Descartado, y por qué

- **Menú contextual del Explorador.** Toca el registro (regla 13) y obliga a instalar y
  desinstalar. La ventana con arrastrar y soltar hace lo mismo sin dejar rastro en el sistema.
- **Leer el contenido del PDF y OCR.** Entraría por §1.2, que es la línea más cara de este
  documento. Cuando haga falta de verdad se enmienda §1.2 por escrito, se dice qué se abre y
  qué se hace con lo que se lee, y solo entonces se escribe el código.
- **Papelera en vez de borrar.** No hace falta discutirla: aquí no se borra (regla 2).
- **Vigilar una carpeta y renombrar solo.** Rompe §1.3 entero. Es el camino más corto para
  convertir esto en algo que actúa cuando no miras.
- **Deshacer automático si algo falla.** Suena a red de seguridad y es lo contrario: revertir
  a ciegas sobre un disco que ya cambió puede pisar algo. Se para, se dice qué pasó, y decide
  el usuario.

---

## 5. Corolarios de diseño

1. **La previa y el motor son lógica pura.** `Regla.cs` y `Previa.cs` no escriben en el
   disco: se les da una lista de nombres y devuelven una lista de nombres. Eso es lo que hace
   que `--check` pueda comprobarlos de verdad, sin tocar nada.
2. **El único fichero que mueve ficheros es `Aplicar.cs`.** Si `File.Move` aparece en otro
   sitio, el diseño se ha escapado. Lo vigila `auditar.ps1`.
3. **La ventana sí roba el foco**, al revés que el dock: se escribe en ella. Pero no necesita
   `SetForegroundWindow` para eso — es una ventana normal y Windows se la da al abrirla.
4. **La carpeta temporal de `--check` va en `%TEMP%` y no se borra**, porque aquí no se borra
   nada (regla 2). Se llama `renombrar-check`, se reutiliza en cada pasada, y limpiarla es
   cosa de Windows.

---

## 6. Cómo se audita

```powershell
pwsh -File auditar.ps1        # tiene que decir TODO LIMPIO
dotnet run -- --check         # la previa detecta los cuatro estados malos
```

Y a mano, lo que un patrón no puede ver:

```powershell
# Sin red: con el programa abierto no debe aparecer ninguna conexion suya.
Get-NetTCPConnection | Where-Object OwningProcess -eq (Get-Process Renombrar).Id

# Lo que se guarda: abrelo y leelo. Solo el ultimo lote, y solo nombres.
Get-Content "$env:LOCALAPPDATA\Renombrar\ultimo-lote.json"

# La prueba de fuego: un lote de ida y vuelta sobre una copia tiene que dejar los
# nombres EXACTAMENTE como estaban, incluidas mayusculas y acentos.
```

---

## 7. Cómo se enmienda

Se edita este fichero, se dice **qué se permite ahora, por qué, y qué corte lo sustituye**, y
se actualiza `auditar.ps1` en el mismo commit. Una enmienda que no toca el auditor no es una
enmienda: es una intención.
