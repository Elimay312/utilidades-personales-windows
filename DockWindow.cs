using System.Runtime.InteropServices;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Dwm;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.UI.Controls;
using Windows.Win32.UI.HiDpi;
using Windows.Win32.UI.Input.KeyboardAndMouse;
using Windows.Win32.UI.WindowsAndMessaging;

namespace Dock;

/// <summary>
/// Una ventana de dock, anclada a un monitor concreto.
/// Se parametriza por monitor desde M0 aunque solo se instancie una: en M4 se crea
/// una por pantalla y así no hay que rehacer nada.
/// </summary>
internal sealed unsafe class DockWindow : IDisposable
{
    private const string ClassName = "DockWindowClass";

    /// Margen vertical del icono dentro del dock, en unidades lógicas.
    private const int LogicalPadding = 12;
    private const int LogicalBottomMargin = 8;

    // Mensajes que manejamos. Se declaran aquí para no arrastrar cientos de
    // constantes desde la metadata del SDK.
    private const uint WM_DESTROY = 0x0002;
    private const uint WM_MOUSEACTIVATE = 0x0021;
    private const uint WM_NCCALCSIZE = 0x0083;
    private const uint WM_NCACTIVATE = 0x0086;
    private const uint WM_MOUSEMOVE = 0x0200;
    private const uint WM_LBUTTONUP = 0x0202;
    private const uint WM_RBUTTONUP = 0x0205;
    private const uint WM_MOUSELEAVE = 0x02A3;
    private const uint WM_DPICHANGED = 0x02E0;


    /// Los iconos terminaron de extraerse en background. WM_APP + 1.
    private const uint WM_APP_ICONS_READY = 0x8001;

    // IDC_ARROW = MAKEINTRESOURCE(32512)
    private const int IdcArrow = 32512;

    // Respuesta a WM_MOUSEACTIVATE: no activar, pero tampoco descartar el clic.
    private const int MA_NOACTIVATE = 3;

    // DWMWA_BORDER_COLOR: quita el borde manteniendo el redondeo.
    private const uint DwmwaColorNone = 0xFFFFFFFE;

    /// El delegate se guarda en un campo estático para que el GC no lo recoja
    /// mientras Windows conserva el puntero dentro de la clase de ventana.
    private static readonly WNDPROC WndProcThunk = WndProc;

    /// Un dock por HWND. En M0 solo hay una entrada; en M4, una por monitor.
    private static readonly Dictionary<nint, DockWindow> Instances = [];

    private static ushort _classAtom;

    private readonly HMONITOR _monitor;
    private readonly DockConfig _config;

    private HWND _hwnd;
    private DockVisuals? _visuals;
    private uint _dpi = 96;
    private bool _trackingMouse;

    /// Apps que sí llegaron a tener icono, en el mismo orden que los visuals.
    private List<(DockApp App, IconBitmap Icon)> _loaded = [];

    public DockWindow(HMONITOR monitor, DockConfig config)
    {
        _monitor = monitor;
        _config = config;

        EnsureClassRegistered();
        Create();
    }

    /// <summary>Monitor principal. En M4 esto lo sustituye EnumDisplayMonitors.</summary>
    public static HMONITOR PrimaryMonitor =>
        PInvoke.MonitorFromPoint(default, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTOPRIMARY);

    private static HINSTANCE ModuleHandle
    {
        get
        {
            HMODULE module = PInvoke.GetModuleHandle((PCWSTR)null);
            return (HINSTANCE)(nint)module;
        }
    }

    private static void EnsureClassRegistered()
    {
        if (_classAtom != 0) return;

        fixed (char* className = ClassName)
        {
            WNDCLASSEXW wc = new()
            {
                // Marshal.SizeOf y no sizeof: WNDCLASSEXW lleva un delegate, así que
                // el compilador la trata como tipo administrado y su sizeof no es el
                // tamaño nativo que espera RegisterClassEx.
                cbSize = (uint)Marshal.SizeOf<WNDCLASSEXW>(),
                lpfnWndProc = WndProcThunk,
                hInstance = ModuleHandle,
                lpszClassName = new PCWSTR(className),
                hCursor = PInvoke.LoadCursor(default, new PCWSTR((char*)IdcArrow)),
                hbrBackground = default,
            };

            _classAtom = PInvoke.RegisterClassEx(in wc);
            if (_classAtom == 0)
                throw new InvalidOperationException($"RegisterClassEx falló: {Marshal.GetLastWin32Error()}");
        }
    }

