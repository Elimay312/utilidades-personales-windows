# Plan de Implementación: Instalador Nativo de Brújula

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Construir un ejecutable único y autónomo (`Instalador-Brujula.exe`) que empaqueta `brujula.exe`, lo instala en `%LOCALAPPDATA%\Programs\Brujula\`, crea accesos directos, asocia el protocolo `brujula://`, registra el desinstalador y ofrece instalar GitHub CLI si no está presente.

**Architecture:** Aplicación Win32 en C++20 con CRT estático (`/MT`). Incrusta el binario `brujula.exe` como recurso binario (`RCDATA`). Utiliza la API moderna de `TaskDialogIndirect` de Windows para una interfaz clara e integrada sin dependencias de terceros. Soporta modo de instalación y modo de desinstalación (`--uninstall`).

**Tech Stack:** C++20, Win32 API (`TaskDialogIndirect`, Shell COM `IShellLinkW`, Reg APIs), CMake / MSVC, PowerShell para el empaquetado.

## Global Constraints

- Compilar exclusivamente con MSVC x64 en C++20.
- Runtime CRT estático (`/MT`): cero dependencias redistribuibles externas.
- Instalación por usuario (per-user en `%LOCALAPPDATA%` y `HKCU`): no requiere elevación UAC ni permisos de administrador.
- Interfaz en español con codificación UTF-8 pura.

---

### Task 1: Recursos y estructura del instalador

**Files:**
- Create: `src/installer/resource.h`
- Create: `src/installer/installer.rc`

**Interfaces:**
- Consumes: `src/app/Version.h`, `src/brujula.ico`, `build/brujula.exe`
- Produces: `IDR_BRUJULA_EXE` (101) como `RCDATA` y `IDI_APP_ICON` (1) como `ICON`.

- [x] **Step 1: Crear `src/installer/resource.h`**
  Definir los identificadores numéricos de recursos (`IDI_APP_ICON 1`, `IDR_BRUJULA_EXE 101`).

- [x] **Step 2: Crear `src/installer/installer.rc`**
  Vincular `#pragma code_page(65001)`, la versión desde `src/app/Version.h`, el icono `src/brujula.ico` y el recurso `IDR_BRUJULA_EXE RCDATA "build/brujula.exe"`.

---

### Task 2: Lógica del instalador y desinstalador Win32

**Files:**
- Create: `src/installer/Installer.h`
- Create: `src/installer/Installer.cpp`
- Create: `src/installer/main.cpp`

**Interfaces:**
- Consumes: Win32 APIs (`FindResourceW`, `LoadResource`, `IShellLinkW`, `RegCreateKeyExW`, `TaskDialogIndirect`, `SearchPathW`).
- Produces: Ejecutable del instalador con soporte para instalación y `--uninstall`.

- [x] **Step 1: Crear `src/installer/Installer.h`**
  Declarar funciones modulares y comprobables:
  - `bool IsBrujulaRunning();`
  - `bool TerminateBrujula();`
  - `std::wstring GetInstallDir();`
  - `bool ExtractEmbeddedExe(const std::wstring& targetPath);`
  - `bool CreateShortcut(const std::wstring& shortcutPath, const std::wstring& targetExe, const std::wstring& description);`
  - `bool RegisterProtocol(const std::wstring& targetExe);`
  - `bool UnregisterProtocol();`
  - `bool RegisterUninstall(const std::wstring& targetDir, const std::wstring& uninstallerExe);`
  - `bool UnregisterUninstall();`
  - `bool IsGhInstalled();`
  - `bool InstallGhWithWinget();`
  - `bool PerformInstall();`
  - `bool PerformUninstall();`

- [x] **Step 2: Implementar `src/installer/Installer.cpp`**
  Implementar cada función usando APIs limpias de Win32:
  - `CreateToolhelp32Snapshot` para comprobar y cerrar `brujula.exe`.
  - `LockResource` y `SizeofResource` para volcar `IDR_BRUJULA_EXE` en `%LOCALAPPDATA%\Programs\Brujula\brujula.exe`.
  - `CoCreateInstance(CLSID_ShellLink)` y `IPersistFile` para crear accesos directos en Menú Inicio y Escritorio.
  - `RegCreateKeyExW` para registrar `HKCU\Software\Classes\brujula` y `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Brujula`.
  - `SearchPathW(nullptr, L"gh.exe", ...)` para detectar GitHub CLI.

- [x] **Step 3: Implementar `src/installer/main.cpp` con `TaskDialogIndirect`**
  - Evaluar argumentos de línea de comandos: si contiene `--uninstall`, ejecutar el flujo de desinstalación.
  - Si es instalación normal:
    1. Mostrar diálogo de bienvenida con opción para instalar.
    2. Verificar si `gh` existe; si no, consultar al usuario si desea instalarlo con `winget`.
    3. Extraer archivos, crear accesos directos y registrar protocolo.
    4. Mostrar diálogo de éxito con opción de "Iniciar Brújula ahora".

---

### Task 3: Integración con CMake y creación de `empaquetar.ps1`

**Files:**
- Modify: `CMakeLists.txt`
- Create: `empaquetar.ps1`

**Interfaces:**
- Consumes: `preparar.ps1`, `build/brujula.exe`
- Produces: `build/Instalador-Brujula.exe`

- [x] **Step 1: Añadir objetivo de compilación para el instalador en `CMakeLists.txt`**
  Configurar el target `brujula_installer` con subsistema `WINDOWS`, enlazando `ole32`, `shell32`, `comctl32`, `advapi32`.

- [x] **Step 2: Crear el script `empaquetar.ps1`**
  Script de PowerShell automatizado que:
  1. Verifica o compila `build/brujula.exe` en Release.
  2. Compila el instalador generando `build/Instalador-Brujula.exe`.
  3. Muestra el tamaño y la ruta del ejecutable listo para su distribución.

---

### Task 4: Verificación y pruebas de extremo a extremo

**Files:**
- Test execution: `build/Instalador-Brujula.exe`

- [x] **Step 1: Compilar el instalador con `empaquetar.ps1`**
  Asegurar compilación limpia sin warnings ni errores.

- [x] **Step 2: Probar la instalación**
  Verificar que extrae el archivo en `%LOCALAPPDATA%\Programs\Brujula\brujula.exe`, crea los accesos directos y registra la URL `brujula://`.

- [x] **Step 3: Probar la desinstalación**
  Verificar que `desinstalar.exe --uninstall` elimina los accesos directos, las claves del registro y los binarios.

