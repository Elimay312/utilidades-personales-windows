using System.Runtime.InteropServices;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.UI.HiDpi;
using Windows.Win32.UI.Input.KeyboardAndMouse;
using Windows.Win32.UI.Shell;
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

    /// Margen del icono dentro de la barra, en unidades lógicas.
    private const int LogicalPadding = 12;
    private const int LogicalBottomMargin = 8;

    /// Franja que asoma cuando el dock está escondido, en unidades lógicas.
    private const int LogicalRevealStrip = 3;

    /// Margen antes de esconderse al salir el ratón. Sin él, rozar el dock de paso
    /// lo haría parpadear.
    private const uint HideDelayMs = 450;

    /// Escala máxima del icono justo bajo el cursor.
    private const float MaxScale = 2.0f;

    /// Radio de influencia del cursor, medido en ranuras. Junto con MaxScale son los
    /// dos mandos que gobiernan el tacto de la magnificación, y los únicos números de
    /// aquí que piden ajustarse a ojo.
    ///
    /// A 2.5 el bulto abarcaba casi un dock de 4 iconos y lo ensanchaba un 62%, mucho
    /// más de lo que hace macOS. Con 1.75 se magnifican unos 3 iconos.
    private const float RadiusInSlots = 1.75f;

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
    private const uint WM_TIMER = 0x0113;
    private const uint WM_WINDOWPOSCHANGING = 0x0046;
    private const uint WM_NCHITTEST = 0x0084;

    /// Respuestas a WM_NCHITTEST. HTTRANSPARENT hace que el clic atraviese la ventana
    /// y llegue a la de debajo.
    private const int HTTRANSPARENT = -1;
    private const int HTCLIENT = 1;
    private const uint WM_DPICHANGED = 0x02E0;

    /// HWND_TOPMOST: primera posición de la banda de ventanas siempre encima.
    private static readonly HWND HwndTopmost = (HWND)(nint)(-1);

    /// Identificador del temporizador que vigila el z-order y la pantalla completa.
    private const nuint TopmostTimerId = 1;

    /// Identificador del temporizador de un disparo que esconde el dock.
    private const nuint HideTimerId = 2;


    /// Los iconos terminaron de extraerse en background. WM_APP + 1.
    private const uint WM_APP_ICONS_READY = 0x8001;

    // IDC_ARROW = MAKEINTRESOURCE(32512)
    private const int IdcArrow = 32512;

    // Respuesta a WM_MOUSEACTIVATE: no activar, pero tampoco descartar el clic.
    private const int MA_NOACTIVATE = 3;

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

    /// Curva vigente, con la que se invierte el cursor y se hace el hit-test. Tiene
    /// que ser la MISMA que generó las expresiones, o el icono que se resalta no
    /// coincide con el que se lanza.
    private DockCurve _curve;

    /// Última posición del cursor en coordenadas de reposo. El clic la reutiliza en
    /// vez de volver a invertir la curva: invertir necesita saber cuánto vale Amount,
    /// y reutilizar el valor ya calculado es más simple y no puede desincronizarse
    /// del icono que se está viendo magnificado.
    private float _lastRest;

    private float _windowWidth;
    private float _windowHeight;
    private bool _hovering;

    /// Estado del autoocultar.
    private bool _hidden;

    /// Y de pantalla a partir de la cual empieza la franja que asoma.
    private int _revealTop;

    public DockWindow(HMONITOR monitor, DockConfig config)
    {
        _monitor = monitor;
        _config = config;

        EnsureClassRegistered();
        Create();
    }

    /// <summary>
    /// Todos los monitores conectados, uno por dock. La ventana se parametrizó por
    /// HMONITOR desde M0 justo para que esto no necesitara refactor.
    /// </summary>
    public static List<HMONITOR> AllMonitors()
    {
        List<HMONITOR> monitors = [];

        PInvoke.EnumDisplayMonitors(default, (RECT*)null, (monitor, _, _, _) =>
        {
            monitors.Add(monitor);
            return true;
        }, default);

        // Si la enumeración fallara, al menos el principal.
        if (monitors.Count == 0)
            monitors.Add(PInvoke.MonitorFromPoint(default, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTOPRIMARY));

        return monitors;
    }

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

        // Composition pasa a ser dueña del contenido de la ventana. Lo que no pinte
        // queda transparente y deja ver el material acrílico de DWM.
        _visuals = new DockVisuals(_hwnd);

        // Vigilancia del z-order. No es animación (eso va en el compositor): es una
        // comprobación de 1 vez por segundo de que seguimos arriba.
        //
        // Hace falta porque WM_WINDOWPOSCHANGING solo cubre los casos en que Windows
        // nos avisa. Cuando otro proceso se inserta por encima, a nosotros no llega
        // ningún mensaje y nos quedamos hundidos para siempre.
        PInvoke.SetTimer(_hwnd, TopmostTimerId, 1000, null);

        StartIconLoad();
    }

    private float Scale(int logical) => (float)(logical * _dpi / 96.0);

    /// <summary>Alto de la barra visible, sin contar el espacio para magnificar.</summary>
    private float BarHeight => Scale(_config.IconSize) + Scale(LogicalPadding) * 2f;

    /// <summary>Cuánto hay que deslizar el contenido para dejar solo la franja.</summary>
    private float HiddenOffset => BarHeight - Scale(LogicalRevealStrip);

    private DockCurve CurveFor(int count) => new()
    {
        IconSize = Scale(_config.IconSize),
        Spacing = Scale(_config.IconSpacing),
        Count = count,
        Radius = Scale(_config.IconSize + _config.IconSpacing) * RadiusInSlots,
        MaxScale = MaxScale,
    };

    /// <summary>Rectángulo del dock en píxeles físicos, centrado abajo en su monitor.</summary>
    private (int X, int Y, int W, int H) ComputeBounds()
    {
        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        PInvoke.GetMonitorInfo(_monitor, &info);

        PInvoke.GetDpiForMonitor(_monitor, MONITOR_DPI_TYPE.MDT_EFFECTIVE_DPI, out uint dpiX, out _);
        _dpi = dpiX;

        // La ventana se dimensiona para el caso MÁXIMO y no se vuelve a tocar: tiene
        // que caber la fila ya ensanchada y el icono magnificado, que crece hacia
        // arriba. Lo que se ve moverse es la barra de fondo, no la ventana.
        DockCurve curve = CurveFor(Math.Max(_config.Apps.Count, 1));
        float padding = Scale(LogicalPadding);
        int w = (int)MathF.Ceiling(curve.RestWidth + curve.MaxGrowth + padding * 2f);
        int h = (int)MathF.Ceiling(curve.IconSize * MaxScale + padding * 2f);

        RECT work = info.rcWork;
        int x = work.left + ((work.right - work.left) - w) / 2;

        // Al autoocultarse, la ventana se pega al borde: la franja que asoma tiene que
        // estar justo en el filo de la pantalla para que se revele al empujar ahí el
        // ratón, como en macOS.
        int y = work.bottom - h - (_config.AutoHide ? 0 : (int)Scale(LogicalBottomMargin));

        _windowWidth = w;
        _windowHeight = h;
        _revealTop = y + h - (int)Scale(LogicalRevealStrip);
        return (x, y, w, h);
    }

    /// <summary>
    /// Ya no se le pide nada a DWM, y es a propósito.
    ///
    /// La ventana es del tamaño MÁXIMO que puede ocupar el dock magnificado y está
    /// casi toda transparente, así que un backdrop de DWM pintaría ese rectángulo
    /// entero en vez de solo la barra, y DWMWA_WINDOW_CORNER_PREFERENCE redondearía
    /// unas esquinas que nadie ve. El material y el redondeo los hace Composition,
    /// sobre la barra y solo sobre ella (ver DockVisuals.BuildBar).
    /// </summary>

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

        _curve = CurveFor(_loaded.Count);
        _visuals.Build(
            _curve,
            [.. _loaded.Select(entry => entry.Icon)],
            _windowWidth,
            _windowHeight,
            Scale(LogicalPadding));

        Console.WriteLine($"[iconos] {_loaded.Count} listos");

        if (_config.AutoHide)
        {
            _hidden = true;
            _visuals.SetHidden(true, HiddenOffset);
        }
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

            // Windows saca la ventana de la banda topmost cuando se activan otras
            // aplicaciones, y lo hace SIN quitar el estilo WS_EX_TOPMOST: el bit
            // sigue puesto pero la ventana ya está por debajo. Ése era el bug de "el
            // dock desaparece": no dejaba de pintarse, se hundía detrás de la ventana
            // maximizada de turno y no volvía nunca.
            //
            // Reafirmarlo aquí, antes de que el cambio se aplique, evita tener que
            // sondear con un timer. Verificado con WindowFromPoint: la ventana que lo
            // tapaba ni siquiera era topmost.
            case WM_WINDOWPOSCHANGING:
                ((WINDOWPOS*)lParam.Value)->hwndInsertAfter = HwndTopmost;
                break;

            // Mientras está escondido, todo lo que no sea la franja deja pasar el
            // clic a la ventana de debajo: el dock no estorba a lo que haya ahí.
            case WM_NCHITTEST when self is { _config.AutoHide: true, _hidden: true }:
                return new LRESULT(HiWord(lParam) >= self._revealTop ? HTCLIENT : HTTRANSPARENT);

            case WM_TIMER when wParam.Value == TopmostTimerId:
                self?.OnWatchdogTick();
                return new LRESULT(0);

            case WM_TIMER when wParam.Value == HideTimerId:
                self?.Hide();
                return new LRESULT(0);

            case WM_APP_ICONS_READY:
                self?.OnIconsReady();
                return new LRESULT(0);

            case WM_MOUSEMOVE:
                self?.OnMouseMove(lParam);
                return new LRESULT(0);

            case WM_MOUSELEAVE:
                if (self is not null)
                {
                    self._trackingMouse = false;
                    self._hovering = false;
                    self._visuals?.SetHover(false);
                    self.ScheduleHide();
                }
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

    /// <summary>Saca el dock a la vista y cancela cualquier ocultamiento pendiente.</summary>
    private void Reveal()
    {
        PInvoke.KillTimer(_hwnd, HideTimerId);
        if (!_hidden) return;

        _hidden = false;
        _visuals?.SetHidden(false, HiddenOffset);
    }

    /// <summary>Programa el ocultamiento, con margen para no parpadear al rozarlo.</summary>
    private void ScheduleHide()
    {
        if (!_config.AutoHide) return;
        PInvoke.SetTimer(_hwnd, HideTimerId, HideDelayMs, null);
    }

    private void Hide()
    {
        PInvoke.KillTimer(_hwnd, HideTimerId);
        if (_hidden || !_config.AutoHide) return;

        _hidden = true;
        _visuals?.SetHidden(true, HiddenOffset);
    }

    /// <summary>
    /// Latido de una vez por segundo: reafirma el z-order y mira si hay algo a
    /// pantalla completa.
    /// </summary>
    private void OnWatchdogTick()
    {
        if (IsFullscreenAppRunning())
        {
            // Nada de reafirmar el z-order por encima de un juego o un vídeo.
            Hide();
            return;
        }

        EnsureTopmost();
    }

    /// <summary>
    /// Si hay una app ocupando la pantalla entera. SHQueryUserNotificationState es la
    /// API documentada para esto, pero la propia doc avisa de que NO emite
    /// notificaciones: hay que preguntarla, de ahí el sondeo.
    ///
    /// No cubre el fullscreen sin bordes (vídeo en el navegador, muchos juegos
    /// modernos), así que se complementa comparando el rect de la ventana en primer
    /// plano con el del monitor. Solo se LEE su geometría: no se la toca.
    /// </summary>
    private bool IsFullscreenAppRunning()
    {
        if (PInvoke.SHQueryUserNotificationState(out QUERY_USER_NOTIFICATION_STATE state).Succeeded
            && state is QUERY_USER_NOTIFICATION_STATE.QUNS_RUNNING_D3D_FULL_SCREEN
                or QUERY_USER_NOTIFICATION_STATE.QUNS_PRESENTATION_MODE
                or QUERY_USER_NOTIFICATION_STATE.QUNS_BUSY)
        {
            return true;
        }

        HWND foreground = PInvoke.GetForegroundWindow();
        if (foreground.IsNull || foreground == _hwnd) return false;

        // Una ventana MAXIMIZADA no es pantalla completa. Con la barra de tareas en
        // autoocultar el área de trabajo es la pantalla entera, así que comparar solo
        // rectángulos daría por fullscreen cualquier ventana maximizada y el dock no
        // volvería a aparecer. Las de verdad no tienen barra de título ni borde
        // redimensionable.
        nint style = PInvoke.GetWindowLongPtr(foreground, WINDOW_LONG_PTR_INDEX.GWL_STYLE);
        const nint WsCaption = 0x00C00000;
        const nint WsThickFrame = 0x00040000;
        if ((style & (WsCaption | WsThickFrame)) != 0) return false;

        if (!PInvoke.GetWindowRect(foreground, out RECT rect)) return false;

        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        if (!PInvoke.GetMonitorInfo(_monitor, &info)) return false;

        RECT screen = info.rcMonitor;
        return rect.left <= screen.left && rect.top <= screen.top
            && rect.right >= screen.right && rect.bottom >= screen.bottom;
    }

    /// <summary>Reafirma la posición en la banda topmost. Barato y sin efecto si ya estamos.</summary>
    private void EnsureTopmost()
    {
        PInvoke.SetWindowPos(_hwnd, HwndTopmost, 0, 0, 0, 0,
            SET_WINDOW_POS_FLAGS.SWP_NOMOVE | SET_WINDOW_POS_FLAGS.SWP_NOSIZE
                | SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE);
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

        if (_visuals is null || _curve.Count == 0) return;

        Reveal();

        if (!_hovering)
        {
            _hovering = true;
            _visuals.SetHover(true);


        }

        // Lo ÚNICO que hace el hilo de UI por cada movimiento: invertir la curva y
        // escribir un escalar. Ni layout, ni repintado, ni recorrer los iconos.
        //
        // Composition agrupa los cambios y los confirma al volver al bucle de
        // mensajes. A partir de ahí la animación vive en el proceso de DWM: se
        // verificó bloqueando este hilo 3 s a propósito y viendo que el muelle seguía
        // oscilando (ancho 352 -> 529 -> 500 sin ejecutar nosotros una instrucción).
        _lastRest = _curve.Invert(LoWord(lParam), _windowWidth);
        _visuals.SetCursor(_lastRest);
    }

    private void OnLeftClick(LPARAM lParam)
    {
        int index = _visuals?.HitTest(_lastRest) ?? -1;
        if (index < 0 || index >= _loaded.Count) return;

        DockApp app = _loaded[index].App;

        // Lanzar fuera de este hilo: Process.Start con UseShellExecute acaba en
        // ShellExecuteEx, que puede bloquear varios segundos, y este hilo es el que
        // atiende el ratón y el que tiene la DispatcherQueue del compositor.
        Task.Run(() =>
        {
            try
            {
                app.Launch();
                Console.WriteLine($"[dock] lanzada '{app.Name}'");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[dock] no se pudo lanzar '{app.Name}': {ex.Message}");
            }
        });
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
        PInvoke.KillTimer(_hwnd, TopmostTimerId);
        PInvoke.KillTimer(_hwnd, HideTimerId);
        _visuals?.Dispose();
        PInvoke.DestroyWindow(_hwnd);
        _hwnd = default;
    }
}
