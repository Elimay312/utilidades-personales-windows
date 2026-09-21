#include "shell/Window.h"

#include <windowsx.h>

#include <algorithm>

#include "shell/Backdrop.h"
#include "shell/Dpi.h"

namespace Shell {

namespace {

constexpr wchar_t kClassName[] = L"BrujulaWindow";
constexpr DWORD kStyle = WS_OVERLAPPEDWINDOW;

// Por debajo de esto la barra de título no cabe ni con los tres botones.
constexpr float kMinWidthDip = 560.0f;
constexpr float kMinHeightDip = 400.0f;

int ToHitTest(Caption::Zone zone) {
    switch (zone) {
    case Caption::Zone::Caption:     return HTCAPTION;
    case Caption::Zone::Minimize:    return HTMINBUTTON;
    // Lo que hace salir el menú de ajuste de Windows 11 al pasar por encima. No hay que
    // hacer nada más: basta con contestar esto al hit-test y el resto lo pone el sistema.
    case Caption::Zone::Maximize:    return HTMAXBUTTON;
    case Caption::Zone::Close:       return HTCLOSE;
    case Caption::Zone::Top:         return HTTOP;
    case Caption::Zone::Bottom:      return HTBOTTOM;
    case Caption::Zone::Left:        return HTLEFT;
    case Caption::Zone::Right:       return HTRIGHT;
    case Caption::Zone::TopLeft:     return HTTOPLEFT;
    case Caption::Zone::TopRight:    return HTTOPRIGHT;
    case Caption::Zone::BottomLeft:  return HTBOTTOMLEFT;
    case Caption::Zone::BottomRight: return HTBOTTOMRIGHT;
    case Caption::Zone::Client:      break;
    }
    return HTCLIENT;
}

Caption::Zone FromHitTest(WPARAM hit) {
    switch (hit) {
    case HTMINBUTTON: return Caption::Zone::Minimize;
    case HTMAXBUTTON: return Caption::Zone::Maximize;
    case HTCLOSE:     return Caption::Zone::Close;
    default:          return Caption::Zone::Client;
    }
}

// Centrada en el área de trabajo y encogida si no cabe: 1280x800 DIP no entran en un
// portátil de 1080p al 150 %.
RECT InitialPlacement(float widthDip, float heightDip, UINT dpi) {
    const float scale = Dpi::ScaleFor(dpi);
    RECT rect{0, 0, Dpi::ToPixels(widthDip, scale), Dpi::ToPixels(heightDip, scale)};
    AdjustWindowRectExForDpi(&rect, kStyle, FALSE, 0, dpi);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    // LONG y no int: RECT los declara long, y en MSVC long e int son tipos distintos,
    // asi que std::min no deduce.
    const LONG workWidth = work.right - work.left;
    const LONG workHeight = work.bottom - work.top;

    const LONG width = std::min(rect.right - rect.left, workWidth);
    const LONG height = std::min(rect.bottom - rect.top, workHeight);
    const LONG x = work.left + (workWidth - width) / 2;
    const LONG y = work.top + (workHeight - height) / 2;
    return RECT{x, y, x + width, y + height};
}

}  // namespace

bool Window::Create(HINSTANCE instance, const wchar_t* title, float widthDip, float heightDip) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &Window::Thunk;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // Sin pincel de fondo: el contenido es del compositor. Un pincel aquí pintaría un
    // rectángulo opaco por debajo y taparía la Mica, que es exactamente el destello
    // blanco que el criterio de aceptación prohíbe.
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;

    const UINT dpi = GetDpiForSystem();
    const RECT place = InitialPlacement(widthDip, heightDip, dpi);

    // WS_EX_NOREDIRECTIONBITMAP, y es una corrección MEDIDA, no una precaución.
    //
    // Sin él, la ventana tiene superficie de redirección: el sitio donde Windows guarda lo
    // que pinta GDI. Nosotros no pintamos nada ahí, pero la superficie existe igual y sale
    // BLANCA OPACA, así que tapa la Mica entera. Medido: el cuerpo daba (255,255,255) con
    // Windows en oscuro. Se probó antes lo que manda el manual clásico —extender el marco
    // a toda la ventana con márgenes de -1— y no cambia nada, porque ese truco es de la
    // época de Aero y necesita que el cliente se pinte de NEGRO para que DWM lo tome como
    // cristal; blanco es blanco. Con esta bandera no hay superficie que tapar: lo único
    // que se ve es el árbol de composición, y por debajo, la Mica.
    //
    // Sin WS_VISIBLE: la ventana no se enseña hasta que el árbol de composición tiene su
    // primer fotograma dibujado. Enseñarla antes deja ver un fotograma vacío.
    if (!CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, kClassName, title, kStyle, place.left, place.top,
                         place.right - place.left, place.bottom - place.top, nullptr, nullptr,
                         instance, this)) {
        return false;
    }
    return true;
}

