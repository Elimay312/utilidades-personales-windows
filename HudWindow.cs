using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.UI.HiDpi;
using Windows.Win32.UI.Input.KeyboardAndMouse;
using Windows.Win32.UI.WindowsAndMessaging;

namespace Hud;

/// <summary>
/// La ventana del HUD: el HWND, su WndProc y los atajos. Es el fichero grande del
/// proyecto y esta bien asi -- es una ventana con su bucle de mensajes, y partirla por
/// partirla solo aniadiria saltos.
///
/// <para>
/// H0: la ventana se crea pero <b>no se ensena</b>. Todavia no hay nada que pintar, y
/// una ventana vacia encima de todo es peor que ninguna. Lo que si se comprueba en este
/// hito es que se coloca donde debe, en el monitor del cursor y a la escala correcta.
/// </para>
/// </summary>
internal sealed unsafe class HudWindow
{
    private const string ClassName = "HudVolumenBrillo";

    // El numero es el articulo de SEGURIDAD.md §3.1. Los de volumen llegan en H2.
    private const int AtajoSalir = 1;

    // --- mensajes que nos interesan ------------------------------------------------
    // CsWin32 no genera las WM_*, y tenerlas aqui con su valor es mas legible que
    // buscarlas en winuser.h cada vez.
    private const uint WM_DESTROY = 0x0002;
    private const uint WM_NCCALCSIZE = 0x0083;
    private const uint WM_NCACTIVATE = 0x0086;
    private const uint WM_WINDOWPOSCHANGING = 0x0046;
    private const uint WM_MOUSEACTIVATE = 0x0021;
    private const uint WM_DISPLAYCHANGE = 0x007E;
    private const uint WM_DPICHANGED = 0x02E0;
    private const uint WM_HOTKEY = 0x0312;

    private const int MA_NOACTIVATE = 3;
    private static readonly HWND HWND_TOPMOST = new(-1);

    // --- medidas, en px logicos (a 96 dpi) -----------------------------------------
    // La capsula es lo que se ve; la holgura es el margen invisible que necesita el
    // squash del tope (morph 5) para no quedarse recortado contra el borde de la
    // ventana. La region de H1 recorta sobre la capsula, no sobre la ventana.
    private const float AnchoCapsula = 280f;
    private const float AltoCapsula = 68f;
    private const float Holgura = 24f;

    /// <summary>Separacion del borde de la pantalla. 110 deja libre la barra de tareas.</summary>
    private const float Margen = 110f;

    private static readonly WNDPROC WndProcThunk = WndProc;
    private static ushort _classAtom;
    private static HudWindow? _instancia;

    private readonly HudConfig _config;
    private readonly HWND _hwnd;
    private uint _dpi;
    private bool _atajoSalir;

    private HudWindow(HudConfig config)
    {
        _config = config;

        (int x, int y, int w, int h) = MedirEnElMonitorDelCursor(out _dpi, out string pantalla);

        EnsureClassRegistered();

        fixed (char* clase = ClassName)
        fixed (char* titulo = "Hud")
        {
            _hwnd = PInvoke.CreateWindowEx(
                // NOACTIVATE: nunca roba el foco, que en un HUD que sale mientras
                // escribes no es un detalle. TOOLWINDOW: fuera de Alt+Tab y de la barra
                // de tareas. TOPMOST: encima de todo, que es el sitio de un aviso.
                //
                // Aqui NO va WS_EX_TRANSPARENT: la isla midio que NO deja pasar los
                // clics entre procesos. Lo unico que aparta el raton es la region, y
                // llega en H1 junto con lo que hay que recortar.
                WINDOW_EX_STYLE.WS_EX_NOACTIVATE
                    | WINDOW_EX_STYLE.WS_EX_TOOLWINDOW
                    | WINDOW_EX_STYLE.WS_EX_TOPMOST,
                new PCWSTR(clase), new PCWSTR(titulo),
                WINDOW_STYLE.WS_POPUP,
                x, y, w, h,
                default, default, ModuleHandle, null);
        }

        if (_hwnd.IsNull) throw new InvalidOperationException("CreateWindowEx fallo");
        _instancia = this;

        // Ctrl+Alt+H para salir. Mientras no haya icono de bandeja es la unica forma
        // limpia de cerrar el HUD, y el cierre limpio importa aqui mas que en el dock:
        // es lo que devuelve el flyout nativo a su sitio (SEGURIDAD.md §3.5).
        _atajoSalir = PInvoke.RegisterHotKey(_hwnd, AtajoSalir,
            HOT_KEY_MODIFIERS.MOD_CONTROL | HOT_KEY_MODIFIERS.MOD_ALT, 'H');
        if (!_atajoSalir)
        {
            // Un atajo global que falla en silencio es media hora perdida. El dock tiene
            // Ctrl+Alt+D y la isla Ctrl+Alt+I, asi que hay vecinos compitiendo.
            Console.Error.WriteLine("[hud] Ctrl+Alt+H ya esta cogido; se cierra desde el Administrador de tareas.");
        }

        Console.WriteLine($"[hud] {pantalla} al {_dpi * 100 / 96}%, ventana {w}x{h} en {x},{y}");
        Console.WriteLine($"[hud] capsula {_config.Posicion}, paso {_config.PasoVolumen}%, {_config.MsAutoocultar} ms");
    }

