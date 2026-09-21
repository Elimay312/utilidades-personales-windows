using System.Numerics;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.System.Power;
using Windows.Win32.UI.HiDpi;
using Windows.Win32.UI.Input.KeyboardAndMouse;
using Windows.Win32.UI.WindowsAndMessaging;

namespace Isla;

/// <summary>
/// Los tres tamanos de la isla. El orden importa por dos motivos: el atajo rota por el,
/// y de la comparacion sale si el movimiento es de apertura (con rebote) o de cierre
/// (sin rebote).
/// </summary>
internal enum Estado
{
    /// <summary>Una tira pegada al borde. No roba ni un clic.</summary>
    Brasa,
    /// <summary>Asoma para decir algo y se vuelve a ir.</summary>
    Asomada,
    /// <summary>Despegada del borde, con contenido y clicable.</summary>
    Abierta,
}

internal sealed unsafe class IslaWindow : IDisposable
{
    // --- medidas, en unidades logicas (96 ppp) -------------------------------------
    //
    // La ventana es FIJA y nunca se redimensiona: la pastilla es un visual dentro de
    // ella y todo el morph vive en el compositor. Redimensionar la ventana a 60 fps
    // desde el hilo de UI es justo lo que se quiere evitar.
    private const int LogicalWindowWidth = 520;
    private const int LogicalWindowHeight = 260;

    // Zona caliente cuando la isla esta recogida. Deliberadamente baja: el borde
    // superior de Windows esta vivo (pestanas del navegador) y no queremos invitarnos.
    private const int LogicalHotWidth = 320;
    private const int LogicalHotHeight = 10;

    // Margen alrededor del panel para decidir que el raton "sigue encima" estando
    // abierta. Da histeresis gratis: la zona crece al abrirse, asi que la isla no
    // parpadea cuando el cursor roza el borde.
    private const int LogicalOpenMargin = 24;

    private const string ClassName = "IslaDinamica";
    private const int HotkeyId = 1;
    private const nuint TimerId = 1;
    private const uint TimerMs = 120;

    // Cuanto se queda asomada un aviso. La caducidad la vigila el tic de 8 Hz que ya
    // existe, asi que no hace falta un temporizador para esto.
    private const int AsomoSegundos = 4;

    private const int AtajoPomodoro = 2;

    // El reloj de los tiempos, y SOLO mientras el panel esta abierto: el texto de
    // los segundos no se puede animar en el compositor como si se anima la barra.
    private const nuint TimerReloj = 3;
    private const uint RelojMs = 1000;

    // La onda, y SOLO con el panel desplegado. En brasa el latido se cuelga del
    // temporizador que ya existe (TimerMs), asi que ahi no se despierta nada nuevo.
    private const nuint TimerOnda = 4;
    private const uint OndaMs = 50;

    // Cuantos latidos seguidos hay que estar dentro para que se abra. Dos son 240 ms:
    // bastante para que pasar de largo no cuente, poco para que no se note al esperar.
    private const int TicksParaAbrir = 2;

    // Mensajes. CsWin32 no genera las constantes WM_*, asi que van a mano.
    private const uint WM_DESTROY = 0x0002;
    private const uint WM_MOUSEACTIVATE = 0x0021;
    private const uint WM_WINDOWPOSCHANGING = 0x0046;
    private const uint WM_NCCALCSIZE = 0x0083;
    private const uint WM_NCACTIVATE = 0x0086;
    private const uint WM_TIMER = 0x0113;
    private const uint WM_MOUSEMOVE = 0x0200;
    private const uint WM_LBUTTONDOWN = 0x0201;
    private const uint WM_LBUTTONUP = 0x0202;
    private const uint WM_HOTKEY = 0x0312;
    private const uint WM_POWERBROADCAST = 0x0218;
    private const uint WM_DISPLAYCHANGE = 0x007E;
    private const uint WM_DPICHANGED = 0x02E0;
    private const nuint PBT_APMPOWERSTATUSCHANGE = 0xA;

    // WM_APP + 1. Lo manda Medios desde el pool de hilos para avisar de que hay
    // algo nuevo que pintar; el dato viaja aparte, en Medios.Ultima.
    private const uint WM_APP_MEDIA = 0x8001;

    // Del vigilante del fichero a la ventana, y de la ventana al bucle. Son dos saltos
    // porque rehacer la ventana desde dentro de su propio WndProc no se puede.
    private const uint WM_APP_RECARGAR = 0x8002;
    private const uint WM_APP_REHACER = 0x8003;

    private const int MA_NOACTIVATE = 3;

