#include "shell/Files.h"

#include <shobjidl_core.h>

#include <winrt/base.h>

namespace Shell {
namespace {

// Un handle de archivo que se cierra solo. Tres líneas, y a cambio ningún return por el
// medio deja el archivo abierto — que con una copia de seguridad significaría no poder
// volver a escribirla hasta cerrar la aplicación.
class File {
public:
    explicit File(HANDLE handle) : m_handle(handle) {}
    ~File() {
        if (m_handle != INVALID_HANDLE_VALUE) CloseHandle(m_handle);
    }

    File(const File&) = delete;
    File& operator=(const File&) = delete;

    HANDLE Get() const { return m_handle; }
    bool Valid() const { return m_handle != INVALID_HANDLE_VALUE; }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

Model::Error LastError(std::wstring what) {
    Model::Error error;
    error.kind = Model::Fail::Storage;
    error.code = static_cast<int>(GetLastError());
    error.detail = std::move(what);
    return error;
}

// El cuadro, con lo que comparten los tres. Devuelve vacío si el usuario cierra, que no es
// un error y por eso no hay Result por ninguna parte.
std::wstring Show(HWND owner, const CLSID& which, const wchar_t* title, FILEOPENDIALOGOPTIONS
                                                                           extra,
                  const wchar_t* extension, const wchar_t* suggested) {
    winrt::com_ptr<IFileDialog> dialog;
    if (FAILED(CoCreateInstance(which, nullptr, CLSCTX_INPROC_SERVER,
                                winrt::guid_of<IFileDialog>(), dialog.put_void()))) {
        return std::wstring();
    }

    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    // FORCEFILESYSTEM: nada de bibliotecas ni de carpetas virtuales. Lo que se pide es una
    // ruta que se pueda abrir con CreateFileW, no un elemento del shell.
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | extra);
    dialog->SetTitle(title);

    if (extension != nullptr) {
        const std::wstring pattern = L"*." + std::wstring(extension);
        const COMDLG_FILTERSPEC filters[] = {{L"Copia de Brújula", pattern.c_str()},
                                             {L"Todos los archivos", L"*.*"}};
        dialog->SetFileTypes(2, filters);
        dialog->SetDefaultExtension(extension);
    }
    if (suggested != nullptr) dialog->SetFileName(suggested);

    if (dialog->Show(owner) != S_OK) return std::wstring();

    winrt::com_ptr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put()))) return std::wstring();

    wchar_t* path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || path == nullptr) {
        return std::wstring();
    }
    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
}

}  // namespace

std::wstring PickFolder(HWND owner, const wchar_t* title) {
    return Show(owner, CLSID_FileOpenDialog, title, FOS_PICKFOLDERS, nullptr, nullptr);
}

std::wstring PickOpenFile(HWND owner, const wchar_t* title, const wchar_t* extension) {
    return Show(owner, CLSID_FileOpenDialog, title, FOS_FILEMUSTEXIST, extension, nullptr);
}

std::wstring PickSaveFile(HWND owner, const wchar_t* title, const wchar_t* extension,
                          const wchar_t* suggested) {
    return Show(owner, CLSID_FileSaveDialog, title, FOS_OVERWRITEPROMPT, extension, suggested);
}

Model::Outcome SaveText(const std::wstring& path, std::string_view utf8) {
    File file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.Valid()) return LastError(L"No se pudo crear el archivo");

    // Sin BOM. Es un JSON, y un JSON con BOM delante es un JSON que algunos lectores no
    // aceptan; el nuestro sí, pero no se escribe para nosotros solos.
    std::size_t written = 0;
    while (written < utf8.size()) {
        const DWORD chunk = static_cast<DWORD>(
            (utf8.size() - written) > 0x10000000u ? 0x10000000u : (utf8.size() - written));
        DWORD done = 0;
        if (WriteFile(file.Get(), utf8.data() + written, chunk, &done, nullptr) == FALSE) {
            return LastError(L"No se pudo escribir el archivo entero");
        }
        if (done == 0) return LastError(L"No se pudo escribir el archivo entero");
        written += done;
    }
    return Model::Ok();
}

Model::Result<std::string> LoadText(const std::wstring& path) {
    File file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.Valid()) return LastError(L"No se pudo abrir el archivo");

    LARGE_INTEGER size{};
    if (GetFileSizeEx(file.Get(), &size) == FALSE) {
        return LastError(L"No se pudo medir el archivo");
    }
    // Un tope, y no por miedo al disco: una copia de las notas de 120 repositorios son unos
    // pocos cientos de kilobytes, así que treinta y dos megas es "esto no es una copia".
    constexpr long long kMax = 32LL * 1024 * 1024;
    if (size.QuadPart > kMax) {
        return Model::Oops(Model::Fail::Storage, L"Ese archivo es demasiado grande para ser "
                                                 L"una copia de Brújula");
    }

    std::string text(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t read = 0;
    while (read < text.size()) {
        DWORD done = 0;
        if (ReadFile(file.Get(), text.data() + read, static_cast<DWORD>(text.size() - read),
                     &done, nullptr) == FALSE) {
            return LastError(L"No se pudo leer el archivo entero");
        }
        if (done == 0) break;
        read += done;
    }
    text.resize(read);

    // El BOM que pueda haber puesto otro programa. Sin quitarlo, nlohmann no lo acepta y el
    // error que sale habla de un carácter inesperado en la posición cero.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

}  // namespace Shell
