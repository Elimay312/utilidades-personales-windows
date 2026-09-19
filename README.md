# Lanzador

`Alt+Espacio`, escribes tres letras, abres lo que sea.

Un buscador estilo Fluent Search para Windows 11: aplicaciones, ficheros a través de
**Everything**, prefijos web y un ranking que aprende de lo que abres.

> **Estado: en construcción, pero ya se usa.** `Alt+Espacio`, escribes, Enter y abre —
> aplicaciones y ficheros. Faltan los prefijos web, los comandos del sistema, la
> calculadora y los iconos. Lo que hay hecho está en el [CHANGELOG](CHANGELOG.md).

---

## Qué hace

| | |
|---|---|
| **Aplicaciones** | Los dos menús Inicio y las apps de la Store |
| **Ficheros** | Lo que tenga indexado Everything, preguntándoselo por IPC mientras escribes |
| **Web** | `g gatos` → Google, `y ...` → YouTube. Los prefijos los pones tú en `lanzador.json` |
| **Sistema** | `apagar`, `bloquear`, `bluetooth`… abren lo que toque de Windows |
| **Cuentas** | `2+2*7` y la primera fila es el resultado |
| **Ranking** | Lo que más abres sube. Y si para `br` elegiste Brave, `br` da Brave siempre |

## Qué necesita

- **Windows 11** y **.NET 10**.
- **[Everything](https://www.voidtools.com/) de voidtools**, si quieres buscar ficheros. Sin
  él el lanzador funciona igual, pero solo enseña aplicaciones, y lo dice. No se instala ni se
  arranca desde aquí.

  Un detalle que cuesta encontrar: **tiene que estar abierta la aplicación, no basta con el
  servicio.** Everything instala las dos cosas; el servicio mantiene el índice, pero el buzón
  al que se le pregunta lo publica la aplicación. Si solo corre el servicio, el lanzador no
  encuentra ficheros. Con "Start Everything on system startup" marcado queda resuelto.

## Cómo se usa desde la consola

```powershell
lanzador --check          # comprueba el algoritmo, el decaimiento y la calculadora
lanzador --indice         # vuelca las aplicaciones encontradas y cuanto costo
lanzador --buscar "br"    # los mejores resultados, con su puntuacion desglosada
lanzador --olvidar        # borra uso.json entero
```

`--buscar` es lo que hace que afinar el ranking no sea adivinar: enseña de dónde sale cada
punto de cada resultado.

## Cómo está montado

`Windows.UI.Composition` sobre un HWND propio, igual que el [dock](../dock/README.md) y la
[isla](../isla/README.md), con una excepción: **la caja de texto es un control `EDIT` del
sistema**. El caret, la selección, las teclas muertas y Ctrl+V los implementa Windows, y por
eso no hay ni una línea de edición de texto en este repo.

Una sola dependencia, `Microsoft.Windows.CsWin32`, que es un generador y no aparece en la
salida. Todo lo Win32 sale de `NativeMethods.txt`, que es una lista cerrada y auditada.

## Lo que no hace, a propósito

No lee tus marcadores del navegador. No busca dentro de los ficheros. No ejecuta comandos
arbitrarios. No hace ni una sola llamada de red. No guarda lo que escribes — solo lo que
lanzas, y `--olvidar` lo borra.

El razonamiento de cada una de esas líneas está en **[SEGURIDAD.md](SEGURIDAD.md)**, que se
escribió antes de la primera línea de código y se comprueba con `auditar.ps1` en cada commit.
