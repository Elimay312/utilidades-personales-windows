using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.UI.Input.KeyboardAndMouse;
using Windows.Win32.UI.WindowsAndMessaging;

namespace QuickLook;

/// <summary>
/// El unico sitio del programa que ve una tecla. Ver SEGURIDAD.md §3.1.
///
/// <para>
/// <b>Por que un hook y no RegisterHotKey.</b> Sin modificador, RegisterHotKey reserva la
/// barra espaciadora en TODO el sistema mientras el proceso vive: escribir deja de
/// funcionar en todas partes. Registrarla y liberarla segun quien este delante obliga a
/// sondear con un temporizador y sigue comiendose el espacio al renombrar con F2. Con
/// modificador esto ya no es Quick Look.
/// </para>
///
/// <para>
/// <b>Por que no es inyeccion.</b> WH_KEYBOARD_LL no carga ninguna DLL en ningun proceso:
/// la doc dice que el callback "is called in the context of the thread that installed it",
/// mandandole un mensaje a ese hilo. De ahi que HostWindow exista y tenga bucle de
/// mensajes.
/// </para>
///
/// <para>
/// <b>La trampa.</b> Windows desinstala en silencio un hook de bajo nivel que tarde mas de
/// LowLevelHooksTimeout (~300 ms por defecto) en contestar. Por eso el callback no abre
/// nada: hace PostMessage y devuelve. Todo el trabajo ocurre en el WndProc de HostWindow.
/// </para>
/// </summary>
internal sealed unsafe class Hook : IDisposable
{
    private const uint WM_KEYDOWN = 0x0100;
    private const uint WM_SYSKEYDOWN = 0x0104;

    // Si el foco esta en uno de estos, el espacio pasa de largo: renombrar con F2, la
    // caja de busqueda y la barra de direcciones son campos de texto y el usuario esta
    // escribiendo. Sin esto el programa hace el Explorador inusable.
    private static readonly string[] Editing = ["Edit", "ComboBox", "ComboBoxEx32", "RichEditD2DPT", "SearchEditBoxWrapperClass"];

    private static readonly HOOKPROC Thunk = Callback;

    private static HWND _host;

    /// <summary>
    /// Si hay un panel abierto ahora mismo. Lo pone y lo quita <c>HostWindow</c>, que es
    /// quien lo abre y lo cierra.
    ///
    /// <para>
    /// Es lo que acota la segunda tecla: <c>Esc</c> solo se mira mientras esto es true, o
    /// sea el rato que tienes un panel delante. El resto del tiempo —que es casi todo— la
    /// tecla sale por <c>CallNextHookEx</c> sin que nadie la compare, igual que cualquier
    /// otra. Ver SEGURIDAD.md §3.1, enmienda 2.
    /// </para>
    /// </summary>
    internal static volatile bool PanelOpen;

    private HHOOK _handle;

    public Hook(HWND host)
    {
        _host = host;

        // hMod es el modulo propio: con WH_KEYBOARD_LL el hook se resuelve aqui dentro,
        // no se carga en ningun sitio.
        _handle = PInvoke.SetWindowsHookEx(
            WINDOWS_HOOK_ID.WH_KEYBOARD_LL, Thunk, PInvoke.GetModuleHandle((PCWSTR)null), 0);

        if (_handle.IsNull) throw new InvalidOperationException("no se pudo instalar el hook de teclado");
    }

    private static LRESULT Callback(int code, WPARAM wParam, LPARAM lParam)
    {
        // Todo lo que no sea una pulsacion sale por aqui sin que nadie lo mire.
        if (code < 0) return Next(code, wParam, lParam);

        uint message = (uint)wParam.Value;
        if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN) return Next(code, wParam, lParam);

        KBDLLHOOKSTRUCT* key = (KBDLLHOOKSTRUCT*)lParam.Value;

        bool space = key->vkCode == (uint)VIRTUAL_KEY.VK_SPACE;
        bool escape = PanelOpen && key->vkCode == (uint)VIRTUAL_KEY.VK_ESCAPE;
        if (!space && !escape) return Next(code, wParam, lParam);

        // Ctrl+Espacio, Alt+Espacio y Shift+Espacio son atajos de otros: no son nuestros.
        if (Down(VIRTUAL_KEY.VK_CONTROL) || Down(VIRTUAL_KEY.VK_MENU) || Down(VIRTUAL_KEY.VK_SHIFT))
            return Next(code, wParam, lParam);

        // La lista de clases del Explorador vive en Foreground.cs porque la comparten el
        // hook, el cierre automatico del panel y Selection. Es el cortafuegos del §3.1.
        HWND front = PInvoke.GetForegroundWindow();
        if (!Foreground.IsExplorer(front) || Typing(front)) return Next(code, wParam, lParam);

        // Esc manda su propio mensaje y no el interruptor: entre esta comprobacion y el
        // mensaje el panel puede haberse cerrado solo, y un interruptor ABRIRIA uno nuevo.
        // Pulsar Esc y que aparezca un panel seria de las cosas mas raras posibles.
        PInvoke.PostMessage(
            _host, escape ? HostWindow.WM_APP_CLOSE : HostWindow.WM_APP_QUICKLOOK, default, default);

        // 1 se come la tecla: el Explorador no la llega a ver.
        return new LRESULT(1);
    }

    private static LRESULT Next(int code, WPARAM wParam, LPARAM lParam)
        => PInvoke.CallNextHookEx(null, code, wParam, lParam);

    private static bool Down(VIRTUAL_KEY key) => (PInvoke.GetKeyState((int)key) & 0x8000) != 0;

    /// <summary>Hay un campo de texto con el foco en esa ventana.</summary>
    private static bool Typing(HWND window)
    {
        GUITHREADINFO info = new() { cbSize = (uint)sizeof(GUITHREADINFO) };
        if (!PInvoke.GetGUIThreadInfo(PInvoke.GetWindowThreadProcessId(window, null), ref info)) return false;

        // Un cursor de texto parpadeando es la senal mas fiable de que se esta
        // escribiendo, y no depende de acertar con el nombre de la clase.
        return !info.hwndCaret.IsNull || Foreground.Is(info.hwndFocus, Editing);
    }

    public void Dispose()
    {
        if (_handle.IsNull) return;

        PInvoke.UnhookWindowsHookEx(_handle);
        _handle = default;
        _host = default;
    }
}
