#include "shell/Window.h"

#include <imm.h>
#include <windowsx.h>

#include <algorithm>

#include "shell/Backdrop.h"
#include <cstdint>

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

void Window::TrackClientLeave() {
    if (m_trackingClient) return;
    // Solo TME_LEAVE esta vez: el seguimiento de la barra de título lo lleva
    // OnNcMouseMove con su propia bandera y con TME_NONCLIENT.
    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, m_hwnd, 0};
    TrackMouseEvent(&track);
    m_trackingClient = true;
}

Input::Modifiers Window::CurrentModifiers() const {
    Input::Modifiers modifiers = Input::Modifiers::None;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= Input::Modifiers::Shift;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= Input::Modifiers::Control;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= Input::Modifiers::Alt;
    return modifiers;
}

void Window::Emit(Input::Action action, Input::Button button, LPARAM lparam, int clicks) {
    if (!callbacks.onPointer) return;
    Input::Pointer pointer;
    pointer.action = action;
    pointer.button = button;
    pointer.x = Dpi::ToDip(static_cast<float>(GET_X_LPARAM(lparam)), m_scale);
    pointer.y = Dpi::ToDip(static_cast<float>(GET_Y_LPARAM(lparam)), m_scale);
    pointer.clicks = clicks;
    pointer.modifiers = CurrentModifiers();
    callbacks.onPointer(pointer);
}

void Window::EmitWheel(bool horizontal, int delta, POINT clientPx) {
    if (!callbacks.onPointer) return;
    Input::Pointer pointer;
    pointer.action = Input::Action::Wheel;
    pointer.x = Dpi::ToDip(static_cast<float>(clientPx.x), m_scale);
    pointer.y = Dpi::ToDip(static_cast<float>(clientPx.y), m_scale);
    // Las unidades crudas de Windows, sin tocar: las 120 por muesca y las líneas por
    // muesca las traduce Ui::Wheel, que está probado y sabe guardar el resto que manda un
    // panel táctil de precisión.
    if (horizontal) {
        pointer.wheelX = static_cast<float>(delta);
    } else {
        pointer.wheelY = static_cast<float>(delta);
    }
    pointer.modifiers = CurrentModifiers();
    callbacks.onPointer(pointer);
}

