# Changelog

## H0 — andamio y SEGURIDAD.md

Lo primero es el documento, no el código.

- **`SEGURIDAD.md`, escrito antes de la primera línea.** 16 reglas, heredadas del dock y de la
  isla, con una diferencia: la regla 15 está abierta por una grieta de una sola operación
  —mover el host del aviso nativo con `SetWindowPos`— razonada entera en la §1.
- **`auditar.ps1` con una regla 17 propia**, que confina esa grieta a `FlyoutNativo.cs`. Probada
  metiendo el fallo a propósito: una sonda con `FindWindowW`, `WH_KEYBOARD_LL` y `EnumWindows`
  hace saltar las reglas 17, 4 y 15 y devuelve código 1. Sin la sonda, `TODO LIMPIO`.
  ASCII puro y sin sintaxis de PowerShell 7: en esta máquina solo hay 5.1.
- **Ventana Win32** `WS_POPUP` + `NOACTIVATE | TOOLWINDOW | TOPMOST`, con `MA_NOACTIVATE`,
  `WM_NCACTIVATE` forzado y `hwndInsertAfter` en `WM_WINDOWPOSCHANGING`. Se crea pero **no se
  enseña**: todavía no hay nada que pintar. Sin `WS_EX_TRANSPARENT` — la isla midió que no deja
  pasar los clics entre procesos, así que eso lo resolverá la región en H1.
- **Colocación por monitor del cursor y DPI**, con `PerMonitorV2` en el manifest desde el primer
  commit. `--check` la comprueba a 100% y 150%, en un monitor secundario a la derecha, en uno a
  la izquierda con coordenadas negativas, arriba y abajo, y en 800x600.
- **Ctrl+Alt+H para salir.** Sin icono de bandeja todavía; el cierre limpio hace falta porque es
  lo que devolverá el aviso nativo a su sitio.
- **Una sola dependencia** (`CsWin32`). La segunda, `System.Management`, entra en H3 con el
  brillo y está anotada en `Hud.csproj` con el porqué.

Medido: compila con 0 advertencias, `--check` pasa 6 comprobaciones, la auditoría sale limpia.
