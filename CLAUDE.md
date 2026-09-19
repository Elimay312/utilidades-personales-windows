# Cómo se trabaja en este proyecto

Un lanzador para Windows 11: `Alt+Espacio`, escribes tres letras, abres lo que sea. .NET 10,
Win32 crudo y `Windows.UI.Composition`. Lee el [README](README.md) para saber qué hace y cómo
está montado, y [SEGURIDAD.md](SEGURIDAD.md) **antes de escribir código**.

---

## Lo innegociable

1. **`SEGURIDAD.md` manda.** Si algo necesita una API prohibida, o se rediseña o se enmienda
   el documento **por escrito y antes** de tocar código. No se piden excepciones de palabra.
2. **`pwsh -File auditar.ps1` antes de cada commit.** Tiene que decir `TODO LIMPIO`.
3. **Comentarios en español, código en inglés.**
4. **Solo lo que se pide.** Sin features extra, sin abstracciones especulativas, sin capas ni
   ficheros de más. No hay interfaces con una implementación ni fábricas de un producto.
5. **Sin dependencias nuevas sin preguntar.** Hoy hay una: `Microsoft.Windows.CsWin32`, que
   es un generador y no aparece en la salida. **Everything no es una dependencia**: es un
   programa que instala el usuario y al que se le pregunta por IPC si está.

### Parar y preguntar antes de

- Borrar cualquier fichero.
- Añadir una dependencia.
- Escribir en el registro fuera de `HKCU\...\Run`.
- Cualquier cosa que pida elevación.
- Tocar algo fuera de la carpeta del proyecto.
- Desviarse de lo acordado.

---

## Cómo se construye

**Un hito por vez, y cada uno compila y se ejecuta antes de empezar el siguiente.** Nada de
escribir seis funciones y depurar al final. Al terminar uno: commit, y reportar
`✅ [hito] — [ficheros]` con lo que se midió.

**Un commit por cosa.** El mensaje cuenta *qué se midió*, no qué se tocó — el diff ya dice
eso. Los mensajes de este repo son la documentación de por qué las cosas son como son;
`git log` es el sitio donde vive el historial de decisiones.

---

## Medir, no suponer

**La sonda se equivoca más que el código.** Es la lección que trajeron el dock y la isla, y en
este repo ya cobró en el primer hito: `auditar.ps1` daba `TODO LIMPIO` y parecía terminado,
pero contaba *líneas* en vez de *apariciones*, así que dos `SetForegroundWindow` en la misma
línea colaban. No lo encontró leerlo: lo encontró meter el fallo a propósito.

De ahí, las costumbres:

1. **Antes de creer que algo está roto, comprueba que la sonda mide lo que crees.**
2. **Cuando arregles algo, mete el fallo a propósito otra vez** y comprueba que la prueba lo
   detecta. Si no lo detecta, la prueba no vale.
3. **Prefiere señales de texto a píxeles.** Casi todo lo de este programa se puede observar
   desde la consola: `--indice` dice cuántas apps hay, `--buscar` dice qué puntúa cada
   resultado y por qué, `--check` dice si el algoritmo sigue dando lo mismo. Un diff de
   capturas es el último recurso, y aquí casi nunca hace falta.
4. **Las sondas van en el scratchpad, no en el repo.** Lo que sí vive en el repo son los modos
   de consola, para lo que es lógica pura.

Y al reportar: si algo no se pudo medir, se dice. Nada de dar por bueno lo que no se vio.

---

## Convenciones del código

- **Los comentarios explican *por qué*, no *qué*.** Lo que se midió, lo que se probó y no
  funcionó, la trampa que hay debajo. Si alguien va a repetir un error, el comentario está
  para evitarlo.
- **Los atajos deliberados se marcan** con un comentario `ponytail:` que dice cuál es el
  techo y cuándo tocaría subirlo.
- **`NativeMethods.txt` es la lista cerrada de P/Invokes.** Cada grupo va bajo un comentario
  que dice para qué es y de qué sección de `SEGURIDAD.md §3` sale: ahí es donde mira un
  auditor.
- **Finales de línea LF.** El repo guarda LF; escribir CRLF hace que git vea el fichero
  entero como cambiado.
- **Los modos de consola no piden el mutex.** `--check` y compañía no abren ventana ni
  registran el atajo, así que tienen que poder correr con el lanzador ya arrancado.

---

## Las tres cosas que explican el resto

1. **La ventana se crea al arrancar y se esconde.** Lo que se siente rápido es *mostrar*, no
   *construir*. Si alguna vez la ventana se crea al pulsar el atajo, el lanzador se sentirá
   lento por mucho que la búsqueda sea instantánea.
2. **El lanzador sí roba el foco, al revés que el dock.** Y para eso necesita
   `SetForegroundWindow` sobre su propio HWND justo tras `WM_HOTKEY`, que es cuando Windows
   lo autoriza. Es la única excepción de la regla 15 y `auditar.ps1` la vigila aparte.
3. **La caja de texto es un control `EDIT` del sistema, no un dibujo.** El caret, la
   selección, las teclas muertas y Ctrl+V los hace el control. Por eso la franja de arriba va
   en color sólido y el acrílico empieza debajo: un `EDIT` pinta su propio fondo.

---

## Comprobación antes de dar algo por terminado

```powershell
dotnet build                                                      # 0 errores, 0 advertencias
dotnet run -- --check                                             # algoritmo, decaimiento, calculadora
dotnet run -- --indice                                            # cuantas apps y en cuantos ms
dotnet run -- --buscar "br"                                       # que sale primero y por que
pwsh -File auditar.ps1                                            # TODO LIMPIO
dotnet publish -c Release -o "$env:LOCALAPPDATA\Lanzador\app"     # y usarlo de verdad
```