    private void Create()
    {
        (int x, int y, int w, int h) = ComputeBounds();

        // WS_POPUP a secas. En M0 hizo falta WS_CAPTION para que DWM pintara su
        // backdrop; al pasar el fondo a Composition ese engaño dejó de hacer falta.
        // Si M3 vuelve al backdrop de DWM, habrá que reponer WS_CAPTION.
        const WINDOW_STYLE Style = WINDOW_STYLE.WS_POPUP;

        fixed (char* className = ClassName)
        fixed (char* title = "Dock")
        {
            _hwnd = PInvoke.CreateWindowEx(
                // NOACTIVATE: no roba el foco. TOOLWINDOW: fuera de Alt+Tab y de la
                // barra de tareas. TOPMOST: siempre encima.
                WINDOW_EX_STYLE.WS_EX_NOACTIVATE
                    | WINDOW_EX_STYLE.WS_EX_TOOLWINDOW
                    | WINDOW_EX_STYLE.WS_EX_TOPMOST,
                new PCWSTR(className),
                new PCWSTR(title),
                Style,
                x, y, w, h,
                default, default, ModuleHandle, null);
        }

        if (_hwnd.IsNull)
            throw new InvalidOperationException($"CreateWindowEx falló: {Marshal.GetLastWin32Error()}");

        Instances[(nint)_hwnd.Value] = this;
        ApplyDwmAttributes();

        // Composition pasa a ser dueña del contenido de la ventana. Lo que no pinte
        // queda transparente y deja ver el material acrílico de DWM.
        _visuals = new DockVisuals(_hwnd);

        StartIconLoad();
    }

    private float Scale(int logical) => (float)(logical * _dpi / 96.0);

    /// <summary>Rectángulo del dock en píxeles físicos, centrado abajo en su monitor.</summary>
    private (int X, int Y, int W, int H) ComputeBounds()
    {
        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        PInvoke.GetMonitorInfo(_monitor, &info);

        PInvoke.GetDpiForMonitor(_monitor, MONITOR_DPI_TYPE.MDT_EFFECTIVE_DPI, out uint dpiX, out _);
        _dpi = dpiX;

        int count = Math.Max(_config.Apps.Count, 1);
        int w = (int)(Scale(_config.IconSize) * count + Scale(_config.IconSpacing) * (count + 1));
        int h = (int)(Scale(_config.IconSize) + Scale(LogicalPadding) * 2);

        RECT work = info.rcWork;
        int x = work.left + ((work.right - work.left) - w) / 2;
        int y = work.bottom - h - (int)Scale(LogicalBottomMargin);

        return (x, y, w, h);
    }

    private void ApplyDwmAttributes()
    {
        // Esquinas redondeadas. La doc lo llama un hint, no una garantía.
        DWM_WINDOW_CORNER_PREFERENCE corner = DWM_WINDOW_CORNER_PREFERENCE.DWMWCP_ROUND;
        PInvoke.DwmSetWindowAttribute(_hwnd, DWMWINDOWATTRIBUTE.DWMWA_WINDOW_CORNER_PREFERENCE,
            &corner, (uint)sizeof(DWM_WINDOW_CORNER_PREFERENCE));

        // Borde invisible, manteniendo el redondeo.
        uint border = DwmwaColorNone;
        PInvoke.DwmSetWindowAttribute(_hwnd, DWMWINDOWATTRIBUTE.DWMWA_BORDER_COLOR,
            &border, sizeof(uint));

        // De momento no se pide backdrop a DWM: el fondo lo pinta Composition, que
        // es la dueña del contenido de la ventana. DWM solo redondea las esquinas,
        // que es geometría y no contenido.
        //
        // El acrílico de verdad es trabajo de M3, y entonces habrá que elegir entre
        // DWMWA_SYSTEMBACKDROP_TYPE (que en M0 funcionó, pero obliga a WS_CAPTION +
        // WM_NCCALCSIZE) y Compositor.CreateHostBackdropBrush, que se queda dentro de
        // Composition y no depende de las heurísticas de DWM.
    }

