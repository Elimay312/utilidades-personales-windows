# Renombrar

Renombrado masivo con vista previa: **ves la tabla antes → después de todo el lote, y solo
entonces se aplica.**

Nace de una carpeta de recibos de oficina que nadie termina de organizar, pero no es un
renombrador de recibos: son reglas encadenables sobre el nombre, así que sirven igual para
fotos, exportaciones o descargas. Los recibos se resuelven guardando una cadena de reglas
como preset.

> **Estado: en construcción.** Va por hitos; lo que hay hecho está en el
> [CHANGELOG](CHANGELOG.md). Hoy hay andamio y reglas, todavía no hay ventana.

---

## Qué hace

| | |
|---|---|
| **Sueltas** | Archivos o una carpeta encima de la ventana. Solo eso: no busca nada por su cuenta |
| **Encadenas reglas** | Buscar/reemplazar (texto o regex), insertar, quitar, numerar, fecha del archivo, MAYÚS/minús, extensión |
| **Ves la tabla** | Antes → después, fila a fila, con las colisiones y los nombres imposibles marcados en rojo |
| **Aplicas** | Y si te arrepientes, **Deshacer** devuelve el último lote a como estaba |

## Qué no hace, y no por descuido

**No borra nada. Nunca.** La única operación sobre el disco es cambiarle el nombre a un
archivo, así que por mal que salga un lote los archivos siguen ahí, con otro nombre, en la
misma carpeta. Tampoco sobrescribe: si el destino ya existe, la vista previa lo marca y no te
deja aplicar.

**No abre tus archivos.** Solo ve el nombre, la extensión y las fechas. De ahí que la v1 no
lea el contenido del PDF ni haga OCR: eso cruzaría esa línea y hay que escribirlo antes de
cruzarla.

**No recorre el disco**, no toca el registro, no se arranca solo, no lanza programas y no sale
a la red. Todo esto está por escrito y auditado en [SEGURIDAD.md](SEGURIDAD.md), que se
escribió **antes** de la primera línea de código.

## Qué necesita

- **Windows 11** y **.NET 10**.
- Nada más. Una sola dependencia de compilación (`Microsoft.Windows.CsWin32`, un generador
  que no aparece en la salida).

## Cómo se usa desde la terminal

Todo lo que hace la ventana se puede hacer y observar desde la consola, que es como se
construyó:

```powershell
renombrar --previa "C:\recibos"     # la tabla, con sus estados, sin tocar nada
renombrar --aplicar "C:\recibos"    # renombra, despues de que escribas "si"
renombrar --deshacer                # revierte el ultimo lote
renombrar --check                   # el motor y la vista previa se comprueban solos
```

## Cómo está montado

Win32 crudo y `Windows.UI.Composition` del sistema, sin XAML, igual que el resto de
utilidades de esta carpeta. La franja de arriba son controles nativos del sistema (`EDIT`,
`COMBOBOX`, `BUTTON`) sobre color sólido, y la tabla de abajo es Composition sobre acrílico:
el desplazamiento de la lista es una `ExpressionAnimation`, así que corre en el proceso de
DWM y no se entrecorta aunque la previa esté recalculándose.

- `Regla.cs` — una regla y cómo se aplica. Lógica pura.
- `Previa.cs` — archivos + reglas → filas antes/después con su estado. Lógica pura.
- `Aplicar.cs` — el único sitio que mueve archivos, y el diario para deshacerlo.
- `Ventana.cs`, `Visuales.cs`, `Texto.cs` — el HWND, el compositor y el texto.
