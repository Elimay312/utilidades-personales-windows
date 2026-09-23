# Changelog

Formato [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/), versiones SemVer.

## [Unreleased]

### Añadido

- **Fase 1: el esqueleto, con datos de ejemplo.**
  - `Ctrl+Alt+A` abre y cierra el panel abajo a la derecha del monitor del ratón: sube 8 DIP
    en 160 ms y se funde en 120, como Agenda.
  - `Esc`, `Alt+F4` y hacer clic fuera lo esconden. El clic derecho abre un menú con «Abrir
    panel.json» y «Salir».
  - El diseño completo está pintado:
    - cuatro tiles (Wi-Fi, Bluetooth, Luz nocturna y Configuración, que dice «Próximamente»);
    - brillo y volumen con deslizadores gruesos, cada uno con su icono dentro;
    - el brillo desplegado, con una barra por pantalla;
    - el volumen desplegado, con las salidas;
    - la fila de utilidades, con un punto verde en las que están en marcha.
  - Temas oscuro, claro y alto contraste, con el color de acento de Windows.
  - `PanelState` es el esquema de todo lo que enseña, completo desde ya, y cada fase solo lo
    rellena. Tiene un campo `notice` para contar errores dentro del panel.
  - Si otra aplicación tiene el atajo, el panel se abre una vez al arrancar y lo dice en esa
    línea.
  - `--render-snapshot` genera las vistas `panel`, `panel-brillo` y `panel-volumen`, con
    `--theme`.
  - Instancia única y `--monitor=N` / `PANEL_DEV_MONITOR`.
  - 15 casos de prueba con doctest: el atajo, las opciones y el layout.
  - **Medido en esta máquina** (una pantalla, al 125 %):
    - se abre en 11 ms;
    - escondido gasta 0,8 MB de memoria privada y unos 31 ms de CPU en 30 s;
    - en Release, el ejecutable ocupa 612 KB.

- **Fase 0: documentos antes del código.**
  - `CLAUDE.md` recoge el stack fijado, la API elegida para cada función, el sistema de
    diseño, las reglas de arquitectura y las fases.
  - `SEGURIDAD.md` dice qué no hará nunca el panel (red, ubicación, administrador, ganchos
    de teclado, matar procesos) y con qué cortes toca el sistema (`IPolicyConfig`, WMI,
    DDC/CI, luz nocturna, radios y utilidades).
  - `auditar.ps1` comprueba 14 reglas. Se probó con código que incumple cada una y con
    código correcto que las pasa.
  - El plan completo está en `docs/superpowers/plans/`.