    /// <summary>
    /// La doc de Microsoft dice que extraer iconos "can be time consuming" y que
    /// nunca debe hacerse en el hilo de UI. Se extrae en background y se avisa con
    /// un mensaje, porque las superficies de Composition sí hay que crearlas en el
    /// hilo que tiene la DispatcherQueue.
    /// </summary>
    private void StartIconLoad()
    {
        HWND hwnd = _hwnd;
        List<DockApp> apps = _config.Apps;

        Task.Run(() =>
        {
            List<(DockApp, IconBitmap)> loaded = [];
            foreach (DockApp app in apps)
            {
                try
                {
                    loaded.Add((app, Icons.Extract(app.Target)));
                }
                catch (Exception ex)
                {
                    // Un icono que falla no puede tumbar el dock: se omite esa app.
                    Console.WriteLine($"[iconos] '{app.Name}' falló: {ex.Message}");
                }
            }

            _loaded = loaded;
            PInvoke.PostMessage(hwnd, WM_APP_ICONS_READY, default, default);
        });
    }

    private void OnIconsReady()
    {
        if (_visuals is null) return;

        _visuals.BuildIcons(
            [.. _loaded.Select(entry => entry.Icon)],
            Scale(_config.IconSize),
            Scale(_config.IconSpacing),
            Scale(_config.IconSize) + Scale(LogicalPadding) * 2);

        Console.WriteLine($"[iconos] {_loaded.Count} listos");
    }

    public void Show()
    {
        // SW_SHOWNOACTIVATE: mostrar sin activar, coherente con WS_EX_NOACTIVATE.
        PInvoke.ShowWindow(_hwnd, SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE);
        Console.WriteLine($"[dock] HWND=0x{(nint)_hwnd.Value:X} DPI={_dpi} ({_dpi * 100 / 96}%)");
    }

