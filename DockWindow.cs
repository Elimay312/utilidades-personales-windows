using System.Numerics;
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

    /// Hueco por encima del icono magnificado para la etiqueta con el nombre. La
    /// ventana medía exactamente lo que el icono más grande, así que sin esto la
    /// etiqueta caería fuera y no se vería.
    private const int LogicalLabelRoom = 30;

    /// Margen antes de esconderse al salir el ratón. Sin él, rozar el dock de paso
    /// lo haría parpadear.
    private const uint HideDelayMs = 450;

    /// Escala máxima del icono justo bajo el cursor.
    private const float MaxScale = 2.0f;

    /// Lo que hay que mover el ratón con el botón pulsado para que deje de ser un clic
    /// y pase a ser un arrastre, en unidades lógicas. Por debajo de esto, un pulso con
    /// mano temblorosa seguiría lanzando la app, que es lo que se espera.
    private const int LogicalDragThreshold = 6;

    /// Cuánto hay que subir por encima de la barra para que soltar signifique quitar.
    private const int LogicalPullOffDistance = 40;

    /// Ancho de la zona del "+" a la derecha de la barra, en unidades lógicas. Tiene
    /// que casar con lo que dibuja DockVisuals.BuildAddZone.
    private const float AddZoneFraction = 0.58f;

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
    private const uint WM_LBUTTONDOWN = 0x0201;
    private const uint WM_LBUTTONUP = 0x0202;
    private const uint WM_CAPTURECHANGED = 0x0215;
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

    /// Cada cuánto se reafirma el z-order.
    ///
    /// Era un segundo, y se notaba: cuando la barra de tareas se revela se pone por
    /// encima del dock, y quedarse debajo un segundo entero es justo lo que se veía en
    /// la captura del usuario. Bajarlo NO evita que la barra salga —eso se midió a 200
    /// y a 60 ms y sale igual—, pero recupera el sitio enseguida.
    private const uint WatchdogMs = 250;

    /// Un latido de cada cuántos mira qué apps están abiertas. Reafirmar el z-order es
    /// una llamada; recorrer procesos y ventanas no, y no hace falta cinco veces por
    /// segundo.
    private const int RunningEveryTicks = 4;

    /// Identificador del temporizador de un disparo que esconde el dock.
    private const nuint HideTimerId = 2;

    /// Espera a que se vea desvanecerse el icono que se acaba de sacar del dock,
    /// porque reconstruir borra el arbol de visuals y se comeria la animacion.
    private const nuint PuffTimerId = 3;


    /// Los iconos terminaron de extraerse en background. WM_APP + 1.
    private const uint WM_APP_ICONS_READY = 0x8001;

    /// dock.json cambió en disco. WM_APP + 2.
    private const uint WM_APP_RELOAD = 0x8002;

    /// Terminó de mirarse qué apps están abiertas. WM_APP + 3.
    private const uint WM_APP_RUNNING = 0x8003;

    /// Se soltó algo encima. WM_APP + 4.
    private const uint WM_APP_DROP = 0x8004;

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

    /// <summary>
    /// Caché de iconos por target, compartida entre todos los docks. Sirve para dos
    /// cosas: al recargar el JSON solo se extraen los iconos nuevos, y con varios
    /// monitores cada icono se extrae una sola vez en vez de una por pantalla.
    /// </summary>
    private static readonly Dictionary<string, IconBitmap> IconCache = [];

    private DockConfig _config;

    private HWND _hwnd;
    private DockVisuals? _visuals;
    private DockDropTarget? _dropTarget;
    private uint _dpi = 96;
    private bool _trackingMouse;

    /// Elementos del dock en el mismo orden que los visuals. El icono es null en los
    /// separadores, que no tienen app detrás.
    private List<(DockApp App, IconBitmap? Icon)> _loaded = [];

    /// Estado de cada app: si corre y dónde está su ventana. Se recalcula fuera del
    /// hilo de UI porque resolver las MSIX obliga a recorrer procesos y ventanas.
    private AppState[] _state = [];
    private bool _checkingRunning;
    private int _watchdogTicks;

    /// <summary>
    /// Icono que está botando porque su app se está abriendo, y hasta cuándo. El tope
    /// existe porque no toda app acaba teniendo ventana: si se le lanza un instalador o
    /// algo que no abre nada, el icono no puede quedarse botando para siempre.
    /// </summary>
    private int _launchingIndex = -1;
    private DateTime _launchingUntil;

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

    /// Extremos de la última región aplicada, para no reaplicarla igual.
    private int _regionLeft = int.MinValue;
    private int _regionRight = int.MinValue;

    /// X de pantalla del borde izquierdo de la ventana.
    private int _windowLeft;
    private int _windowTop;

    // --- Arrastre de iconos ---------------------------------------------------
    //
    // El botón pulsado no basta para saber si esto es un clic o un arrastre, así que
    // se apunta dónde empezó y no se captura el ratón hasta pasar el umbral: hasta
    // entonces el clic tiene que seguir comportándose exactamente como siempre.

    /// <summary>Icono bajo el cursor cuando se pulsó, o -1.</summary>
    private int _pressedIndex = -1;

    /// <summary>Punto de cliente donde se pulsó.</summary>
    private int _pressX;
    private int _pressY;

    /// <summary>Ya se pasó el umbral: esto es un arrastre y tenemos la captura.</summary>
    private bool _dragging;

    /// <summary>
    /// El orden en el que están los iconos ahora mismo, como índices de <c>_loaded</c>.
    /// Se va permutando mientras se arrastra y al soltar es lo que se persiste.
    /// </summary>
    private List<int> _dragOrder = [];

    /// <summary>
    /// Lo último que se soltó, esperando a lanzarse fuera de IDropTarget.Drop. Lanzar
    /// ahí dentro bloquearía el hilo que el Explorador está esperando.
    /// </summary>
    private DroppedItem[] _pendingDrop = [];

    /// <summary>Qué significaría soltar en el punto por el que va el arrastre.</summary>
    private enum DropKind { None, Open, Add }

    private DropKind _dropKind;
    private int _dropSlot = -1;

    /// <summary>
    /// El último fotograma de cada ventana que el dock minimizó, para poder reproducir
    /// el genio al revés al restaurarla. Una ventana minimizada no se puede capturar, y
    /// restaurarla para capturarla sería justo el parpadeo que se quiere evitar.
    ///
    /// Se consume al restaurar, así que solo sobreviven las de las ventanas que están
    /// minimizadas ahora mismo: como mucho una por icono del dock.
    /// </summary>
    private readonly Dictionary<nint, (IconBitmap Shot, Box Source)> _shots = [];

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

        // A partir de aquí la ventana acepta sueltas del Explorador. Devuelve
        // E_OUTOFMEMORY si el hilo se inicializó con CoInitialize en vez de
        // OleInitialize, que es la trampa que documenta la propia API; por eso
        // Program lo hace lo primero de todo.
        _dropTarget = new DockDropTarget(this);
        HRESULT hr = PInvoke.RegisterDragDrop(_hwnd, _dropTarget);
        Console.WriteLine($"[drop] RegisterDragDrop -> 0x{(uint)hr.Value:X8}");

        // Vigilancia del z-order. No es animación (eso va en el compositor): es una
        // comprobación de 1 vez por segundo de que seguimos arriba.
        //
        // Hace falta porque WM_WINDOWPOSCHANGING solo cubre los casos en que Windows
        // nos avisa. Cuando otro proceso se inserta por encima, a nosotros no llega
        // ningún mensaje y nos quedamos hundidos para siempre.
        PInvoke.SetTimer(_hwnd, TopmostTimerId, WatchdogMs, null);

        StartIconLoad();
    }

    private float Scale(int logical) => (float)(logical * _dpi / 96.0);

    /// <summary>Alto de la barra visible, sin contar el espacio para magnificar.</summary>
    private float BarHeight => Scale(_config.IconSize) + Scale(LogicalPadding) * 2f;

    /// <summary>Cuánto hay que deslizar el contenido para dejar solo la franja.</summary>
    private float HiddenOffset => BarHeight - Scale(LogicalRevealStrip);

    /// <summary>
    /// Construye la curva a partir de una lista de elementos. Los separadores ocupan
    /// una ranura mucho más estrecha que un icono, que es justo para lo que la curva
    /// admite ranuras de ancho variable.
    /// </summary>
    private DockCurve CurveFor(IReadOnlyList<DockApp> apps)
    {
        float icon = Scale(_config.IconSize);
        float spacing = Scale(_config.IconSpacing);
        float separator = MathF.Max(2f, spacing * 0.2f);

        List<DockSlot> slots = [];
        foreach (DockApp app in apps)
        {
            slots.Add(app.Separator
                ? new DockSlot(separator + spacing, separator)
                : new DockSlot(icon + spacing, icon));
        }

        // Aunque no haya nada configurado, la ventana necesita un tamaño con sentido.
        if (slots.Count == 0) slots.Add(new DockSlot(icon + spacing, icon));

        return new DockCurve(slots, radius: (icon + spacing) * RadiusInSlots, maxScale: MaxScale);
    }

    /// <summary>Rectángulo del dock en píxeles físicos, centrado abajo en su monitor.</summary>
    private (int X, int Y, int W, int H) ComputeBounds()
    {
        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        PInvoke.GetMonitorInfo(_monitor, &info);

        PInvoke.GetDpiForMonitor(_monitor, MONITOR_DPI_TYPE.MDT_EFFECTIVE_DPI, out uint dpiX, out _);
        _dpi = dpiX;

        // La ventana ocupa TODO el ancho del monitor, no solo el del dock.
        //
        // Así la franja que asoma cubre el borde inferior entero y el dock se revela
        // empujando el ratón a cualquier punto de abajo. La contrapartida, aceptada:
        // la barra de tareas en autoocultar ya no se revela con el ratón, porque
        // somos nosotros quienes lo recibimos ahí.
        //
        // El alto sí es el del caso máximo: tiene que caber el icono magnificado, que
        // crece hacia arriba. Lo que se ve moverse es la barra de fondo, no la ventana.
        float padding = Scale(LogicalPadding);
        int w = info.rcWork.right - info.rcWork.left;
        int h = (int)MathF.Ceiling(
            Scale(_config.IconSize) * MaxScale + padding * 2f + Scale(LogicalLabelRoom));

        RECT work = info.rcWork;
        int x = work.left;

        // Al autoocultarse, la ventana se pega al borde: la franja que asoma tiene que
        // estar justo en el filo de la pantalla para que se revele al empujar ahí el
        // ratón, como en macOS.
        int y = work.bottom - h - (_config.AutoHide ? 0 : (int)Scale(LogicalBottomMargin));

        _windowWidth = w;
        _windowHeight = h;
        _revealTop = y + h - (int)Scale(LogicalRevealStrip);
        _windowLeft = x;
        _windowTop = y;
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
            List<(DockApp, IconBitmap?)> loaded = [];
            foreach (DockApp app in apps)
            {
                // Un separador no tiene icono que extraer: ocupa su ranura y ya.
                if (app.Separator)
                {
                    loaded.Add((app, null));
                    continue;
                }

                try
                {
                    IconBitmap? cached;
                    lock (IconCache) IconCache.TryGetValue(app.Target, out cached);

                    IconBitmap icon = cached ?? Icons.Extract(app.Target);
                    if (cached is null)
                        lock (IconCache) IconCache[app.Target] = icon;

                    loaded.Add((app, icon));
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

        _curve = CurveFor([.. _loaded.Select(entry => entry.App)]);
        _visuals.Build(
            _curve,
            [.. _loaded.Select(entry => entry.Icon)],
            [.. _loaded.Select(entry => entry.App.Name)],
            _windowWidth,
            _windowHeight,
            Scale(LogicalPadding),
            Scale(_config.IconSize));

        Console.WriteLine($"[iconos] {_loaded.Count} elementos");

        // Aquí y no en ComputeBounds: la región depende del ancho de la barra, y ese
        // no se sabe hasta tener la curva, que es justo lo que se acaba de construir.
        // Todos los caminos que recolocan la ventana (recarga, cambio de DPI,
        // reordenar) acaban pasando por aquí.
        ApplyRegion();

        if (_config.AutoHide)
        {
            _hidden = true;
            _visuals.SetHidden(true, HiddenOffset);
        }

        // Estado inicial de los puntos, sin esperar al primer latido.
        RefreshRunning();
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

            case WM_NCHITTEST when self is not null:
                return new LRESULT(self.OnHitTest(lParam));

            case WM_TIMER when wParam.Value == TopmostTimerId:
                self?.OnWatchdogTick();
                return new LRESULT(0);

            case WM_TIMER when wParam.Value == PuffTimerId:
                PInvoke.KillTimer(hwnd, PuffTimerId);
                self?.StartIconLoad();
                return new LRESULT(0);

            case WM_TIMER when wParam.Value == HideTimerId:
                self?.Hide();
                return new LRESULT(0);

            case WM_APP_ICONS_READY:
                self?.OnIconsReady();
                return new LRESULT(0);

            case WM_APP_RELOAD:
                self?.OnReload();
                return new LRESULT(0);

            case WM_APP_DROP:
                self?.OnDropped();
                return new LRESULT(0);

            case WM_APP_RUNNING:
                if (self is not null)
                {
                    self._visuals?.SetRunning([.. self._state.Select(entry => entry.HasWindow)]);
                    self.DropDeadShots();
                    self.StopBounceIfOpened();
                }
                return new LRESULT(0);

            case WM_MOUSEMOVE:
                self?.OnMouseMove(lParam);
                return new LRESULT(0);

            case WM_MOUSELEAVE:
                if (self is not null)
                {
                    self._trackingMouse = false;

                    // Con la captura tomada el raton puede salirse de la ventana sin
                    // que el arrastre haya terminado. Esconder el dock justo entonces
                    // seria quitarle al usuario lo que esta manipulando.
                    if (!self._dragging)
                    {
                        self._hovering = false;
                        self._visuals?.SetHover(false);
                        self._visuals?.SetLabel(-1);
                        self.ScheduleHide();
                    }
                }
                return new LRESULT(0);

            case WM_LBUTTONDOWN:
                self?.OnLeftDown(lParam);
                return new LRESULT(0);

            case WM_LBUTTONUP:
                self?.OnLeftUp(lParam);
                return new LRESULT(0);

            case WM_CAPTURECHANGED:
                // Windows nos ha quitado la captura por su cuenta (un Alt+Tab, un
                // diálogo). Sin esto el dock se quedaría con un icono a medio arrastrar.
                self?.CancelDrag();
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

    /// <summary>
    /// Avisa de que dock.json cambió. Se hace por mensaje porque el watcher notifica
    /// desde un hilo del pool, y todo lo de Composition tiene que ocurrir en el hilo
    /// que tiene la DispatcherQueue.
    /// </summary>
    public void RequestReload() => PInvoke.PostMessage(_hwnd, WM_APP_RELOAD, default, default);

    private void OnReload()
    {
        DockConfig fresh;
        try
        {
            fresh = DockConfig.Load(DockConfig.DefaultPath);
        }
        catch (Exception ex)
        {
            // Un JSON a medio guardar o mal escrito no puede tumbar el dock: se avisa
            // y se sigue con la configuración anterior.
            Console.WriteLine($"[config] no se pudo recargar, se mantiene la anterior: {ex.Message}");
            return;
        }

        _config = fresh;

        // El tamaño depende del número de iconos, así que hay que recolocar.
        (int x, int y, int w, int h) = ComputeBounds();
        PInvoke.SetWindowPos(_hwnd, default, x, y, w, h,
            SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE | SET_WINDOW_POS_FLAGS.SWP_NOZORDER);

        Console.WriteLine($"[config] recargada: {_config.Apps.Count} apps");
        StartIconLoad();
    }

    /// <summary>
    /// Qué partes de la ventana recogen el ratón. El resto devuelve HTTRANSPARENT y
    /// el clic atraviesa hasta la ventana de debajo.
    ///
    /// Hacen falta dos zonas, y la primera no es opcional: si solo fuera nuestra la
    /// barra, revelar el dock desde un extremo del borde lo escondería al instante
    /// (el cursor quedaría fuera de la barra, llegaría WM_MOUSELEAVE y se ocultaría),
    /// o sea un parpadeo. Con la franja siempre nuestra, el dock se queda quieto
    /// mientras el ratón siga abajo.
    /// </summary>
    private int OnHitTest(LPARAM lParam)
    {
        // La franja del borde inferior: siempre nuestra, de lado a lado.
        if (_config.AutoHide && HiWord(lParam) >= _revealTop) return HTCLIENT;

        // Por encima, solo la barra. La ventana ocupa el ancho de la pantalla, así que
        // sin esto se tragaría cualquier clic en la franja inferior del escritorio.
        (float left, float right) = BarBounds();
        float x = LoWord(lParam) - _windowLeft;
        return x >= left && x <= right ? HTCLIENT : HTTRANSPARENT;
    }

    /// <summary>Extremos de la barra en coordenadas de cliente, ahora mismo.</summary>
    private (float Left, float Right) BarBounds()
    {
        if (_curve.Count == 0) return (0f, _windowWidth);

        float amount = _hovering ? 1f : 0f;
        float padding = Scale(LogicalPadding);
        float origin = _curve.Origin(_windowWidth, _lastRest, amount);
        float width = _curve.Transfer(_curve.RestWidth, _lastRest, amount);
        return (origin - padding, origin + width + padding);
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
        _visuals?.SetLabel(-1);
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

        if (++_watchdogTicks < RunningEveryTicks) return;

        _watchdogTicks = 0;
        RefreshRunning();
    }

    /// <summary>
    /// Mira qué apps están abiertas, FUERA del hilo de UI: resolver las MSIX obliga a
    /// recorrer la lista de procesos y eso no puede bloquear el ratón.
    /// </summary>
    /// <summary>Deja de botar en cuanto la app abre su ventana, o al agotarse el tope.</summary>
    private void StopBounceIfOpened()
    {
        if (_launchingIndex < 0) return;

        bool abierta = _launchingIndex < _state.Length && _state[_launchingIndex].HasWindow;
        if (!abierta && DateTime.UtcNow < _launchingUntil) return;

        _visuals?.StopBounce(_launchingIndex);
        _launchingIndex = -1;
    }

    /// <summary>
    /// Suelta los fotogramas de ventanas que ya no existen. Sin esto, minimizar algo
    /// con el genio y luego cerrarlo dejaría su captura (varios MB) ahí para siempre.
    /// </summary>
    private void DropDeadShots()
    {
        if (_shots.Count == 0) return;

        foreach (nint handle in _shots.Keys.Where(h => !PInvoke.IsWindow((HWND)h)).ToList())
        {
            _shots.Remove(handle);
        }
    }

    private void RefreshRunning()
    {
        if (_checkingRunning || _loaded.Count == 0) return;
        _checkingRunning = true;

        HWND hwnd = _hwnd;
        List<DockApp> apps = [.. _loaded.Select(entry => entry.App)];

        Task.Run(() =>
        {
            try
            {
                _state = Running.Check(apps);
                PInvoke.PostMessage(hwnd, WM_APP_RUNNING, default, default);
            }
            finally
            {
                _checkingRunning = false;
            }
        });
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
        const SET_WINDOW_POS_FLAGS quieto =
            SET_WINDOW_POS_FLAGS.SWP_NOMOVE | SET_WINDOW_POS_FLAGS.SWP_NOSIZE
                | SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE;

        // Dos llamadas y hacen cosas distintas. HWND_TOPMOST devuelve la ventana a la
        // banda de las siempre-encima, que es de donde Windows la echa a veces dejando
        // el bit de estilo puesto. HWND_TOP la sube al principio de esa banda, que es
        // lo que hace falta cuando otra ventana de la misma banda -la barra de tareas
        // al revelarse- se ha puesto delante.
        PInvoke.SetWindowPos(_hwnd, HwndTopmost, 0, 0, 0, 0, quieto);
        PInvoke.SetWindowPos(_hwnd, HWND.Null, 0, 0, 0, 0, quieto);
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

        // El nombre del icono de debajo. Mientras se arrastra no: ahí el icono ya no
        // está donde dice la curva y la etiqueta se quedaría señalando al hueco.
        _visuals.SetLabel(_dragging || _hidden ? -1 : _visuals.HitTest(_lastRest));

        if (_pressedIndex >= 0) OnDragMove(LoWord(lParam), HiWord(lParam));
    }

    /// <summary>
    /// Apunta dónde se pulsó, y nada más. <b>No</b> captura el ratón todavía: hasta
    /// saber si esto va a ser un clic o un arrastre, capturar sería cambiar el
    /// comportamiento del camino más usado del dock.
    /// </summary>
    private void OnLeftDown(LPARAM lParam)
    {
        if (_visuals is null || _curve.Count == 0) return;

        _pressedIndex = _visuals.HitTest(_lastRest);
        _pressX = LoWord(lParam);
        _pressY = HiWord(lParam);
        _dragging = false;
    }

    /// <summary>Un clic que nunca llegó a ser arrastre sigue siendo un clic.</summary>
    private void OnLeftUp(LPARAM lParam)
    {
        if (!_dragging)
        {
            _pressedIndex = -1;
            OnLeftClick(lParam);
            return;
        }

        // ReleaseCapture manda un WM_CAPTURECHANGED, y CancelDrag limpiaría lo que
        // acabamos de decidir. Por eso se consuma ANTES de soltar la captura.
        FinishDrag(HiWord(lParam));
        PInvoke.ReleaseCapture();
    }

    /// <summary>
    /// El arrastre en sí. Arranca al pasar el umbral y desde ahí mueve el icono y va
    /// apartando a los vecinos.
    /// </summary>
    private void OnDragMove(int x, int y)
    {
        if (_visuals is null) return;

        if (!_dragging)
        {
            // El umbral mira las DOS direcciones. Mirando solo la X, sacar un icono
            // del dock tirando de el recto hacia arriba no llegaba a contar como
            // arrastre nunca, porque la X no cambiaba ni un pixel.
            float moved = MathF.Max(MathF.Abs(x - _pressX), MathF.Abs(y - _pressY));
            if (moved < Scale(LogicalDragThreshold)) return;

            _dragging = true;
            _dragOrder = [.. Enumerable.Range(0, _loaded.Count)];
            _visuals.SetLifted(_pressedIndex, true);

            // Ahora sí: a partir de aquí queremos el ratón aunque se salga de la
            // ventana, para poder arrastrar hacia arriba y sacar el icono.
            PInvoke.SetCapture(_hwnd);
        }

        // El icono va donde va el dedo. Directo, sin muelle: interpolar aquí solo
        // añadiría retraso, igual que pasa con el cursor de la magnificación.
        _visuals.SetShift(_pressedIndex, x - _pressX);

        // Mientras esté arriba, fuera del dock, nadie hace hueco: se está sacando.
        if (IsPulledOff(y)) return;

        int from = _dragOrder.IndexOf(_pressedIndex);

        // _lastRest ya viene de invertir esta misma x en OnMouseMove: invertir otra vez
        // serian 40 iteraciones de biseccion por cada pixel de raton, para nada.
        int over = _visuals.HitTest(_lastRest);
        if (over < 0 || over == from) return;

        _dragOrder.RemoveAt(from);
        _dragOrder.Insert(over, _pressedIndex);
        ApplyDragShifts();
    }

    /// <summary>
    /// Manda a cada icono al sitio que le toca con el orden de ahora mismo.
    ///
    /// El desplazamiento se calcula en píxeles de PANTALLA con <c>Project</c>, no en
    /// coordenadas de reposo: bajo la lupa una ranura mide casi el doble, y usar el
    /// ancho de reposo dejaría a los vecinos apartándose demasiado poco justo donde se
    /// está mirando.
    ///
    /// ponytail: se recalcula solo al cambiar el orden, no en cada movimiento, así que
    /// entre permutación y permutación la magnificación lo desvía un poco. Se nota
    /// menos que el coste de relanzar N muelles por cada píxel de ratón.
    /// </summary>
    private void ApplyDragShifts()
    {
        for (int position = 0; position < _dragOrder.Count; position++)
        {
            int index = _dragOrder[position];
            if (index == _pressedIndex) continue;

            float now = _curve.Project(_curve.RestLeft(index), _windowWidth, _lastRest);
            float target = _curve.Project(_curve.RestLeft(position), _windowWidth, _lastRest);
            _visuals?.SpringShift(index, target - now);
        }
    }

    /// <summary>Si el cursor está lo bastante por encima de la barra como para sacarlo.</summary>
    private bool IsPulledOff(int y)
    {
        float barTop = _windowHeight - BarHeight;
        return y < barTop - Scale(LogicalPullOffDistance);
    }

    /// <summary>Se acabó el arrastre: o se reordena, o el icono se va.</summary>
    private void FinishDrag(int y)
    {
        _dragging = false;
        int dragged = _pressedIndex;
        _pressedIndex = -1;

        List<DockApp> apps = [.. _dragOrder.Select(i => _loaded[i].App)];
        bool quitado = false;

        if (IsPulledOff(y))
        {
            _visuals?.Puff(dragged);
            apps.Remove(_loaded[dragged].App);
            Console.WriteLine($"[dock] quitada '{_loaded[dragged].App.Name}'");
            quitado = true;
        }

        DockLocal.Save(_config.BaseApps, apps);

        // Reconstruir con el orden nuevo. Los Shift vuelven a cero al crearse los
        // visuales, y como ya estaban donde toca, no se ve ningún salto.
        _config = new DockConfig
        {
            IconSize = _config.IconSize,
            IconSpacing = _config.IconSpacing,
            AutoHide = _config.AutoHide,
            AutoStart = _config.AutoStart,
            BaseApps = _config.BaseApps,
            Apps = apps,
        };

        // Si se ha quitado uno, se le deja acabar de desvanecerse antes de
        // reconstruir; si no, cuanto antes mejor.
        if (quitado) PInvoke.SetTimer(_hwnd, PuffTimerId, 200, null);
        else StartIconLoad();
    }


    /// <summary>
    /// Recorta la ventana a lo que de verdad es dock, y deja pasar el resto.
    ///
    /// La ventana ocupa todo el ancho del monitor pero casi todo es aire. Hasta ahora
    /// eso se resolvía devolviendo HTTRANSPARENT desde WM_NCHITTEST, y para los CLICS
    /// funciona. Para las SUELTAS no: el spike de F2a midió que el Explorador, que es
    /// otro proceso, manda DragEnter y 30 DragOver sobre la zona transparente. O sea
    /// que el dock se tragaría las sueltas de todo el borde inferior de la pantalla.
    ///
    /// <para>
    /// Con una región, los píxeles de fuera no son de la ventana en absoluto: el
    /// sistema los excluye del hit testing en el kernel y no hay ambigüedad entre
    /// procesos. Es lo que la documentación de WS_EX_TRANSPARENT recomienda para lograr
    /// transparencia sin sus limitaciones.
    /// </para>
    ///
    /// Son dos rectángulos:
    /// <list type="bullet">
    /// <item>La franja central, de <b>alto completo</b>. No puede ser solo la barra: los
    /// iconos magnificados crecen hacia arriba y la región también recorta lo que se
    /// dibuja, así que se quedarían cortados por la mitad.</item>
    /// <item>La franja de revelado, a lo ancho de todo, para que el dock siga
    /// asomándose empujando el ratón a cualquier punto del borde inferior.</item>
    /// </list>
    /// </summary>
    private void ApplyRegion()
    {
        // El ancho máximo de la barra: la curva no puede ensanchar más de MaxGrowth, y
        // Origin la mantiene siempre centrada.
        float widest = _curve.Count > 0 ? _curve.RestWidth + _curve.MaxGrowth : _windowWidth;
        int half = (int)MathF.Ceiling(widest * 0.5f + Scale(LogicalPadding)) + 2;
        int center = (int)(_windowWidth * 0.5f);

        // Por la derecha, además, la zona del "+": si se queda fuera de la región no
        // llegan los eventos de arrastre ahí y no habría dónde soltar para añadir.
        int extra = (int)MathF.Ceiling(BarHeight * AddZoneFraction * 1.4f) + 4;
        int left = center - half;
        int right = center + half + extra;

        // Si no ha cambiado, no se toca. Cada SetWindowRgn reajusta la forma de la
        // ventana, y esto se llama en CADA reconstrucción: recargar el JSON, reordenar,
        // volver a cargar iconos. Reformar la ventana por nada es justo el momento en
        // que otra cosa puede ganarle la carrera por el borde inferior de la pantalla.
        if (left == _regionLeft && right == _regionRight) return;

        _regionLeft = left;
        _regionRight = right;

        HRGN region = PInvoke.CreateRectRgn(left, 0, right, (int)_windowHeight);

        if (_config.AutoHide)
        {
            HRGN strip = PInvoke.CreateRectRgn(
                0, _revealTop - _windowTop, (int)_windowWidth, (int)_windowHeight);

            PInvoke.CombineRgn(region, region, strip, RGN_COMBINE_MODE.RGN_OR);
            PInvoke.DeleteObject((HGDIOBJ)(nint)strip.Value);
        }

        // SetWindowRgn se queda con la región: no hay que borrarla después.
        PInvoke.SetWindowRgn(_hwnd, region, true);
    }

    /// <summary>
    /// Qué icono hay bajo ese punto de PANTALLA, o -1 si ahí no hay barra.
    ///
    /// Lo llama el destino de sueltas, que recibe las coordenadas en pantalla y no en
    /// cliente. De paso revela el dock: mientras se arrastra algo por encima tiene que
    /// estar a la vista, o no habría dónde soltarlo.
    /// </summary>
    public int SlotAtScreen(int screenX, int screenY)
    {
        if (_visuals is null || _curve.Count == 0) return -1;

        int y = screenY - _windowTop;
        if (y < _windowHeight - BarHeight || y > _windowHeight) return -1;

        Reveal();

        float rest = _curve.Invert(screenX - _windowLeft, _windowWidth);
        _visuals.SetCursor(rest);

        (float left, float right) = BarBounds();
        float x = screenX - _windowLeft;
        if (x < left || x > right) return -1;

        return _visuals.HitTest(rest);
    }

    /// <summary>
    /// Decide qué significaría soltar en ese punto de pantalla, y lo enseña: levanta el
    /// icono que recibiría el fichero, o resalta el "+". Devuelve false si ahí no se
    /// puede soltar nada, que es lo que le cambia el cursor al usuario.
    /// </summary>
    public bool AcceptsDropAt(int screenX, int screenY)
    {
        (_dropKind, _dropSlot) = KindAt(screenX, screenY);

        _visuals?.SetDropTarget(_dropKind == DropKind.Open ? _dropSlot : -1, Scale(_config.IconSize) * 0.25f);
        _visuals?.SetAddZone(true);
        _visuals?.SetAddZoneHot(_dropKind == DropKind.Add);
        return _dropKind != DropKind.None;
    }

    private (DropKind Kind, int Slot) KindAt(int screenX, int screenY)
    {
        if (_visuals is null || _curve.Count == 0) return (DropKind.None, -1);

        int y = screenY - _windowTop;
        if (y < _windowHeight - BarHeight || y > _windowHeight) return (DropKind.None, -1);

        // Mientras se arrastra algo por encima, el dock tiene que estar a la vista: si
        // no, no habría dónde soltarlo.
        Reveal();

        float x = screenX - _windowLeft;
        _lastRest = _curve.Invert(x, _windowWidth);
        _visuals.SetCursor(_lastRest);

        (float addLeft, float addRight) = AddZoneBounds();
        if (x >= addLeft && x <= addRight) return (DropKind.Add, -1);

        (float left, float right) = BarBounds();
        if (x < left || x > right) return (DropKind.None, -1);

        int slot = _visuals.HitTest(_lastRest);
        bool app = slot >= 0 && slot < _loaded.Count && _loaded[slot].App.IsApp;

        // Un separador o un documento no abren nada: ni se levantan ni aceptan.
        return app ? (DropKind.Open, slot) : (DropKind.None, -1);
    }

    /// <summary>Extremos de la zona del "+", pegada al borde derecho de la barra.</summary>
    private (float Left, float Right) AddZoneBounds()
    {
        float padding = Scale(LogicalPadding);
        float size = BarHeight * AddZoneFraction;
        float barRight = _curve.Project(_curve.RestWidth, _windowWidth, _lastRest) + padding;
        float gap = size * 0.35f;
        return (barRight + gap, barRight + gap + size);
    }

    /// <summary>El HWND, para el ayudante que dibuja la miniatura de arrastre.</summary>
    public HWND Handle => _hwnd;

    /// <summary>
    /// Apunta lo soltado y se quita de en medio. El trabajo de verdad va por mensaje:
    /// mientras estemos dentro de Drop, el Explorador está esperando a que volvamos.
    /// </summary>
    public void QueueDrop(DroppedItem[] items)
    {
        if (_dropKind == DropKind.None || items.Length == 0) return;

        _pendingDrop = items;
        PInvoke.PostMessage(_hwnd, WM_APP_DROP, default, default);
    }

    /// <summary>El arrastre se fue o terminó: el dock puede volver a esconderse.</summary>
    public void OnDragOutside()
    {
        _visuals?.SetDropTarget(-1, 0f);
        _visuals?.SetAddZone(false);
        _visuals?.SetAddZoneHot(false);
        ScheduleHide();
    }

    /// <summary>Ya fuera del arrastre: abrir lo soltado, o añadirlo al dock.</summary>
    private void OnDropped()
    {
        DroppedItem[] items = _pendingDrop;
        _pendingDrop = [];

        if (items.Length == 0) return;

        if (_dropKind == DropKind.Add) { AddToDock(items); return; }
        if (_dropSlot < 0 || _dropSlot >= _loaded.Count) return;

        DockApp app = _loaded[_dropSlot].App;
        if (!app.IsApp) return;

        // Solo lo que tenga fichero de verdad: a una app no se le puede pasar un objeto
        // virtual que no existe en disco.
        string[] paths = [.. items.Select(i => i.FilePath).OfType<string>()];
        if (paths.Length == 0) return;

        _visuals?.Bounce(_dropSlot, Scale(_config.IconSize) * 0.35f);
        Console.WriteLine($"[dock] '{app.Name}' abre {string.Join(", ", paths)}");

        // Fuera del hilo de UI, como al lanzar: ShellExecuteEx puede tardar segundos.
        Task.Run(() =>
        {
            try { app.OpenWith(paths); }
            catch (Exception ex) { Console.WriteLine($"[dock] falló abrir con '{app.Name}': {ex.Message}"); }
        });
    }

    /// <summary>Mete lo soltado en el dock, al final, y lo guarda.</summary>
    private void AddToDock(DroppedItem[] items)
    {
        List<DockApp> apps = [.. _loaded.Select(entry => entry.App)];
        bool changed = false;

        foreach (DroppedItem item in items)
        {
            string target = item.Target.Replace('/', '\\');

            int existing = apps.FindIndex(a =>
                !a.Separator && a.Target.Equals(target, StringComparison.OrdinalIgnoreCase));

            if (existing >= 0)
            {
                // Ya estaba: se rebota el que hay en vez de duplicarlo. Más barato que
                // un diálogo y se entiende solo.
                _visuals?.Bounce(existing, Scale(_config.IconSize) * 0.35f);
                Console.WriteLine($"[dock] '{item.Name}' ya estaba en el dock");
                continue;
            }

            // Por el mismo filtro que las de dock.json. Si el target no existe, mejor
            // no llegar a guardarlo: quedaría en dock.local.json para siempre y la
            // entrada desaparecería en cada arranque sin decir por qué.
            List<DockApp> ok = DockConfig.Validate([new DockApp { Name = item.Name, Target = target }]);
            if (ok.Count == 0)
            {
                Console.WriteLine($"[dock] no se pudo añadir '{item.Name}': {target}");
                continue;
            }

            apps.Add(ok[0]);
            Console.WriteLine($"[dock] añadida '{item.Name}' -> {target}");
            changed = true;
        }

        if (!changed) return;

        DockLocal.Save(_config.BaseApps, apps);
        _config = new DockConfig
        {
            IconSize = _config.IconSize,
            IconSpacing = _config.IconSpacing,
            AutoHide = _config.AutoHide,
            AutoStart = _config.AutoStart,
            BaseApps = _config.BaseApps,
            Apps = apps,
        };

        StartIconLoad();
    }

    /// <summary>Deshace un arrastre a medias y devuelve todo a su sitio.</summary>
    private void CancelDrag()
    {
        if (!_dragging)
        {
            _pressedIndex = -1;
            return;
        }

        _dragging = false;
        _visuals?.SetLifted(_pressedIndex, false);

        for (int i = 0; i < _loaded.Count; i++) _visuals?.SpringShift(i, 0f);
        _pressedIndex = -1;
    }

    /// <summary>
    /// Monta y arranca el genio hacia el icono <paramref name="index"/>. Devuelve false
    /// si algo no salió, y entonces el minimizado va sin animación.
    /// </summary>
    /// <summary>
    /// Traga la ventana hacia su icono. Devuelve false si algo no salió, y entonces el
    /// minimizado va sin animación.
    /// </summary>
    private bool PlayGenie(int index, HWND window)
    {
        if (!PInvoke.GetWindowRect(window, out RECT rect)) return false;
        if (rect.right <= rect.left || rect.bottom <= rect.top) return false;

        IconBitmap? shot = WindowCapture.Capture(window);
        if (shot is null) return false;

        Box source = new(rect.left, rect.top, rect.right, rect.bottom);

        // Guardado para el camino de vuelta: una ventana minimizada ya no se puede
        // capturar, así que el único fotograma que habrá nunca es este.
        _shots[(nint)window.Value] = (shot, source);

        return Genie(index, source, shot, reverse: false, onFinished: null);
    }

    /// <summary>
    /// La saca del icono. Solo funciona si la minimizó el propio dock, porque es de ahí
    /// de donde sale el fotograma.
    /// </summary>
    private bool PlayGenieBack(int index, HWND window)
    {
        if (!_shots.Remove((nint)window.Value, out (IconBitmap Shot, Box Source) saved)) return false;

        return Genie(index, saved.Source, saved.Shot, reverse: true,
            onFinished: () => WindowActions.BringToFront(window, instant: true));
    }

    private bool Genie(int index, Box source, IconBitmap shot, bool reverse, Action? onFinished)
    {
        if (_visuals is null || index >= _curve.Count) return false;

        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        if (!PInvoke.GetMonitorInfo(_monitor, &info)) return false;

        // Dónde está el icono AHORA mismo, magnificado y todo: el ratón está encima de
        // él, así que hay que preguntárselo a la curva, no a la posición en reposo.
        float left = _windowLeft + _curve.Project(_curve.RestLeft(index), _windowWidth, _lastRest);
        float right = _windowLeft + _curve.Project(_curve.RestRight(index), _windowWidth, _lastRest);

        // El icono crece hacia arriba desde su borde inferior, que no se mueve.
        float bottom = _windowTop + _windowHeight - Scale(LogicalPadding);
        float size = right - left;

        // El destino NO es el icono entero: es una franja fina en su centro. Apuntando
        // al icono completo, la ventana acababa como una miniatura de 60x60 parada
        // encima, y eso se lee como "ha parado y ha desaparecido" en vez de "se lo ha
        // tragado". Con la franja, el último tramo se consume al entrar.
        float middle = bottom - size * 0.5f;
        float thin = size * 0.10f;
        Box target = new(left, middle - thin, right, middle + thin);

        return GenieOverlay.Play(
            _visuals.Compositor,
            info.rcMonitor,
            _visuals.CreateBitmapBrush(shot),
            new Vector2(shot.Width, shot.Height),
            new GenieCurve(source, target, GenieOverlay.Slices),
            reverse,
            onFinished);
    }

    private void OnLeftClick(LPARAM lParam)
    {
        int index = _visuals?.HitTest(_lastRest) ?? -1;
        if (index < 0 || index >= _loaded.Count) return;

        DockApp app = _loaded[index].App;
        if (app.Separator) return;

        // Tres estados, como la barra de tareas de Windows. Esto es lo que autoriza la
        // enmienda 1 de SEGURIDAD.md: se toca una ventana ajena SOLO aquí, como
        // respuesta directa a un clic sobre su icono, y nunca desde ningún otro sitio.
        AppState state = index < _state.Length ? _state[index] : default;

        if (state.HasWindow && WindowActions.IsForeground(state.MainWindow))
        {
            // Ya la estabas mirando: el segundo clic la esconde. El genio se monta
            // ANTES de minimizar, porque para capturarla tiene que estar todavía ahí.
            // Si la captura falla, se minimiza a secas: degradar es mejor que romperse.
            bool genie = PlayGenie(index, state.MainWindow);
            WindowActions.Minimize(state.MainWindow, instant: genie);
            Console.WriteLine($"[dock] minimizada '{app.Name}'{(genie ? " con genio" : "")}");
            return;
        }

        // El rebote arranca ya, sin esperar: es acuse de recibo del clic. Corre en el
        // compositor, así que no le afecta lo que tarde nada de lo de abajo.
        _visuals?.Bounce(index, Scale(_config.IconSize) * 0.35f);

        if (state.HasWindow)
        {
            // Si la minimizó el dock, vuelve saliendo del icono. El genio restaura la
            // ventana él mismo al acabar, por eso aquí no se hace nada más.
            bool genie = WindowActions.IsMinimized(state.MainWindow) && PlayGenieBack(index, state.MainWindow);
            if (!genie) WindowActions.BringToFront(state.MainWindow);

            Console.WriteLine($"[dock] al frente '{app.Name}'{(genie ? " con genio" : "")}");
            return;
        }

        // Y sigue botando hasta que la app tenga ventana, como en macOS: es el único
        // aviso de que el clic llegó cuando una app tarda en arrancar.
        _visuals?.Bounce(index, Scale(_config.IconSize) * 0.35f, forever: true);
        _launchingIndex = index;
        _launchingUntil = DateTime.UtcNow.AddSeconds(20);

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
        if (!_hwnd.IsNull && _dropTarget is not null) PInvoke.RevokeDragDrop(_hwnd);
        _dropTarget = null;

        _visuals?.Dispose();
        PInvoke.DestroyWindow(_hwnd);
        _hwnd = default;
    }
}
