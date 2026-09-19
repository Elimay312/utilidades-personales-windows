# Cómo se trabaja en este proyecto

El aviso de volumen y brillo de Windows, rehecho: .NET 10, Win32 crudo y
`Windows.UI.Composition`. Lee el [README](README.md) para saber qué hace, y
[SEGURIDAD.md](SEGURIDAD.md) **antes de escribir código** — aquí más que en los vecinos,
porque este proyecto tiene una excepción que ellos no tienen.

---

## Lo innegociable

1. **`SEGURIDAD.md` manda.** Si algo necesita una API prohibida, o se rediseña o se enmienda
   el documento **por escrito y antes** de tocar código. No se piden excepciones de palabra.
   Hubo una excepción abierta durante unas horas, con todo el papeleo hecho, y **al medirla
   resultó que sobraba** (§1). Escribirla obligó a medirla y medirla ahorró un fichero. Esa es
   la proporción que cuesta abrir la siguiente.
2. **`powershell -File auditar.ps1` antes de cada commit.** Tiene que decir `TODO LIMPIO`.
3. **Comentarios en español, código en inglés.** Los tipos de dominio van en español, como en
   la isla: `Volumen`, `Brillo`, `Glifos`, `FlyoutNativo`.
4. **Solo lo que se pide.** Sin features extra, sin abstracciones especulativas, sin capas ni
   ficheros de más. No hay interfaces con una implementación ni fábricas de un producto.
5. **Dos dependencias, y la segunda costó preguntarlo.** `CsWin32` y, desde H3,
   `System.Management` para `WmiMonitorBrightnessEvent`. No hay una tercera sin preguntar.

### Parar y preguntar antes de

- Borrar cualquier fichero.
- Añadir una dependencia.
- Escribir en el registro fuera de `HKCU\...\Run`.
- Cualquier cosa que pida elevación.
- Tocar algo fuera de la carpeta del proyecto.
- **Tocar una ventana ajena, de cualquier forma.** No hay excepciones y `FindWindow` es el
  centinela: es el primer paso de cualquier intento de reabrir la grieta.
- Desviarse de lo acordado.

---

## Cómo se construye

**Un hito por vez, y cada uno compila y se ejecuta antes de empezar el siguiente.** Nada de
escribir seis funciones y depurar al final. Al terminar uno: commit, y reportar
`✅ [hito] — [ficheros]` con lo que se midió.

**Un commit por cosa.** El mensaje cuenta *qué se midió*, no qué se tocó — el diff ya dice eso.

Los hitos están en el README. El orden no es negociable por una razón concreta: **H1 se hace
con datos falsos** (`--demo`), antes de tocar audio. Afinar un muelle mientras peleas con COM
es cómo se pierde un día sin saber cuál de las dos cosas está mal.

---

## Medir, no suponer

La regla que más veces salva a los proyectos de esta carpeta. Aquí hay tres sitios donde ya se
sabe que la intuición falla:

1. **Una sonda que no reporta su cadencia no vale.** La primera sonda de la §1 dijo "no hay
   ninguna ventana" muestreando **una vez cada cinco segundos**, porque hacía
   `Process.GetProcessById` sobre las 417 ventanas en cada pasada. El aviso dura dos segundos.
   La versión buena mide su propio intervalo y se declara no concluyente si hay un hueco de
   más de 900 ms. Y antes de eso, la versión cero no sabía distinguir "no hay ventana" de
   "nadie pulsó nada". **Dos mentiras distintas de la misma sonda, en la misma tarde.**
2. **`WmiMonitorBrightnessEvent` puede no llegar** en algunas máquinas, o pedir permisos que no
   tenemos. Se comprueba en H3 antes de construir nada encima. El plan B es sondeo, nunca
   elevar el proceso.
3. **`WS_EX_TRANSPARENT` no deja pasar los clics entre procesos.** Lo midió la isla con Paint
   detrás. Lo único que aparta el ratón es `SetWindowRgn`, y la región **también recorta el
   dibujo**, así que tiene que cubrir la cápsula entera incluido el squash del tope.

Y la costumbre que va con todo esto: **cuando arregles algo, mete el fallo a propósito otra
vez** y comprueba que la prueba lo detecta. Así se validó la regla 17 en H0.

Las sondas van en el scratchpad, no en el repo. Lo que sí vive en el repo es `--check`, para lo
que es lógica pura.

---

## Convenciones del código

- **Los comentarios explican *por qué*, no *qué*.** Lo que se midió, lo que se probó y no
  funcionó, la trampa que hay debajo.
- **Los atajos deliberados se marcan** con un comentario `ponytail:` que dice cuál es el techo
  y cuándo tocaría subirlo.
- **`NativeMethods.txt` es la lista cerrada de P/Invokes**, y crece **un grupo por hito**. Si
  una entrada no se puede señalar a una sección de `SEGURIDAD.md` §3, o sobra la entrada o
  falta la sección.
- **Finales de línea LF.**
- **El fichero grande es `HudWindow.cs`** y está bien así: es una ventana con su `WndProc`.

---

## Las tres cosas que explican el resto

1. **La animación no corre en nuestro hilo.** Todo es `ExpressionAnimation` sobre un
   `CompositionPropertySet`; el hilo de UI solo escribe escalares. **Las expresiones tienen un
   límite de longitud** que el dock alcanzó dos veces: los subtérminos compartidos van
   precalculados en el property set.
2. **El HUD nunca roba el foco.** Sale mientras escribes. `WS_EX_NOACTIVATE` no basta: hay que
   responder `MA_NOACTIVATE` a `WM_MOUSEACTIVATE`.
3. **Se transforma, no va y viene.** Es la razón de ser del proyecto. Cualquier cambio que haga
   que el HUD se cierre y se vuelva a abrir para cambiar de volumen a brillo está mal, aunque
   funcione.

---

## Comprobación antes de dar algo por terminado

```powershell
dotnet build                                                  # 0 errores, 0 advertencias
dotnet run -- --check                                         # la colocacion y los mapeos
powershell -File auditar.ps1                                  # TODO LIMPIO
dotnet publish -c Release -o "$env:LOCALAPPDATA\Hud\app"      # y arrancarlo de verdad
```

Y para cualquier cambio que toque colocación o escala, medirlo con las tres pantallas puestas:
en los vecinos, casi todos los fallos de ese tipo solo aparecían con más de una.
