using System.Numerics;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Gdi;
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

    // El asomo por cancion nueva: sale, se lee, y se va solo.
    private const nuint TimerAsoma = 2;
    private const uint AsomaMs = 4000;

    // El reloj de los tiempos, y SOLO mientras el panel esta abierto: el texto de
    // los segundos no se puede animar en el compositor como si se anima la barra.
    private const nuint TimerReloj = 3;
    private const uint RelojMs = 1000;

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

    // WM_APP + 1. Lo manda Medios desde el pool de hilos para avisar de que hay
    // algo nuevo que pintar; el dato viaja aparte, en Medios.Ultima.
    private const uint WM_APP_MEDIA = 0x8001;

    private const int MA_NOACTIVATE = 3;
    private const nint WS_EX_TRANSPARENT = 0x00000020;

    private static readonly HWND HWND_TOPMOST = new(-1);

    // El delegate va en un campo estatico de solo lectura: si se pasa un lambda suelto
    // a RegisterClassEx, el GC se lo lleva y la ventana muere al primer mensaje.
    private static readonly WNDPROC WndProcThunk = WndProc;
    private static ushort _classAtom;
    private static IslaWindow? _instancia;

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
    private bool _arrastrando;
    private DateTime _leido;
    private TimeSpan _duracion;

    public static IslaWindow? Create()
    {
        try { return new IslaWindow(); }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[isla] {ex.GetType().Name}: {ex.Message}");
            return null;
        }
    }

    private IslaWindow()
    {
        // Monitor principal. Una isla sola, no una por pantalla: elegir cual es de M7.
        HMONITOR monitor = PInvoke.MonitorFromPoint(default, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTOPRIMARY);
        PInvoke.GetDpiForMonitor(monitor, MONITOR_DPI_TYPE.MDT_EFFECTIVE_DPI, out uint dpiX, out _);
        _dpi = dpiX;

        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        if (!PInvoke.GetMonitorInfo(monitor, &info)) throw new InvalidOperationException("sin monitor");

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
                // NOACTIVATE: nunca roba el foco. TOOLWINDOW: fuera de Alt+Tab y de la
                // barra de tareas. TOPMOST: siempre encima. TRANSPARENT: arranca sin
                // recoger un solo clic, que es el estado en el que mas tiempo esta.
                WINDOW_EX_STYLE.WS_EX_NOACTIVATE
                    | WINDOW_EX_STYLE.WS_EX_TOOLWINDOW
                    | WINDOW_EX_STYLE.WS_EX_TOPMOST
                    | WINDOW_EX_STYLE.WS_EX_TRANSPARENT,
                new PCWSTR(clase), new PCWSTR(titulo),
                WINDOW_STYLE.WS_POPUP,
                _x, _y, _w, h,
                default, default, ModuleHandle, null);
        }

        if (_hwnd.IsNull) throw new InvalidOperationException("CreateWindowEx fallo");
        _instancia = this;

        // La ventana mide 520x260 pero solo se pinta en 380x190, arriba y centrado.
        // Sin region, estando abierta se tragaria los clics de los 70 px de margen
        // de cada lado. Es FIJA y cubre a la vez la brasa (pegada arriba) y el panel
        // abierto (que empieza 10 px mas abajo), asi que no hay que tocarla nunca ni
        // puede recortar nada de lo que se dibuja.
        (float pw, float ph, _, float plift) = Medidas(Estado.Abierta);
        int rw = (int)Scale(pw);
        int rh = (int)Scale(plift + ph);
        int rx = (_w - rw) / 2;
        PInvoke.SetWindowRgn(_hwnd, PInvoke.CreateRectRgn(rx, 0, rx + rw, rh), false);

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

        PInvoke.SetTimer(_hwnd, TimerId, TimerMs, null);

        // Todavia no se ensena: la isla no existe mientras no haya nada que decir.
        // Medios avisa con WM_APP_MEDIA en cuanto encuentra una sesion de audio.
        Medios.Arrancar(_hwnd, WM_APP_MEDIA);

        Console.WriteLine($"[isla] {_dpi * 100 / 96}% de escala, ventana {_w}x{h} en {_x},{_y}");
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
                if (wParam.Value == TimerAsoma) isla?.OnFinAsoma();
                else if (wParam.Value == TimerReloj) isla?.OnReloj();
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
                isla?.OnHotkey();
                return new LRESULT(0);

            case WM_DESTROY:
                PInvoke.PostQuitMessage(0);
                return new LRESULT(0);
        }

        return PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);
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

    // --- estado --------------------------------------------------------------------

    /// <summary>
    /// Con WS_EX_TRANSPARENT no llegan mensajes de raton, asi que el hover se mira
    /// preguntando donde esta el cursor. Una llamada cada 120 ms no se mide, y a cambio
    /// la isla recogida no le quita un solo clic a las pestanas del navegador.
    /// </summary>
    private void OnTick()
    {
        // Arrastrando la barra el cursor puede salirse de la zona sin querer, y
        // cerrar la isla en mitad del gesto seria perder el arrastre.
        if (_arrastrando) return;

        PInvoke.GetCursorPos(out System.Drawing.Point p);

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
        _visuals.Mostrar(c);
        _visuals.Progreso(
            c.Duracion > TimeSpan.Zero ? c.Posicion / c.Duracion : 0d,
            c.Duracion - c.Posicion,
            c.Sonando);

        if (!otra) return;
        Console.WriteLine($"[isla] {c.App}: {c.Titulo} - {c.Artista} ({c.Duracion:mm\\:ss})");

        // Cancion nueva: asoma y se vuelve a ir sola. Si el raton ya esta encima no
        // se toca nada, que bastante esta viendo.
        if (_hover) return;
        _base = Estado.Asomada;
        Aplicar();
        PInvoke.SetTimer(_hwnd, TimerAsoma, AsomaMs, null);
    }

    private void OnFinAsoma()
    {
        PInvoke.KillTimer(_hwnd, TimerAsoma);
        if (_base != Estado.Asomada) return;
        _base = Estado.Brasa;
        Aplicar();
    }

    private void Ensenar(bool si)
    {
        if (si == _visible) return;
        _visible = si;
        PInvoke.ShowWindow(_hwnd, si ? SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE : SHOW_WINDOW_CMD.SW_HIDE);
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
        RecogerClics(efectivo == Estado.Abierta);

        // El reloj solo corre con el panel abierto. En reposo la isla no gasta ni un
        // temporizador de mas.
        if (efectivo == Estado.Abierta) PInvoke.SetTimer(_hwnd, TimerReloj, RelojMs, null);
        else PInvoke.KillTimer(_hwnd, TimerReloj);
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
    /// WS_EX_TRANSPARENT es lo unico que deja pasar el raton cruzando procesos: el
    /// HTTRANSPARENT del hit-test no lo hace, medido en el dock poniendo Paint debajo.
    /// Solo estando abierta hay botones que pulsar, asi que solo entonces se quita.
    /// </summary>
    private void RecogerClics(bool recibir)
    {
        nint ex = PInvoke.GetWindowLongPtr(_hwnd, WINDOW_LONG_PTR_INDEX.GWL_EXSTYLE);
        nint nuevo = recibir ? ex & ~WS_EX_TRANSPARENT : ex | WS_EX_TRANSPARENT;
        if (nuevo != ex) PInvoke.SetWindowLongPtr(_hwnd, WINDOW_LONG_PTR_INDEX.GWL_EXSTYLE, nuevo);
    }

    /// <summary>
    /// Ancho, alto, radio y despegue de cada estado, en unidades logicas.
    /// El radio crece con la caja: es lo que hace que parezca que se deforma en vez de
    /// que se redimensiona.
    /// </summary>
    public static (float W, float H, float R, float Lift) Medidas(Estado e) => e switch
    {
        Estado.Brasa => (140f, 5f, 2.5f, 0f),
        Estado.Asomada => (300f, 38f, 19f, 6f),
        _ => (380f, 180f, 28f, 10f),
    };

    public void Dispose()
    {
        PInvoke.KillTimer(_hwnd, TimerId);
        PInvoke.KillTimer(_hwnd, TimerAsoma);
        PInvoke.KillTimer(_hwnd, TimerReloj);
        if (_atajo) PInvoke.UnregisterHotKey(_hwnd, HotkeyId);
        _visuals.Dispose();
        if (!_hwnd.IsNull) PInvoke.DestroyWindow(_hwnd);
        _instancia = null;
    }
}