    public static HudWindow? Create(HudConfig config)
    {
        try
        {
            return new HudWindow(config);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[hud] {ex.Message}");
            return null;
        }
    }

    // --- donde va la ventana ---------------------------------------------------------

    /// <summary>
    /// El rectangulo de la ventana dentro de un monitor, en pixeles fisicos. Es logica
    /// pura a proposito: no toca Win32, asi que <c>--check</c> la puede comprobar a
    /// 150% y en un monitor secundario sin necesitar ese monitor.
    ///
    /// <para>
    /// Se centra sobre <c>rcMonitor</c> y no sobre <c>rcWork</c>: el HUD flota por
    /// encima de lo que haya reservado cualquier otro, incluida la barra de tareas.
    /// </para>
    /// </summary>
    internal static (int X, int Y, int W, int H) Colocar(
        int mLeft, int mTop, int mRight, int mBottom, uint dpi, bool abajo)
    {
        float escala = dpi / 96f;
        int w = (int)MathF.Round((AnchoCapsula + 2 * Holgura) * escala);
        int h = (int)MathF.Round((AltoCapsula + 2 * Holgura) * escala);
        int margen = (int)MathF.Round(Margen * escala);

        int x = mLeft + (mRight - mLeft - w) / 2;
        int y = abajo ? mBottom - margen - h : mTop + margen;
        return (x, y, w, h);
    }

    private (int X, int Y, int W, int H) MedirEnElMonitorDelCursor(out uint dpi, out string pantalla)
    {
        // El monitor del cursor, no el primario: un HUD que sale en otra pantalla es un
        // HUD que no ves. GetCursorPos devuelve un punto y nada mas (SEGURIDAD.md §3.4
        // del dock, misma justificacion).
        PInvoke.GetCursorPos(out System.Drawing.Point cursor);
        HMONITOR monitor = PInvoke.MonitorFromPoint(cursor, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTONEAREST);

        PInvoke.GetDpiForMonitor(monitor, MONITOR_DPI_TYPE.MDT_EFFECTIVE_DPI, out uint dpiX, out _);
        dpi = dpiX == 0 ? 96 : dpiX;

        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        if (!PInvoke.GetMonitorInfo(monitor, &info)) throw new InvalidOperationException("sin monitor");

        pantalla = $"monitor {info.rcMonitor.right - info.rcMonitor.left}x{info.rcMonitor.bottom - info.rcMonitor.top}";
        return Colocar(info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom,
            dpi, _config.Abajo);
    }

    /// <summary>
    /// Vuelve a medir y se mueve. Se llama al cambiar de pantallas o de escala, y en H1
    /// pasara a llamarse tambien cada vez que el HUD se ensene, para seguir al cursor.
    /// </summary>
    private void Recolocar()
    {
        (int x, int y, int w, int h) = MedirEnElMonitorDelCursor(out _dpi, out _);
        PInvoke.SetWindowPos(_hwnd, HWND_TOPMOST, x, y, w, h,
            SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE);
    }

    // --- mensajes ------------------------------------------------------------------

