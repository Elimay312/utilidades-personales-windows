#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <filesystem>

#include "Installer.h"
#include "resource.h"
#include "../app/Version.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

// Habilitar estilos visuales modernos de Comctl32 v6 (TaskDialog moderno)
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;
using namespace brujula::installer;

static int ShowModernDialog(
    HWND parent,
    const std::wstring& title,
    const std::wstring& mainInstruction,
    const std::wstring& content,
    TASKDIALOG_COMMON_BUTTON_FLAGS buttons,
    PCWSTR mainIcon
) {
    TASKDIALOGCONFIG tdc{};
    tdc.cbSize = sizeof(tdc);
    tdc.hwndParent = parent;
    tdc.hInstance = GetModuleHandleW(nullptr);
    tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    tdc.pszWindowTitle = title.c_str();
    tdc.pszMainInstruction = mainInstruction.c_str();
    tdc.pszContent = content.c_str();
    tdc.pszMainIcon = mainIcon;
    tdc.dwCommonButtons = buttons;

    int buttonClicked = 0;
    TaskDialogIndirect(&tdc, &buttonClicked, nullptr, nullptr);
    return buttonClicked;
}

static void HandleUninstall() {
    int confirm = ShowModernDialog(
        nullptr,
        L"Desinstalar Brújula",
        L"¿Deseas desinstalar Brújula de tu equipo?",
        L"Se eliminarán el ejecutable, los accesos directos y las asociaciones de protocolo.",
        TDCBF_YES_BUTTON | TDCBF_NO_BUTTON,
        TD_WARNING_ICON
    );

    if (confirm != IDYES) return;

    if (IsBrujulaRunning()) {
        CloseBrujulaProcesses();
    }

    // 1. Quitar accesos directos
    RemoveShortcut(GetStartMenuShortcutPath());
    RemoveShortcut(GetDesktopShortcutPath());

    // 2. Quitar registros
    UnregisterProtocol();
    UnregisterUninstall();

    // 3. Consultar sobre datos locales
    int removeData = ShowModernDialog(
        nullptr,
        L"Datos de trabajo",
        L"¿Deseas conservar tu historial y notas locales?",
        L"Tus repositorios, notas y configuraciones se encuentran en %LOCALAPPDATA%\\Brujula.\n\nElige 'Sí' para conservarlos o 'No' para eliminarlos por completo.",
        TDCBF_YES_BUTTON | TDCBF_NO_BUTTON,
        TD_INFORMATION_ICON
    );

    if (removeData == IDNO) {
        wchar_t buf[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) > 0) {
            fs::path dataPath = fs::path(buf) / "Brujula";
            RemoveDirectoryRecursive(dataPath.wstring());
        }
    }

    // 4. Auto-eliminación del directorio de instalación
    std::wstring installDir = GetInstallDir();
    std::wstring cmd = L"/c timeout /t 1 /nobreak > NUL & rmdir /s /q \"" + installDir + L"\"";

    ShellExecuteW(nullptr, L"open", L"cmd.exe", cmd.c_str(), nullptr, SW_HIDE);

    ShowModernDialog(
        nullptr,
        L"Desinstalación completada",
        L"Brújula se ha desinstalado correctamente",
        L"Gracias por usar Brújula.",
        TDCBF_OK_BUTTON,
        TD_INFORMATION_ICON
    );
}

static void HandleInstall() {
    std::wstring installDir = GetInstallDir();

    std::wstring msg = L"Brújula es un priorizador de repositorios de GitHub para Windows 11 con diseño de alta calidad.\n\n"
                       L"Ruta de instalación:\n" + installDir + L"\n\n"
                       L"¿Deseas continuar con la instalación?";

    int welcome = ShowModernDialog(
        nullptr,
        L"Instalador de Brújula v" BRUJULA_VERSION_WSTR,
        L"Instalar Brújula en tu equipo",
        msg,
        TDCBF_YES_BUTTON | TDCBF_CANCEL_BUTTON,
        TD_INFORMATION_ICON
    );

    if (welcome != IDYES) return;

    if (IsBrujulaRunning()) {
        int closeProc = ShowModernDialog(
            nullptr,
            L"Brújula está en ejecución",
            L"Se ha detectado una instancia de Brújula abierta",
            L"Es necesario cerrarla para actualizar los archivos. ¿Deseas cerrarla ahora?",
            TDCBF_YES_BUTTON | TDCBF_CANCEL_BUTTON,
            TD_WARNING_ICON
        );

        if (closeProc != IDYES) return;

        if (!CloseBrujulaProcesses()) {
            ShowModernDialog(
                nullptr,
                L"Error",
                L"No se pudo cerrar la instancia en ejecución",
                L"Por favor, cierra Brújula manualmente antes de continuar.",
                TDCBF_OK_BUTTON,
                TD_ERROR_ICON
            );
            return;
        }
    }

    // Comprobar GitHub CLI
    if (!IsGhInstalled()) {
        int installGh = ShowModernDialog(
            nullptr,
            L"Herramienta recomendada: GitHub CLI",
            L"GitHub CLI (gh) no está instalado",
            L"Brújula sincroniza tus repositorios de forma automática y transparente si cuentas con GitHub CLI autenticado.\n\n"
            L"¿Deseas que descarguemos e instalemos GitHub CLI ahora mismo mediante winget?",
            TDCBF_YES_BUTTON | TDCBF_NO_BUTTON,
            TD_SHIELD_ICON
        );

        if (installGh == IDYES) {
            InstallGhWithWinget(nullptr);
        }
    }

    // 1. Extraer brujula.exe
    std::wstring exePath = GetExePath();
    if (!ExtractEmbeddedExe(exePath)) {
        ShowModernDialog(
            nullptr,
            L"Error de instalación",
            L"No se pudo extraer el ejecutable principal",
            L"Asegúrate de tener permisos en " + installDir,
            TDCBF_OK_BUTTON,
            TD_ERROR_ICON
        );
        return;
    }

    // 2. Copiar instalador como desinstalador
    CopySelfAsUninstaller(GetUninstallerPath());

    // 3. Crear accesos directos
    CreateShortcut(GetStartMenuShortcutPath(), exePath, L"Priorizador de repositorios de GitHub");
    CreateShortcut(GetDesktopShortcutPath(), exePath, L"Priorizador de repositorios de GitHub");

    // 4. Registrar protocolo de URL y desinstalador
    RegisterProtocol(exePath);
    RegisterUninstall(installDir, GetUninstallerPath());

    // 5. Pantalla final
    int finish = ShowModernDialog(
        nullptr,
        L"Instalación completada",
        L"¡Brújula está lista para usarse!",
        L"Se han creado los accesos directos en el Menú Inicio y en tu Escritorio.\n\n¿Deseas iniciar Brújula ahora?",
        TDCBF_YES_BUTTON | TDCBF_NO_BUTTON,
        TD_INFORMATION_ICON
    );

    if (finish == IDYES) {
        LaunchBrujula();
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR pCmdLine, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::wstring cmdLine(pCmdLine ? pCmdLine : L"");
    if (cmdLine.find(L"--uninstall") != std::wstring::npos) {
        HandleUninstall();
    } else {
        HandleInstall();
    }

    CoUninitialize();
    return 0;
}
