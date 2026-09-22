#include "Installer.h"
#include "resource.h"
#include "../app/Version.h"

#include <shlobj.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <filesystem>
#include <vector>

namespace brujula::installer {

namespace fs = std::filesystem;

static std::wstring GetKnownFolder(REFKNOWNFOLDERID rfid) {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(rfid, KF_FLAG_CREATE, nullptr, &path))) {
        std::wstring result(path);
        CoTaskMemFree(path);
        return result;
    }
    return L"";
}

std::wstring GetInstallDir() {
    std::wstring localAppData = GetKnownFolder(FOLDERID_LocalAppData);
    if (localAppData.empty()) {
        wchar_t buf[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) > 0) {
            localAppData = buf;
        }
    }
    return (fs::path(localAppData) / "Programs" / "Brujula").wstring();
}

std::wstring GetExePath() {
    return (fs::path(GetInstallDir()) / "brujula.exe").wstring();
}

std::wstring GetUninstallerPath() {
    return (fs::path(GetInstallDir()) / "desinstalar.exe").wstring();
}

std::wstring GetStartMenuShortcutPath() {
    std::wstring programs = GetKnownFolder(FOLDERID_Programs);
    return (fs::path(programs) / "Brújula.lnk").wstring();
}

std::wstring GetDesktopShortcutPath() {
    std::wstring desktop = GetKnownFolder(FOLDERID_Desktop);
    return (fs::path(desktop) / "Brújula.lnk").wstring();
}

bool IsBrujulaRunning() {
    DWORD currentPid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    bool running = false;
    if (Process32FirstW(snap, &entry)) {
        do {
            if (entry.th32ProcessID != currentPid && _wcsicmp(entry.szExeFile, L"brujula.exe") == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return running;
}

static BOOL CALLBACK CloseWindowCallback(HWND hwnd, LPARAM lParam) {
    DWORD targetPid = static_cast<DWORD>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == targetPid && IsWindowVisible(hwnd)) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

bool CloseBrujulaProcesses() {
    DWORD currentPid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::vector<DWORD> pids;

    if (Process32FirstW(snap, &entry)) {
        do {
            if (entry.th32ProcessID != currentPid && _wcsicmp(entry.szExeFile, L"brujula.exe") == 0) {
                pids.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);

    for (DWORD pid : pids) {
        EnumWindows(CloseWindowCallback, static_cast<LPARAM>(pid));
    }

    // Esperar hasta 2 segundos para cierre ordenado
    for (int i = 0; i < 20; ++i) {
        Sleep(100);
        if (!IsBrujulaRunning()) return true;
    }

    // Si aún persiste, forzar terminación
    for (DWORD pid : pids) {
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProc) {
            TerminateProcess(hProc, 0);
            CloseHandle(hProc);
        }
    }

    return !IsBrujulaRunning();
}

bool ExtractEmbeddedExe(const std::wstring& targetPath) {
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_BRUJULA_EXE), RT_RCDATA);
    if (!hRes) return false;

    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData) return false;

    DWORD size = SizeofResource(nullptr, hRes);
    const void* ptr = LockResource(hData);
    if (!ptr || size == 0) return false;

    std::error_code ec;
    fs::create_directories(fs::path(targetPath).parent_path(), ec);

    HANDLE hFile = CreateFileW(
        targetPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(hFile, ptr, size, &written, nullptr);
    CloseHandle(hFile);

    return ok && (written == size);
}

bool CopySelfAsUninstaller(const std::wstring& targetPath) {
    wchar_t selfPath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, selfPath, MAX_PATH) == 0) return false;

    std::error_code ec;
    fs::create_directories(fs::path(targetPath).parent_path(), ec);

    return CopyFileW(selfPath, targetPath.c_str(), FALSE) != FALSE;
}

bool CreateShortcut(const std::wstring& shortcutPath, const std::wstring& targetExe, const std::wstring& description) {
    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&psl));
    if (FAILED(hr)) return false;

    psl->SetPath(targetExe.c_str());
    psl->SetDescription(description.c_str());
    psl->SetWorkingDirectory(fs::path(targetExe).parent_path().wstring().c_str());

    IPersistFile* ppf = nullptr;
    hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
    bool success = false;
    if (SUCCEEDED(hr)) {
        std::error_code ec;
        fs::create_directories(fs::path(shortcutPath).parent_path(), ec);
        hr = ppf->Save(shortcutPath.c_str(), TRUE);
        success = SUCCEEDED(hr);
        ppf->Release();
    }
    psl->Release();
    return success;
}

bool RemoveShortcut(const std::wstring& shortcutPath) {
    std::error_code ec;
    return fs::remove(shortcutPath, ec);
}

