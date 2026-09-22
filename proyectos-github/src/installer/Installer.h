#pragma once

#include <string>
#include <windows.h>

namespace brujula::installer {

// Comprobación y gestión de procesos en ejecución
bool IsBrujulaRunning();
bool CloseBrujulaProcesses();

// Rutas clave del sistema para instalación de usuario
std::wstring GetInstallDir();
std::wstring GetExePath();
std::wstring GetUninstallerPath();
std::wstring GetStartMenuShortcutPath();
std::wstring GetDesktopShortcutPath();

// Extracción y copia de binarios
bool ExtractEmbeddedExe(const std::wstring& targetPath);
bool CopySelfAsUninstaller(const std::wstring& targetPath);

// Gestión de accesos directos
bool CreateShortcut(const std::wstring& shortcutPath, const std::wstring& targetExe, const std::wstring& description);
bool RemoveShortcut(const std::wstring& shortcutPath);

// Protocolo de URL brujula:// en HKCU
bool RegisterProtocol(const std::wstring& targetExe);
bool UnregisterProtocol();

// Registro en Configuración de Windows (Aplicaciones instaladas)
bool RegisterUninstall(const std::wstring& targetDir, const std::wstring& uninstallerExe);
bool UnregisterUninstall();

// Detección e instalación opcional de GitHub CLI
bool IsGhInstalled();
bool InstallGhWithWinget(HWND parentHwnd);

// Lanzamiento de la aplicación instalada
bool LaunchBrujula();

// Limpieza de directorio
bool RemoveDirectoryRecursive(const std::wstring& path);

} // namespace brujula::installer
