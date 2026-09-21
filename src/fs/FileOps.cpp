#include "fs/FileOps.h"

#include <Windows.h>
// shellapi.h: los FOF_*, que WIN32_LEAN_AND_MEAN deja fuera de Windows.h. Los FOFX_* estan
// en shobjidl, que entra por shlobj.h con IFileOperation.
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>

#include "fs/DirectoryReader.h"

using Microsoft::WRL::ComPtr;

namespace {

DWORD OperationFlags(FileOp op) {
    switch (op) {
    case FileOp::Delete:
        // Sin FOF_ALLOWUNDO el borrado es definitivo. FOF_NOCONFIRMATION porque el popup
        // modal de la app ya ha preguntado y no vamos a preguntar dos veces.
        return FOF_NOCONFIRMATION;
    case FileOp::Recycle:
        // FOFX_RECYCLEONDELETE es lo que fuerza la Papelera aunque la politica del sistema
        // diga otra cosa; FOF_ALLOWUNDO es lo que hace que se pueda restaurar desde ella.
        return FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE;
    default:
        return FOF_ALLOWUNDO;
    }
}

std::string Done(FileOp op, size_t count) {
    const std::string n = std::to_string(count);
    switch (op) {
    case FileOp::Copy:
        return n + (count == 1 ? " copiado" : " copiados");
    case FileOp::Move:
        return n + (count == 1 ? " movido" : " movidos");
    case FileOp::Recycle:
        return n + " a la Papelera";
    case FileOp::Delete:
        return n + (count == 1 ? " borrado" : " borrados");
    case FileOp::Rename:
        return "Renombrado";
    case FileOp::Create:
        return "Creado";
    }
    return {};
}

bool NeedsDestination(FileOp op) {
    return op == FileOp::Copy || op == FileOp::Move || op == FileOp::Create;
}

}  // namespace

std::string RunFileOp(FileOp op, const std::vector<std::wstring>& sources,
                      const std::wstring& dest, const std::wstring& name) {
    ComPtr<IFileOperation> operation;
    HRESULT hr =
        CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    if (SUCCEEDED(hr)) hr = operation->SetOperationFlags(OperationFlags(op));

    // Sin SetOwnerWindow a proposito, por lo mismo que ShellExecuteExW en la fase 3: los
    // dialogos del shell corren en otro hilo y deshabilitarian nuestra ventana desde fuera
    // del hilo de UI. El precio es que salen como ventanas independientes.

    ComPtr<IShellItem> destination;
    if (SUCCEEDED(hr) && NeedsDestination(op))
        hr = SHCreateItemFromParsingName(dest.c_str(), nullptr, IID_PPV_ARGS(&destination));

    size_t count = 0;
    if (op == FileOp::Create) {
        std::wstring item = name;
        const DWORD attributes =
            SplitNewName(item) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        if (SUCCEEDED(hr))
            hr = operation->NewItem(destination.Get(), attributes, item.c_str(), nullptr, nullptr);
        count = 1;
    }

    // Elemento a elemento y no con IShellItemArray: los dialogos son los mismos y es una
    // forma COM menos que construir.
    for (const std::wstring& source : sources) {
        if (FAILED(hr) || op == FileOp::Create) break;

        ComPtr<IShellItem> item;
        hr = SHCreateItemFromParsingName(source.c_str(), nullptr, IID_PPV_ARGS(&item));
        if (FAILED(hr)) break;

        switch (op) {
        case FileOp::Copy:
            hr = operation->CopyItem(item.Get(), destination.Get(), nullptr, nullptr);
            break;
        case FileOp::Move:
            hr = operation->MoveItem(item.Get(), destination.Get(), nullptr, nullptr);
            break;
        case FileOp::Rename:
            hr = operation->RenameItem(item.Get(), name.c_str(), nullptr);
            break;
        default:
            hr = operation->DeleteItem(item.Get(), nullptr);
            break;
        }
        ++count;
    }

    if (SUCCEEDED(hr)) hr = operation->PerformOperations();
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return "Cancelado";
    if (FAILED(hr)) return FormatWin32Error(static_cast<DWORD>(hr));

    // Cancelar desde el dialogo del shell no es un error: PerformOperations devuelve S_OK.
    BOOL aborted = FALSE;
    if (SUCCEEDED(operation->GetAnyOperationsAborted(&aborted)) && aborted) return "Cancelado";
    return Done(op, count);
}

size_t StemLength(const std::string& nameUtf8) {
    const size_t dot = nameUtf8.rfind('.');
    return (dot == std::string::npos || dot == 0) ? nameUtf8.size() : dot;
}

bool SplitNewName(std::wstring& name) {
    const size_t end = name.find_last_not_of(L"\\/");
    if (end + 1 == name.size()) return false;  // tambien cubre npos: todo son barras
    name.erase(end + 1);
    return true;
}
