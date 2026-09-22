# Especificación de Diseño: Instalador Nativo de Brújula

**Fecha:** 2026-09-22  
**Estado:** Aprobado  
**Objetivo:** Proporcionar un ejecutable único (`Instalador-Brujula.exe`) que empaquete la aplicación autónoma `brujula.exe`, cree accesos directos, registre el protocolo URI `brujula://`, verifique la presencia de GitHub CLI (`gh`) y permita una instalación y desinstalación limpias sin requerir privilegios de administrador.

---

## 1. Motivación y Contexto

Brújula es una aplicación nativa de C++20 que utiliza el CRT estático (`/MT`) y APIs del sistema (Win32, Direct2D, DirectWrite, Windows.UI.Composition), por lo que `brujula.exe` ya es un binario 100% autónomo que no requiere dependencias externas como Visual C++ Redistributable, .NET, Electron o Visual Studio.

Sin embargo, al no disponer de releases preempaquetadas en GitHub, los usuarios necesitan una forma sencilla de instalar la aplicación con un solo clic, registrarla en el sistema operativo y configurar opcionalmente las credenciales vía GitHub CLI.

---

## 2. Componentes

1. **`src/installer/main.cpp`**:
   - Punto de entrada Win32 nativo para la instalación y desinstalación (`--uninstall`).
   - Rutinas de extracción de recursos binarios.
   - Creación de accesos directos vía interfaz COM `IShellLinkW` / `IPersistFile`.
   - Registro de protocolo y entrada de desinstalación en `HKEY_CURRENT_USER`.
   - Verificación e instalación opcional de `gh` usando `winget`.
2. **`src/installer/installer.rc`**:
   - Define el recurso binario `RCDATA` con el contenido compilado de `build/brujula.exe`.
   - Asigna el icono de la aplicación al instalador.
3. **`empaquetar.ps1`**:
   - Script de compilación que:
     1. Asegura que `build/brujula.exe` esté compilado en modo `Release`.
     2. Compila el instalador con MSVC/CMake produciendo `build/Instalador-Brujula.exe`.

---

## 3. Comportamiento en la Instalación

1. **Detección de procesos:**
   - Comprueba si `brujula.exe` está ejecutándose. Si es así, pide al usuario cerrarlo para evitar bloqueos de archivo (`ERROR_SHARING_VIOLATION`).
2. **Extracción:**
   - Directorio destino: `%LOCALAPPDATA%\Programs\Brujula\`.
   - Escribe `brujula.exe` y una copia de sí mismo como `desinstalar.exe` para permitir desinstalación nativa.
3. **Accesos directos:**
   - Menú Inicio: `%APPDATA%\Microsoft\Windows\Start Menu\Programs\Brújula.lnk`.
   - Escritorio: `%USERPROFILE%\Desktop\Brújula.lnk`.
4. **Registro del protocolo de URL `brujula://`:**
   - Ruta de registro: `HKCU\Software\Classes\brujula`
   - Claves:
     - `(Default) = "URL:Protocolo Brújula"`
     - `"URL Protocol" = ""`
     - `DefaultIcon\ = "%LOCALAPPDATA%\Programs\Brujula\brujula.exe,0"`
     - `shell\open\command\ = "\"%LOCALAPPDATA%\Programs\Brujula\brujula.exe\" \"%1\""`
5. **Registro en Configuración de Windows ("Aplicaciones instaladas"):**
   - Ruta: `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Brujula`
   - Claves: `DisplayName`, `DisplayIcon`, `DisplayVersion`, `Publisher`, `InstallLocation`, `UninstallString`.
6. **Verificación de dependencias externas:**
   - Comprueba si `gh.exe` está disponible en `PATH` (o en `%LOCALAPPDATA%\Programs\GitHub CLI\bin`).
   - Si no está instalado, muestra opción interactiva para ejecutar la instalación desatendida mediante `winget install --id GitHub.cli --accept-source-agreements --accept-package-agreements`.
7. **Opción de ejecución:**
   - Pregunta o permite iniciar Brújula inmediatamente al terminar.

---

## 4. Comportamiento en la Desinstalación (`--uninstall`)

1. Cierra instancias abiertas de `brujula.exe`.
2. Elimina los accesos directos en Menú Inicio y Escritorio.
3. Elimina las claves de registro en `HKCU\Software\Classes\brujula` y `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Brujula`.
4. Pregunta al usuario si desea eliminar o conservar la base de datos local y notas en `%LOCALAPPDATA%\Brujula`.
5. Elimina los binarios en `%LOCALAPPDATA%\Programs\Brujula\`.

---

## 5. Criterios de Éxito

- El binario `Instalador-Brujula.exe` funciona en sistemas Windows 10/11 sin privilegios de administrador.
- La aplicación queda registrada en el Menú Inicio y responde al protocolo `brujula://`.
- Desinstalación completa y limpia verificada.
