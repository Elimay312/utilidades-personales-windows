#include "store/Paths.h"

#include <Windows.h>

#include <ShlObj.h>

#include "model/Utf.h"

namespace Store {
namespace {

// CoTaskMemFree en todos los caminos, también en el de fallo.
class KnownFolder {
public:
    ~KnownFolder() { CoTaskMemFree(m_path); }
    PWSTR* Receive() { return &m_path; }
    const wchar_t* Path() const { return m_path; }

private:
    PWSTR m_path = nullptr;
};

}  // namespace

Model::Result<std::string> DatabasePath() {
    KnownFolder local;
    // SHGetKnownFolderPath y no %LOCALAPPDATA%: la variable de entorno se puede haber
    // quedado sin definir o apuntando a otro sitio, y entonces la caché se escribiría donde
    // no toca sin que nadie se entere.
    const HRESULT got = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr,
                                             local.Receive());
    if (FAILED(got) || local.Path() == nullptr) {
        return Model::Oops(Model::Fail::Storage,
                           L"No se encuentra la carpeta de datos locales de Windows",
                           static_cast<int>(got));
    }

    const std::wstring folder = std::wstring(local.Path()) + L"\\Brujula";
    if (CreateDirectoryW(folder.c_str(), nullptr) == 0) {
        const DWORD why = GetLastError();
        // Que ya exista es el caso normal a partir del segundo arranque.
        if (why != ERROR_ALREADY_EXISTS) {
            return Model::Oops(Model::Fail::Storage,
                               L"No se pudo crear la carpeta de Brújula en los datos locales",
                               static_cast<int>(why));
        }
    }

    return Model::ToUtf8(folder + L"\\brujula.db");
}

}  // namespace Store