void Window::PlaceImeWindow() {
    if (!callbacks.onCaretRect) return;

    float x = 0.0f;
    float y = 0.0f;
    float height = 0.0f;
    if (!callbacks.onCaretRect(x, y, height)) return;

    const HIMC context = ImmGetContext(m_hwnd);
    if (!context) return;

    // La ventana de composición justo en el cursor, y la de candidatos debajo sin tapar
    // la línea que se está escribiendo.
    COMPOSITIONFORM form{};
    form.dwStyle = CFS_POINT;
    form.ptCurrentPos.x = Dpi::ToPixels(x, m_scale);
    form.ptCurrentPos.y = Dpi::ToPixels(y, m_scale);
    ImmSetCompositionWindow(context, &form);

    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = form.ptCurrentPos;
    candidate.rcArea.left = form.ptCurrentPos.x;
    candidate.rcArea.top = form.ptCurrentPos.y;
    candidate.rcArea.right = form.ptCurrentPos.x + 1;
    candidate.rcArea.bottom = form.ptCurrentPos.y + Dpi::ToPixels(height, m_scale);
    ImmSetCandidateWindow(context, &candidate);

    ImmReleaseContext(m_hwnd, context);
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
        TrackClientLeave();
        Emit(Input::Action::Move, Input::Button::None, lparam);
        return 0;

    case WM_MOUSELEAVE:
        m_trackingClient = false;
        if (callbacks.onPointer) {
            Input::Pointer pointer;
            pointer.action = Input::Action::Leave;
            callbacks.onPointer(pointer);
        }
        return 0;

    // WM_LBUTTONDBLCLK junto al DOWN, y salió probando: la clase lleva CS_DBLCLKS para
    // que el doble clic en la barra de título maximice, pero eso hace que el SEGUNDO
    // clic rápido en el contenido llegue como DBLCLK y no como DOWN. Sin esta línea,
    // pulsar dos veces seguidas en una tarjeta cuenta una sola vez.
    //
    // Los dos emiten un Down normal; lo que los distingue es el contador de clics, que
    // llevamos nosotros porque Windows solo sabe contar hasta dos y el campo de texto
    // necesita el triple para seleccionar la línea entera.
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN: {
        SetFocus(m_hwnd);
        // Arrastrar fuera del control tiene que seguir llegando: sin captura, seleccionar
        // texto se corta en cuanto el puntero se sale del campo.
        SetCapture(m_hwnd);
        const LONG when = GetMessageTime();
        const int clicks =
            m_clicks.Count(static_cast<std::uint64_t>(when < 0 ? 0 : when),
                           Dpi::ToDip(static_cast<float>(GET_X_LPARAM(lparam)), m_scale),
                           Dpi::ToDip(static_cast<float>(GET_Y_LPARAM(lparam)), m_scale));
        Emit(Input::Action::Down, Input::Button::Left, lparam, clicks);
        return 0;
    }

    case WM_LBUTTONUP:
        if (GetCapture() == m_hwnd) ReleaseCapture();
        Emit(Input::Action::Up, Input::Button::Left, lparam);
        return 0;

    case WM_RBUTTONDOWN:
        SetFocus(m_hwnd);
        Emit(Input::Action::Down, Input::Button::Right, lparam);
        return 0;

    case WM_RBUTTONUP:
        Emit(Input::Action::Up, Input::Button::Right, lparam);
        return 0;

    // El que todo el mundo olvida. Un Alt+Tab a mitad de una pulsación roba la captura y
    // el control se queda hundido para siempre.
    case WM_CAPTURECHANGED:
        if (callbacks.onPointer) {
            Input::Pointer pointer;
            pointer.action = Input::Action::Cancel;
            callbacks.onPointer(pointer);
        }
        return 0;

    case WM_CONTEXTMENU: {
        const bool keyboard = lparam == -1;
        POINT point{};
        if (!keyboard) {
            point = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(m_hwnd, &point);
        }
        if (callbacks.onContextMenu) {
            callbacks.onContextMenu(Dpi::ToDip(static_cast<float>(point.x), m_scale),
                                    Dpi::ToDip(static_cast<float>(point.y), m_scale), keyboard);
        }
        return 0;
    }

    // Ojo: la rueda llega en coordenadas de PANTALLA, al revés que los botones.
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(m_hwnd, &point);
        EmitWheel(message == WM_MOUSEHWHEEL, GET_WHEEL_DELTA_WPARAM(wparam), point);
        return 0;
    }

    // lparam no trae las coordenadas: hay que preguntarlas. Y llega en cada movimiento,
    // así que el cursor hay que volver a ponerlo cada vez.
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT && callbacks.onSetCursor) {
            POINT point{};
            if (GetCursorPos(&point) && ScreenToClient(m_hwnd, &point)) {
                if (callbacks.onSetCursor(Dpi::ToDip(static_cast<float>(point.x), m_scale),
                                          Dpi::ToDip(static_cast<float>(point.y), m_scale))) {
                    return TRUE;
                }
            }
        }
        // Sin return: los bordes de redimensionado conservan sus flechas.
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        Input::Key key;
        key.virtualKey = static_cast<int>(wparam);
        key.down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        key.repeat = (lparam & 0x40000000) != 0;
        key.system = message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
        key.modifiers = CurrentModifiers();
        if (callbacks.onKey && callbacks.onKey(key)) return 0;
        // Si nadie la quiso, que la vea Windows: ahí viven Alt+F4 y Alt+Espacio.
        break;
    }

    // TranslateMessage ya compuso la tecla muerta: el acento y luego la «a» llegan aquí
    // como un solo WM_CHAR con U+00E1, y la eñe llega directa. Lo único que hay que hacer
    // es no dejar pasar los controles: Ctrl+V manda un 0x16 por aquí además de su
    // WM_KEYDOWN, y sin este filtro se escribe un carácter invisible en el campo.
    case WM_CHAR:
        if (wparam >= 0x20 && wparam != 0x7F && callbacks.onChar) {
            callbacks.onChar(static_cast<wchar_t>(wparam));
        }
        return 0;

    // Anunciar que hablamos Unicode. Tres líneas y evita la ruta ANSI.
    case WM_UNICHAR:
        if (wparam == UNICODE_NOCHAR) return TRUE;
        if (callbacks.onChar) callbacks.onChar(static_cast<wchar_t>(wparam));
        return 0;

    case WM_SETFOCUS:
        if (callbacks.onWindowFocus) callbacks.onWindowFocus(true);
        return 0;

    case WM_KILLFOCUS:
        if (callbacks.onWindowFocus) callbacks.onWindowFocus(false);
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE && callbacks.onDeactivate) callbacks.onDeactivate();
        break;

    // El IME. Lo único nuestro es colocar su ventana donde se está escribiendo: la cadena
    // confirmada la convierte DefWindowProc en mensajes WM_CHAR, que es por donde ya entra
    // todo lo demás, así que no hay una segunda ruta de texto que mantener.
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
        PlaceImeWindow();
        break;

    case kFlushMessage:
        if (callbacks.onFlush) callbacks.onFlush();
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
