# QuickLook

Vista previa con la barra espaciadora para Windows 11. Seleccionas un archivo en el
Explorador, pulsas espacio, lo ves. Otro espacio y se va.

Es lo que más se echa de menos al volver de macOS, y es la tercera utilidad de esta
carpeta, junto a [dock](../dock) e [isla](../isla). Mismo stack que las otras: .NET 10,
Win32 crudo y `Windows.UI.Composition`. Sin WPF, sin WinUI, sin WinForms.

**Una sola dependencia**, la misma que las otras dos: `Microsoft.Windows.CsWin32`, que es
un generador de código y no aparece en la salida. PDF, vídeo y audio salen de WinRT, que ya
viene en el SDK.

---

## Qué hace

| Gesto | Qué pasa |
|---|---|
| `Espacio` con un archivo marcado en el Explorador | Abre el panel sobre él. Otro espacio y se va. |
| `Espacio` con el foco en un campo de texto | **Escribe un espacio.** Renombrar con F2, la caja de búsqueda y la barra de direcciones siguen funcionando. |
| `Esc`, clic fuera, la ✕, o irse a otra app | Cierra el panel. |
| Marcar otro archivo sin cerrar | La tarjeta **morfa** al nuevo en vez de parpadear. |
| Rueda | Desplaza el texto, o pasa página en un PDF. |
| `Shift` + rueda | Hojea entre los archivos marcados, sin tocar lo que el Explorador tiene seleccionado. El pie dice `2 de 5`. |
| Clic derecho sobre el panel | *Salir de QuickLook*. |

Qué sabe enseñar: imágenes, PDF, vídeo, audio con su carátula, texto y código, y la
miniatura de los documentos de Office. Lo que no tiene miniatura cae a una ficha con su
icono, nombre, tamaño y fecha.

---

## Estado

**M4 — el morph.** El espacio abre el panel sobre el archivo seleccionado y enseña su
miniatura real: imágenes, PDFs, vídeos y documentos de Office. Lo que no tiene miniatura cae
a una ficha con su icono, nombre, tamaño y fecha. Nace en el cursor, se cierra con otro
espacio, con un clic, con la ✕, o solo en cuanto te vas a otra app — y si marcas otro
archivo sin cerrarlo, la tarjeta **morfa** al nuevo en vez de parpadear.

**M5 — texto y código.** Los `.txt`, `.md`, `.json`, `.cs` y compañía se leen y se dibujan
con DirectWrite, en una tarjeta con forma de página y con la rueda para desplazar.

**M6 — PDF.** Se rasteriza con el renderizador de Windows y la rueda pasa página.

**M7 — vídeo y audio.** Con `MediaPlayer` y su superficie de Composition: el vídeo es un
visual más dentro del panel, en bucle y mudo; el audio suena y enseña su carátula.

**M8.1 — selección múltiple.** Con varios archivos marcados el pie dice `2 de 5` y
**Shift + rueda** hojea entre ellos, sin tocar lo que el Explorador tiene marcado.

**M8 — config y salida.** `quicklook.json` para el tamaño del panel, el sonido y el
autoarranque, y clic derecho sobre el panel para salir. El programa **no escribe ningún
archivo**: lo único que guarda entre sesiones es la clave de autoarranque del registro.

**M7.2 — probado con vídeo de verdad**, con un AVI generado a mano. Destapó que en pantallas
con DPI alto la vista previa se encogía dentro de un marco a escala: el tope de escalado era
de 1× en píxeles físicos y ahora es la escala de la pantalla.

**M7.1 — `Esc` cierra el panel**, y el filtro del hook por fin tiene prueba automática: el
espacio abre, `Esc` cierra, `F2` renombra con espacios, y con otra app delante el espacio
pasa de largo.

El plan está completo. Lo que queda fuera a propósito: la rejilla con todas las miniaturas a
la vez, el resaltado de sintaxis, y previsualizar dentro de un `.zip`.

---

## Compilar y ejecutar

```powershell
dotnet build                                                      # 0 errores, 0 advertencias
dotnet run -- --check                                             # lógica pura
pwsh -File auditar.ps1                                            # TODO LIMPIO
dotnet publish -c Release -o "$env:LOCALAPPDATA\QuickLook\app"
& "$env:LOCALAPPDATA\QuickLook\app\QuickLook.exe"
```

