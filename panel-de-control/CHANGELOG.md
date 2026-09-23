# Changelog

Formato [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/), versiones SemVer.

## [Unreleased]

### Añadido

- **Fase 0: documentos antes del código.**
  - `CLAUDE.md` recoge el stack fijado, la API elegida para cada función, el sistema de
    diseño, las reglas de arquitectura y las fases.
  - `SEGURIDAD.md` dice qué no hará nunca el panel (red, ubicación, administrador, ganchos
    de teclado, matar procesos) y con qué cortes toca el sistema (`IPolicyConfig`, WMI,
    DDC/CI, luz nocturna, radios y utilidades).
  - `auditar.ps1` comprueba 14 reglas. Se probó con código que incumple cada una y con
    código correcto que las pasa.
  - El plan completo está en `docs/superpowers/plans/`.