    private static LRESULT WndProc(HWND hwnd, uint msg, WPARAM wParam, LPARAM lParam)
    {
        HudWindow? hud = _instancia;

        switch (msg)
        {
            // WS_EX_NOACTIVATE por si solo NO basta: al hacer clic llegan
            // WM_ACTIVATE(WA_CLICKACTIVE) y WM_SETFOCUS igualmente. MA_NOACTIVATE
            // rechaza la activacion sin descartar el clic.
            case WM_MOUSEACTIVATE:
                return new LRESULT(MA_NOACTIVATE);

            // Hay que decirle a DWM que la ventana esta activa aunque nunca lo este. Si
            // no, deja de pintar el acrilico y el HUD se vuelve invisible mientras sigue
            // recibiendo mensajes. Medido en el dock, y cuesta un dia encontrarlo.
            case WM_NCACTIVATE:
                return PInvoke.DefWindowProc(hwnd, msg, new WPARAM(1), lParam);

            // Colapsa el area no cliente: la ventana es toda cliente.
            case WM_NCCALCSIZE:
                return new LRESULT(0);

            // Windows saca la ventana de la banda topmost sin quitar el bit del estilo.
            case WM_WINDOWPOSCHANGING:
                ((WINDOWPOS*)lParam.Value)->hwndInsertAfter = HWND_TOPMOST;
                break;

            case WM_HOTKEY:
                if (wParam.Value == AtajoSalir) Salir();
                return new LRESULT(0);

            // Llega al enchufar y desenchufar pantallas. Un cambio de escala invalida
            // todas las medidas en pixeles, asi que se vuelve a medir.
            case WM_DISPLAYCHANGE:
            case WM_DPICHANGED:
                hud?.Recolocar();
                return new LRESULT(0);

            case WM_DESTROY:
                PInvoke.PostQuitMessage(0);
                return new LRESULT(0);
        }

        return PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);
    }

    public static void RunMessageLoop()
    {
        while (PInvoke.GetMessage(out MSG msg, default, 0, 0).Value > 0)
        {
            PInvoke.TranslateMessage(in msg);
            PInvoke.DispatchMessage(in msg);
        }
    }

    public static void Salir()
    {
        HudWindow? hud = _instancia;
        if (hud is null) return;

        if (hud._atajoSalir)
        {
            PInvoke.UnregisterHotKey(hud._hwnd, AtajoSalir);
            hud._atajoSalir = false;
        }

        PInvoke.DestroyWindow(hud._hwnd);
        _instancia = null;
    }

    private static HMODULE ModuleHandle => PInvoke.GetModuleHandle((PCWSTR)null);

    private static void EnsureClassRegistered()
    {
        if (_classAtom != 0) return;

        fixed (char* clase = ClassName)
        {
            WNDCLASSEXW wc = new()
            {
                cbSize = (uint)System.Runtime.InteropServices.Marshal.SizeOf<WNDCLASSEXW>(),
                lpfnWndProc = WndProcThunk,
                hInstance = ModuleHandle,
                lpszClassName = new PCWSTR(clase),
                hCursor = PInvoke.LoadCursor(default, PInvoke.IDC_ARROW),
                // Sin pincel de fondo: el contenido es del compositor. Un pincel aqui
                // pintaria un rectangulo opaco por debajo de la capsula.
                hbrBackground = default,
            };
            _classAtom = PInvoke.RegisterClassEx(wc);
        }

        if (_classAtom == 0) throw new InvalidOperationException("RegisterClassEx fallo");
    }

    // --- autocomprobacion -------------------------------------------------------------

    /// <summary>
    /// Lo que se rompe en silencio: la colocacion. Un HUD tres pixeles descentrado no
    /// falla, solo se ve mal, y eso no lo detecta nadie hasta que lleva un mes asi.
    /// </summary>
    public static void SelfCheck()
    {
        // 1920x1080 al 100%. Capsula 280+48 de holgura = 328 de ancho, 68+48 = 116.
        var a = Colocar(0, 0, 1920, 1080, 96, abajo: true);
        Assert(a == (796, 854, 328, 116), $"1080p al 100% -> {a}");

        // Al 150% todo escala, incluido el margen: 110*1.5 = 165.
        var b = Colocar(0, 0, 1920, 1080, 144, abajo: true);
        Assert(b == (714, 741, 492, 174), $"1080p al 150% -> {b}");

        // Monitor secundario a la derecha: las coordenadas son del escritorio virtual,
        // asi que la x lleva el offset del monitor y no empieza en cero.
        var c = Colocar(1920, 0, 3840, 1080, 96, abajo: true);
        Assert(c.X == 1920 + 796 && c.Y == 854, $"secundario -> {c}");

        // Un monitor a la IZQUIERDA del primario tiene coordenadas negativas. Es el
        // caso que rompe cualquier calculo escrito suponiendo que la pantalla empieza
        // en 0, y aqui hay dos pantallas de las tres en negativo.
        var d = Colocar(-1920, 0, 0, 1080, 96, abajo: true);
        Assert(d.X == -1920 + 796, $"a la izquierda -> {d}");

        // Arriba mide igual y solo cambia la y, que cuelga del borde superior.
        var e = Colocar(0, 0, 1920, 1080, 96, abajo: false);
        Assert(e == (796, 110, 328, 116), $"arriba -> {e}");

        // Y no se sale de la pantalla ni en el monitor mas pequenio que admite Windows.
        var f = Colocar(0, 0, 800, 600, 96, abajo: true);
        Assert(f.X >= 0 && f.Y >= 0 && f.X + f.W <= 800 && f.Y + f.H <= 600, $"800x600 -> {f}");

        Console.WriteLine("[hud] colocacion: 6 comprobaciones OK");
    }

    private static void Assert(bool condicion, string queFallo)
    {
        if (!condicion) throw new InvalidOperationException($"colocacion: {queFallo}");
    }
}