    private static LRESULT WndProc(HWND hwnd, uint msg, WPARAM wParam, LPARAM lParam)
    {
        Instances.TryGetValue((nint)hwnd.Value, out DockWindow? self);

        switch (msg)
        {
            // WS_EX_NOACTIVATE por sí solo NO basta: verificado en M0, al hacer clic
            // llegaban WM_ACTIVATE(WA_CLICKACTIVE) y WM_SETFOCUS. MA_NOACTIVATE
            // rechaza la activación sin descartar el clic, que es lo que hace falta
            // para poder lanzar apps sin robar el foco.
            case WM_MOUSEACTIVATE:
                return new LRESULT(MA_NOACTIVATE);

            // DWM deja de pintar el material acrílico en las ventanas que considera
            // inactivas, y la nuestra nunca se activa por diseño (MA_NOACTIVATE).
            // Al arrancar aún lo pinta, pero en cuanto otra ventana toma y suelta el
            // foco la marca inactiva y el dock se vuelve invisible del todo: sigue
            // ahí y sigue recibiendo clics, pero no se ve nada.
            //
            // Pasar wParam=TRUE hace que DWM la dibuje siempre como activa sin tocar
            // el foco real de Win32. Es la mitigación documentada para
            // microsoft-ui-xaml#10570.
            case WM_NCACTIVATE:
                return PInvoke.DefWindowProc(hwnd, msg, new WPARAM(1), lParam);

            // Colapsa el área no cliente: devolver 0 deja el área cliente igual al
            // rect de la ventana, así que no se dibuja barra de título ni bordes.
            //
            // Sin filtrar por wParam a propósito. Llega en dos formas (FALSE con un
            // RECT, TRUE con un NCCALCSIZE_PARAMS) y ambas quieren la misma
            // respuesta; la de CreateWindowEx es la FALSE, que es justo la que hay
            // que atrapar para que la barra no aparezca nunca.
            //
            // Tampoco depende de la instancia: este mensaje llega durante
            // CreateWindowEx, cuando la ventana aún no está en el diccionario.
            case WM_NCCALCSIZE:
                return new LRESULT(0);

            case WM_APP_ICONS_READY:
                self?.OnIconsReady();
                return new LRESULT(0);

            case WM_MOUSEMOVE:
                self?.OnMouseMove(lParam);
                return new LRESULT(0);

            case WM_MOUSELEAVE:
                if (self is not null) self._trackingMouse = false;
                return new LRESULT(0);

            case WM_LBUTTONUP:
                self?.OnLeftClick(lParam);
                return new LRESULT(0);

            case WM_RBUTTONUP:
                // Única vía de salida por ahora: la ventana no sale en Alt+Tab.
                PInvoke.PostQuitMessage(0);
                return new LRESULT(0);

            case WM_DPICHANGED:
                self?.OnDpiChanged(wParam, lParam);
                return new LRESULT(0);

            case WM_DESTROY:
                Instances.Remove((nint)hwnd.Value);
                PInvoke.PostQuitMessage(0);
                return new LRESULT(0);
        }

        return PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);
    }

    private static short LoWord(LPARAM lParam) => (short)(lParam.Value & 0xFFFF);

    private static short HiWord(LPARAM lParam) => (short)((lParam.Value >> 16) & 0xFFFF);

    private void OnMouseMove(LPARAM lParam)
    {
        if (!_trackingMouse)
        {
            // TrackMouseEvent se desarma solo al dispararse: hay que re-armarlo en
            // cada entrada.
            TRACKMOUSEEVENT tme = new()
            {
                cbSize = (uint)sizeof(TRACKMOUSEEVENT),
                dwFlags = TRACKMOUSEEVENT_FLAGS.TME_LEAVE,
                hwndTrack = _hwnd,
            };
            PInvoke.TrackMouseEvent(&tme);
            _trackingMouse = true;
        }
    }

    private void OnLeftClick(LPARAM lParam)
    {
        int index = _visuals?.HitTest(LoWord(lParam)) ?? -1;
        if (index < 0 || index >= _loaded.Count) return;

        DockApp app = _loaded[index].App;
        try
        {
            app.Launch();
            Console.WriteLine($"[dock] lanzada '{app.Name}'");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[dock] no se pudo lanzar '{app.Name}': {ex.Message}");
        }
    }

    private void OnDpiChanged(WPARAM wParam, LPARAM lParam)
    {
        // Se IGNORA a propósito el rect sugerido que viene en lParam.
        //
        // Windows lo calcula escalando el rect anterior por el cambio de DPI, y eso
        // solo vale para ventanas cuyo tamaño lo decide el usuario. El del dock sale
        // de su contenido: nº de iconos x tamaño de icono x DPI. Recalcularlo es la
        // única forma de que quede bien.
        //
        // Aplicar el rect sugerido además era acumulativo y destructivo: lanzar
        // ciertas apps (Paint) dispara un WM_DPICHANGED transitorio a 96 DPI, así que
        // el dock se encogía un 20% y se desplazaba con cada una. Tras unas cuantas
        // quedaba diminuto y parecía que había desaparecido.
        (int x, int y, int w, int h) = ComputeBounds();

        PInvoke.SetWindowPos(_hwnd, default, x, y, w, h,
            SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE | SET_WINDOW_POS_FLAGS.SWP_NOZORDER);

        // El layout de los iconos va en píxeles físicos: hay que rehacerlo.
        OnIconsReady();
        Console.WriteLine($"[dpi] WM_DPICHANGED -> recalculado a {_dpi} DPI ({_dpi * 100 / 96}%), {w}x{h} en ({x},{y})");
    }

    public static void RunMessageLoop()
    {
        MSG msg;
        while (PInvoke.GetMessage(&msg, default, 0, 0).Value > 0)
        {
            PInvoke.TranslateMessage(&msg);
            PInvoke.DispatchMessage(&msg);
        }
    }

    public void Dispose()
    {
        if (_hwnd.IsNull) return;
        _visuals?.Dispose();
        PInvoke.DestroyWindow(_hwnd);
        _hwnd = default;
    }
}
