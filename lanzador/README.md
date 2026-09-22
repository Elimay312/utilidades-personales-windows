# Lanzador

`Alt+Espacio`, escribes tres letras, abres lo que sea.

Un buscador estilo Fluent Search para Windows 11: aplicaciones, ficheros a través de
**Everything**, prefijos web y un ranking que aprende de lo que abres.

> **Estado: en uso diario.** `Alt+Espacio`, escribes, Enter y abre. Lo que falta ahora es
> usarlo y ver qué molesta. El detalle de cada hito, con lo que se midió, está en el
> [CHANGELOG](CHANGELOG.md).

---

## Qué hace

| | |
|---|---|
| **Aplicaciones** | Los dos menús Inicio y las apps de la Store |
| **Ficheros** | Lo que tenga indexado Everything, preguntándoselo por IPC mientras escribes |
| **Web** | `g gatos` → Google, `y ...` → YouTube. Los prefijos los pones tú en `lanzador.json` |
| **Sistema** | `bluetooth`, `papelera`, `descargas`, `bloquear`… 18 sitios de Windows |
| **Cuentas** | `1234*0,15` y la primera fila es `185,1` |
| **Ranking** | Lo que más abres sube. Y si para `br` elegiste Brave, `br` da Brave siempre |

## Las teclas

| | |
|---|---|
| `Alt+Espacio` | abre y cierra |
| flechas | elegir fila |
| `Enter` | abrir |
| `Ctrl+Enter` | abrir la carpeta que lo contiene |
| `Esc` | cerrar |
| `Ctrl+V` | pegar en la caja |
| `Ctrl`+flechas, `Ctrl+Retroceso`, `Ctrl+A` | lo de siempre al editar |

**El teclado manda sobre el ratón.** Si el puntero se queda encima de la lista no te roba la
selección: solo cuenta si lo mueves de verdad.

## Qué necesita

- **Windows 11** (build 26100 o más).
- **.NET 10**, el runtime de escritorio. En un Windows recién instalado no está.
- **[Everything](https://www.voidtools.com/) de voidtools**, para buscar ficheros. Sin él el
  lanzador abre aplicaciones y lo dice. El programa, una vez instalado, no lo instala ni lo
  arranca. El instalador sí, porque se lo estás pidiendo al ejecutarlo.

  **Tiene que estar abierta la aplicación, no basta con el servicio.** El servicio mantiene
  el índice, pero el buzón al que se le pregunta lo publica la aplicación. Si solo corre el
  servicio, el lanzador no encuentra ficheros. El instalador deja marcado el arranque con
  la sesión.

## Cómo se instala

Doble clic en `instalar.cmd`, en el propio PC o en uno recién instalado. Si falta .NET 10
lo baja al perfil del usuario, sin administrador. Si falta Everything, baja el instalador
oficial (1.4.1.1032 x64, no la Lite: la Lite no abre el buzón), comprueba el SHA256 y lo
ejecuta. Ahí Windows pide administrador una vez: el servicio que indexa el disco no se
puede instalar sin eso.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File instalar.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File instalar.ps1 -SinEverything
powershell -NoProfile -ExecutionPolicy Bypass -File instalar.ps1 -SinAutoArranque
powershell -NoProfile -ExecutionPolicy Bypass -File instalar.ps1 -Desinstalar
```

Publica en `%LOCALAPPDATA%\Lanzador\app`, deja un acceso directo en Inicio y enciende el
autoarranque. Desinstalar no borra `lanzador.json`, `uso.json` ni Everything.

## Cómo se usa desde la consola

```powershell
lanzador --check          # comprueba el algoritmo, el decaimiento y la calculadora
lanzador --indice         # vuelca las aplicaciones encontradas y cuanto costo
lanzador --buscar "br"    # los mejores resultados, con su puntuacion desglosada
lanzador --iconos         # cuanto cuesta tener todos los iconos en memoria
lanzador --olvidar        # borra uso.json entero
```

`--buscar` es lo que hace que afinar el ranking no sea adivinar: enseña de dónde sale cada
punto de cada resultado.

## Cómo está montado

`Windows.UI.Composition` sobre un HWND propio, igual que el [dock](../dock/README.md) y la
[isla](../isla/README.md). **No hay ni una ventana hija**: la píldora de búsqueda, el cursor,
la selección y las filas los dibujamos nosotros.

Empezó con un control `EDIT` del sistema, que salía gratis. El diseño translúcido lo vino a
cobrar: un `EDIT` pinta su fondo opaco con GDI y no puede ser transparente. Lo que Windows
hacía por nosotros —caret, selección, teclas muertas, pegar— vive ahora en
[`Caja.cs`](Caja.cs), sin nada de Win32 dentro para poder comprobarlo con `--check`.

**La ventana no se ve**: es `WS_EX_NOREDIRECTIONBITMAP` y sin backdrop de DWM, así que solo
aparecen la píldora y, debajo, el panel de resultados. El acrílico del sistema se quitó porque
rellena el rectángulo entero de la ventana y no hay forma de darle forma de píldora:
`SetWindowRgn` no lo recorta y `CreateHostBackdropBrush` pinta negro en una app sin empaquetar.

Una sola dependencia, `Microsoft.Windows.CsWin32`, que es un generador y no aparece en la
salida. Todo lo Win32 sale de `NativeMethods.txt`, que es una lista cerrada y auditada.

## Lo que no hace, a propósito

No lee tus marcadores del navegador. No busca dentro de los ficheros. No ejecuta comandos
arbitrarios. No hace ni una sola llamada de red. No guarda lo que escribes — solo lo que
lanzas, y `--olvidar` lo borra.

**No escribe en el portapapeles.** Lo lee, y solo al pulsar `Ctrl+V`: por eso se puede pegar
una ruta en la caja, pero el resultado de la calculadora no se puede copiar.

**Y no apaga ni reinicia el ordenador.** Bloquear la sesión sí, porque no puede salir mal.
Apagar no: Enter sobre una coincidencia difusa no es sitio para perder trabajo.

El razonamiento de cada una de esas líneas está en **[SEGURIDAD.md](SEGURIDAD.md)**, que se
escribió antes de la primera línea de código y se comprueba con `auditar.ps1` en cada commit.
