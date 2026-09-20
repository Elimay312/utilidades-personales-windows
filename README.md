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

## Estado

**M4 — el morph.** El espacio abre el panel sobre el archivo seleccionado y enseña su
miniatura real: imágenes, PDFs, vídeos y documentos de Office. Lo que no tiene miniatura cae
a una ficha con su icono, nombre, tamaño y fecha. Nace en el cursor, se cierra con otro
espacio, con un clic, con la ✕, o solo en cuanto te vas a otra app — y si marcas otro
archivo sin cerrarlo, la tarjeta **morfa** al nuevo en vez de parpadear.

**M5 — texto y código.** Los `.txt`, `.md`, `.json`, `.cs` y compañía se leen y se dibujan
con DirectWrite, en una tarjeta con forma de página y con la rueda para desplazar.

**M6 — PDF.** Se rasteriza con el renderizador de Windows y la rueda pasa página.

Lo que viene, en orden:

| | |
|---|---|
| M7 | Vídeo y audio |
| M8 | Pulido |

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
| `Panel.cs` | El panel. Ventana fija, tarjeta que morfa dentro. |
| `Motion.cs` | Las tres animaciones: abrir, cerrar y morfar. |
| `Foreground.cs` | Quién está delante. Lo preguntan el hook, el panel y la selección. |
| `SelfCheck.cs` | Lo que `--check` comprueba: la lógica pura. |
| `NativeMethods.txt` | La lista cerrada de P/Invokes. Si algo no está aquí, no compila. |
| `auditar.ps1` | Comprueba `SEGURIDAD.md` contra el código. Tiene que decir `TODO LIMPIO`. |