void Window::Show(int showCommand) {
    ShowWindow(m_hwnd, showCommand);
    SetForegroundWindow(m_hwnd);
}

int Window::Run() {
    // Bucle bloqueante y punto: los fotogramas los pone el compositor en el proceso de
    // DWM, así que este hilo no tiene que despertarse para animar nada. Rayo necesita un
    // MsgWaitForMultipleObjectsEx porque repinta él; aquí sería un temporizador sin nadie
    // al otro lado.
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK Window::Thunk(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    Window* self = nullptr;
    if (message == WM_NCCREATE) {
        self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->Proc(message, wparam, lparam)
                : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT Window::OnNcCalcSize(WPARAM wparam, LPARAM lparam) {
    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
    const RECT original = params->rgrc[0];

    // Primero que Windows calcule lo suyo: así seguimos teniendo los bordes de
    // redimensionado y las sombras del sistema, que es lo que se pierde al devolver 0 a
    // secas (que es lo que hacen la isla y el HUD, pero ellos son ventanas sin marco).
    const LRESULT result = DefWindowProcW(m_hwnd, WM_NCCALCSIZE, wparam, lparam);
    if (result != 0) return result;

    RECT& client = params->rgrc[0];

    // Y ahora recuperamos la franja del título para el contenido. Esto es todo el truco
    // de la barra de título integrada.
    client.top = original.top;

    if (IsZoomed(m_hwnd)) {
        // Maximizada, Windows coloca la ventana un marco por fuera de la pantalla para
        // que los bordes queden ocultos. Sin compensarlo, la primera fila de píxeles del
        // contenido —la barra de título entera— se va fuera y deja de poder pulsarse.
        const UINT dpi = GetDpiForWindow(m_hwnd);
        client.top += GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) +
                      GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    }
    return 0;
}

LRESULT Window::OnNcHitTest(LPARAM lparam) {
    POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    ScreenToClient(m_hwnd, &point);

    const Caption::Zone zone =
        Caption::HitTest(m_caption, Dpi::ToDip(static_cast<float>(point.x), m_scale),
                         Dpi::ToDip(static_cast<float>(point.y), m_scale));
    return ToHitTest(zone);
}

void Window::OnNcMouseMove(WPARAM wparam) {
    if (!m_trackingNc) {
        // TME_NONCLIENT además de TME_LEAVE: sin él no llega WM_NCMOUSELEAVE y el botón
        // se queda resaltado para siempre cuando el ratón sale por arriba.
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_NONCLIENT, m_hwnd, 0};
        TrackMouseEvent(&track);
        m_trackingNc = true;
    }

    const Caption::Zone zone = FromHitTest(wparam);
    // Si el botón izquierdo ya no está pulsado, no hay nada pulsado. DefWindowProc lleva
    // su propio bucle de seguimiento mientras se mantiene, y de ahí no nos llegan los
    // WM_NCLBUTTONUP.
    const bool down = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
    SetCaptionState(zone, down ? m_pressed : Caption::Zone::Client);
}

void Window::SetCaptionState(Caption::Zone hovered, Caption::Zone pressed) {
    if (m_hovered == hovered && m_pressed == pressed) return;
    m_hovered = hovered;
    m_pressed = pressed;
    if (callbacks.onCaptionState) callbacks.onCaptionState();
}

void Window::UpdateLayout() {
    RECT client{};
    if (!GetClientRect(m_hwnd, &client)) return;

    m_scale = Dpi::ScaleFor(GetDpiForWindow(m_hwnd));
    m_widthDip = Dpi::ToDip(static_cast<float>(client.right - client.left), m_scale);
    m_heightDip = Dpi::ToDip(static_cast<float>(client.bottom - client.top), m_scale);
    m_caption = Caption::Compute(m_widthDip, m_heightDip, IsZoomed(m_hwnd) != FALSE);

    if (callbacks.onLayout) callbacks.onLayout(m_widthDip, m_heightDip, m_scale);
}

LRESULT Window::Proc(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        Backdrop::ExtendFrame(m_hwnd);
        m_mica = Backdrop::Apply(m_hwnd);
        // Obliga a recalcular el marco ahora que ya somos nosotros quien lo decide.
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateLayout();
        return 0;

    case WM_NCCALCSIZE:
        if (wparam == FALSE) break;
        return OnNcCalcSize(wparam, lparam);

    case WM_NCHITTEST:
        return OnNcHitTest(lparam);

    case WM_NCMOUSEMOVE:
        OnNcMouseMove(wparam);
        // Sin return: DefWindowProc necesita verlo para el menú de ajuste.
        break;

    case WM_NCMOUSELEAVE:
        m_trackingNc = false;
        SetCaptionState(Caption::Zone::Client, Caption::Zone::Client);
        break;

    case WM_NCLBUTTONDOWN: {
        const Caption::Zone zone = FromHitTest(wparam);
        if (zone != Caption::Zone::Client) SetCaptionState(zone, zone);
        // Y se deja pasar: minimizar, maximizar y cerrar los ejecuta DefWindowProc con el
        // comportamiento nativo entero —incluido soltar fuera para cancelar—, que es
        // justo lo que pide la fase. Nosotros solo dibujamos.
        break;
    }

    case WM_NCLBUTTONUP:
        SetCaptionState(m_hovered, Caption::Zone::Client);
        break;

    case WM_MOUSEMOVE:
        // Entrar al cliente apaga cualquier resalte de la barra de título.
        SetCaptionState(Caption::Zone::Client, Caption::Zone::Client);
        return 0;

    // WM_LBUTTONDBLCLK junto al DOWN, y salió probando: la clase lleva CS_DBLCLKS para
    // que el doble clic en la barra de título maximice, pero eso hace que el SEGUNDO
    // clic rápido en el contenido llegue como DBLCLK y no como DOWN. Sin esta línea,
    // pulsar dos veces seguidas en una tarjeta cuenta una sola vez.
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN:
        if (callbacks.onPointerDown) {
            callbacks.onPointerDown(Dpi::ToDip(static_cast<float>(GET_X_LPARAM(lparam)), m_scale),
                                    Dpi::ToDip(static_cast<float>(GET_Y_LPARAM(lparam)), m_scale));
        }
        return 0;

    case WM_LBUTTONUP:
        if (callbacks.onPointerUp) {
            callbacks.onPointerUp(Dpi::ToDip(static_cast<float>(GET_X_LPARAM(lparam)), m_scale),
                                  Dpi::ToDip(static_cast<float>(GET_Y_LPARAM(lparam)), m_scale));
        }
        return 0;

    case WM_KEYDOWN:
        if (callbacks.onKeyDown) callbacks.onKeyDown(static_cast<int>(wparam));
        return 0;

    case WM_SIZE:
        UpdateLayout();
        return 0;

    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateLayout();
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        const float scale = m_hwnd ? Dpi::ScaleFor(GetDpiForWindow(m_hwnd)) : 1.0f;
        info->ptMinTrackSize.x = Dpi::ToPixels(kMinWidthDip, scale);
        info->ptMinTrackSize.y = Dpi::ToPixels(kMinHeightDip, scale);
        return 0;
    }

    case kThemeMessage:
        if (callbacks.onTheme) callbacks.onTheme();
        return 0;

    case WM_SETTINGCHANGE:
        if (wparam == SPI_SETCLIENTAREAANIMATION) {
            if (callbacks.onMotionSetting) callbacks.onMotionSetting();
        } else if (lparam && CompareStringOrdinal(reinterpret_cast<const wchar_t*>(lparam), -1,
                                                  L"ImmersiveColorSet", -1,
                                                  TRUE) == CSTR_EQUAL) {
            if (callbacks.onTheme) callbacks.onTheme();
        }
        break;

    // Nada de GDI. Las dos juntas son media promesa de "sin destellos": ni borramos el
    // fondo ni pintamos nada, así que no hay un solo fotograma con píxeles que no sean
    // del compositor o de la Mica.
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        ValidateRect(m_hwnd, nullptr);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(m_hwnd, message, wparam, lparam);
}

}  // namespace Shell