No se ejecuta desde `bin\`, igual que en el dock: un `dotnet clean` se llevaría la config
por delante.

Lanzado desde una terminal escribe en la consola. Con `QL_LOG=1` añade la traza con marca
de tiempo, que es la que se usa para calibrar las sondas:

```powershell
$env:QL_LOG = "1"
& "$env:LOCALAPPDATA\QuickLook\app\QuickLook.exe"
```

---

## Configurar: `quicklook.json`

Vive en `%LOCALAPPDATA%\QuickLook\quicklook.json`, un nivel por encima de `app\`. Se copia
a mano del que hay junto al ejecutable. **Admite comentarios y comas finales**, y se relee
sola cuando cambia de fecha: no hace falta reiniciar.

```jsonc
{
  "autoStart": false,     // arrancar al iniciar sesion (HKCU\...\Run)
  "panelWidth": 0.62,     // tamano maximo del panel, en fraccion del area de trabajo
  "panelHeight": 0.72,    // los dos se recortan a [0,2 - 0,95]
  "videoMuted": true,     // el video entra mudo: es un vistazo, no una reproduccion
  "audioPlays": true      // el audio si suena, porque ahi el sonido es el contenido
}
```

**El programa no escribe este archivo, ni ningún otro.** Y no es una limitación: el único
estado que necesita recordar entre sesiones es si arranca solo, y eso vive en el registro,
donde lo ves y lo quitas desde la pestaña Inicio del Administrador de tareas. Como no
escribe nada, la regla de auditoría que prohíbe escribir archivos puede seguir siendo un
`grep` a secas, sin excepciones.

Si el JSON tiene una coma de más, se sigue con lo anterior y se dice en la traza: un error
de tecleo no puede dejarte sin programa. Y los tamaños se recortan porque un `panelWidth`
de 5 daría un panel más grande que la pantalla, imposible de cerrar con el ratón.

---

## Lo que hay que leer antes de tocar código

**[SEGURIDAD.md](SEGURIDAD.md)**, y no es una formalidad. Este programa hace dos cosas que
el dock tiene prohibidas:

1. **Instala un hook de teclado** (`WH_KEYBOARD_LL`) para ver la barra espaciadora. Es la
   única forma de que el espacio pelado funcione sin secuestrarlo en todo el sistema.
2. **Le pregunta al Explorador qué archivo tienes seleccionado**, por COM y desde fuera.

Las dos están abiertas aquí, por escrito, con sus cortafuegos y con su comprobación en
`auditar.ps1`. Y siguen prohibidas en el dock: son documentos hermanos, no el mismo
documento.

Lo que sostiene la defensa no es que el hook sea pequeño, es todo lo demás: sin red, sin
escribir nada del teclado en disco, sin sintetizar entrada, sin ofuscar el binario. Un
observador de una sola tecla que no puede hablar con nadie no exfiltra nada porque no tiene
a dónde.

---

## Arquitectura

Tres decisiones que explican casi todo lo demás:

1. **El callback del hook no hace trabajo.** Windows desinstala en silencio un hook de bajo
   nivel que tarde más de `LowLevelHooksTimeout` (~300 ms) en responder. El callback mira
   una tecla, hace `PostMessage` y devuelve; todo lo demás ocurre en el `WndProc` de
   `HostWindow`.
2. **El panel nunca roba el foco.** `WS_EX_NOACTIVATE` no basta: hay que responder
   `MA_NOACTIVATE` a `WM_MOUSEACTIVATE`. Si lo robara, el Explorador perdería el resaltado
   de la selección y el segundo espacio no llegaría por el mismo camino que el primero.
3. **La animación no corre en nuestro hilo.** Todo es Composition sobre el hilo de DWM,
   igual que el dock. El hilo de UI solo dispara. De ahí sale la decisión que hace posible
   el morph: **la ventana es siempre la caja máxima y lo que cambia de tamaño es la tarjeta
   de dentro**, porque ajustar la ventana a cada contenido obligaría a un `SetWindowPos` por
   fotograma desde nuestro hilo.

Y una cuarta que no es de arquitectura pero decide si esto sirve: **si hay un campo de
texto con el foco, el espacio pasa de largo.** Renombrar con F2, la caja de búsqueda, la
barra de direcciones. Sin eso, el programa hace el Explorador inusable.

---

## Mapa de ficheros

| Fichero | Qué hace |
|---|---|
| `Program.cs` | Entrada. Consola, ventana-host, bucle de mensajes. |
| `HostWindow.cs` | La ventana que nunca se ve. Donde aterriza el aviso del hook. |
| `Hook.cs` | Lo único que ve una tecla. 88 líneas, y así se queda. |
| `Selection.cs` | Qué archivo tiene seleccionado el Explorador. COM desde fuera. |
| `Shell.cs` | Los píxeles del archivo: icono o miniatura, misma llamada. |
| `Visuals.cs` | El compositor, el device de dibujo y los pinceles. |
| `Text.cs` | El texto del panel, con DirectWrite. |
| `Content/Preview.cs` | Qué trato le toca a cada extensión. |
| `Content/TextFile.cs` | Leer un archivo de texto sin tragarse un binario. |
| `Content/PdfFile.cs` | Una página de PDF, rasterizada por Windows. |
| `Content/MediaFile.cs` | Vídeo y audio. Lo importante es soltarlo. |
| `Log.cs` | La traza de `QL_LOG`, con marca de tiempo. |
| `Panel.cs` | El panel. Ventana fija, tarjeta que morfa dentro. |
| `Motion.cs` | Las tres animaciones: abrir, cerrar y morfar. |
| `Foreground.cs` | Quién está delante. Lo preguntan el hook, el panel y la selección. |
| `Config.cs` | `quicklook.json`, de solo lectura. |
| `AutoStart.cs` | El único sitio del registro donde se escribe. |
| `SelfCheck.cs` | Lo que `--check` comprueba: la lógica pura. |
| `NativeMethods.txt` | La lista cerrada de P/Invokes. Si algo no está aquí, no compila. |
| `auditar.ps1` | Comprueba `SEGURIDAD.md` contra el código. Tiene que decir `TODO LIMPIO`. |
