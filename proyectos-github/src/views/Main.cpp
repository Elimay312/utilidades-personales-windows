#include "views/Main.h"

#include <Windows.h>

#include <algorithm>

#include "ui/Controls.h"
#include "ui/Host.h"
#include "views/RepoList.h"
#include "views/Sidebar.h"

namespace Views {

namespace {

using Ui::Rect;

// Qué tecla produce la barra en la distribución de teclado de ahora mismo. En un teclado
// estadounidense es VK_OEM_2 a secas y en uno español es Mayús+7, así que preguntarlo es lo
// único que hace que el atajo "/" de CLAUDE.md exista en los dos. Con la tecla escrita a
// mano, el atajo funcionaría en la máquina de quien lo escribió y en ninguna otra.
bool IsSlash(const Input::Key& key) {
    const SHORT mapped = VkKeyScanW(L'/');
    if (mapped == -1) return false;
    if (key.virtualKey != (mapped & 0xFF)) return false;
    if (Input::Has(key.modifiers, Input::Modifiers::Control)) return false;
    if (Input::Has(key.modifiers, Input::Modifiers::Alt)) return false;
    const bool needsShift = ((mapped >> 8) & 1) != 0;
    return Input::Has(key.modifiers, Input::Modifiers::Shift) == needsShift;
}

bool IsNavigation(const Input::Key& key) {
    switch (key.virtualKey) {
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_HOME:
    case VK_END:
    case VK_RETURN:
    case 'J':
    case 'K':
        return true;
    default:
        return false;
    }
}

}  // namespace

bool Main::OnAttach() {
    // El respaldo de Windows 10, donde no hay Mica. En Windows 11 se queda a alfa cero y no
    // cuesta nada: crearlo siempre evita tener dos árboles distintos según el sistema.
    if (!CreateMaterial(0.0f)) return false;

    m_sidebar = Add<Sidebar>();
    m_content = Add<RepoList>();
    // El último, y por eso el de arriba: los botones de la ventana no pueden quedar debajo
    // de nada.
    m_chrome = Add<Chrome>();

    m_sidebar->OnLens([this](App::Lens lens) { ChooseLens(lens, true); });
    m_sidebar->OnSync([this] {
        if (m_syncRequested) m_syncRequested();
    });
    m_sidebar->OnSignOut([this] {
        if (m_signOutRequested) m_signOutRequested();
    });

    m_content->OnQueryChanged([this](const std::wstring& query) {
        if (m_state == nullptr) return;
        m_state->SetQuery(query);
        m_content->Refresh(true);
    });
    m_content->OnLensRequested([this](App::Lens lens) { ChooseLens(lens, false); });
    m_content->OnSyncRequested([this] {
        if (m_syncRequested) m_syncRequested();
    });
    return true;
}

void Main::Bind(App::State* state) {
    m_state = state;
    if (m_content) m_content->Bind(state);
    if (state && m_sidebar) m_sidebar->Select(state->CurrentLens());
}

void Main::SetMica(bool hasMica) {
    if (m_hasMica == hasMica) return;
    m_hasMica = hasMica;
    if (Attached()) OnTheme(Tokens(), 0.0f);
}

void Main::OnArrange() {
    const float width = Frame().width;
    const float height = Frame().height;

    if (m_sidebar) {
        m_sidebar->SetFrame(Rect{0.0f, 0.0f, std::min(Sidebar::kWidth, width), height});
    }
    if (m_content) {
        const float left = std::min(Sidebar::kWidth, width);
        m_content->SetFrame(Rect{left, 0.0f, std::max(width - left, 1.0f), height});
    }
    if (m_chrome) m_chrome->SetFrame(Rect{0.0f, 0.0f, width, Caption::kBarHeight});
}

void Main::OnTheme(const Theme::Tokens& tokens, float crossfadeMs) {
    if (Gfx::Material* material = MaterialOf()) {
        // Sin Mica, el mismo color de tarjeta pero OPACO: translúcido sobre nada solo
        // enseñaría el escritorio.
        Theme::Color base = tokens.cardSurface;
        base.a = m_hasMica ? 0 : 255;
        material->SetColor(base, HostRef().Animator(), crossfadeMs);
    }
    Ui::Element::OnTheme(tokens, crossfadeMs);
}

// ------------------------------------------------------------------------- Los mandos --

void Main::ChooseLens(App::Lens lens, bool fromSidebar) {
    if (m_state == nullptr) return;
    if (!fromSidebar && m_sidebar) m_sidebar->Select(lens);

    m_state->SetLens(lens);
    if (m_content) {
        // Cambiar de vista NO borra la búsqueda: quien escribe "fase" y va saltando de
        // grupo está buscando dónde cayó algo, y borrarle el filtro en cada salto le
        // obligaría a escribirlo otra vez en cada uno.
        m_content->Refresh(true);
        m_content->FocusList();
    }
    if (m_lensChanged) m_lensChanged(lens);
}

void Main::Reload(bool animate) {
    if (m_state == nullptr) return;
    if (m_sidebar) m_sidebar->SetCounts(*m_state);
    if (m_content) m_content->Refresh(animate);
}

void Main::SetCaption(const Caption::Layout& layout, Caption::Zone hovered,
                      Caption::Zone pressed) {
    if (m_chrome) m_chrome->SetCaption(layout, hovered, pressed);
}

void Main::SetSync(const Chrome::Sync& sync) {
    if (m_chrome) m_chrome->SetSync(sync);
}

void Main::SetAccount(const std::wstring& account) {
    if (m_sidebar) m_sidebar->SetAccount(account);
}

void Main::SetSyncing(bool running) {
    if (m_sidebar) m_sidebar->SetSyncing(running);
}

bool Main::OnKey(const Input::Key& e) {
    if (!e.down || m_content == nullptr) return false;

    // Mientras se escribe en la búsqueda, las letras son letras. Sin esta comprobación, la
    // «j» de «bruja» movería la selección de la lista en vez de escribirse.
    const bool typing = m_content->SearchFocused();

    if (e.virtualKey == 'F' && Input::Has(e.modifiers, Input::Modifiers::Control)) {
        m_content->FocusSearch();
        return true;
    }
    if (!typing && IsSlash(e)) {
        m_content->FocusSearch();
        return true;
    }
    if (e.virtualKey == VK_ESCAPE) {
        // Esc vacía la búsqueda y devuelve el foco a la lista. Si no había búsqueda no hay
        // nada que cerrar: las capas flotantes ya se las llevó el enrutador antes de llegar
        // aquí.
        if (m_state && !m_state->Query().empty()) {
            m_content->ClearSearch();
            return true;
        }
        return false;
    }
    if (e.virtualKey == 'G' && Input::Has(e.modifiers, Input::Modifiers::Control)) {
        m_content->ToggleLayout();
        return true;
    }
    if (e.virtualKey == 'O' && Input::Has(e.modifiers, Input::Modifiers::Control)) {
        if (m_openInGitHub && m_content->Selected() >= 0) m_openInGitHub(m_content->Selected());
        return true;
    }

    // Las flechas desde la búsqueda bajan a la lista; las letras, no.
    if (typing) {
        switch (e.virtualKey) {
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
            return m_content->Navigate(e);
        default:
            return false;
        }
    }

    // Y con el foco en cualquier otro sitio —o en ninguno—, la navegación es de la lista.
    if (IsNavigation(e)) return m_content->Navigate(e);
    return false;
}

}  // namespace Views
