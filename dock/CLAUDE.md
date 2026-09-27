# Cómo se trabaja en este proyecto

Un dock estilo macOS para Windows 11 que sustituye a la barra de tareas: C++20, Win32 crudo y
`Windows.UI.Composition` vía C++/WinRT. Lee el [README](README.md) para saber qué hace.

**Estado: en reescritura.** El dock de C# que está en uso diario vive en `legacy/`, fuera de
git (la versión completa está en el tag `dock-csharp-final`). Es la **referencia** de
comportamiento y de trampas, no una plantilla: lo que C++ o las restricciones ya levantadas
permitan hacer mejor, se hace mejor. `SEGURIDAD.md` y `auditar.ps1` se retiraron con el
paso a C++; el historial de por qué existían está en ese tag.

---

## Lo innegociable

1. **Comentarios en español, código en inglés.**
2. **Solo lo que se pide.** Sin features extra, sin abstracciones especulativas, sin capas ni
   ficheros de más. No hay interfaces con una implementación ni fábricas de un producto.
3. **Sin dependencias nuevas sin preguntar.** Hoy hay una aprobada: `nlohmann_json 3.12.0`,
   fijada por URL y SHA256 como en `panel-de-control`. C++/WinRT sale de las cabeceras del
   SDK y no cuenta.
4. **Convenciones de los hermanos en C++.** Las de `panel-de-control`: CMake con presets,
   CRT estático, `/W4 /permissive- /utf-8 /EHsc`, `ComPtr`. El código que se reutiliza de
   otro proyecto se **copia** y se le cambia el namespace; nada se enlaza entre proyectos.

### Parar y preguntar antes de

- Borrar cualquier fichero.
- Añadir una dependencia.
- Cualquier cosa que pida elevación.
- Tocar algo fuera de la carpeta del proyecto (salvo lo que el plan ya nombra:
  `actualizar.ps1` y la ruta del dock en `panel-de-control`).
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
- La cola del log se pierde al matar el proceso con `Stop-Process -Force` si el log no se
  vacía en cada línea.
- La memoria del dock de C# parecía una fuga por pantalla: eran 107 MB comprometidos por el
  GC con 19 MB vivos, y la parte nativa se quedaba plana. Hizo falta un volcado para verlo.

De ahí, cuatro costumbres:

1. **Antes de creer que algo está roto, comprueba que la sonda mide lo que crees.**
2. **Cuando arregles algo, mete el fallo a propósito otra vez** y comprueba que la prueba lo
   detecta. Si no lo detecta, la prueba no vale.
3. **Prefiere señales de texto a píxeles.** Casi todo lo del dock se puede observar con una
   traza detrás de `DOCK_HOVER_LOG`, con `GetWindowRect`, o preguntándole al propio dock por
   `WM_NCHITTEST` desde fuera. Un diff de capturas es el último recurso.
4. **Las sondas van en el scratchpad, no en el repo.** Lo que sí vive en el repo es
   `--check`, para lo que es lógica pura.

Y al reportar: si algo no se pudo medir, se dice. Nada de dar por bueno lo que no se vio.

**La memoria se mide en bytes privados**, no en conjunto de trabajo, y con hilos, handles y
objetos GDI/USER al lado. El presupuesto y los números de referencia del C# están en el
README.

---

## Convenciones del código

- **Los comentarios explican *por qué*, no *qué*.** Lo que se midió, lo que se probó y no
  funcionó, la trampa que hay debajo. Si alguien va a repetir un error, el comentario está
  para evitarlo.
- **Los atajos deliberados se marcan** con un comentario `ponytail:` que dice cuál es el
  techo y cuándo tocaría subirlo.
- **El modelo solo lo toca el hilo de UI.** Lo que puede bloquear va a un worker STA que
  devuelve valores nuevos con `PostMessage`. Sin mutex en el modelo.
- **Finales de línea LF.** El repo guarda LF; escribir CRLF hace que git vea el fichero
  entero como cambiado.
- **El fichero grande es `dock_window.cpp`** y está bien así: es una ventana con su
  `WndProc`, y partirla por partirla solo añadiría saltos.

---

## Las cosas que explican el resto

Si vas a tocar el dibujado o la interacción, estas deciden casi todo:

1. **La animación no corre en nuestro hilo.** La lupa es `ExpressionAnimation` sobre un
   `CompositionPropertySet`: el hilo de UI solo escribe la posición del ratón. Ojo, **las
   expresiones tienen un límite de longitud** que se alcanza antes de lo que parece.
2. **El dock nunca roba el foco.** `WS_EX_NOACTIVATE` no basta: hay que responder
   `MA_NOACTIVATE` a `WM_MOUSEACTIVATE`. De ahí sale que el menú esté dibujado a mano.
3. **La región decide qué es del dock.** `HTTRANSPARENT` **no** atraviesa procesos; lo único
   que deja pasar el ratón es `SetWindowRgn`. Y la región también recorta el dibujo, así que
   tiene que cubrir todo lo que se pinte.
4. **Si el dock oculta la barra de Windows, tiene que devolverla pase lo que pase.** El
   estado previo se escribe antes de tocar nada, y un proceso guardián la restaura si el
   dock muere o se cuelga.

---

## Comprobación antes de dar algo por terminado

```powershell
cmake --preset debug; cmake --build --preset debug              # 0 errores, 0 avisos /W4
build\debug\Dock.exe --check                                    # curvas, config, iconos
.\empaquetar.ps1; build\release\Instalar-Dock.exe --silent      # y arrancarlo de verdad
```

Y para un cambio que toque ventanas, pantalla completa o la barra de tareas, medirlo con las
tres pantallas puestas: casi todos los fallos de la fase 4 del C# solo aparecían con más de
una.
