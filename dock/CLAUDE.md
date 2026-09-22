# Cómo se trabaja en este proyecto

Un dock estilo macOS para Windows 11: .NET 10, Win32 crudo y `Windows.UI.Composition`.
Lee el [README](README.md) para saber qué hace y cómo está montado, y
[SEGURIDAD.md](SEGURIDAD.md) **antes de escribir código**.

---

## Lo innegociable

1. **`SEGURIDAD.md` manda.** Si algo necesita una API prohibida, o se rediseña o se enmienda
   el documento **por escrito y antes** de tocar código. No se piden excepciones de palabra.
2. **`pwsh -File auditar.ps1` antes de cada commit.** Tiene que decir `TODO LIMPIO`.
3. **Comentarios en español, código en inglés.**
4. **Solo lo que se pide.** Sin features extra, sin abstracciones especulativas, sin capas ni
   ficheros de más. No hay interfaces con una implementación ni fábricas de un producto.
5. **Sin dependencias nuevas sin preguntar.** Hoy hay una: `Microsoft.Windows.CsWin32`, que
   es un generador y no aparece en la salida.

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

**Y el cambio se apunta en `CHANGELOG.md`, siempre.** Va en `## Sin publicar`, bajo
Añadido, Cambiado o Arreglado, con el número medido igual que las entradas de al lado —el
estilo del fichero es «de X a Y», no adjetivos—. El commit es para quien viene a leer el
porqué; el CHANGELOG, para quien solo quiere saber qué ha cambiado. Si un commit se fue
sin entrada, se recupera en el siguiente.

---

## Medir, no suponer

Esta es la regla que más veces ha salvado el proyecto, y la que más veces se ha roto.

**En este proyecto el método de prueba se equivocó más veces que el código.** Casos reales:

- Un clic sintético en el borde izquierdo de la barra parecía demostrar que los clics habían
  dejado de llegar. La coordenada estaba justo en el borde, y al poner el cursor ahí el
  layout se desplazaba lo suficiente para excluirla.
- Un diff de píxeles que medía la etiqueta dio 78 px en vez de 35 porque había un vídeo
  reproduciéndose detrás.
- `WindowFromPoint` ignora `HTTRANSPARENT`, así que decía que el dock recogía clics que en
  realidad dejaba pasar.
- La cola del log se pierde al matar el proceso con `Stop-Process -Force`: el buffer de
  stdout no se vacía y las últimas líneas nunca llegan al fichero.

De ahí, cuatro costumbres:

1. **Antes de creer que algo está roto, comprueba que la sonda mide lo que crees.**
2. **Cuando arregles algo, mete el fallo a propósito otra vez** y comprueba que la prueba lo
   detecta. Si no lo detecta, la prueba no vale. Esto encontró el fallo de la lupa y el de
   las etiquetas.
3. **Prefiere señales de texto a píxeles.** Casi todo lo del dock se puede observar con una
   traza detrás de `DOCK_HOVER_LOG`, con `GetWindowRect`, o preguntándole al propio dock por
   `WM_NCHITTEST` desde fuera. Un diff de capturas es el último recurso.
4. **Las sondas van en el scratchpad, no en el repo.** Lo que sí vive en el repo es
   `--check`, para lo que es lógica pura.

Y al reportar: si algo no se pudo medir, se dice. Nada de dar por bueno lo que no se vio.

---

## Convenciones del código

- **Los comentarios explican *por qué*, no *qué*.** Lo que se midió, lo que se probó y no
  funcionó, la trampa que hay debajo. Si alguien va a repetir un error, el comentario está
  para evitarlo.
- **Los atajos deliberados se marcan** con un comentario `ponytail:` que dice cuál es el
  techo y cuándo tocaría subirlo.
- **`NativeMethods.txt` es la lista cerrada de P/Invokes.** Cada grupo va bajo un comentario
  que dice para qué es: ahí es donde mira un auditor.
- **Finales de línea LF.** El repo guarda LF; escribir CRLF hace que git vea el fichero
  entero como cambiado.
- **El fichero grande es `DockWindow.cs`** (~2600 líneas) y está bien así: es una ventana con
  su `WndProc`, y partirla por partirla solo añadiría saltos.

---

## Las tres cosas que explican el resto

Si vas a tocar el dibujado o la interacción, estas tres deciden casi todo:

1. **La animación no corre en nuestro hilo.** Todo es `ExpressionAnimation` sobre un
   `CompositionPropertySet`: el hilo de UI solo escribe la posición del ratón. Ojo, **las
   expresiones tienen un límite de longitud** que se alcanza antes de lo que parece.
2. **El dock nunca roba el foco.** `WS_EX_NOACTIVATE` no basta: hay que responder
   `MA_NOACTIVATE` a `WM_MOUSEACTIVATE`. De ahí sale que el menú esté dibujado a mano.
3. **La región decide qué es del dock.** `HTTRANSPARENT` **no** atraviesa procesos; lo único
   que deja pasar el ratón es `SetWindowRgn`. Y la región también recorta el dibujo, así que
   tiene que cubrir todo lo que se pinte.

---

## Comprobación antes de dar algo por terminado

```powershell
dotnet build                                                    # 0 errores, 0 advertencias
dotnet run -- --check                                           # las curvas y los iconos
pwsh -File auditar.ps1                                          # TODO LIMPIO
dotnet publish -c Release -o "$env:LOCALAPPDATA\Dock\app"       # y arrancarlo de verdad
```

Y para un cambio que toque ventanas, pantalla completa o la barra de tareas, medirlo con las
tres pantallas puestas: casi todos los fallos de la fase 4 solo aparecían con más de una.
