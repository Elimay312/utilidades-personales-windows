# Cómo se trabaja en este proyecto

Renombrado masivo con vista previa para Windows 11: .NET 10, Win32 crudo y
`Windows.UI.Composition`. Lee el [README](README.md) para saber qué hace y cómo está montado,
y [SEGURIDAD.md](SEGURIDAD.md) **antes de escribir código**.

---

## Lo innegociable

1. **`SEGURIDAD.md` manda.** Si algo necesita una API prohibida, o se rediseña o se enmienda
   el documento **por escrito y antes** de tocar código. No se piden excepciones de palabra.
2. **`pwsh -File auditar.ps1` antes de cada commit.** Tiene que decir `TODO LIMPIO`.
3. **Todo en español: comentarios e identificadores.** Es lo que hacen la isla y el lanzador.
4. **Solo lo que se pide.** Sin features extra, sin abstracciones especulativas, sin capas ni
   ficheros de más. No hay interfaces con una implementación ni fábricas de un producto.
5. **Sin dependencias nuevas sin preguntar.** Hoy hay una: `Microsoft.Windows.CsWin32`, que
   es un generador y no aparece en la salida.

### Parar y preguntar antes de

- **Cualquier cosa que borre o sobrescriba un fichero.** Aquí eso no es una operación
  arriesgada: es una regla rota. Si parece que hace falta, el diseño está mal.
- Añadir una dependencia.
- Escribir en el registro, o cualquier cosa que pida elevación.
- Tocar algo fuera de la carpeta del proyecto.
- Probar sobre ficheros de verdad que no sean una copia.
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

**La sonda se equivoca más que el código.** Es la lección que trajeron el dock, la isla y el
lanzador, y aquí tiene una forma propia: **la vista previa es la sonda del programa y a la
vez su producto.** Si la previa miente, no falla una prueba: fallan los ficheros del usuario.

De ahí, las costumbres:

1. **Antes de creer que algo está roto, comprueba que la sonda mide lo que crees.**
2. **Cuando arregles algo, mete el fallo a propósito otra vez** y comprueba que `--check` lo
   detecta. Si no lo detecta, la prueba no vale. Esto se aplica sobre todo a los estados de
   `Previa`: una colisión que la tabla no marca es exactamente el fallo que este programa
   existe para evitar.
3. **Prefiere señales de texto a píxeles.** Todo lo que hace este programa se puede observar
   desde la consola: `--previa` imprime la misma tabla que la ventana, con los mismos
   estados. Un diff de capturas es el último recurso, y aquí casi nunca hace falta.
4. **Lo que se prueba sobre el disco se prueba sobre una copia**, y la prueba de fuego es la
   ida y vuelta: aplicar y deshacer tiene que dejar los nombres **exactamente** como estaban,
   incluidas mayúsculas y acentos.
5. **Las sondas van en el scratchpad, no en el repo.** Lo que sí vive en el repo son los modos
   de consola, para lo que es lógica pura.

Y al reportar: si algo no se pudo medir, se dice. Nada de dar por bueno lo que no se vio.

---

## Convenciones del código

- **Los comentarios explican *por qué*, no *qué*.** Lo que se midió, lo que se probó y no
  funcionó, la trampa que hay debajo.
- **Los atajos deliberados se marcan** con un comentario `ponytail:` que dice cuál es el
  techo y cuándo tocaría subirlo.
- **`NativeMethods.txt` es la lista cerrada de P/Invokes.** Cada grupo va bajo un comentario
  que dice para qué es y de qué sección de `SEGURIDAD.md §3` sale: ahí es donde mira un
  auditor.
- **Finales de línea LF.** El repo guarda LF; escribir CRLF hace que git vea el fichero
  entero como cambiado.
- **Los modos de consola no abren ventana.** `--check`, `--previa` y compañía tienen que
  poder correr con la ventana ya abierta.

---

## Las tres cosas que explican el resto

1. **Solo se mueve, nunca se borra ni se sobrescribe.** `File.Move` sin tercer argumento, y
   solo dentro de `Aplicar.cs`. De ahí sale casi todo lo demás: que haya estado `YaExiste`,
   que los ciclos `A→B, B→A` necesiten un temporal, y que deshacer siempre tenga algo que
   deshacer. `auditar.ps1` lo vigila y falla si `File.Move` aparece en otro fichero.
2. **`Regla.cs` y `Previa.cs` son lógica pura.** No tocan el disco: entran nombres, salen
   nombres. Es lo que hace que `--check` compruebe de verdad lo que se ejecuta, y lo que
   permite que la ventana recalcule la tabla en cada tecla sin consecuencias.
3. **Las cajas de texto son controles `EDIT` del sistema, no un dibujo.** El caret, la
   selección, las teclas muertas y Ctrl+V los hace el control — es la lección que el lanzador
   ya pagó. Y como un `EDIT` pinta su propio fondo, **la franja de arriba va en color sólido y
   el acrílico empieza debajo**.

---

## Comprobación antes de dar algo por terminado

```powershell
dotnet build                                                        # 0 errores, 0 advertencias
dotnet run -- --check                                               # motor, casos limite, ida y vuelta
dotnet run -- --previa "C:\copia\de\recibos"                        # la tabla, con sus estados
pwsh -File auditar.ps1                                              # TODO LIMPIO
dotnet publish -c Release -o "$env:LOCALAPPDATA\Renombrar\app"      # y usarlo de verdad
```