    private static readonly HWND HWND_TOPMOST = new(-1);

    // El delegate va en un campo estatico de solo lectura: si se pasa un lambda suelto
    // a RegisterClassEx, el GC se lo lleva y la ventana muere al primer mensaje.
    private static readonly WNDPROC WndProcThunk = WndProc;
    private static ushort _classAtom;
    private static IslaWindow? _instancia;
    private static bool _rehacerPendiente;
    private static bool _rehaciendo;

    private readonly IslaConfig _config;
    private readonly HMONITOR _monitor;
    private readonly HWND _hwnd;
    private readonly IslaVisuals _visuals;
    private readonly uint _dpi;
    private readonly int _x, _y, _w;

    private Estado _base = Estado.Brasa;
    private Estado _actual = Estado.Brasa;
    private bool _hover;
    private int _seguidos;
    private bool _atajo;
    private bool _visible;
    private string? _sonando;
    private string _firma = string.Empty;
    private Aviso? _transitorio;
    private DateTime _finTransitorio;
    private DateTime _finAsomo;
    private DateTime _finPomodoro;
    private bool _hayPomodoro;
    private int _segundoPomodoro = -1;
    private float _volumenAnterior = -1f;
    private int _tic;
    private bool _atajoPomodoro;
    private bool _arrastrando;
    private DateTime _leido;
    private TimeSpan _duracion;
    private bool _apartada;
    private DateTime _regionCuando;
    private Estado _regionEstado = Estado.Brasa;

