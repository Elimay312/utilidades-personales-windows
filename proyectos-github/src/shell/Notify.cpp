#include "shell/Notify.h"

#include <shellapi.h>

#include <algorithm>
#include <iterator>

#include "shell/Window.h"

namespace Shell {

namespace {

// Uno, y siempre el mismo. El icono se añade y se quita, así que no hay dos vivos a la vez
// y no hace falta repartir números.
constexpr UINT kIconId = 1;

// Copia recortando, que es lo que piden los campos de tamaño fijo de NOTIFYICONDATAW.
// Recortar a mano y no con wcsncpy_s: aquí lo que sobra se tira y no es un error.
void Copy(wchar_t* destination, std::size_t capacity, const std::wstring& text) {
    const std::size_t count = std::min(text.size(), capacity - 1);
    std::copy_n(text.begin(), count, destination);
    destination[count] = L'\0';
}

NOTIFYICONDATAW Describe(HWND owner) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner;
    data.uID = kIconId;
    return data;
}

}  // namespace

Balloon::~Balloon() { Hide(); }

bool Balloon::Show(HWND owner, const std::wstring& title, const std::wstring& body) {
    if (owner == nullptr) return false;
    // Si ya había uno, se quita y se pone otro. Modificar el que hay serviría, pero solo
    // mientras el anterior siga vivo, y no hay manera de saberlo desde aquí sin llevar la
    // cuenta de una cosa que el shell ya lleva.
    Hide();

    NOTIFYICONDATAW data = Describe(owner);
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO | NIF_SHOWTIP;
    data.uCallbackMessage = Window::kNotifyMessage;
    // El icono de la aplicación llega en la fase 8; hasta entonces, el del sistema. Sin
    // NIF_ICON el globo no sale: el área de notificación enseña globos DE un icono.
    data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    Copy(data.szTip, std::size(data.szTip), L"Brújula");
    Copy(data.szInfoTitle, std::size(data.szInfoTitle), title);
    Copy(data.szInfo, std::size(data.szInfo), body);
    data.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;

    if (!Shell_NotifyIconW(NIM_ADD, &data)) return false;
    m_owner = owner;
    m_shown = true;

    // La versión 4 es la que hace que el aviso traiga el suceso en el LOWORD del LPARAM.
    // Con la de siempre llega en el LPARAM entero y el identificador en el WPARAM, que es
    // lo mismo escrito del revés; se pide la 4 porque es la que documenta NIN_POPUPOPEN y
    // la que no depende de que el icono quepa en un WPARAM.
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
    return true;
}

bool Balloon::OnMessage(LPARAM lparam) {
    if (!m_shown) return false;
    const UINT event = LOWORD(lparam);
    switch (event) {
    case NIN_BALLOONUSERCLICK:
        // Lo pulsó: el icono se va y quien llama abre lo que el globo prometía.
        Hide();
        return true;
    case NIN_BALLOONTIMEOUT:
    case NIN_BALLOONHIDE:
        // Se cansó de esperar o lo cerraron. El icono no se queda a vivir ahí.
        Hide();
        return false;
    default:
        return false;
    }
}

void Balloon::Hide() {
    if (!m_shown) return;
    NOTIFYICONDATAW data = Describe(m_owner);
    Shell_NotifyIconW(NIM_DELETE, &data);
    m_shown = false;
    m_owner = nullptr;
}

}  // namespace Shell
