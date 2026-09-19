# Cómo se trabaja en este proyecto

Vista previa con la barra espaciadora para Windows 11: .NET 10, Win32 crudo y
`Windows.UI.Composition`. Lee el [README](README.md) para saber qué hace, y
[SEGURIDAD.md](SEGURIDAD.md) **antes de escribir código**.

Mismas costumbres que [dock](../dock), con una diferencia que importa: aquí el hook de
teclado está permitido y acotado. Esa excepción se gana cada día siendo más estricto en
todo lo demás.

---

## Lo innegociable

1. **`SEGURIDAD.md` manda.** Si algo necesita una API prohibida, o se rediseña o se
   enmienda el documento **por escrito y antes** de tocar código.
2. **`pwsh -File auditar.ps1` antes de cada commit.** Tiene que decir `TODO LIMPIO`.
3. **`Hook.cs` se queda pequeño.** Es el fichero que mira un auditor primero. Si crece, la
   pregunta no es cómo escribirlo mejor: es qué función lo hizo crecer y si merece existir.
4. **Comentarios en español, código en inglés.**
5. **Solo lo que se pide.** Sin features extra, sin abstracciones especulativas, sin capas
   ni ficheros de más. No hay interfaces con una implementación ni fábricas de un producto.
6. **Sin dependencias nuevas sin preguntar.** Hoy hay una: `Microsoft.Windows.CsWin32`, que
   es un generador y no aparece en la salida. PDF, vídeo y audio ya salen de WinRT.

### Parar y preguntar antes de

- Borrar cualquier fichero.
- Añadir una dependencia.
- Escribir en el registro fuera de `HKCU\...\Run`.
- Abrir un archivo del usuario con permiso de escritura. (La respuesta es no. Ver §3.3.)
- Cualquier cosa que pida elevación.
- Tocar algo fuera de la carpeta del proyecto.
- Desviarse de lo acordado.

---

## Cómo se construye

**Un hito por vez, y cada uno compila y se ejecuta antes de empezar el siguiente.** Al
terminar uno: commit, y reportar `✅ [hito] — [ficheros]` con lo que se midió.

**Un commit por cosa.** El mensaje cuenta *qué se midió*, no qué se tocó — el diff ya dice
eso. `git log` es donde vive el historial de decisiones.

---

## Medir, no suponer

**Antes de creer que algo está roto, comprueba que la sonda mide lo que crees.** Y cuando
arregles algo, **mete el fallo a propósito otra vez** y comprueba que la prueba lo detecta.
Si no lo detecta, la prueba no vale.

Aquí eso se aplica sobre todo a una cosa: **el filtro del hook**. Una sonda que no detecta
que el espacio se está comiendo dentro de un campo de texto no vale para nada, porque ese
es el fallo que hace el programa inservible y es silencioso.

Las sondas van en el scratchpad, no en el repo. Lo que sí vive en el repo es `--check`,
para lo que es lógica pura.

---

## Convenciones del código

- **Los comentarios explican *por qué*, no *qué*.** Lo que se midió, lo que se probó y no
  funcionó, la trampa que hay debajo.
- **Los atajos deliberados se marcan** con un comentario `ponytail:` que dice cuál es el
  techo y cuándo tocaría subirlo.
- **`NativeMethods.txt` es la lista cerrada de P/Invokes.** Cada grupo va bajo un
  comentario que dice para qué es: ahí es donde mira un auditor.
- **Finales de línea LF.** El repo guarda LF; escribir CRLF hace que git vea el fichero
  entero como cambiado.

---

## Comprobación antes de dar algo por terminado

```powershell
dotnet build                                                      # 0 errores, 0 advertencias
dotnet run -- --check                                             # lógica pura
pwsh -File auditar.ps1                                            # TODO LIMPIO
dotnet publish -c Release -o "$env:LOCALAPPDATA\QuickLook\app"    # y arrancarlo de verdad
```

Y las tres sondas manuales que deciden si esto se puede usar a diario:

1. Escribir un párrafo en Word o Chrome con el Explorador detrás: **llegan todos los
   espacios**.
2. `F2` sobre un archivo y escribir `mi archivo nuevo`: **los espacios entran en el
   nombre**.
3. Caja de búsqueda del Explorador, escribir `dos palabras`: **igual**.