    public static IslaWindow? Create(IslaConfig config)
    {
        try { return new IslaWindow(config); }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[isla] {ex.GetType().Name}: {ex.Message}");
            return null;
        }
    }

    private IslaWindow(IslaConfig config)
    {
        _config = config;

        // Una isla sola, no una por pantalla: la muesca de un portatil tampoco se
        // repite en cada monitor.
        _monitor = Elegir(config.Pantalla);
        PInvoke.GetDpiForMonitor(_monitor, MONITOR_DPI_TYPE.MDT_EFFECTIVE_DPI, out uint dpiX, out _);
        _dpi = dpiX;

        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        if (!PInvoke.GetMonitorInfo(_monitor, &info)) throw new InvalidOperationException("sin monitor");

        // rcMonitor y no rcWork: la isla vive en el filo de la pantalla, por encima de
        // lo que haya reservado cualquier otro.
        _w = (int)Scale(LogicalWindowWidth);
        int h = (int)Scale(LogicalWindowHeight);
        _x = info.rcMonitor.left + (info.rcMonitor.right - info.rcMonitor.left - _w) / 2;
        _y = info.rcMonitor.top;

        EnsureClassRegistered();

        fixed (char* clase = ClassName)
        fixed (char* titulo = "Isla")
        {
            _hwnd = PInvoke.CreateWindowEx(
                // NOACTIVATE: nunca roba el foco. TOOLWINDOW: fuera de Alt+Tab, de la
                // barra de tareas y del dock. TOPMOST: siempre encima.
                //
                // Aqui NO va WS_EX_TRANSPARENT, y es una correccion: NO deja pasar los
                // clics. Medido -- con el bit puesto y la isla recogida, un clic a 129
                // px de alto le llegaba igual y movia la barra de progreso 123 s. Lo
                // unico que de verdad aparta el raton es la region, que es lo que el
                // dock tenia escrito en su README desde el primer dia.
                WINDOW_EX_STYLE.WS_EX_NOACTIVATE
                    | WINDOW_EX_STYLE.WS_EX_TOOLWINDOW
                    | WINDOW_EX_STYLE.WS_EX_TOPMOST,
                new PCWSTR(clase), new PCWSTR(titulo),
                WINDOW_STYLE.WS_POPUP,
                _x, _y, _w, h,
                default, default, ModuleHandle, null);
        }

        if (_hwnd.IsNull) throw new InvalidOperationException("CreateWindowEx fallo");
        _instancia = this;

        // La region ES el hit-test de esta ventana, no un adorno: recorta los 520x260
        // a lo que se este dibujando de verdad. Empieza en la brasa, que son 140x5.
        AplicarRegion(Estado.Brasa);

        _visuals = new IslaVisuals(_hwnd, Scale(1f), _w);
        _visuals.GoTo(Estado.Brasa, abriendo: false, instantaneo: true);

        _atajo = PInvoke.RegisterHotKey(_hwnd, HotkeyId,
            HOT_KEY_MODIFIERS.MOD_CONTROL | HOT_KEY_MODIFIERS.MOD_ALT, 'I');
        if (!_atajo)
        {
            // Un atajo global que falla en silencio es media hora perdida. El dock
            // registra el suyo (Ctrl+Alt+D) y podria haber mas cosas compitiendo.
            Console.Error.WriteLine("[isla] Ctrl+Alt+I ya esta cogido por otro programa.");
        }

        _atajoPomodoro = PInvoke.RegisterHotKey(_hwnd, AtajoPomodoro,
            HOT_KEY_MODIFIERS.MOD_CONTROL | HOT_KEY_MODIFIERS.MOD_ALT, 'T');
        if (!_atajoPomodoro) Console.Error.WriteLine("[isla] Ctrl+Alt+T ya esta cogido.");

        PInvoke.SetTimer(_hwnd, TimerId, TimerMs, null);

        // Todavia no se ensena: la isla no existe mientras no haya nada que decir.
        // Medios avisa con WM_APP_MEDIA en cuanto encuentra una sesion de audio.
        Medios.Arrancar(_hwnd, WM_APP_MEDIA);

        Console.WriteLine($"[isla] {NombreDe(_monitor)} al {_dpi * 100 / 96}%, ventana {_w}x{h} en {_x},{_y}");
        Console.WriteLine($"[isla] pantallas: {string.Join(", ", Monitores().Select(NombreDe))}");
    }

    /// <summary>
    /// La pantalla por su nombre de dispositivo. No vale el HMONITOR: cambia entre
    /// arranques, asi que lo que se guarda en isla.json es el \\.\DISPLAYn.
    /// </summary>
    private static HMONITOR Elegir(string pantalla)
    {
        if (!string.IsNullOrWhiteSpace(pantalla))
        {
            foreach (HMONITOR m in Monitores())
            {
                if (NombreDe(m) == pantalla) return m;
            }

            Console.Error.WriteLine($"[isla] no encuentro la pantalla {pantalla}; se usa la principal.");
        }

        return PInvoke.MonitorFromPoint(default, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTOPRIMARY);
    }

    private static HMONITOR[] Monitores()
    {
        List<HMONITOR> lista = [];
        MONITORENUMPROC recoge = (m, _, _, _) => { lista.Add(m); return true; };
        PInvoke.EnumDisplayMonitors(default, null, recoge, default);

        // Si la enumeracion falla, al menos la principal.
        return lista.Count > 0
            ? [.. lista]
            : [PInvoke.MonitorFromPoint(default, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTOPRIMARY)];
    }

    private static string NombreDe(HMONITOR m)
    {
        MONITORINFOEXW info = default;
        info.monitorInfo.cbSize = (uint)sizeof(MONITORINFOEXW);
        if (!PInvoke.GetMonitorInfo(m, (MONITORINFO*)&info)) return "?";
        return new string((char*)&info.szDevice).TrimEnd('\0');
    }

    private float Scale(float logical) => (float)(logical * _dpi / 96.0);

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
                // pintaria un rectangulo opaco por debajo de la pastilla.
                hbrBackground = default,
            };
            _classAtom = PInvoke.RegisterClassEx(wc);
        }

        if (_classAtom == 0) throw new InvalidOperationException("RegisterClassEx fallo");
    }

    // --- mensajes ------------------------------------------------------------------

    private static LRESULT WndProc(HWND hwnd, uint msg, WPARAM wParam, LPARAM lParam)
    {
        IslaWindow? isla = _instancia;

        switch (msg)
        {
            // WS_EX_NOACTIVATE por si solo NO basta: al hacer clic llegan
            // WM_ACTIVATE(WA_CLICKACTIVE) y WM_SETFOCUS igualmente. MA_NOACTIVATE
            // rechaza la activacion sin descartar el clic.
            case WM_MOUSEACTIVATE:
                return new LRESULT(MA_NOACTIVATE);

            // Hay que decirle a DWM que la ventana esta activa aunque nunca lo este. Si
            // no, deja de pintar el acrilico y la isla se vuelve invisible mientras
            // sigue recibiendo mensajes. Medido en el dock, y cuesta un dia encontrarlo.
            case WM_NCACTIVATE:
                return PInvoke.DefWindowProc(hwnd, msg, new WPARAM(1), lParam);

            // Colapsa el area no cliente: la ventana es toda cliente.
            case WM_NCCALCSIZE:
                return new LRESULT(0);

            // Windows saca la ventana de la banda topmost sin quitar el bit del estilo.
            case WM_WINDOWPOSCHANGING:
                ((WINDOWPOS*)lParam.Value)->hwndInsertAfter = HWND_TOPMOST;
                break;

            case WM_TIMER:
                if (wParam.Value == TimerReloj) isla?.OnReloj();
                else if (wParam.Value == TimerOnda) isla?.OnOnda();
                else isla?.OnTick();
                return new LRESULT(0);

            case WM_APP_MEDIA:
                isla?.OnMedios();
                return new LRESULT(0);

            case WM_LBUTTONDOWN:
                isla?.OnPulsar(lParam);
                return new LRESULT(0);

            case WM_MOUSEMOVE:
                isla?.OnArrastrar(lParam);
                return new LRESULT(0);

            case WM_LBUTTONUP:
                isla?.OnSoltar(lParam);
                return new LRESULT(0);

            case WM_HOTKEY:
                if (wParam.Value == AtajoPomodoro) isla?.OnPomodoro();
                else isla?.OnHotkey();
                return new LRESULT(0);

            // Llega al enchufar y desenchufar. No se sondea nada.
            // Un cambio de pantallas o de DPI invalida el HMONITOR y todas las medidas
            // en pixeles, que estan horneadas en los visuales. Se rehace entera, que es
            // bruto pero es una sola ventana.
            case WM_DISPLAYCHANGE:
            case WM_DPICHANGED:
            case WM_APP_RECARGAR:
                PedirRehacer();
                return new LRESULT(0);

            case WM_POWERBROADCAST:
                if (wParam.Value == PBT_APMPOWERSTATUSCHANGE) isla?.OnEnergia();
                return new LRESULT(1);

            case WM_DESTROY:
                // Rehacerse tambien destruye la ventana, y PostQuitMessage ahi mata el
                // bucle: la ventana nueva nace y el proceso se cierra detras, sin dejar
                // rastro. Medido -- tras mover la isla de pantalla el proceso
                // desaparecia. El dock lleva el mismo guardia por el mismo motivo.
                if (!_rehaciendo) PInvoke.PostQuitMessage(0);
                return new LRESULT(0);
        }

        return PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);
    }

    public static void RunMessageLoop()
    {
        MSG msg;
        while (PInvoke.GetMessage(&msg, default, 0, 0).Value > 0)
        {
            // Un mensaje de hilo no tiene ventana, asi que DispatchMessage lo tiraria.
            if (msg.hwnd.IsNull && msg.message == WM_APP_REHACER)
            {
                _rehacerPendiente = false;
                Rehacer();
                continue;
            }

            PInvoke.TranslateMessage(&msg);
            PInvoke.DispatchMessage(&msg);
        }
    }

    /// <summary>Lo llama el vigilante de isla.json, desde otro hilo. PostMessage vale.</summary>
    public static void Recargar()
    {
        HWND h = _instancia?._hwnd ?? default;
        if (!h.IsNull) PInvoke.PostMessage(h, WM_APP_RECARGAR, default, default);
    }

    public static void Cerrar()
    {
        _instancia?.Dispose();
        _instancia = null;
    }

    private static void PedirRehacer()
    {
        // Enchufar un monitor dispara varios mensajes seguidos; con uno basta.
        if (_rehacerPendiente) return;
        _rehacerPendiente = true;
        PInvoke.PostMessage(HWND.Null, WM_APP_REHACER, default, default);
    }

    private static void Rehacer()
    {
        _rehaciendo = true;
        _instancia?.Dispose();
        _instancia = null;
        _rehaciendo = false;

        IslaConfig config = Config.Cargar();
        Config.AplicarAutoArranque(config.AutoArranque);
        if (Create(config) is null) Console.Error.WriteLine("[isla] no se pudo rehacer la ventana.");
    }

    // --- estado --------------------------------------------------------------------

    /// <summary>
    /// Recogida, la region de la ventana son 140x5 px, asi que por ahi no llegan
    /// mensajes de raton de la franja entera. El hover se mira preguntando donde esta
    /// el cursor: una llamada cada 120 ms no se mide, y a cambio la isla no tiene que
    /// ocupar con region una zona que no dibuja para enterarse de que te acercas.
    /// </summary>
    private void OnTick()
    {
        // Arrastrando la barra el cursor puede salirse de la zona sin querer, y
        // cerrar la isla en mitad del gesto seria perder el arrastre.
        if (_arrastrando) return;

        PInvoke.GetCursorPos(out System.Drawing.Point p);

        // El latido de la brasa se cuelga de este mismo tic en vez de traerse un
        // temporizador propio: son 8 lecturas por segundo, y para una tira de 5 px de
        // alto eso sobra. Asi el reposo no gasta ni una vuelta de reloj de mas.
        Vigilar();
        if (_visible && _actual == Estado.Brasa) Latir();

        // La zona crece al estar abierta: eso es la histeresis, y sale gratis.
        RECT z = ZonaCaliente(_hover);
        bool dentro = p.X >= z.left && p.X < z.right && p.Y >= z.top && p.Y < z.bottom;

        if (!dentro)
        {
            _seguidos = 0;
            if (!_hover) return;
            _hover = false;
            Aplicar();
            return;
        }

        if (_hover) return;

        // Hay que QUEDARSE, no solo pasar. Sin esto la isla se abre al cruzar el filo
        // de camino al boton de cerrar de una ventana maximizada, que es un recorrido
        // de lo mas normal. Medido: con el cursor puesto en la franja, la isla pasaba a
        // opaca antes de 300 ms.
        if (++_seguidos < TicksParaAbrir) return;
        _hover = true;
        Aplicar();
    }

    private void OnOnda() => Latir();

    /// <summary>
    /// Una lectura del medidor y una vuelta de la onda. Si no suena nada no se le
    /// pregunta al audio siquiera: la isla en reposo con la musica parada no tiene por
    /// que gastar nada.
    /// </summary>
    private void Latir()
    {
        bool sonando = Medios.Ultima is { Sonando: true };
        _visuals.Pulso(sonando ? Audio.Pico() : 0f, sonando);
    }

    private void OnHotkey()
    {
        // El atajo tambien sirve para invocarla cuando no suena nada.
        Ensenar(true);
        _base = (Estado)(((int)_base + 1) % 3);
        Console.WriteLine($"[isla] atajo -> {_base}");
        Aplicar();
    }

    /// <summary>
    /// Llega por PostMessage desde el pool de hilos, asi que aqui ya estamos en el
    /// hilo que tiene la DispatcherQueue y se pueden tocar los visuales.
    /// </summary>
    private void OnMedios()
    {
        Cancion? c = Medios.Ultima;
        Ensenar(c is not null);

        if (c is null) { _sonando = null; return; }

        bool otra = c.Titulo != _sonando;
        _sonando = c.Titulo;
        _leido = DateTime.UtcNow;
        _duracion = c.Duracion;

        // Repintar SOLO si cambio algo que se dibuja.
        //
        // Spotify empuja la linea de tiempo cada dos por tres, y Mostrar rehace las
        // siete superficies de texto mas la caratula. Hacerlo en cada aviso costaba
        // CPU en reposo para redibujar exactamente los mismos pixeles. La posicion no
        // entra en la firma: de eso se encarga Progreso, que es una sola animacion.
        string firma = string.Join('|', c.Titulo, c.Artista, c.App, c.Sonando,
            c.PuedeAnterior, c.PuedeSiguiente, c.PuedePlayPausa, c.Duracion.Ticks, c.Tinte);
        if (firma != _firma)
        {
            _firma = firma;
            _visuals.Mostrar(c);
            RefrescarTitular();
        }
        _visuals.Progreso(
            c.Duracion > TimeSpan.Zero ? c.Posicion / c.Duracion : 0d,
            c.Duracion - c.Posicion,
            c.Sonando);

        if (!otra) return;
        Console.WriteLine($"[isla] {c.App}: {c.Titulo} - {c.Artista} ({c.Duracion:mm\\:ss})");

        // Cancion nueva: asoma y se vuelve a ir sola. Si el raton ya esta encima no
        // se toca nada, que bastante esta viendo.
        Asomar();
    }

    // --- los avisos ----------------------------------------------------------------

    /// <summary>
    /// El titular: lo unico que se ve en la pastilla asomada. Manda el aviso
    /// transitorio; si no hay, el pomodoro en marcha; si no, lo que suene.
    /// </summary>
    private Aviso Titular()
    {
        if (_transitorio is Aviso t) return t;

        if (_hayPomodoro)
        {
            TimeSpan queda = _finPomodoro - DateTime.UtcNow;
            if (queda < TimeSpan.Zero) queda = TimeSpan.Zero;
            return new Aviso($"Pomodoro   {queda:mm\\:ss}", false);
        }

        Cancion? c = Medios.Ultima;
        if (c is null) return new Aviso(string.Empty, false);
        return new Aviso(
            string.IsNullOrWhiteSpace(c.Artista) ? c.Titulo : $"{c.Titulo}   ·   {c.Artista}",
            true);
    }

    private void RefrescarTitular() => _visuals.Compacto(Titular(), Medios.Ultima?.Arte is not null);

    private void Asomar()
    {
        _finAsomo = DateTime.UtcNow.AddSeconds(AsomoSegundos);
        RefrescarTitular();
        if (_hover) return;
        _base = Estado.Asomada;
        Aplicar();
    }

    /// <summary>Un aviso que se lee y se va: bateria, volumen, fin de pomodoro.</summary>
    private void Avisar(string texto)
    {
        _transitorio = new Aviso(texto, false);
        _finTransitorio = DateTime.UtcNow.AddSeconds(AsomoSegundos);
        Ensenar(true);
        Asomar();
    }

    /// <summary>
    /// Llega de WM_POWERBROADCAST, al enchufar y al desenchufar. No hay temporizador
    /// mirando la bateria: lo avisa el sistema.
    /// </summary>
    private void OnEnergia()
    {
        if (!PInvoke.GetSystemPowerStatus(out SYSTEM_POWER_STATUS energia)) return;

        // 128 = esta maquina no tiene bateria. En un sobremesa esto no dice nada.
        if ((energia.BatteryFlag & 128) != 0) return;

        string carga = energia.BatteryLifePercent <= 100 ? $"{energia.BatteryLifePercent} %" : "";
        Avisar(energia.ACLineStatus == 1 ? $"Bateria   {carga}   ·   cargando" : $"Bateria   {carga}");
    }

    private void OnPomodoro()
    {
        if (_hayPomodoro)
        {
            _hayPomodoro = false;
            Avisar("Pomodoro cancelado");
            return;
        }

        _hayPomodoro = true;
        _finPomodoro = DateTime.UtcNow + TimeSpan.FromMinutes(_config.PomodoroMinutos);
        _segundoPomodoro = -1;
        Ensenar(true);
        Asomar();
    }

    /// <summary>
    /// Caducidades y lecturas que se cuelgan del tic de 8 Hz que ya esta despierto.
    /// Un pomodoro en marcha mantiene la isla asomada: es una cuenta atras, y una
    /// cuenta atras que no se ve no sirve de nada.
    /// </summary>
    private void Vigilar()
    {
        DateTime ahora = DateTime.UtcNow;

        if (_transitorio is not null && ahora > _finTransitorio)
        {
            _transitorio = null;
            RefrescarTitular();
            if (Medios.Ultima is null && !_hayPomodoro) Ensenar(false);
        }

        if (_hayPomodoro)
        {
            TimeSpan queda = _finPomodoro - ahora;
            if (queda <= TimeSpan.Zero)
            {
                _hayPomodoro = false;
                Avisar("Pomodoro terminado");
            }
            else
            {
                int segundo = (int)queda.TotalSeconds;
                if (segundo != _segundoPomodoro)
                {
                    _segundoPomodoro = segundo;
                    RefrescarTitular();
                }
            }
        }

        // El volumen, a 2 Hz y no a 8.
        //
        // GetMasterVolumeLevelScalar no es leer un campo: cruza al servicio de audio,
        // asi que cada llamada es una ida y vuelta entre procesos. Medido, a 8 Hz subia
        // la CPU en reposo de 0.42% a 2.29% -- mas que todo lo demas junto.
        //
        // ponytail: sondeo a 2 Hz, o sea hasta medio segundo de retraso frente al aviso
        // de Windows. El techo es IAudioEndpointVolumeCallback, que avisa por evento y
        // no cuesta nada; son unas cuarenta lineas de COM y no compensan todavia.
        // La primera lectura no avisa, que si no la isla saltaria nada mas arrancar.
        float v = (++_tic & 3) == 0 ? Audio.Volumen() : -1f;
        if (v >= 0f)
        {
            if (_config.VolumenAsoma && _volumenAnterior >= 0f && Math.Abs(v - _volumenAnterior) > 0.005f)
                Avisar($"Volumen   {(int)Math.Round(v * 100)} %");
            _volumenAnterior = v;
        }

        if (_regionCuando != default && ahora > _regionCuando)
        {
            _regionCuando = default;
            AplicarRegion(_regionEstado);
        }

        // Una vez por segundo: apartarse de lo que este a pantalla completa y volver
        // al principio de la banda topmost.
        if ((_tic & 7) == 0)
        {
            bool pleno = HayPlenoPantalla();
            if (pleno != _apartada)
            {
                _apartada = pleno;
                Pintar();
            }

            if (_visible && !_apartada) AsegurarTopmost();
        }

        if (_base == Estado.Asomada && !_hayPomodoro && ahora > _finAsomo)
        {
            _base = Estado.Brasa;
            Aplicar();
        }
    }

    private void Ensenar(bool si)
    {
        if (si == _visible) return;
        _visible = si;
        Pintar();
    }

    /// <summary>
    /// Un unico sitio que decide si la ventana se ve: hay algo que decir Y no hay nada
    /// a pantalla completa. Con dos sitios llamando a ShowWindow se acaba con la isla
    /// parpadeando encima de un juego.
    /// </summary>
    private void Pintar() => PInvoke.ShowWindow(
        _hwnd,
        _visible && !_apartada ? SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE : SHOW_WINDOW_CMD.SW_HIDE);

    /// <summary>
    /// Si hay algo ocupando la pantalla entera. Lo decide la VENTANA, no el estado del
    /// sistema: SHQueryUserNotificationState devuelve BUSY de forma transitoria despues
    /// de cualquier minimizado, y en el dock eso provocaba que se apartara sola una vez
    /// por minimizado. Aqui se comparan los estilos y el rectangulo, que no mienten.
    ///
    /// Solo se LEE la geometria de una ventana, la que esta en primer plano. No se
    /// enumera nada y no se toca nada (SEGURIDAD.md §3.5).
    /// </summary>
    private bool HayPlenoPantalla()
    {
        HWND frente = PInvoke.GetForegroundWindow();
        if (frente.IsNull || frente == _hwnd) return false;

        // Solo cuenta lo que pase en NUESTRA pantalla.
        if (PInvoke.MonitorFromWindow(frente, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTONEAREST) != _monitor)
            return false;

        // Una maximizada cubre el monitor igual que una a pantalla completa; lo que las
        // separa son los estilos. IsZoomed devuelve true en los dos casos y no sirve.
        const nint WsCaption = 0x00C00000;
        const nint WsThickFrame = 0x00040000;
        nint estilo = PInvoke.GetWindowLongPtr(frente, WINDOW_LONG_PTR_INDEX.GWL_STYLE);
        if ((estilo & (WsCaption | WsThickFrame)) != 0) return false;

        if (!PInvoke.GetWindowRect(frente, out RECT r)) return false;

        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        if (!PInvoke.GetMonitorInfo(_monitor, &info)) return false;

        RECT p = info.rcMonitor;
        return r.left <= p.left && r.top <= p.top && r.right >= p.right && r.bottom >= p.bottom;
    }

    /// <summary>
    /// Windows saca las ventanas de la banda topmost sin quitarles el bit. Dos llamadas:
    /// la primera vuelve a la banda, la segunda sube al principio de ella.
    /// </summary>
    private void AsegurarTopmost()
    {
        const SET_WINDOW_POS_FLAGS Quieta = SET_WINDOW_POS_FLAGS.SWP_NOMOVE
            | SET_WINDOW_POS_FLAGS.SWP_NOSIZE
            | SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE;

        PInvoke.SetWindowPos(_hwnd, HWND_TOPMOST, 0, 0, 0, 0, Quieta);
        PInvoke.SetWindowPos(_hwnd, HWND.Null, 0, 0, 0, 0, Quieta);
    }

    private RECT ZonaCaliente(bool abierta)
    {
        (float w, float h, _, float lift) = Medidas(Estado.Abierta);
        float ancho = Scale(abierta ? w + LogicalOpenMargin * 2 : LogicalHotWidth);
        float alto = Scale(abierta ? lift + h + LogicalOpenMargin : LogicalHotHeight);

        int cx = _x + _w / 2;
        return new RECT
        {
            left = cx - (int)(ancho / 2),
            right = cx + (int)(ancho / 2),
            top = _y,
            bottom = _y + (int)alto,
        };
    }

    private void Aplicar()
    {
        Estado efectivo = _hover ? Estado.Abierta : _base;
        if (efectivo == _actual) return;

        bool abriendo = efectivo > _actual;
        _actual = efectivo;

        _visuals.GoTo(efectivo, abriendo);

        // Al CRECER la region se pone ya, o recortaria lo que esta creciendo. Al
        // ENCOGERSE hay que esperar a que el muelle termine, o se recortaria la
        // animacion de cierre a media carrera. La espera la vigila el tic de 120 ms.
        _regionEstado = efectivo;
        if (abriendo)
        {
            _regionCuando = default;
            AplicarRegion(efectivo);
        }
        else
        {
            _regionCuando = DateTime.UtcNow.AddMilliseconds(380);
        }

        // El reloj solo corre con el panel abierto. En reposo la isla no gasta ni un
        // temporizador de mas.
        if (efectivo == Estado.Abierta) PInvoke.SetTimer(_hwnd, TimerReloj, RelojMs, null);
        else PInvoke.KillTimer(_hwnd, TimerReloj);

        if (efectivo == Estado.Brasa) PInvoke.KillTimer(_hwnd, TimerOnda);
        else PInvoke.SetTimer(_hwnd, TimerOnda, OndaMs, null);
    }

    // --- los mandos ----------------------------------------------------------------

    private static Vector2 Punto(LPARAM lParam)
    {
        int v = (int)lParam.Value;
        return new Vector2((short)(v & 0xFFFF), (short)(v >> 16));
    }

    private void OnPulsar(LPARAM lParam)
    {
        Vector2 p = _visuals.EnPanel(Punto(lParam), _w);

        switch (_visuals.Golpe(p))
        {
            case Zona.Anterior: Medios.Anterior(); break;
            case Zona.Siguiente: Medios.Siguiente(); break;
            case Zona.PlayPausa: Medios.Alternar(); break;

            case Zona.Barra:
                _arrastrando = true;
                PInvoke.SetCapture(_hwnd);
                _visuals.VistaPrevia(_visuals.FraccionEnX(p.X));
                break;
        }
    }

    private void OnArrastrar(LPARAM lParam)
    {
        if (!_arrastrando) return;
        _visuals.VistaPrevia(_visuals.FraccionEnX(_visuals.EnPanel(Punto(lParam), _w).X));
    }

    private void OnSoltar(LPARAM lParam)
    {
        if (!_arrastrando) return;
        _arrastrando = false;
        PInvoke.ReleaseCapture();

        if (_duracion <= TimeSpan.Zero) return;

        // Una sola llamada, al soltar. Mandarla en cada WM_MOUSEMOVE seria pedirle a la
        // app de turno treinta saltos por segundo.
        double f = _visuals.FraccionEnX(_visuals.EnPanel(Punto(lParam), _w).X);
        Medios.Buscar(_duracion * f);
    }

    /// <summary>
    /// Los segundos que pasan. La posicion se extrapola desde la ultima que dio el
    /// sistema, porque TimelinePropertiesChanged no llega cada segundo.
    /// </summary>
    private void OnReloj()
    {
        Cancion? c = Medios.Ultima;
        if (c is null || _arrastrando) return;
        _visuals.Transcurrido(Ahora(c));
    }

    private TimeSpan Ahora(Cancion c)
    {
        TimeSpan p = c.Sonando ? c.Posicion + (DateTime.UtcNow - _leido) : c.Posicion;
        return p > c.Duracion ? c.Duracion : p;
    }

    /// <summary>
    /// Recorta la ventana a lo que se dibuja en ese estado. <b>Es lo unico que aparta
    /// el raton de verdad</b>: WS_EX_TRANSPARENT no lo hace, y HTTRANSPARENT tampoco
    /// cruza procesos -- las dos cosas medidas, la primera aqui y la segunda en el dock.
    ///
    /// Recogida, la isla pasa de tragarse un rectangulo invisible de 380x190 a ocupar
    /// los 140x5 que de verdad se ven.
    /// </summary>
    private void AplicarRegion(Estado estado)
    {
        (float w, float h, _, float lift) = Medidas(estado);
        int rw = (int)Scale(w);
        int rh = (int)Scale(h);
        int rx = (_w - rw) / 2;
        int ry = (int)Scale(lift);

        PInvoke.SetWindowRgn(_hwnd, PInvoke.CreateRectRgn(rx, ry, rx + rw, ry + rh), false);
    }

    /// <summary>
    /// Ancho, alto, radio y despegue de cada estado, en unidades logicas.
    /// El radio crece con la caja: es lo que hace que parezca que se deforma en vez de
    /// que se redimensiona.
    /// </summary>
    public static (float W, float H, float R, float Lift) Medidas(Estado e) => e switch
    {
        Estado.Brasa => (140f, 5f, 2.5f, 0f),
        // 56 de alto y no 38: con 38 la rampa del contenido ni arrancaba y el
        // aviso salia vacio. Ahora cabe el titular entero.
        Estado.Asomada => (320f, 56f, 26f, 8f),
        _ => (380f, 180f, 28f, 10f),
    };

    public void Dispose()
    {
        PInvoke.KillTimer(_hwnd, TimerId);
        PInvoke.KillTimer(_hwnd, TimerReloj);
        PInvoke.KillTimer(_hwnd, TimerOnda);
        if (_atajo) PInvoke.UnregisterHotKey(_hwnd, HotkeyId);
        if (_atajoPomodoro) PInvoke.UnregisterHotKey(_hwnd, AtajoPomodoro);
        _visuals.Dispose();
        if (!_hwnd.IsNull) PInvoke.DestroyWindow(_hwnd);
        _instancia = null;
    }
}
