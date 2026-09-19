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

    /// Hueco por encima del icono magnificado, en unidades lógicas. La ventana medía
    /// exactamente lo que el icono más grande, así que sin esto ni la etiqueta ni el
    /// menú cabrían.
    ///
    /// Lo dimensiona el MENÚ, no la etiqueta: al bajar la magnificación la ventana
    /// encogió y el menú pasó a dibujarse fuera de ella, donde queda recortado y no se
    /// puede clicar. La etiqueta sola necesitaría menos de la mitad.
    ///
    /// Eran 76, dos filas de 30 más sus márgenes, que es lo que pedía el menú del clic
    /// derecho cuando solo tenía dos entradas. Ahora ese menú lleva además los
    /// documentos recientes de la app, y la lista de ventanas de la rueda quiere sitio
    /// para varias: con 200 entran SEIS filas a cualquier magnificación, y de ahí en
    /// adelante la lista se desplaza con la selección.
    ///
    /// Que el hueco sea generoso no le quita sitio al escritorio: cuando el ratón no
    /// está encima, la región de la ventana llega solo hasta la barra más lo que crece
    /// el icono magnificado, sin contar esto (ver ApplyRegion).
    private const int LogicalLabelRoom = 200;

    /// Lo que hay que dejar por encima del icono magnificado para que quepa la
    /// etiqueta. NO es lo mismo que LogicalLabelRoom: aquel es el hueco que se reserva
    /// en la VENTANA para que quepa el menú; este es lo único que hace falta que la
    /// región deje pasar cuando solo hay una etiqueta.
    private const int LogicalLabelStrip = 36;

    /// Cuántos recientes caben en el menú del clic derecho, dejando sitio a sus dos
    /// entradas de siempre.
    private const int MenuJumpLimit = 4;

    /// Margen antes de esconderse al salir el ratón. Sin él, rozar el dock de paso
    /// lo haría parpadear.
    private const uint HideDelayMs = 450;

    /// Lo que hay que mover el ratón con el botón pulsado para que deje de ser un clic
    /// y pase a ser un arrastre, en unidades lógicas. Por debajo de esto, un pulso con
    /// mano temblorosa seguiría lanzando la app, que es lo que se espera.
    private const int LogicalDragThreshold = 6;

    /// Cuánto hay que subir por encima de la barra para que soltar signifique quitar.
    private const int LogicalPullOffDistance = 40;

    /// Ancho de la zona del "+" a la derecha de la barra, en unidades lógicas. Tiene
    /// que casar con lo que dibuja DockVisuals.BuildAddZone.
    private const float AddZoneFraction = 0.58f;

    /// Radio de influencia del cursor, medido en ranuras. Junto con la magnificación
    /// (que ahora vive en dock.json) son los dos mandos que gobiernan el tacto, y los
    /// únicos números de aquí que piden ajustarse a ojo.
    ///
    /// A 2.5 el bulto abarcaba casi un dock de 4 iconos y lo ensanchaba un 62%, mucho
    /// más de lo que hace macOS. Con 1.75 se magnifican unos 3 iconos.
    private const float RadiusInSlots = 1.75f;

    // Mensajes que manejamos. Se declaran aquí para no arrastrar cientos de
    // constantes desde la metadata del SDK.
    private const uint WM_DESTROY = 0x0002;
    private const uint WM_DISPLAYCHANGE = 0x007E;
    private const uint WM_ACTIVATE = 0x0006;
    private const uint WM_WINDOWPOSCHANGED = 0x0047;
    private const uint WM_HOTKEY = 0x0312;
    private const uint WM_MOUSEACTIVATE = 0x0021;
    private const uint WM_NCCALCSIZE = 0x0083;
    private const uint WM_NCACTIVATE = 0x0086;
    private const uint WM_MOUSEMOVE = 0x0200;
    private const uint WM_LBUTTONDOWN = 0x0201;
    private const uint WM_LBUTTONUP = 0x0202;
    private const uint WM_CAPTURECHANGED = 0x0215;
    private const uint WM_RBUTTONUP = 0x0205;
    private const uint WM_MBUTTONUP = 0x0208;
    private const uint WM_CLOSE = 0x0010;
    private const uint WM_MOUSEWHEEL = 0x020A;
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

    /// Cada cuánto se reafirma el z-order <b>por reloj</b>.
    ///
    /// Fue un segundo, luego 250 ms —quedarse debajo de la barra de tareas un segundo
    /// entero se veía—, y ahora vuelve a ser un segundo por otra razón: desde M4 el
    /// shell nos avisa cuando se activa una ventana, que es justo cuando el z-order
    /// cambia, y ahí se reafirma al instante. El reloj queda de red de seguridad para
    /// lo que no genera aviso.
    ///
    /// Medido: a 250 ms el dock gastaba 4,1% de un núcleo en reposo con tres pantallas.
    private const uint WatchdogMs = 1000;

    /// Red de seguridad del inventario de ventanas.
    ///
    /// El barrido ya no va por latido sino por aviso del shell. Esto queda para el caso
    /// que los avisos no cubren: una ventana que nace sin título, se titula después y
    /// nadie la activa nunca. Diez segundos frente al segundo de antes.
    private const uint RunningSafetyMs = 10000;

    /// Rebote de los avisos del shell: llegan a rachas al abrir una app.
    private const uint ShellHookDebounceMs = 400;

    /// Identificador del temporizador de rebote del inventario.
    private const nuint RunningTimerId = 4;

    /// Identificador del temporizador de red de seguridad del inventario.
    private const nuint SafetyTimerId = 5;

    /// Cada cuánto se vuelve a capturar la ventana que se está previsualizando.
    ///
    /// No es vídeo: es para que la miniatura no se quede congelada si la ventana cambia
    /// mientras la miras. Medio segundo basta y cuesta un PrintWindow, no una sesión de
    /// captura abierta (ver la enmienda 3 de SEGURIDAD.md).
    private const uint PreviewRefreshMs = 500;

    /// Identificador del temporizador que refresca la miniatura.
    private const nuint PreviewTimerId = 6;

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

    /// Ya se leyó el contenido de una carpeta y se puede desplegar. WM_APP + 5.
    private const uint WM_APP_STACK = 0x8005;

    /// <summary>
    /// Cambió el conjunto de pantallas. WM_APP + 6, y va al HILO, no a una ventana:
    /// atenderlo implica destruir docks, y hacerlo dentro del WndProc de uno de ellos
    /// sería destruir la ventana cuyo mensaje se está despachando.
    /// </summary>
    private const uint WM_APP_DISPLAYS = 0x8006;

    /// <summary>Avisos ABN_* del shell a nuestra appbar. WM_APP + 7.</summary>
    private const uint WM_APP_APPBAR = 0x8007;

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

    /// <summary>
    /// Id del mensaje "SHELLHOOK". No es constante de compilación: hay que pedirlo con
    /// RegisterWindowMessage y resolverlo en el default del switch.
    /// </summary>
    private static uint _shellHookMessage;

    /// <summary>Quién tiene el registro. Uno por proceso basta: los avisos son los mismos.</summary>
    private static HWND _shellHookOwner;

    /// <summary>Quién tiene registrado el atajo de perfiles. Uno por proceso basta.</summary>
    private static HWND _hotkeyOwner;

    /// <summary>Barrido en curso. Es uno para todos los docks, que comparten la pasada.</summary>
    private static bool _checkingRunning;

    private readonly HMONITOR _monitor;

    /// <summary>Clave de esta pantalla en dock.json y dock.local.json.</summary>
    private readonly string _device;

    /// <summary>El registro como barra de herramientas de escritorio.</summary>
    private AppBar? _appBar;

    /// <summary>El hueco que el sistema nos concedio como appbar, o null si no hay.</summary>
    private RECT? _reserved;

    /// <summary>Guardia contra que ABM_SETPOS dispare el ABN_POSCHANGED que lo llamo.</summary>
    private bool _inAppBarPos;

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

    /// <summary>
    /// Apps abiertas que no están ancladas. No se guardan en ningún sitio: se calculan
    /// del inventario y desaparecen al cerrar la app.
    /// </summary>
    private List<DockApp> _extras = [];

    /// Estado de cada app: si corre y dónde está su ventana. Se recalcula fuera del
    /// hilo de UI porque resolver las MSIX obliga a recorrer procesos y ventanas.
    private AppState[] _state = [];
    private string _lastRunningTrace = " ";

    /// <summary>Icono sobre el que se está girando la rueda, y por qué ventana va.</summary>
    private int _wheelIndex = -1;
    private int _wheelAt;
    private HWND[] _wheelWindows = [];

    /// <summary>Primera ventana de las que se están viendo, porque no caben todas.</summary>
    private int _wheelFirst;

    /// <summary>La miniatura de la ventana elegida, mientras la lista está abierta.</summary>
    private PreviewOverlay? _preview;

    /// <summary>
    /// Icono que está botando porque su app se está abriendo, y hasta cuándo. El tope
    /// existe porque no toda app acaba teniendo ventana: si se le lanza un instalador o
    /// algo que no abre nada, el icono no puede quedarse botando para siempre.
    /// </summary>
    private int _launchingIndex = -1;
    private DateTime _launchingUntil;

    /// <summary>Icono sobre el que se abrió el menú, o -1 si se abrió en hueco.</summary>
    private int _menuIndex = -1;

    /// <summary>Documentos recientes que enseña el menú abierto ahora mismo.</summary>
    private List<JumpItem> _menuJumps = [];

    /// <summary>La rejilla desplegada ahora mismo, si hay alguna.</summary>
    private StackOverlay? _stack;

    /// <summary>Contenido leído en segundo plano, esperando a dibujarse.</summary>
    private (int Index, string Folder, List<StackItem> Items)? _pendingStack;

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

    /// <summary>Si hay algo a pantalla completa en ESTA pantalla y el dock se ha apartado.</summary>
    private bool _fullscreen;

    /// Y de pantalla a partir de la cual empieza la franja que asoma.
    private int _revealTop;

    /// Extremos de la última región aplicada, para no reaplicarla igual.
    private int _regionLeft = int.MinValue;
    private int _regionRight = int.MinValue;
    private int _regionTop = int.MinValue;

    /// <summary>
    /// Si la región llega hasta arriba del todo. Solo hace falta cuando hay algo
    /// dibujado ahí: la etiqueta, el menú o el icono magnificado.
    /// </summary>
    private bool _tallRegion;

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

    public DockWindow(HMONITOR monitor, DockConfig raw)
    {
        _monitor = monitor;
        _device = DeviceNameOf(monitor);
        _config = raw.For(_device);

        EnsureClassRegistered();
        Create();
    }

    /// <summary>
    /// El nombre de dispositivo del monitor (<c>\\.\DISPLAY2</c>), que es la clave con
    /// la que se guardan sus apps. El HMONITOR no sirve: cambia entre arranques.
    /// </summary>
    public static string DeviceNameOf(HMONITOR monitor)
    {
        MONITORINFOEXW info = default;
        info.monitorInfo.cbSize = (uint)sizeof(MONITORINFOEXW);

        if (!PInvoke.GetMonitorInfo(monitor, (MONITORINFO*)&info)) return "";

        return new string((char*)&info.szDevice).TrimEnd('\0');
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

        _appBar = new AppBar(_hwnd, WM_APP_APPBAR);
        ReserveAppBarSpace();

        RegisterShellHook();

        if (_hotkeyOwner.IsNull && Profiles.Register(_hwnd, _config.AtajoPerfil)) _hotkeyOwner = _hwnd;
        PInvoke.SetTimer(_hwnd, SafetyTimerId, RunningSafetyMs, null);

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

        return new DockCurve(slots, radius: (icon + spacing) * RadiusInSlots, maxScale: _config.Magnification);
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
            Scale(_config.IconSize) * _config.Magnification + padding * 2f + Scale(LogicalLabelRoom));

        // Si tenemos hueco reservado como appbar, el suelo es el que el sistema nos
        // concedió, NO el área de trabajo: el área de trabajo ya descuenta nuestra
        // propia reserva, así que recalcular desde ella nos subiría un poco en cada
        // aviso, y al siguiente otro poco. Una escalera infinita.
        RECT work = _reserved ?? info.rcWork;
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
    /// <summary>
    /// La lista que se dibuja: lo anclado, luego un separador, luego lo que está
    /// abierto sin anclar, y la papelera siempre la última, como en macOS.
    /// </summary>
    /// <summary>
    /// Nombre del separador que <see cref="Compose"/> mete delante de las apps
    /// abiertas. Sirve para reconocerlo y no guardarlo: no es del usuario.
    /// </summary>
    private const string RunningSeparator = "|abiertas|";

    private List<DockApp> Compose()
    {
        if (_extras.Count == 0) return _config.Apps;

        List<DockApp> ancladas = [.. _config.Apps];
        DockApp? papelera = ancladas.FirstOrDefault(a => a.Target == DockConfig.TrashTarget);
        if (papelera is not null) ancladas.Remove(papelera);

        List<DockApp> todas =
            [.. ancladas, new DockApp { Separator = true, Name = RunningSeparator }, .. _extras];
        if (papelera is not null) todas.Add(papelera);

        return todas;
    }

    /// <summary>
    /// De la lista DIBUJADA a la lista ANCLADA: quita las apps abiertas sin anclar y el
    /// separador que las precede.
    ///
    /// <b>Todo lo que se guarde en dock.local.json tiene que pasar por aquí.</b> Las
    /// tres rutas que guardan —reordenar, quitar y añadir— construyen la lista a partir
    /// de lo que hay dibujado, así que sin este filtro reordenar un icono anclaría de
    /// paso todo lo que estuviera abierto en ese momento.
    /// </summary>
    /// <summary>Si esa entrada solo está ahí porque la app está abierta.</summary>
    private bool EsExtra(DockApp app)
        => !app.Separator && _extras.Any(e => e.Target.Equals(app.Target, StringComparison.OrdinalIgnoreCase));

    private List<DockApp> OnlyPinned(IEnumerable<DockApp> drawn)
    {
        if (_extras.Count == 0) return [.. drawn];

        HashSet<string> abiertas = new(_extras.Select(app => app.Target), StringComparer.OrdinalIgnoreCase);

        return [.. drawn.Where(app => app.Separator
            ? app.Name != RunningSeparator
            : !abiertas.Contains(app.Target))];
    }

    private void StartIconLoad()
    {
        HWND hwnd = _hwnd;
        List<DockApp> apps = Compose();

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

        if (Program.Startup is { } reloj)
        {
            Console.WriteLine($"[iconos] {_loaded.Count} elementos ({reloj.ElapsedMilliseconds} ms desde el arranque)");
            if (Instances.Count > 1) Program.Startup = null;   // solo el primero deja la marca
        }
        else
        {
            Console.WriteLine($"[iconos] {_loaded.Count} elementos");
        }

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
        Console.WriteLine($"[dock] {_device} al {_dpi * 100 / 96}%, {_config.Apps.Count} iconos " +
            $"(HWND=0x{(nint)_hwnd.Value:X})");
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

            case WM_TIMER when wParam.Value == PreviewTimerId:
                self?.ShowPreview();
                return new LRESULT(0);

            case WM_TIMER when wParam.Value == RunningTimerId:
                PInvoke.KillTimer(hwnd, RunningTimerId);
                RefreshRunning();
                return new LRESULT(0);

            case WM_TIMER when wParam.Value == SafetyTimerId:
                RefreshRunning();
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

            case WM_APP_STACK:
                self?.ShowStack();
                return new LRESULT(0);

            case WM_APP_DROP:
                self?.OnDropped();
                return new LRESULT(0);

            case WM_APP_RUNNING:
                if (self is not null)
                {
                    if (wParam.Value != 0)
                    {
                        // Cambio que apps hay abiertas sin anclar: hay que rehacer la
                        // barra entera. StartIconLoad acaba volviendo aqui con 0.
                        self.StartIconLoad();
                        return new LRESULT(0);
                    }

                    self.TraceRunning();
                    self._visuals?.SetRunning([.. self._state.Select(entry => entry.HasWindow)]);
                    self.DropDeadShots();
                    self.UpdateSmartHide();
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

                        // Sacar el ratón del dock cierra el menú, y tiene que ser aquí:
                        // un clic FUERA del dock no nos llega —el hit-test lo deja
                        // pasar—, así que sin esto el menú se quedaba abierto, y con él
                        // abierto el dock tampoco se escondía.
                        self.CloseMenu();
                        self.SetTallRegion(false);
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
                self?.OnRightClick();
                return new LRESULT(0);

            case WM_MBUTTONUP:
                self?.OnMiddleClick();
                return new LRESULT(0);

            case WM_MOUSEWHEEL:
                self?.OnWheel(wParam);
                return new LRESULT(0);

            case WM_DPICHANGED:
                self?.OnDpiChanged(wParam, lParam);
                return new LRESULT(0);

            case WM_ACTIVATE:
                self?._appBar?.Activated();
                return new LRESULT(0);

            case WM_WINDOWPOSCHANGED:
                self?._appBar?.Moved();
                break;

            case WM_HOTKEY:
                self?.RotateProfile();
                return new LRESULT(0);

            case WM_APP_APPBAR:
                self?.OnAppBarNotify(wParam.Value);
                return new LRESULT(0);

            case WM_DISPLAYCHANGE:
                // Llega a las tres ventanas; el rebote deja una sola reconstrucción.
                if (!_displaysPending)
                {
                    _displaysPending = true;
                    PInvoke.PostMessage(HWND.Null, WM_APP_DISPLAYS, default, default);
                }
                return new LRESULT(0);

            case WM_DESTROY:
                if (hwnd == _hotkeyOwner)
                {
                    Profiles.Unregister(hwnd);
                    _hotkeyOwner = default;
                }

                if (hwnd == _shellHookOwner)
                {
                    PInvoke.DeregisterShellHookWindow(hwnd);
                    _shellHookOwner = default;
                }

                Instances.Remove((nint)hwnd.Value);

                // Solo el último apaga la luz. Con un dock daba igual; con uno por
                // pantalla, cualquier DestroyWindow inesperado se llevaba los tres.
                if (Instances.Count == 0) PInvoke.PostQuitMessage(0);
                return new LRESULT(0);
        }

        // El id de "SHELLHOOK" se pide en tiempo de ejecución, así que no puede ser un
        // case del switch.
        if (_shellHookMessage != 0 && msg == _shellHookMessage)
        {
            OnShellHook((nuint)wParam.Value);
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

    /// <summary>
    /// Avisa a los demás docks de que la superposición local acaba de cambiar.
    ///
    /// El vigilante de ficheros no sirve para esto: filtra por <c>dock.json</c> exacto,
    /// a propósito, para que escribir <c>dock.local.json</c> no provoque una recarga en
    /// bucle. La consecuencia con varias pantallas era que reordenar en una dejaba a
    /// las otras con el orden viejo hasta reiniciar. Se avisa directamente, sin pasar
    /// por el disco: cada hermano vuelve a leer y aplica la superposición recién
    /// guardada.
    /// </summary>
    private void ReloadSiblings()
    {
        foreach (DockWindow sibling in Instances.Values)
        {
            if (sibling != this) sibling.RequestReload();
        }
    }

    private void OnReload()
    {
        DockConfig fresh;
        try
        {
            fresh = DockConfig.Load(DockConfig.DefaultPath).For(_device);
        }
        catch (Exception ex)
        {
            // Un JSON a medio guardar o mal escrito no puede tumbar el dock: se avisa
            // y se sigue con la configuración anterior.
            Console.WriteLine($"[config] no se pudo recargar, se mantiene la anterior: {ex.Message}");
            return;
        }

        _config = fresh;

        // El tamaño depende del número de iconos, así que hay que recolocar. Y la
        // reserva puede cambiar con la config: autoHide entra y sale de ella.
        ReserveAppBarSpace();
        Reposition();

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

        // Por encima del techo de la región no somos nadie. La región ya lo corta, así
        // que esto es cinturón además de tirantes — pero OJO: en WM_NCHITTEST lParam
        // viene en coordenadas de PANTALLA y la región está en coordenadas de cliente.
        if (HiWord(lParam) - _windowTop < _regionTop) return HTTRANSPARENT;

        // Por encima, solo la barra. La ventana ocupa el ancho de la pantalla, así que
        // sin esto se tragaría cualquier clic en la franja inferior del escritorio.
        (float left, float right) = BarBounds();
        float x = LoWord(lParam) - _windowLeft;
        return x >= left && x <= right ? HTCLIENT : HTTRANSPARENT;
    }

    /// <summary>Extremos de la barra en coordenadas de cliente, ahora mismo.</summary>
    /// <summary>La barra visible, en coordenadas de pantalla.</summary>
    private RECT BarRect()
    {
        (float left, float right) = BarBounds();
        return new RECT
        {
            left = (int)(_windowLeft + left),
            top = (int)(_windowTop + _windowHeight - BarHeight),
            right = (int)(_windowLeft + right),
            bottom = _windowTop + (int)_windowHeight,
        };
    }

    /// <summary>
    /// Reevalúa si toca esconderse o salir, ahora que las ventanas han cambiado. Lo
    /// llama el inventario, que va por avisos del shell: abrir, cerrar o activar una
    /// ventana es justo cuando esto puede cambiar.
    /// </summary>
    private void UpdateSmartHide()
    {
        if (!_config.AutoHide || _hovering || _dragging || _fullscreen) return;
        if (_visuals?.MenuOpen == true || _stack is not null) return;

        bool tapado = Running.AnythingOver(BarRect());
        if (tapado == _hidden) return;

        if (tapado) Hide(force: true);
        else Reveal();

        Console.WriteLine($"[autoocultar] {_device}: {(tapado ? "escondido, hay algo debajo" : "a la vista, no hay nada debajo")}");
    }

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

        if (Environment.GetEnvironmentVariable("DOCK_HOVER_LOG") is not null)
            Console.WriteLine($"[visible] {_device}");

        _hidden = false;
        _visuals?.SetHidden(false, HiddenOffset);
    }

    /// <summary>Programa el ocultamiento, con margen para no parpadear al rozarlo.</summary>
    private void ScheduleHide()
    {
        // Con el menú o la rejilla abiertos no: se llevaría por delante lo que el
        // usuario está mirando. Y para LLEGAR a la rejilla hay que salirse del dock,
        // así que sin esto se cerraba sola de camino.
        if (_visuals?.MenuOpen == true || _stack is not null) return;

        if (!_config.AutoHide) return;
        PInvoke.SetTimer(_hwnd, HideTimerId, HideDelayMs, null);
    }

    /// <summary>
    /// Esconde el dock. Con <paramref name="force"/> se salta la comprobación de si hay
    /// algo debajo: lo usa el pleno, porque un juego a pantalla completa puede no tener
    /// título y entonces la criba de ventanas no lo vería.
    /// </summary>
    private void Hide(bool force = false)
    {
        PInvoke.KillTimer(_hwnd, HideTimerId);
        if (_hidden || !_config.AutoHide) return;

        // Autoocultar inteligente: si no hay ninguna ventana debajo, no hay de qué
        // esconderse. Con el escritorio a la vista el dock se queda donde está, que es
        // lo que se pidió; esconderse por reloj teniendo sitio de sobra no vale de nada.
        if (!force && !Running.AnythingOver(BarRect())) return;

        if (Environment.GetEnvironmentVariable("DOCK_HOVER_LOG") is not null)
            Console.WriteLine($"[escondido] {_device}");

        _hidden = true;
        _visuals?.SetLabel(-1);

        // El menú vive dentro de esta ventana, así que se va con ella. La rejilla no:
        // es una ventana aparte y se cierra por su cuenta al salirse el ratón.
        CloseMenu();
        _visuals?.SetHidden(true, HiddenOffset);
    }

    /// <summary>
    /// Pide al sistema el hueco del borde inferior, si toca pedirlo.
    ///
    /// <b>Con el autoocultar puesto no se reserva nada</b>, a propósito: el dock está
    /// escondido casi todo el rato, y quitarle una franja a todas las ventanas por algo
    /// que no se ve sería robar pantalla. El registro como appbar sigue valiendo igual,
    /// que es de donde vienen los avisos ABN_*.
    /// </summary>
    private void ReserveAppBarSpace()
    {
        if (_appBar is null || !_appBar.Registered || _inAppBarPos) return;

        if (_config.AutoHide)
        {
            _reserved = null;
            return;
        }

        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        if (!PInvoke.GetMonitorInfo(_monitor, &info)) return;

        RECT wanted = info.rcMonitor;
        wanted.top = wanted.bottom - (int)MathF.Ceiling(BarHeight + Scale(LogicalBottomMargin));

        // El guardia no es paranoia: ABM_SETPOS puede provocar el ABN_POSCHANGED que
        // nos trajo hasta aquí, y eso se muerde la cola.
        _inAppBarPos = true;
        try { _reserved = _appBar.Reserve(wanted); }
        finally { _inAppBarPos = false; }

        Console.WriteLine($"[appbar] {_device} reserva {_reserved.Value.bottom - _reserved.Value.top} px "
            + $"del borde inferior (se pidieron {wanted.bottom - wanted.top})");
    }

    /// <summary>Lo que el shell nos cuenta de nuestra appbar.</summary>
    private void OnAppBarNotify(nuint notification)
    {
        switch (notification)
        {
            case AppBar.ABN_POSCHANGED:
                Console.WriteLine($"[appbar] {_device} ABN_POSCHANGED");
                // Cambió la barra de tareas, o apareció otra appbar en este borde.
                ReserveAppBarSpace();
                Reposition();
                break;

            case AppBar.ABN_FULLSCREENAPP:
                Console.WriteLine($"[appbar] {_device} ABN_FULLSCREENAPP");
                // No se mira el TRUE/FALSE que trae: el aviso es global y el dock solo
                // debe esconderse si el pleno está en SU pantalla. El latido ya sabe
                // decidir eso; esto solo adelanta la decisión hasta 250 ms.
                OnWatchdogTick();
                break;
        }
    }

    private void Reposition()
    {
        (int x, int y, int w, int h) = ComputeBounds();
        PInvoke.SetWindowPos(_hwnd, default, x, y, w, h,
            SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE | SET_WINDOW_POS_FLAGS.SWP_NOZORDER);
    }

    /// <summary>
    /// Latido: reafirma el z-order y mira si hay algo a pantalla completa. El
    /// inventario de ventanas ya NO va aquí, va por avisos del shell.
    /// </summary>
    private void OnWatchdogTick()
    {
        bool pleno = IsFullscreenAppRunning();

        if (pleno != _fullscreen)
        {
            _fullscreen = pleno;

            // Con algo a pantalla completa el dock se aparta DEL TODO, no solo se
            // esconde. Esconderse baja los visuals pero la ventana se queda, y con ella
            // la franja de revelado del borde inferior: bajar el ratón para tocar la
            // barra de progreso de un vídeo revelaba el dock encima y el clic se lo
            // comía él. Ocultando la ventana, ese borde vuelve a ser del vídeo.
            PInvoke.ShowWindow(_hwnd, pleno
                ? SHOW_WINDOW_CMD.SW_HIDE
                : SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE);

            Console.WriteLine($"[pleno] {_device}: {(pleno ? "el dock se aparta" : "el dock vuelve")}");
        }

        if (pleno)
        {
            // Nada de reafirmar el z-order por encima de un juego o un vídeo.
            Hide(force: true);
            return;
        }

        EnsureTopmost();
    }

    /// <summary>
    /// Lee la carpeta fuera del hilo de UI y avisa cuando esté. Extraer veinte iconos
    /// del shell tarda lo suyo, y este hilo es el que atiende el ratón.
    /// </summary>
    private void OpenStack(int index, string folder)
    {
        HWND hwnd = _hwnd;
        Task.Run(() =>
        {
            List<StackItem> items = StackItems.Read(folder);
            _pendingStack = (index, folder, items);
            PInvoke.PostMessage(hwnd, WM_APP_STACK, default, default);
        });
    }

    private void ShowStack()
    {
        if (_pendingStack is not (int index, string folder, List<StackItem> items)) return;
        _pendingStack = null;

        if (_visuals is null || index >= _curve.Count) return;

        if (items.Count == 0)
        {
            // Vacía o no enumerable: se abre como siempre y ya.
            Console.WriteLine($"[stack] {folder} no tiene nada que desplegar");
            Task.Run(() => { try { _loaded[index].App.Launch(); } catch { } });
            return;
        }

        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        if (!PInvoke.GetMonitorInfo(_monitor, &info)) return;

        float anchor = _windowLeft
            + _curve.Project((_curve.RestLeft(index) + _curve.RestRight(index)) * 0.5f, _windowWidth, _lastRest);

        _stack = StackOverlay.Open(_visuals, items, folder, _visuals.Scale, anchor, _windowTop, info.rcMonitor);
        if (_stack is null) return;

        // Al cerrarse la rejilla el dock vuelve a poder esconderse: mientras estaba
        // abierta se le prohibió, y su WM_MOUSELEAVE ya pasó hace rato.
        _stack.Closed = () => { _stack = null; ScheduleHide(); };
        Console.WriteLine($"[stack] {items.Count} elementos de {folder}");
    }

    private void CloseStack()
    {
        StackOverlay? open = _stack;
        _stack = null;
        open?.Dispose();
    }

    /// <summary>
    /// Cierra el menú, sea el del clic derecho o la lista de ventanas de la rueda. Lo
    /// segundo importa: si no se olvidara la lista, el siguiente clic en el menú del
    /// clic derecho se interpretaría como elegir una ventana.
    /// </summary>
    private void CloseMenu()
    {
        _visuals?.CloseMenu();
        _wheelIndex = -1;
        _menuJumps = [];
        ClosePreview();
        ApplyRegion();
    }

    /// <summary>Cierra lo que esté abierto encima del dock antes de hacer otra cosa.</summary>
    private void CloseMenuAndStack(string? exceptFolder = null)
    {
        CloseMenu();
        if (_stack is not null && _stack.Folder != exceptFolder) CloseStack();
    }

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

    /// <summary>
    /// Pide al shell que nos avise cuando nazca o muera una ventana, en vez de sondear.
    ///
    /// <b>No es un hook.</b> La documentación lo contrasta ella misma con el otro:
    /// <i>"the messages are received through the specified window's WindowProc and not
    /// through a call back procedure"</i>. No se carga ninguna DLL en ningún proceso
    /// ajeno y no se instala ningún callback de bajo nivel: los avisos llegan a nuestro
    /// WndProc como cualquier otro mensaje. La regla 3 de SEGURIDAD.md sigue intacta.
    ///
    /// Uno por proceso basta: los avisos son los mismos para las tres pantallas.
    /// </summary>
    private void RegisterShellHook()
    {
        if (!_shellHookOwner.IsNull) return;

        if (_shellHookMessage == 0)
        {
            fixed (char* name = "SHELLHOOK")
            {
                _shellHookMessage = PInvoke.RegisterWindowMessage(new PCWSTR(name));
            }
        }

        if (_shellHookMessage == 0 || !PInvoke.RegisterShellHookWindow(_hwnd))
        {
            // Sin avisos queda la red de seguridad, que barre cada diez segundos.
            Console.WriteLine("[shell] RegisterShellHookWindow rechazado");
            return;
        }

        _shellHookOwner = _hwnd;
        Console.WriteLine($"[shell] avisos de ventanas en {_device}");
    }

    /// <summary>
    /// Pasa al siguiente perfil. Lo guarda y avisa a TODOS los docks, incluido este: la
    /// lista nueva se lee del disco como cualquier otra recarga, así que no hay dos
    /// caminos que mantener.
    /// </summary>
    private void RotateProfile()
    {
        DockConfig raw;
        try
        {
            raw = DockConfig.Load(DockConfig.DefaultPath);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[perfil] no se pudo releer dock.json: {ex.Message}");
            return;
        }

        string siguiente = Profiles.Next(raw, DockLocal.Load().Perfil);
        DockLocal.SaveProfile(siguiente);

        Console.WriteLine($"[perfil] ahora: {(siguiente.Length == 0 ? "(el de siempre)" : siguiente)}");

        foreach (DockWindow dock in Instances.Values) dock.RequestReload();
    }

    /// <summary>
    /// Lo que el shell cuenta de las ventanas del escritorio. No hay
    /// HSHELL_WINDOWMINIMIZED: minimizar no está en la lista, y no hace falta, porque
    /// el puntito significa "tiene ventana" y una minimizada la sigue teniendo.
    /// </summary>
    private static void OnShellHook(nuint code)
    {
        // RUDEAPPACTIVATED es WINDOWACTIVATED con el bit alto puesto.
        switch (code & ~(nuint)0x8000)
        {
            case 4:   // HSHELL_WINDOWACTIVATED, y RUDEAPPACTIVATED con el bit quitado
                // Activar una ventana es lo que cambia el z-order, así que es aquí
                // donde reafirmarlo, y no cuatro veces por segundo por si acaso.
                foreach (DockWindow dock in Instances.Values) dock.OnWatchdogTick();
                ScheduleRunningRefresh();
                break;

            case 1:   // HSHELL_WINDOWCREATED
            case 2:   // HSHELL_WINDOWDESTROYED
            case 13:  // HSHELL_WINDOWREPLACED
                ScheduleRunningRefresh();
                break;
        }
    }

    /// <summary>
    /// Mira qué apps están abiertas, FUERA del hilo de UI: recorrer la lista de
    /// procesos y todas las ventanas no puede bloquear el ratón.
    ///
    /// Es estático y hace UNA pasada para todos los docks. Lo caro —enumerar procesos y
    /// ventanas— no depende de qué iconos tenga cada pantalla, y con una pantalla por
    /// dock se estaba repitiendo entero tres veces.
    /// </summary>
    private static void RefreshRunning()
    {
        if (_checkingRunning) return;

        // La lista de apps se lee aquí, en el hilo de UI, no dentro de la tarea.
        List<(DockWindow Dock, List<DockApp> Dibujadas, List<DockApp> Ancladas)> targets = [];
        foreach (DockWindow dock in Instances.Values)
        {
            if (dock._loaded.Count == 0) continue;

            targets.Add((dock,
                [.. dock._loaded.Select(entry => entry.App)],
                dock._config.ShowRunning ? [.. dock._config.Apps] : []));
        }

        if (targets.Count == 0) return;
        _checkingRunning = true;

        Task.Run(() =>
        {
            try
            {
                Running.Snapshot snapshot = Running.Take();

                foreach ((DockWindow dock, List<DockApp> dibujadas, List<DockApp> ancladas) in targets)
                {
                    dock._state = Running.Check(dibujadas, snapshot);

                    // Las abiertas sin anclar salen de las ANCLADAS, no de lo dibujado:
                    // si no, las de la vuelta anterior contarian como ya presentes y la
                    // lista no menguaria nunca.
                    List<DockApp> extras = ancladas.Count > 0
                        ? Running.Unpinned(ancladas, snapshot)
                        : [];

                    bool cambiaron = !extras.Select(a => a.Target)
                        .SequenceEqual(dock._extras.Select(a => a.Target), StringComparer.OrdinalIgnoreCase);

                    dock._extras = extras;

                    // Rehacer la barra es caro y se ve, asi que solo cuando de verdad
                    // cambia el juego de apps abiertas.
                    PInvoke.PostMessage(dock._hwnd, WM_APP_RUNNING, (WPARAM)(nuint)(cambiaron ? 1 : 0), default);
                }
            }
            finally
            {
                _checkingRunning = false;
            }
        });
    }

    /// <summary>
    /// Deja rastro de qué apps se ven abiertas, solo cuando cambia. Sirve para poder
    /// comprobar que los avisos del shell llegan sin tener que mirar los puntitos.
    /// </summary>
    private void TraceRunning()
    {
        string ahora = string.Join(" ", _loaded.Select((entry, i) =>
            i < _state.Length && _state[i].HasWindow ? $"{entry.App.Name}:{_state[i].All.Length}" : ""))
            .Trim();

        if (ahora == _lastRunningTrace) return;

        _lastRunningTrace = ahora;
        Console.WriteLine($"[abiertas] {_device}: {(ahora.Length == 0 ? "ninguna" : ahora)}");
    }

    /// <summary>
    /// Un aviso del shell dice que algo cambió. No se barre al momento: al abrir una app
    /// llegan varios seguidos, y SetTimer sobre el mismo id reprograma en vez de añadir,
    /// que es el rebote de toda la vida.
    /// </summary>
    private static void ScheduleRunningRefresh()
    {
        foreach (DockWindow dock in Instances.Values)
        {
            PInvoke.SetTimer(dock._hwnd, RunningTimerId, ShellHookDebounceMs, null);
        }
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
        HWND foreground = PInvoke.GetForegroundWindow();
        if (foreground.IsNull || foreground == _hwnd) return false;

        // Solo cuenta lo que pase en NUESTRO monitor: con tres pantallas, un juego en
        // una no puede esconder los docks de las otras dos.
        if (PInvoke.MonitorFromWindow(foreground, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTONEAREST) != _monitor)
            return false;

        // Lo decide la VENTANA, no el estado del sistema.
        //
        // Antes bastaba con que SHQueryUserNotificationState dijera BUSY o
        // D3D_FULL_SCREEN para dar por bueno el pleno. Resulto ser un disparador falso
        // con consecuencias gordas: al minimizar una ventana el sistema devuelve BUSY
        // durante un instante -y hasta dispara ABN_FULLSCREENAPP-, con lo que el dock
        // se ocultaba ENTERO, y en ese hueco el borde inferior dejaba de ser suyo.
        // Bastaba bajar el raton para sacar la barra de tareas, y la animacion del dock
        // se reiniciaba. Medido con la traza: "el dock se aparta" y "el dock vuelve" en
        // menos de un segundo, justo detras de cada minimizado.
        //
        // La ventana no miente. Se comparo la MISMA ventana de Brave maximizada y con
        // F11:
        //
        //     maximizada  rect=-8,-8 2576x1096  CAPTION=si  THICKFRAME=si
        //     con F11     rect=0,0   2560x1080  CAPTION=no  THICKFRAME=no
        //
        // Una maximizada cubre el monitor igual que una a pantalla completa -con la
        // barra de tareas en autoocultar, el area de trabajo ES la pantalla entera- y
        // lo que las separa son los estilos. IsZoomed no sirve: devuelve true en los
        // dos casos, tambien medido.
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

    /// <summary>Nombres largos recortados: el menú se hace tan ancho como su fila mayor.</summary>
    private static string Shorten(string name)
        => name.Length <= 34 ? name : string.Concat(name.AsSpan(0, 33), "…");

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
            SetTallRegion(true);
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

        if (_visuals.MenuOpen)
        {
            // Con la lista de la rueda abierta, el ratón se queda sobre el ICONO, que
            // está debajo de la lista: el hit-test devuelve -1 y borraba la fila que la
            // rueda acababa de elegir. Solo manda el ratón cuando de verdad está encima
            // de una fila.
            int hot = _visuals.MenuHitTest(LoWord(lParam), HiWord(lParam));
            if (hot >= 0) { _wheelAt = hot + _wheelFirst; _visuals.MenuHot(hot); }
            else if (_wheelIndex < 0) _visuals.MenuHot(-1);

            _visuals.SetLabel(-1);
            return;
        }

        // Traza para calibrar las sondas: qué icono cae en qué x. Apagada salvo que se
        // pida por variable de entorno, y sale barata porque la magnificación ya ha
        // hecho el trabajo caro justo encima.
        if (Environment.GetEnvironmentVariable("DOCK_HOVER_LOG") is not null)
            Console.WriteLine($"[hover] x={LoWord(lParam)} idx={_visuals.HitTest(_lastRest)} mag={_config.Magnification}");

        // El nombre del icono de debajo. Mientras se arrastra no: ahí el icono ya no
        // está donde dice la curva y la etiqueta se quedaría señalando al hueco.
        _visuals.SetLabel(_dragging || _hidden ? -1 : _visuals.HitTest(_lastRest));

        if (_pressedIndex >= 0) OnDragMove(LoWord(lParam), HiWord(lParam));
    }

    /// <summary>
    /// El menú del clic derecho.
    ///
    /// Antes el clic derecho cerraba el dock directamente, que era un atajo de
    /// desarrollo: bastaba un clic mal dado para quedarte sin dock. Ahora salir está
    /// dentro del menú, que es donde se espera encontrarlo.
    /// </summary>
    private void OnRightClick()
    {
        if (_visuals is null || _curve.Count == 0) return;

        if (_visuals.MenuOpen)
        {
            CloseMenu();
            return;
        }

        int index = _visuals.HitTest(_lastRest);
        bool sobreIcono = index >= 0 && index < _loaded.Count && !_loaded[index].App.Separator;

        _menuIndex = sobreIcono ? index : -1;

        // Los documentos recientes de la app, encima de sus dos entradas. Es la mitad
        // de la lista de saltos que la barra de tareas enseña en este mismo gesto: la
        // API no da los anclados ni las tareas, y está escrito en la doc.
        //
        // Aquí es donde se sostienen los tres cortafuegos de la enmienda 3: se pregunta
        // por UNA app, la del icono clicado, que por definición está en dock.json, y
        // solo desde este gesto. No hay ningún otro camino que llegue a JumpList.
        _menuJumps = [];
        if (sobreIcono)
        {
            HWND ventana = index < _state.Length ? _state[index].MainWindow : default;
            string? appId = JumpList.AppIdOf(_loaded[index].App, ventana);
            if (appId is not null)
            {
                // Los recientes primero; si la app no los registra —el Explorador, por
                // ejemplo, lleva carpetas frecuentes y no documentos— se prueba con los
                // frecuentes antes de rendirse.
                List<JumpItem> jumps = JumpList.Read(appId);
                if (jumps.Count == 0) jumps = JumpList.Read(appId, APPDOCLISTTYPE.ADLT_FREQUENT);

                _menuJumps = [.. jumps.Take(MenuJumpLimit)];
            }

            if (Environment.GetEnvironmentVariable("DOCK_HOVER_LOG") is not null)
            {
                Console.WriteLine($"[saltos] '{_loaded[index].App.Name}' appId={appId ?? "(ninguno)"} " +
                    $"ventana=0x{(nint)ventana.Value:X} recientes={_menuJumps.Count}");
            }
        }

        // Una app que solo está abierta no se puede "quitar": se ancla.
        string accion = sobreIcono && EsExtra(_loaded[index].App)
            ? $"Anclar '{_loaded[index].App.Name}' al dock"
            : $"Quitar '{_loaded[index].App.Name}' del dock";

        string[] items = sobreIcono
            ? [.. _menuJumps.Select(j => Shorten(j.Name)), accion, "Salir del dock"]
            : ["Salir del dock"];

        float anchor = sobreIcono
            ? _curve.Project((_curve.RestLeft(index) + _curve.RestRight(index)) * 0.5f, _windowWidth, _lastRest)
            : _windowWidth * 0.5f;

        // La etiqueta ocupa el mismo hueco que el menú: se quita o se solapan.
        _visuals.SetLabel(-1);

        (float left, float right) = BarBounds();
        _visuals.OpenMenu(items, anchor, _windowHeight - BarHeight, left, right);

        // El menú se sale por arriba de lo que la región deja pasar, así que hay que
        // subir el techo o no se podría clicar.
        ApplyRegion();
    }

    /// <summary>Ejecuta lo que se eligió en el menú.</summary>
    /// <summary>
    /// La ✕ de una fila de la lista de ventanas: le pide a esa ventana que se cierre.
    ///
    /// <b>Pedir, no cerrar.</b> Es <c>WM_CLOSE</c>, el mismo mensaje que manda el botón
    /// de cerrar de la propia ventana: la app puede ignorarlo o preguntar si guardar los
    /// cambios, y entonces contesta el usuario. Nunca se mata un proceso — ver la
    /// enmienda 4 de SEGURIDAD.md, que es lo que autoriza esto y con qué límites.
    /// </summary>
    private void OnCloseWindow(int row)
    {
        int at = row + _wheelFirst;
        if (_wheelIndex < 0 || at < 0 || at >= _wheelWindows.Length) return;

        PInvoke.PostMessage(_wheelWindows[at], WM_CLOSE, default, default);
        Console.WriteLine($"[cerrar] pedida la ventana {at + 1} de {_wheelWindows.Length}");

        // La fila desaparece al momento aunque la app tarde en cerrarse, o aunque acabe
        // negándose: si se negara, la siguiente vuelta de rueda la vuelve a enseñar.
        // Esperar a que muera de verdad dejaría una fila muerta durante todo el diálogo
        // de "¿guardar los cambios?".
        HWND[] quedan = [.. _wheelWindows.Where((_, i) => i != at)];

        if (quedan.Length < 2)
        {
            CloseMenu();
            return;
        }

        _wheelWindows = quedan;
        _wheelAt = Math.Min(at, quedan.Length - 1);
        _wheelFirst = -1;
        ShowWheelList();
    }

    private void OnMenuChoice(int choice)
    {
        _visuals?.CloseMenu();
        ClosePreview();

        // El menú de la rueda es una lista de ventanas, no el del clic derecho. Aquí
        // sí se puede activar: el clic es lo que da el permiso que la rueda no da.
        if (_wheelIndex >= 0)
        {
            HWND[] windows = _wheelWindows;
            int at = choice + _wheelFirst;
            _wheelIndex = -1;

            if (at < 0 || at >= windows.Length) return;

            WindowActions.BringToFront(windows[at]);
            Console.WriteLine($"[rueda] al frente la ventana {at + 1}");
            return;
        }

        bool sobreIcono = _menuIndex >= 0;

        // Arriba van los documentos recientes, si los hay.
        if (sobreIcono && choice < _menuJumps.Count && _menuIndex < _loaded.Count)
        {
            DockApp dueño = _loaded[_menuIndex].App;
            JumpItem doc = _menuJumps[choice];
            _menuJumps = [];

            Console.WriteLine($"[saltos] abriendo '{doc.Name}' con '{dueño.Name}'");

            // Fuera del hilo de UI, como lanzar: acaba en ShellExecuteEx.
            Task.Run(() => dueño.OpenWith([doc.Target]));
            return;
        }

        int resto = choice - _menuJumps.Count;
        _menuJumps = [];

        // La última entrada siempre es salir, tenga el menú una entrada o seis.
        if (!sobreIcono || resto == 1)
        {
            PInvoke.PostQuitMessage(0);
            return;
        }

        if (_menuIndex >= _loaded.Count) return;

        DockApp elegida = _loaded[_menuIndex].App;
        List<DockApp> apps = [.. _loaded.Select(entry => entry.App)];

        // Sobre una app que solo estaba abierta, el menú ancla en vez de quitar. Basta
        // con dejarla en la lista: OnlyPinned la deja pasar porque ya no es un extra.
        if (EsExtra(elegida))
        {
            _extras = [.. _extras.Where(app => app.Target != elegida.Target)];
            Console.WriteLine($"[dock] anclada '{elegida.Name}'");
        }
        else
        {
            apps.Remove(elegida);
            _visuals?.Puff(_menuIndex);
            Console.WriteLine($"[dock] quitada '{elegida.Name}'");
        }

        apps = OnlyPinned(apps);
        DockLocal.Save(_device, _config.BaseApps, apps);
        ReloadSiblings();
        _config = _config with { Apps = apps };

        PInvoke.SetTimer(_hwnd, PuffTimerId, 200, null);
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
        if (_visuals?.MenuOpen == true)
        {
            // La aspa va antes: vive DENTRO de su fila, así que el hit-test normal
            // devolvería la misma fila y no se sabría cuál de las dos cosas se quería.
            int cerrar = _visuals.MenuHitTestClose(LoWord(lParam), HiWord(lParam));
            if (cerrar >= 0) OnCloseWindow(cerrar);
            else
            {
                int choice = _visuals.MenuHitTest(LoWord(lParam), HiWord(lParam));
                if (choice >= 0) OnMenuChoice(choice);
                else CloseMenu();
            }

            _pressedIndex = -1;
            return;
        }

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
            CloseMenuAndStack();
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

        apps = OnlyPinned(apps);
        DockLocal.Save(_device, _config.BaseApps, apps);
        ReloadSiblings();

        // Reconstruir con el orden nuevo. Los Shift vuelven a cero al crearse los
        // visuales, y como ya estaban donde toca, no se ve ningún salto.
        _config = _config with { Apps = apps };

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

        // Y hasta dónde llega por arriba. Fuera del dock la ventana se queda en la
        // barra más lo que crece el icono magnificado, porque por encima no hay nada
        // dibujado y ahí el usuario tiene los iconos del escritorio.
        //
        // Y sí, tiene que ser la REGIÓN y no el hit-test: HTTRANSPARENT tampoco
        // atraviesa procesos para los clics. Se midió poniendo Paint debajo del hueco:
        // devolviendo HTTRANSPARENT el clic no le llegaba igual.
        // Y hasta dónde llega por arriba: SOLO hasta lo que de verdad se dibuja.
        //
        // Antes, con el ratón encima, subía hasta el techo de la ventana. Eso funcionó
        // mientras la ventana medía 163 px, pero al crecer el hueco del menú a 200
        // pasó a medir 287, y esa columna entera —invisible— seguía siendo del dock:
        // el ratón a doscientos píxeles por encima de la barra marcaba el icono y lo
        // habría abierto al clicar.
        float top = _windowHeight - BarHeight
            - Scale(_config.IconSize) * (_config.Magnification - 1f);

        // La etiqueta se dibuja justo encima del icono del todo magnificado.
        if (_tallRegion) top -= Scale(LogicalLabelStrip);

        // Y el menú o la lista de ventanas, más arriba todavía. Ahí sí hay que llegar,
        // porque se clican.
        if (_visuals?.MenuOpen == true) top = MathF.Min(top, _visuals.MenuTop);

        // Si no ha cambiado, no se toca. Cada SetWindowRgn reajusta la forma de la
        // ventana, y esto se llama en CADA reconstrucción: recargar el JSON, reordenar,
        // volver a cargar iconos. Reformar la ventana por nada es justo el momento en
        // que otra cosa puede ganarle la carrera por el borde inferior de la pantalla.
        int techo = (int)MathF.Floor(MathF.Max(0f, top));
        if (left == _regionLeft && right == _regionRight && techo == _regionTop) return;

        _regionLeft = left;
        _regionRight = right;
        _regionTop = techo;

        HRGN region = PInvoke.CreateRectRgn(left, (int)MathF.Floor(MathF.Max(0f, top)), right, (int)_windowHeight);

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
    /// Sube o baja el techo de la ventana. Se llama al entrar y salir el ratón y al
    /// empezar y acabar un arrastre, que son los momentos en que aparece o desaparece
    /// algo por encima de la barra.
    /// </summary>
    private void SetTallRegion(bool tall)
    {
        if (tall == _tallRegion) return;

        _tallRegion = tall;
        ApplyRegion();
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
        SetTallRegion(true);
        (_dropKind, _dropSlot) = KindAt(screenX, screenY);

        _visuals?.SetDropTarget(_dropKind == DropKind.Open ? _dropSlot : -1, Scale(_config.IconSize) * 0.25f);
        _visuals?.SetAddZone(true);
        _visuals?.SetAddZoneHot(_dropKind == DropKind.Add);
        return _dropKind != DropKind.None;
    }

    /// <summary>
    /// Qué significaría soltar en ese punto, y es <b>indulgente a propósito</b>.
    ///
    /// Antes solo valía la franja de la barra, abajo del todo, y había que afinar
    /// demasiado: la miniatura que dibuja el shell va por DEBAJO del cursor, así que
    /// uno apunta instintivamente con la imagen y el ratón se queda alto. Ahora vale
    /// toda la altura de la ventana, y la regla es simple: <b>sobre el icono de una app
    /// la abre con ella; en cualquier otro sitio del dock, la añade</b>. Así el
    /// separador, los márgenes y el hueco de la etiqueta también sirven para añadir, en
    /// vez de rechazar la suelta sin decir por qué.
    /// </summary>
    private (DropKind Kind, int Slot) KindAt(int screenX, int screenY)
    {
        if (_visuals is null || _curve.Count == 0) return (DropKind.None, -1);

        int y = screenY - _windowTop;
        if (y < 0 || y > _windowHeight) return (DropKind.None, -1);

        // Mientras se arrastra algo por encima, el dock tiene que estar a la vista: si
        // no, no habría dónde soltarlo.
        Reveal();

        float x = screenX - _windowLeft;
        _lastRest = _curve.Invert(x, _windowWidth);
        _visuals.SetCursor(_lastRest);

        // A la derecha de la barra: la zona del "+", hasta donde llegue la ventana.
        (float left, float right) = BarBounds();
        if (x > right) return (DropKind.Add, -1);
        if (x < left) return (DropKind.Add, -1);

        int slot = _visuals.HitTest(_lastRest);
        bool app = slot >= 0 && slot < _loaded.Count && _loaded[slot].App.IsApp;

        // Sobre un separador o un documento no hay nada que abrir, así que se añade.
        return app ? (DropKind.Open, slot) : (DropKind.Add, -1);
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
        if (!_hovering) SetTallRegion(false);
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

        apps = OnlyPinned(apps);
        DockLocal.Save(_device, _config.BaseApps, apps);
        ReloadSiblings();
        _config = _config with { Apps = apps };

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

        // La superposición tiene que cubrir la ventana Y el icono, y con varias
        // pantallas eso no es un solo monitor: GetWindowRect da coordenadas del
        // escritorio virtual, así que una ventana en la pantalla de al lado quedaba
        // fuera de la superposición y la malla salía recortada o no salía.
        RECT area = info.rcMonitor;
        area.left = Math.Min(area.left, (int)MathF.Floor(source.Left));
        area.top = Math.Min(area.top, (int)MathF.Floor(source.Top));
        area.right = Math.Max(area.right, (int)MathF.Ceiling(source.Right));
        area.bottom = Math.Max(area.bottom, (int)MathF.Ceiling(source.Bottom));

        return GenieOverlay.Play(
            _visuals.Compositor,
            area,
            _visuals.CreateBitmapBrush(shot),
            new Vector2(shot.Width, shot.Height),
            new GenieCurve(source, target, GenieOverlay.Slices),
            reverse,
            onFinished);
    }

    /// <summary>
    /// Clic central: una instancia NUEVA, aunque ya haya ventana. Es lo que hace la
    /// barra de tareas de Windows y lo que hace macOS con el icono del Finder.
    ///
    /// El camino de lanzar ya existía; lo que faltaba era una forma de pedirlo teniendo
    /// ventana, porque el clic izquierdo solo lanza cuando NO la hay.
    /// </summary>
    private void OnMiddleClick()
    {
        int index = _visuals?.HitTest(_lastRest) ?? -1;
        if (index < 0 || index >= _loaded.Count) return;

        DockApp app = _loaded[index].App;
        if (app.Separator || !app.IsApp) return;

        CloseMenuAndStack(exceptFolder: null);
        _visuals?.Bounce(index, Scale(_config.IconSize) * 0.35f);

        // Fuera del hilo de UI por lo mismo que el clic izquierdo: ShellExecuteEx puede
        // bloquear segundos y aquí se atiende el ratón.
        Task.Run(() =>
        {
            try
            {
                app.Launch();
                Console.WriteLine($"[dock] instancia nueva de '{app.Name}'");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[dock] no se pudo lanzar '{app.Name}': {ex.Message}");
            }
        });
    }

    /// <summary>
    /// Rueda sobre un icono: elige entre sus ventanas.
    ///
    /// <para>
    /// El gesto que se pidió era "la rueda trae la siguiente ventana al frente", y eso
    /// <b>Windows no lo deja hacer</b>. Medido, y de dos maneras: con la rueda encima
    /// del icono, <c>SetForegroundWindow</c> no surte efecto ni una vez, mientras que
    /// con un clic sobre el mismo icono y las mismas ventanas sí; y
    /// <c>SetWindowPos(HWND_TOP)</c> y <c>BringWindowToTop</c> sobre una ventana ajena
    /// devuelven TRUE y no mueven nada. Es a propósito: la rueda se enruta a la ventana
    /// bajo el puntero justamente para NO activarla, y sin activación no hay derecho a
    /// reordenar las ventanas de otro proceso.
    /// </para>
    ///
    /// <para>
    /// Lo que sí da ese derecho es un clic. Así que la rueda enseña la lista de
    /// ventanas y mueve el resaltado, y el clic —que es lo único que Windows escucha—
    /// abre la elegida. La lista se dibuja con el mismo menú del clic derecho.
    /// </para>
    /// </summary>
    private void OnWheel(WPARAM wParam)
    {
        // Se usa la posición que ya mantiene el movimiento del ratón: en WM_MOUSEWHEEL
        // lParam viene en coordenadas de PANTALLA, no de cliente como en el resto de
        // mensajes, y además HitTest trabaja en la escala en reposo.
        int index = _visuals?.HitTest(_lastRest) ?? -1;
        if (_visuals is null || index < 0 || index >= _loaded.Count || index >= _state.Length) return;

        HWND[] windows = _state[index].All;
        if (windows.Length < 2) return;

        // El signo del delta va en la parte alta de wParam, con signo.
        int step = (short)((wParam.Value >> 16) & 0xFFFF) > 0 ? -1 : 1;

        if (_wheelIndex != index || !_visuals.MenuOpen)
        {
            _wheelIndex = index;
            _wheelWindows = windows;
            _wheelAt = 0;
            _wheelFirst = -1;
            _menuIndex = -1;
        }
        else
        {
            _wheelAt = ((_wheelAt + step) % _wheelWindows.Length + _wheelWindows.Length) % _wheelWindows.Length;
        }

        ShowWheelList();
        Console.WriteLine($"[rueda] '{_loaded[index].App.Name}' ventana {_wheelAt + 1} de {_wheelWindows.Length}");
    }

    /// <summary>
    /// Dibuja el trozo de la lista que contiene a la ventana elegida.
    ///
    /// Encima de la barra solo caben tres filas, y una app puede tener diez ventanas. En
    /// vez de crecer la ventana del dock —cuyo alto va horneado dentro de las
    /// expresiones de la animación— la lista se desplaza con la selección, que es lo
    /// natural cuando quien navega es la rueda.
    /// </summary>
    private void ShowWheelList()
    {
        if (_visuals is null || _wheelIndex < 0) return;

        int total = _wheelWindows.Length;
        int fit = Math.Min(total, DockMenu.RowsThatFit(_windowHeight - BarHeight, _dpi / 96f));
        int first = Math.Clamp(_wheelAt - fit / 2, 0, total - fit);

        if (first != _wheelFirst || !_visuals.MenuOpen)
        {
            _wheelFirst = first;

            // La etiqueta ocupa el mismo hueco que la lista: se quita o se solapan.
            _visuals.SetLabel(-1);

            (float left, float right) = BarBounds();
            _visuals.OpenMenu(
                [.. _wheelWindows.Skip(first).Take(fit).Select(TitleOf)],
                _curve.Project((_curve.RestLeft(_wheelIndex) + _curve.RestRight(_wheelIndex)) * 0.5f, _windowWidth, _lastRest),
                _windowHeight - BarHeight,
                left,
                right,
                closable: true);

            ApplyRegion();
        }

        _visuals.MenuHot(_wheelAt - first);
        ShowPreview();
    }

    /// <summary>
    /// Captura la ventana elegida y la enseña encima de la lista.
    ///
    /// Va en el hilo de UI a propósito: PrintWindow sobre una ventana cuesta unos pocos
    /// milisegundos y hacerlo en otro hilo obligaría a sincronizar con el compositor,
    /// que solo se puede tocar desde aquí.
    /// </summary>
    private void ShowPreview()
    {
        if (_visuals is null || _wheelIndex < 0 || _wheelAt >= _wheelWindows.Length) return;

        IconBitmap? shot = WindowCapture.Capture(_wheelWindows[_wheelAt]);
        if (shot is null)
        {
            // Una ventana minimizada no se deja capturar. Mejor quitar la miniatura que
            // dejar ahí la de OTRA ventana, que se leería como si fuera esta.
            ClosePreview();
            return;
        }

        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        if (!PInvoke.GetMonitorInfo(_monitor, &info)) return;

        float anchor = _windowLeft + _curve.Project(
            (_curve.RestLeft(_wheelIndex) + _curve.RestRight(_wheelIndex)) * 0.5f, _windowWidth, _lastRest);
        int top = (int)(_windowTop + _visuals.MenuTop);

        if (_preview is null)
        {
            _preview = PreviewOverlay.Show(_visuals, shot, _dpi / 96f, anchor, top, info.rcMonitor);
            if (_preview is not null) PInvoke.SetTimer(_hwnd, PreviewTimerId, PreviewRefreshMs, null);
        }
        else
        {
            _preview.Update(shot, anchor, top, info.rcMonitor);
        }
    }

    private void ClosePreview()
    {
        if (_preview is null) return;

        PInvoke.KillTimer(_hwnd, PreviewTimerId);
        _preview.Dispose();
        _preview = null;
    }

    /// <summary>El título de una ventana, recortado para que quepa en una fila.</summary>
    private static string TitleOf(HWND window)
    {
        int length = PInvoke.GetWindowTextLength(window);
        if (length <= 0) return "(sin título)";

        Span<char> buffer = new char[length + 1];
        fixed (char* p = buffer)
        {
            int got = PInvoke.GetWindowText(window, p, buffer.Length);
            string text = got > 0 ? new string(buffer[..got]) : "(sin título)";
            return text.Length > 48 ? text[..47] + "…" : text;
        }
    }

    private void OnLeftClick(LPARAM lParam)
    {
        int index = _visuals?.HitTest(_lastRest) ?? -1;
        if (index < 0 || index >= _loaded.Count) return;

        DockApp app = _loaded[index].App;
        if (app.Separator) return;

        CloseMenuAndStack(exceptFolder: app.IsFolder ? app.Target : null);

        // Una carpeta se despliega en rejilla en vez de abrir el Explorador. Si ya
        // estaba desplegada, el mismo clic la cierra.
        if (app.IsFolder)
        {
            if (_stack is not null) { CloseStack(); return; }

            _visuals?.Bounce(index, Scale(_config.IconSize) * 0.25f);
            OpenStack(index, app.Target);
            return;
        }

        // Tres estados, como la barra de tareas de Windows. Esto es lo que autoriza la
        // enmienda 1 de SEGURIDAD.md: se toca una ventana ajena SOLO aquí, como
        // respuesta directa a un clic sobre su icono, y nunca desde ningún otro sitio.
        AppState state = index < _state.Length ? _state[index] : default;

        // Si la ventana está A LA VISTA, el clic la esconde — tenga el foco o no.
        //
        // Antes hacía falta que además estuviera al frente, que es lo que hace la barra
        // de tareas. Pero el dock NUNCA roba el foco, así que si estabas escribiendo en
        // otra ventana, la app que veías en pantalla no tenía el foco: el primer clic
        // se lo daba y el segundo minimizaba. Desde fuera eso se lee como "el primer
        // clic no hace nada", y con la ventana en otra pantalla ni siquiera se ve el
        // cambio de foco.
        if (state.HasWindow && !WindowActions.IsMinimized(state.MainWindow))
        {
            // El genio se monta ANTES de minimizar, porque para capturarla tiene que
            // estar todavía ahí. Si la captura falla, se minimiza a secas: degradar es
            // mejor que romperse.
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
        //
        // La reserva de appbar se rehace antes: su alto va en píxeles físicos.
        ReserveAppBarSpace();
        (int x, int y, int w, int h) = ComputeBounds();

        PInvoke.SetWindowPos(_hwnd, default, x, y, w, h,
            SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE | SET_WINDOW_POS_FLAGS.SWP_NOZORDER);

        // El layout de los iconos va en píxeles físicos: hay que rehacerlo.
        OnIconsReady();
        Console.WriteLine($"[dpi] WM_DPICHANGED -> recalculado a {_dpi} DPI ({_dpi * 100 / 96}%), {w}x{h} en ({x},{y})");
    }

    /// <summary>
    /// Qué hacer cuando cambian las pantallas. Lo pone Program, que es quien tiene la
    /// lista de docks y por tanto el único que puede crearlos y destruirlos.
    /// </summary>
    public static Action? DisplaysChanged;

    private static bool _displaysPending;

    public static void RunMessageLoop()
    {
        MSG msg;
        while (PInvoke.GetMessage(&msg, default, 0, 0).Value > 0)
        {
            // Un mensaje de hilo no tiene ventana, así que DispatchMessage lo tiraría.
            if (msg.hwnd.IsNull && msg.message == WM_APP_DISPLAYS)
            {
                _displaysPending = false;
                DisplaysChanged?.Invoke();
                continue;
            }

            PInvoke.TranslateMessage(&msg);
            PInvoke.DispatchMessage(&msg);
        }
    }

    public void Dispose()
    {
        if (_hwnd.IsNull) return;
        PInvoke.KillTimer(_hwnd, TopmostTimerId);
        PInvoke.KillTimer(_hwnd, HideTimerId);
        PInvoke.KillTimer(_hwnd, RunningTimerId);
        PInvoke.KillTimer(_hwnd, SafetyTimerId);
        ClosePreview();
        CloseStack();

        if (!_hwnd.IsNull && _dropTarget is not null) PInvoke.RevokeDragDrop(_hwnd);
        _dropTarget = null;

        _appBar?.Dispose();
        _appBar = null;

        _visuals?.Dispose();
        PInvoke.DestroyWindow(_hwnd);
        _hwnd = default;
    }
}