static bool SetRegistryString(HKEY root, const std::wstring& subkey, const std::wstring& name, const std::wstring& value) {
    HKEY hKey = nullptr;
    LONG res = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (res != ERROR_SUCCESS) return false;

    const wchar_t* valName = name.empty() ? nullptr : name.c_str();
    res = RegSetValueExW(
        hKey,
        valName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))
    );
    RegCloseKey(hKey);
    return res == ERROR_SUCCESS;
}

static bool SetRegistryDword(HKEY root, const std::wstring& subkey, const std::wstring& name, DWORD value) {
    HKEY hKey = nullptr;
    LONG res = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (res != ERROR_SUCCESS) return false;

    res = RegSetValueExW(
        hKey,
        name.c_str(),
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&value),
        sizeof(DWORD)
    );
    RegCloseKey(hKey);
    return res == ERROR_SUCCESS;
}

bool RegisterProtocol(const std::wstring& targetExe) {
    std::wstring baseKey = L"Software\\Classes\\brujula";
    std::wstring iconVal = targetExe + L",0";
    std::wstring cmdVal = L"\"" + targetExe + L"\" \"%1\"";

    bool ok = true;
    ok &= SetRegistryString(HKEY_CURRENT_USER, baseKey, L"", L"URL:Protocolo Brújula");
    ok &= SetRegistryString(HKEY_CURRENT_USER, baseKey, L"URL Protocol", L"");
    ok &= SetRegistryString(HKEY_CURRENT_USER, baseKey + L"\\DefaultIcon", L"", iconVal);
    ok &= SetRegistryString(HKEY_CURRENT_USER, baseKey + L"\\shell\\open\\command", L"", cmdVal);
    return ok;
}

bool UnregisterProtocol() {
    return RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\brujula") == ERROR_SUCCESS;
}

bool RegisterUninstall(const std::wstring& targetDir, const std::wstring& uninstallerExe) {
    std::wstring key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Brujula";
    std::wstring exePath = (fs::path(targetDir) / "brujula.exe").wstring();
    std::wstring uninstCmd = L"\"" + uninstallerExe + L"\" --uninstall";

    bool ok = true;
    ok &= SetRegistryString(HKEY_CURRENT_USER, key, L"DisplayName", L"Brújula");
    ok &= SetRegistryString(HKEY_CURRENT_USER, key, L"DisplayVersion", BRUJULA_VERSION_WSTR);
    ok &= SetRegistryString(HKEY_CURRENT_USER, key, L"DisplayIcon", exePath + L",0");
    ok &= SetRegistryString(HKEY_CURRENT_USER, key, L"Publisher", L"Brújula");
    ok &= SetRegistryString(HKEY_CURRENT_USER, key, L"InstallLocation", targetDir);
    ok &= SetRegistryString(HKEY_CURRENT_USER, key, L"UninstallString", uninstCmd);
    ok &= SetRegistryDword(HKEY_CURRENT_USER, key, L"NoModify", 1);
    ok &= SetRegistryDword(HKEY_CURRENT_USER, key, L"NoRepair", 1);
    return ok;
}

bool UnregisterUninstall() {
    return RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Brujula") == ERROR_SUCCESS;
}

bool IsGhInstalled() {
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"gh.exe", nullptr, MAX_PATH, buf, nullptr) > 0) {
        return true;
    }

    // Comprobar rutas habituales en caso de no haber reiniciado la sesión
    std::wstring localAppData = GetKnownFolder(FOLDERID_LocalAppData);
    if (!localAppData.empty()) {
        fs::path ghLocal = fs::path(localAppData) / "Programs" / "GitHub CLI" / "bin" / "gh.exe";
        if (fs::exists(ghLocal)) return true;
    }

    wchar_t pf[MAX_PATH];
    if (GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH) > 0) {
        fs::path ghPf = fs::path(pf) / "GitHub CLI" / "gh.exe";
        if (fs::exists(ghPf)) return true;
    }

    return false;
}

bool InstallGhWithWinget(HWND parentHwnd) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = parentHwnd;
    sei.lpVerb = L"open";
    sei.lpFile = L"winget.exe";
    sei.lpParameters = L"install --id GitHub.cli --accept-source-agreements --accept-package-agreements";
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        return exitCode == 0;
    }
    return false;
}

bool LaunchBrujula() {
    std::wstring exe = GetExePath();
    HINSTANCE hInst = ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, fs::path(exe).parent_path().wstring().c_str(), SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(hInst) > 32;
}

bool RemoveDirectoryRecursive(const std::wstring& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
    return !ec;
}

} // namespace brujula::installer
