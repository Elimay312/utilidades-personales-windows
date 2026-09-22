using System.Diagnostics;
using System.Numerics;
using Microsoft.Win32.SafeHandles;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.System.Power;
using Windows.Win32.System.Threading;
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

    /// <summary>
    /// Lo que dura un aviso de AUDIO, y solo ese. Cuatro segundos estan bien para una
    /// cancion o un pomodoro, pero el HUD del volumen ensena su capsula en la misma
    /// pulsacion y se va a los 1600 ms: con los dos delante, la isla se quedaba dos
    /// segundos sola despues de que el otro se hubiera ido.
    ///
    /// <para>
    /// <b>No es el mismo numero que el HUD, y esa es la gracia.</b> Copiar su 1600 deja
    /// 162 ms de diferencia medidos, con la isla yendose ANTES: empiezan a la vez, pero
    /// el HUD tarda ~370 ms en salir (animacion de 340) y la isla ~150 (muelle de
    /// periodo 55). Lo que hay que igualar es el FINAL, no el principio, asi que la isla
    /// tiene que empezar mas tarde: 1815 = los ~1965 en que el HUD esta fuera, menos su
    /// propia retraccion.
    /// </para>
    ///
    /// <para>
    /// Medido muestreando los dos a la vez -- el HUD por <c>IsWindowVisible</c>, la isla
    /// por pixeles, porque no se esconde sino que se retrae --: <b>20, 30 y 48 ms de
    /// diferencia en tres pasadas, y el signo cambia entre ellas</b>. O sea que ya
    /// estamos en el suelo de ruido del muestreo (~20 ms): a la vez.
    /// </para>
    ///
    /// <para>
    /// ponytail: es una copia del numero del vecino, no una lectura. Si cambias
    /// <c>msAutoocultar</c> en <c>hud.json</c>, esto se desincroniza y hay que tocarlo a
    /// mano. El techo es leer <c>hud.json</c>, y no se hace porque acoplaria la isla al
    /// HUD justo donde los dos proyectos presumen de no conocerse: hoy ninguno depende
    /// de que el otro exista. Cuando el numero se mueva mas de una vez, tocara subirlo.
    /// </para>
    /// </summary>
    private const int MsAvisoAudio = 1815;

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
    private const uint WM_SETTINGCHANGE = 0x001A;

    // PROCESS_QUERY_LIMITED_INFORMATION. Solo para leer el nombre del exe del
    // primer plano cuando hace falta distinguir TextInputHost de una app.
    private const uint ProcessQueryLimitedInformation = 0x1000;
    private const nuint PBT_APMPOWERSTATUSCHANGE = 0xA;

    // WM_APP + 1. Lo manda Medios desde el pool de hilos para avisar de que hay
    // algo nuevo que pintar; el dato viaja aparte, en Medios.Ultima.
    private const uint WM_APP_MEDIA = 0x8001;

    // Del vigilante del fichero a la ventana, y de la ventana al bucle. Son dos saltos
    // porque rehacer la ventana desde dentro de su propio WndProc no se puede.
    private const uint WM_APP_RECARGAR = 0x8002;
    private const uint WM_APP_REHACER = 0x8003;

    /// <summary>Cambio el nivel de la salida. Lo manda COM, no un temporizador.</summary>
    private const uint WM_APP_VOLUMEN = 0x8004;

    /// <summary>Cambiaste de altavoces. Tambien lo manda COM.</summary>
    private const uint WM_APP_DISPOSITIVO = 0x8005;

    /// <summary>El buzon cambio: llego un aviso de otra app, o se fue uno (SEGURIDAD.md s.3.7).</summary>
    private const uint WM_APP_AVISO = 0x8006;

    /// <summary>
    /// Lo que se queda asomado un aviso de otra app antes de recogerse en su burbuja. Mas que
    /// una cancion: es algo que hay que leer, no reconocer.
    /// </summary>
    private const int MsAsomoAviso = 8000;

    // La burbuja del aviso, en unidades logicas: un circulo que cuelga del borde junto a la
    // brasa, como el de Xiaomi. Lo leen igual el dibujo, la zona caliente y la region.
    public const float BurbujaLado = 28f;
    public const float BurbujaY = 4f;
    // Escondida, como la brasa: solo asoman estas logicas de su borde de abajo.
    public const float BurbujaAsoma = 8f;
    private const float BurbujaHueco = 8f;

    /// <summary>
    /// Cuanto se aparta el centro de la burbuja del centro de la ventana, en logicas.
    /// El ancho es el de la principal ahora: con el de la brasa siempre, al abrir el
    /// panel la burbuja se queda encima de la ficha.
    /// </summary>
    public static float BurbujaDx(bool conPrincipal, Estado principal) =>
        conPrincipal ? Medidas(principal).W * 0.5f + BurbujaHueco + BurbujaLado * 0.5f : 0f;

    private const int MA_NOACTIVATE = 3;

    private static readonly HWND HWND_TOPMOST = new(-1);

    // El delegate va en un campo estatico de solo lectura: si se pasa un lambda suelto
    // a RegisterClassEx, el GC se lo lleva y la ventana muere al primer mensaje.
    private static readonly WNDPROC WndProcThunk = WndProc;
    private static ushort _classAtom;
    private static IslaWindow? _instancia;
    private static bool _rehacerPendiente;
    private static bool _rehaciendo;

    /// <summary>
    /// El ultimo aviso de otra app que ya se anuncio. Estatico, como los avisos del buzon: si
    /// rehacerse lo olvidara, cada cruce de pantalla volveria a asomar el mismo aviso.
    /// </summary>
    private static long _anunciado;

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

    /// <summary>
    /// Tics seguidos con el raton en otra pantalla. Cruzar de monitor pasa por el
    /// borde, asi que sin contar tics la isla se mudaria de ida y de vuelta en el mismo
    /// gesto.
    /// </summary>
    private int _ticsFuera;

    /// <summary>
    /// Si la isla sigue al raton. Sale de <c>isla.json</c>: si has fijado una pantalla,
    /// mandas tu y no se mueve.
    /// </summary>
    private readonly bool _seguirCursor;
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
    /// <summary>
    /// El ultimo porcentaje ANUNCIADO, no el ultimo leido. Sirve para no repetir el
    /// mismo aviso: COM avisa de cualquier cambio del escalar, y dos escalares distintos
    /// pueden redondear al mismo entero.
    /// </summary>
    private int _porcentajeAnterior = -1;
    private int _tic;
    private bool _atajoPomodoro;
    private bool _arrastrando;
    private DateTime _leido;
    private TimeSpan _duracion;
    private bool _apartada;
    private DateTime _regionCuando;
    // Lo que la region tiene puesto ahora de cada isla. Crecer se pone ya; encoger espera al
    // muelle, y el tic pone las dos al dia a la vez.
    private Estado _regionMain = Estado.Brasa;
    private Estado _regionAviso = Estado.Brasa;
    // Con que se hizo la ultima region: si lleva burbuja y si la brasa estaba a su lado.
    private (bool Burbuja, bool Principal) _regionBurbuja;
    // La linea de la principal, visible. Se apaga mientras el aviso ocupa el centro: si no,
    // sus 5 px de alto asoman por encima de la pastilla, pegados al borde.
    private bool _lineaVisible = true;

    // --- la isla del aviso (SEGURIDAD.md s.3.7) -----------------------------------------------
    // Otra isla, no un modo de la principal: en reposo es la burbuja junto a la brasa, asoma con
    // un aviso nuevo y se abre en su tarjeta. Solo una de las dos se despliega a la vez, porque
    // ocupan el mismo centro. Mientras el aviso esta desplegado la linea se apaga; si abres la
    // principal, la burbuja se aparta a su lado.
    private Estado _avisoEstado = Estado.Brasa;
    private bool _avisoVisible;
    // Abierta con el atajo: se queda aunque el raton no este encima, como la principal.
    private bool _avisoFijo;
    private int _seguidosAviso;
    private DateTime _finAvisoAsomo;
    // La burbuja, bajada entera porque el raton esta cerca; si no, solo asoma.
    private bool _burbujaFuera;
    private AvisoApp? _enTarjeta;

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
        _seguirCursor = string.IsNullOrWhiteSpace(config.Pantalla);

        // Una isla sola, no una por pantalla: la muesca de un portatil tampoco se
        // repite en cada monitor. Lo que si hace es MUDARSE a la pantalla donde estas
        // trabajando, que es lo que resuelve el problema de verdad -- avisar en un
        // monitor que no estas mirando -- sin triplicar la ventana, el arbol de
        // Composition, la region y los botones de reproduccion.
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
        AplicarRegion();

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

        // El camino normal: COM avisa en cuanto cambia el nivel o el dispositivo. El tic
        // de 8 Hz solo queda como red por si el aviso se cae.
        Audio.Escuchar(_hwnd, WM_APP_VOLUMEN, WM_APP_DISPOSITIVO);

        // El buzon vive fuera de la ventana y sobrevive a rehacerla; aqui solo se le dice a
        // donde avisar, y se recoge lo que ya estuviera esperando sin volver a anunciarlo.
        Avisos.Ventana(_hwnd, WM_APP_AVISO);
        OnAvisos();

        Console.WriteLine($"[isla] {NombreDe(_monitor)} al {_dpi * 100 / 96}%, ventana {_w}x{h} en {_x},{_y}");
        Console.WriteLine($"[isla] pantallas: {string.Join(", ", Monitores().Select(NombreDe))}");
        Console.WriteLine($"[isla] salida: {Audio.Dispositivo()}");
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

            Console.Error.WriteLine($"[isla] no encuentro la pantalla {pantalla}; se usa la del raton.");
        }

        // Vacio = donde esta el raton, que es donde estas trabajando. Antes era siempre
        // la principal: con tres monitores eso queria decir que la isla avisaba en una
        // pantalla que no estabas mirando.
        PInvoke.GetCursorPos(out System.Drawing.Point donde);
        return PInvoke.MonitorFromPoint(donde, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTONEAREST);
    }

    /// <summary>
    /// Cuantos tics seguidos tiene que estar el raton en otra pantalla para que la isla
    /// se mude. A 8 Hz, tres tics son ~375 ms: lo suficiente para no mudarse al rozar un
    /// borde de camino a otro sitio, y poco para que se sienta inmediato.
    /// </summary>
    private const int TicsParaMudarse = 3;

    /// <summary>
    /// La isla se muda a la pantalla del raton. La llama el tic, que ya tiene la
    /// posicion del cursor leida, asi que esto no cuesta ni una llamada de mas.
    ///
    /// <para>
    /// No se muda si la estas apuntando: mover la ventana debajo del cursor cancelaria
    /// el hover a mitad de gesto.
    /// </para>
    /// </summary>
    private void SeguirAlCursor(System.Drawing.Point p)
    {
        HMONITOR bajo = PInvoke.MonitorFromPoint(p, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTONEAREST);

        if (bajo == _monitor || _hover)
        {
            _ticsFuera = 0;
            return;
        }

        if (++_ticsFuera < TicsParaMudarse) return;

        _ticsFuera = 0;
        PedirRehacer();
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

            case WM_APP_VOLUMEN:
                isla?.OnVolumen();
                return new LRESULT(0);

            case WM_APP_DISPOSITIVO:
                isla?.OnDispositivo();
                return new LRESULT(0);

            case WM_APP_AVISO:
                isla?.OnAvisos();
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

            // Ocultar la barra de tareas cambia el area de trabajo y lo anuncia asi,
            // no con WM_DISPLAYCHANGE. Hay que volver a mirar el primer plano ya: si
            // se espera al tic, la isla se queda escondida un segundo de mas, y si
            // Windows nos movio al recolocar el area, hay que volver al filo.
            case WM_SETTINGCHANGE:
                isla?.RevisarPleno();
                break;

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
        // Lo que tiene que sobrevivir a la mudanza. Rehacerse destruye la ventana ENTERA
        // y con ella se iba un pomodoro en marcha; daba igual mientras rehacerse fuera
        // cosa de enchufar un monitor, pero ahora pasa cada vez que cruzas de pantalla
        // con el raton, y un pomodoro que se muere por pasear el raton no sirve de nada.
        bool pomodoro = _instancia?._hayPomodoro ?? false;
        DateTime finPomodoro = _instancia?._finPomodoro ?? default;

        // Y lo que YA se anuncio. Sin esto la isla renace con _sonando en null, Medios
        // se vuelve a enganchar, OnMedios cree que la cancion es nueva y asoma: cada
        // cruce de pantalla te saltaba encima la ficha entera de lo que ya estabas
        // escuchando. Mudarse tiene que ser mudarse, no volver a presentarse.
        string? sonando = _instancia?._sonando;

        Stopwatch reloj = Stopwatch.StartNew();

        _rehaciendo = true;
        _instancia?.Dispose();
        _instancia = null;
        _rehaciendo = false;

        IslaConfig config = Config.Cargar();
        Config.AplicarAutoArranque(config.AutoArranque);
        if (Create(config) is null)
        {
            Console.Error.WriteLine("[isla] no se pudo rehacer la ventana.");
            return;
        }

        // Antes de que llegue el WM_APP_MEDIA que Medios.Arrancar va a postear: esto corre
        // dentro del bucle de mensajes, asi que el mensaje se procesa despues de volver.
        _instancia!._sonando = sonando;

        if (pomodoro && finPomodoro > DateTime.UtcNow) _instancia.RetomarPomodoro(finPomodoro);

        Console.WriteLine($"[isla] rehecha en {reloj.ElapsedMilliseconds} ms");
    }

    /// <summary>
    /// Vuelve a colgar un pomodoro que ya estaba corriendo antes de la mudanza. No
    /// reinicia nada: se conserva la hora de fin, que es lo unico que lo define.
    /// </summary>
    private void RetomarPomodoro(DateTime fin)
    {
        _hayPomodoro = true;
        _finPomodoro = fin;
        _segundoPomodoro = -1;
        Ensenar(true);
        Asomar();
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

        // La misma lectura del cursor sirve para las dos cosas: saber si te estas
        // acercando, y saber en que pantalla estas trabajando.
        if (_seguirCursor) SeguirAlCursor(p);

        // El latido de la brasa se cuelga de este mismo tic en vez de traerse un
        // temporizador propio: son 8 lecturas por segundo, y para una tira de 5 px de
        // alto eso sobra. Asi el reposo no gasta ni una vuelta de reloj de mas.
        Vigilar();
        if (_visible && _actual == Estado.Brasa) Latir();

        // Desplegada, la isla del aviso tiene el raton: ocupa el sitio de la principal abierta.
        // Abierta se queda mientras el raton este encima; asomada pasa a abierta si te quedas.
        if (_avisoEstado != Estado.Brasa)
        {
            if (!Dentro(ZonaCaliente(_avisoEstado == Estado.Abierta), p))
            {
                _seguidosAviso = 0;
                if (_avisoEstado == Estado.Abierta && !_avisoFijo) AvisoA(Estado.Brasa);
                return;
            }
            if (_avisoEstado == Estado.Abierta || ++_seguidosAviso < TicksParaAbrir) return;
            AvisoA(Estado.Abierta);
            return;
        }

        // La burbuja tiene su zona, y va primero porque cae dentro de la franja de la principal.
        // Baja en cuanto llega el raton, sin esperar: es la respuesta a que te acercas. Abrirse
        // si espera, como la isla.
        bool enBurbuja = !_hover && _enTarjeta is not null && Dentro(ZonaBurbuja(), p);
        if (enBurbuja != _burbujaFuera) SacarBurbuja(enBurbuja);
        if (enBurbuja)
        {
            _seguidos = 0;
            if (++_seguidosAviso >= TicksParaAbrir) AvisoA(Estado.Abierta);
            return;
        }
        _seguidosAviso = 0;

        // La zona crece al estar abierta: eso es la histeresis, y sale gratis.
        bool dentro = Dentro(ZonaCaliente(_hover), p);

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

    private static bool Dentro(RECT z, System.Drawing.Point p) =>
        p.X >= z.left && p.X < z.right && p.Y >= z.top && p.Y < z.bottom;

    /// <summary>
    /// La burbuja en coordenadas de la ventana, desde el borde de arriba: el hueco de encima
    /// tambien cuenta, que es por donde llega el raton cuando se sube hasta el filo.
    /// </summary>
    private RECT Burbuja(int margen)
    {
        int cx = _w / 2 + (int)Scale(BurbujaDx(HayPrincipal(), _actual));
        int medio = (int)Scale(BurbujaLado * 0.5f) + margen;
        float alto = _burbujaFuera ? BurbujaY + BurbujaLado : BurbujaAsoma;
        return new RECT { left = cx - medio, right = cx + medio, top = 0, bottom = (int)Scale(alto) + margen };
    }

    /// <summary>La de la pantalla, con 4 logicas de mas alrededor: apuntar a un circulo cansa.</summary>
    private RECT ZonaBurbuja()
    {
        RECT r = Burbuja((int)Scale(4f));
        return new RECT { left = _x + r.left, right = _x + r.right, top = _y + r.top, bottom = _y + r.bottom };
    }

    /// <summary>Lo que la isla principal tiene que ensenar por si misma: algo sonando o un pomodoro.</summary>
    private bool HayPrincipal() => Medios.Ultima is not null || _hayPomodoro;

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
        // Con la tarjeta del aviso abierta, la siguiente pulsacion lo recoge todo.
        if (_avisoEstado == Estado.Abierta)
        {
            AvisoA(Estado.Brasa);
            Console.WriteLine("[isla] atajo -> Brasa");
            return;
        }
        _base = (Estado)(((int)_base + 1) % 3);
        // Con un aviso esperando, abierta es su tarjeta: es lo que pide atencion, y sin esto
        // solo se llegaria a el con el raton.
        if (_base == Estado.Abierta && _enTarjeta is not null)
        {
            _base = Estado.Brasa;
            AvisoA(Estado.Abierta);
            _avisoFijo = true;
            Console.WriteLine("[isla] atajo -> aviso");
            return;
        }
        // Asomada por el atajo tiene que durar lo que dura un asomo. Sin esto el tic la
        // devolvia a la brasa en 120 ms -- su caducidad era la del ultimo aviso, ya pasada -- y
        // la segunda pulsacion volvia a empezar: el atajo no llegaba nunca a abierta.
        if (_base == Estado.Asomada) _finAsomo = DateTime.UtcNow.AddSeconds(AsomoSegundos);
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
        Ensenar(c is not null || _enTarjeta is not null);
        Burbuja();

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
    /// El buzon cambio. Un aviso nuevo asoma con su punto de color y se recoge en la burbuja
    /// junto a la brasa; la tarjeta ensena siempre el mas antiguo que espera, que es el que
    /// lleva mas tiempo sin contestar. Llega por PostMessage desde el pool de hilos.
    /// </summary>
    private void OnAvisos()
    {
        IReadOnlyList<AvisoApp> pendientes = Avisos.Pendientes;
        _enTarjeta = pendientes.Count > 0 ? pendientes[0] : null;
        _visuals.MostrarAviso(_enTarjeta);
        Ensenar(HayPrincipal() || _enTarjeta is not null || _transitorio is not null);

        if (_enTarjeta is null)
        {
            // Contestado o retirado: la isla del aviso se recoge y se apaga. La principal ni se
            // entera.
            AvisoA(Estado.Brasa);
            if (_avisoVisible) _visuals.AvisoVisible(false, _burbujaFuera);
            _avisoVisible = false;
            Burbuja();
            return;
        }

        AvisoApp? nuevo = pendientes.LastOrDefault(a => a.Numero > _anunciado);
        if (!_avisoVisible)
        {
            // Sale de detras del borde, junto a la brasa.
            _avisoVisible = true;
            _visuals.PintarBurbuja(nuevo ?? _enTarjeta);
            _visuals.AvisoVisible(true, _burbujaFuera);
        }
        if (nuevo is not null)
        {
            _anunciado = pendientes.Max(a => a.Numero);
            Aviso titular = TitularDe(nuevo);
            _visuals.TitularAviso(titular.Texto, titular.Color);
            _visuals.PintarBurbuja(nuevo);
            // Asoma con lo que es, salvo que ya este abierta ensenando otro.
            if (_avisoEstado == Estado.Brasa)
            {
                _finAvisoAsomo = DateTime.UtcNow.AddMilliseconds(MsAsomoAviso);
                AvisoA(Estado.Asomada);
            }
        }
        else if (_avisoEstado == Estado.Brasa)
        {
            _visuals.PintarBurbuja(_enTarjeta);
        }
        Burbuja();
    }

    /// <summary>
    /// La isla del aviso al estado pedido; <see cref="Estado.Brasa"/> es la burbuja. Las dos
    /// desplegadas ocuparian el mismo centro, asi que al desplegarse esta la principal vuelve
    /// a la brasa y su linea se apaga hasta que el aviso se recoge.
    /// </summary>
    private void AvisoA(Estado e)
    {
        if (e == Estado.Brasa) _avisoFijo = false;
        if (e == _avisoEstado) return;
        bool abriendo = e > _avisoEstado;
        _avisoEstado = e;
        _seguidosAviso = 0;

        if (e != Estado.Brasa && (_hover || _base != Estado.Brasa || _actual != Estado.Brasa))
        {
            _hover = false;
            _seguidos = 0;
            _base = Estado.Brasa;
            Aplicar();
        }
        // Abierta es la tarjeta del mas antiguo que espera; recogida, la burbuja tambien es suya.
        if (_enTarjeta is not null && e != Estado.Asomada)
        {
            Aviso titular = TitularDe(_enTarjeta);
            _visuals.TitularAviso(titular.Texto, titular.Color);
            _visuals.PintarBurbuja(_enTarjeta);
        }

        _visuals.AvisoGoTo(e, abriendo, _burbujaFuera);
        // Al crecer la region se pone ya; al encogerse espera a que el muelle termine.
        if (abriendo)
        {
            _regionAviso = e;
            AplicarRegion();
        }
        else
        {
            _regionCuando = DateTime.UtcNow.AddMilliseconds(380);
        }

        // Aplicar ya lo hizo si la principal tuvo que recogerse. Si ya estaba en la brasa,
        // sin esto la linea seguiria asomando por encima de la pastilla.
        Burbuja();
    }

    private void SacarBurbuja(bool fuera)
    {
        _burbujaFuera = fuera;
        if (_avisoEstado != Estado.Brasa) return;
        _visuals.AvisoGoTo(Estado.Brasa, abriendo: fuera, fuera);
        // Al bajar, la region crece ya o recortaria la burbuja; al subir, espera al muelle.
        if (fuera) AplicarRegion();
        else if (_regionCuando == default) _regionCuando = DateTime.UtcNow.AddMilliseconds(380);
    }

    /// <summary>
    /// Lo que depende de si la principal tiene algo: donde cae la burbuja, a su lado o en medio,
    /// y si la linea de la principal se ve.
    /// </summary>
    private void Burbuja()
    {
        _visuals.AvisoDx(Scale(BurbujaDx(HayPrincipal(), _actual)));

        bool ver = LineaCabe();
        bool cambioLinea = ver != _lineaVisible;
        if (cambioLinea)
        {
            _lineaVisible = ver;
            // Al volver, la region tiene que ser la de ahora: puede seguir guardando la
            // ficha abierta de antes de que el aviso ocupara el centro.
            if (ver) _regionMain = _actual;
            // Apagarla ya. Un fundido la dejaria un instante pegada al borde de la pastilla.
            _visuals.Principal(ver, instantaneo: !ver);
        }
        else
        {
            _visuals.Principal(ver);
        }

        // Llega o se va un aviso, o la brasa aparece a su lado, con la isla quieta: la region al
        // dia ya. Si hay un encogimiento esperando, lo pondra el tic. Y solo si cambio algo: esto
        // corre con cada aviso de medios, y Spotify avisa a menudo.
        if (cambioLinea || (_regionCuando == default && (_enTarjeta is not null, HayPrincipal()) != _regionBurbuja))
            AplicarRegion();
    }

    /// <summary>
    /// La linea solo cabe cuando el aviso no esta en el centro. Desplegado, o todavia
    /// encogiendose hacia la burbuja, sus 5 px asomarian por encima de la pastilla.
    /// </summary>
    private bool LineaCabe()
    {
        if (_avisoEstado != Estado.Brasa) return false;
        if (_enTarjeta is not null && _regionAviso != Estado.Brasa) return false;
        return _actual != Estado.Brasa || HayPrincipal();
    }

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

    private static Aviso TitularDe(AvisoApp a) => new(
        a.Linea.Length == 0 ? a.Titulo : $"{a.Titulo}   ·   {a.Linea}", false, a.Color == 0 ? 0xFFFFFFu : a.Color);

    private void RefrescarTitular() => _visuals.Compacto(Titular(), Medios.Ultima?.Arte is not null);

    private void Asomar(int ms = AsomoSegundos * 1000)
    {
        // Las dos caducidades tienen que ir juntas: el texto lo cierra _finTransitorio y
        // el tamano _finAsomo. Si solo se acorta una, el aviso se va y la isla se queda
        // asomada en blanco, o al reves.
        _finAsomo = DateTime.UtcNow.AddMilliseconds(ms);
        RefrescarTitular();
        if (_hover) return;
        _base = Estado.Asomada;
        Aplicar();
    }

    /// <summary>
    /// Cambio el nivel, lo cambie quien lo cambie: el HUD con sus teclas, el mezclador,
    /// o el mando de unos auriculares. La isla no lo toca nunca, solo lo cuenta.
    ///
    /// <para>
    /// Y lo cuenta <b>con el dispositivo</b>, que es lo que le faltaba: con tres salidas
    /// enchufadas, un «Volumen 45 %» a secas dice que algo cambio, no donde.
    /// </para>
    /// </summary>
    private void OnVolumen()
    {
        if (!_config.VolumenAsoma) return;

        float v = Audio.Volumen();
        if (v < 0f) return;

        int porcentaje = (int)Math.Round(v * 100);
        if (porcentaje == _porcentajeAnterior) return;
        _porcentajeAnterior = porcentaje;

        string donde = Audio.Dispositivo();
        Avisar(
            donde.Length == 0 ? $"Volumen   {porcentaje} %" : $"Volumen   {porcentaje} %   ·   {donde}",
            MsAvisoAudio);
    }

    /// <summary>
    /// Has cambiado de altavoces. Es el aviso que mas se echaba de menos: el volumen que
    /// vas a oir a partir de ahora es OTRO, y hasta aqui no habia forma de saberlo.
    ///
    /// <para>
    /// Soltar el endpoint viejo tiene que pasar en este hilo y no en el de COM, y tiene
    /// que pasar aunque el aviso este apagado: si no, la onda seguiria latiendo con el
    /// audio del dispositivo anterior.
    /// </para>
    /// </summary>
    private void OnDispositivo()
    {
        Audio.OtroDispositivo();

        // El nivel del dispositivo nuevo no tiene por que parecerse al del viejo, asi
        // que el guardia del aviso de nivel se reinicia: si no, el primer cambio de
        // volumen despues de cambiar de salida se podria comer.
        _porcentajeAnterior = -1;

        if (!_config.VolumenAsoma) return;

        string donde = Audio.Dispositivo();
        if (donde.Length == 0) return;

        float v = Audio.Volumen();
        Avisar(
            v < 0f ? $"Salida   {donde}" : $"Salida   {donde}   ·   {(int)Math.Round(v * 100)} %",
            MsAvisoAudio);
    }

    /// <summary>
    /// Un aviso que se lee y se va: bateria, volumen, fin de pomodoro. Por defecto dura
    /// lo mismo que cualquier asomo; los de audio piden menos para irse a la vez que el
    /// HUD (ver <see cref="MsAvisoAudio"/>).
    /// </summary>
    private void Avisar(string texto, int ms = AsomoSegundos * 1000, uint color = 0)
    {
        _transitorio = new Aviso(texto, false, color);
        _finTransitorio = DateTime.UtcNow.AddMilliseconds(ms);
        Ensenar(true);
        Asomar(ms);
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
            if (!HayPrincipal() && _enTarjeta is null) Ensenar(false);
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

        // Aqui habia un sondeo del volumen a 2 Hz con su propia deuda anotada: hasta medio
        // segundo de retraso, y a 8 Hz no se podia poner porque GetMasterVolumeLevelScalar
        // cruza al servicio de audio y subia la CPU en reposo de 0,42 % a 2,29 %. La deuda
        // esta saldada: ahora avisa COM (Audio.Escuchar) y esto solo cuenta el tic.
        _tic++;

        // La red de seguridad, una vez por segundo. Si el aviso de nivel se cayo -- que
        // pasa cuando falla algo de COM, no al cambiar de altavoces -- se vuelve a poner.
        if ((_tic & 7) == 0 && !Audio.Escuchando) Audio.Escuchar(_hwnd, WM_APP_VOLUMEN, WM_APP_DISPOSITIVO);

        if (_regionCuando != default && ahora > _regionCuando)
        {
            _regionCuando = default;
            _regionMain = _actual;
            _regionAviso = _avisoEstado;
            // El aviso ya es burbuja: la linea puede volver. Burbuja la ensena si cabe.
            Burbuja();
            AplicarRegion();
        }

        if (_avisoEstado == Estado.Asomada && _seguidosAviso == 0 && ahora > _finAvisoAsomo)
            AvisoA(Estado.Brasa);

        // Una vez por segundo: apartarse de lo que este a pantalla completa y volver
        // al principio de la banda topmost. El mismo criterio corre al cambiar un
        // ajuste del sistema (WM_SETTINGCHANGE), que es como la barra anuncia que
        // se oculta.
        if ((_tic & 7) == 0) RevisarPleno();

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
    private void Pintar()
    {
        if (_visible && !_apartada)
        {
            PInvoke.ShowWindow(_hwnd, SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE);
            // ShowWindow a solas no deshace un movimiento: al cambiar el area de
            // trabajo Windows recoloca las ventanas, y una isla escondida puede
            // volver ya fuera del filo. Se clava aqui, no en cada tic.
            Anclar();
        }
        else
        {
            PInvoke.ShowWindow(_hwnd, SHOW_WINDOW_CMD.SW_HIDE);
        }
    }

    /// <summary>
    /// Apartarse si el primer plano ocupa la pantalla, y si no, seguir en el filo y
    /// en la banda topmost. Lo llaman el tic y el aviso de que cambio un ajuste.
    /// </summary>
    private void RevisarPleno()
    {
        bool pleno = HayPlenoPantalla();
        if (pleno != _apartada)
        {
            _apartada = pleno;
            Pintar();
        }

        if (_visible && !_apartada) Anclar();
    }

    /// <summary>
    /// Si hay algo ocupando la pantalla entera. Lo decide la VENTANA, no el estado del
    /// sistema: SHQueryUserNotificationState devuelve BUSY de forma transitoria despues
    /// de cualquier minimizado, y en el dock eso provocaba que se apartara sola una vez
    /// por minimizado. Aqui se comparan los estilos y el rectangulo, que no mienten.
    ///
    /// Con la barra de tareas en auto-ocultar el area de trabajo PASA A SER el monitor,
    /// asi que una maximizada cubre los cuatro bordes y, si no tiene marco (una
    /// CoreWindow de una app UWP, o el escritorio), antes se contaba como un juego.
    /// La isla se escondia al ocultar la barra y no volvia.
    ///
    /// Solo se lee la ventana en primer plano, y el marco raiz si esa ventana es una
    /// CoreWindow. No se enumera nada y no se toca nada (SEGURIDAD.md §3.5).
    /// </summary>
    private bool HayPlenoPantalla()
    {
        HWND frente = PInvoke.GetForegroundWindow();
        if (frente.IsNull || frente == _hwnd) return false;
        if (frente == PInvoke.GetShellWindow() || frente == PInvoke.GetDesktopWindow()) return false;

        // Solo cuenta lo que pase en NUESTRA pantalla.
        if (PInvoke.MonitorFromWindow(frente, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTONEAREST) != _monitor)
            return false;

        string clase = NombreClase(frente);
        if (clase is "Progman" or "WorkerW" or "Shell_TrayWnd" or "Shell_SecondaryTrayWnd")
            return false;

        const nint WsCaption = 0x00C00000;
        const nint WsThickFrame = 0x00040000;

        // TextInputHost es del shell: una CoreWindow a pantalla completa que no es
        // un video. Una app UWP maximizada tampoco: su CoreWindow no tiene marco,
        // pero el marco raiz si.
        if (clase == "Windows.UI.Core.CoreWindow")
        {
            if (EsProceso(frente, "TextInputHost")) return false;

            HWND raiz = PInvoke.GetAncestor(frente, GET_ANCESTOR_FLAGS.GA_ROOT);
            if (!raiz.IsNull && raiz != frente)
            {
                nint estiloRaiz = PInvoke.GetWindowLongPtr(raiz, WINDOW_LONG_PTR_INDEX.GWL_STYLE);
                if ((estiloRaiz & (WsCaption | WsThickFrame)) != 0) return false;
            }
        }

        // Una maximizada cubre el monitor igual que una a pantalla completa; lo que las
        // separa son los estilos. IsZoomed devuelve true en los dos casos y no sirve.
        nint estilo = PInvoke.GetWindowLongPtr(frente, WINDOW_LONG_PTR_INDEX.GWL_STYLE);
        if ((estilo & (WsCaption | WsThickFrame)) != 0) return false;

        if (!PInvoke.GetWindowRect(frente, out RECT r)) return false;

        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        if (!PInvoke.GetMonitorInfo(_monitor, &info)) return false;

        RECT p = info.rcMonitor;

        // El marco invisible de una maximizada cuelga por fuera del monitor. Con la
        // barra oculta eso cubre los cuatro bordes igual que un juego, pero un juego
        // queda a ras: no sobresale. Medido aqui, con la barra en auto-ocultar:
        // Chrome maximizado da -8,-8-1928,1088 sobre un monitor de 1920x1080.
        const int cuelgue = 4;
        if (r.left <= p.left - cuelgue && r.top <= p.top - cuelgue
            && r.right >= p.right + cuelgue && r.bottom >= p.bottom + cuelgue)
            return false;

        return r.left <= p.left && r.top <= p.top && r.right >= p.right && r.bottom >= p.bottom;
    }

    private static string NombreClase(HWND ventana)
    {
        Span<char> buffer = stackalloc char[64];
        fixed (char* p = buffer)
        {
            int largo = PInvoke.GetClassName(ventana, p, buffer.Length);
            return largo > 0 ? new string(buffer[..largo]) : string.Empty;
        }
    }

    /// <summary>
    /// El nombre del exe, sin ruta. Solo se llama para una CoreWindow: es la unica
    /// forma estable de reconocer TextInputHost, porque el titulo va traducido.
    /// </summary>
    private static bool EsProceso(HWND ventana, string nombre)
    {
        uint pid = 0;
        PInvoke.GetWindowThreadProcessId(ventana, &pid);
        if (pid == 0) return false;

        try
        {
            using SafeFileHandle handle = PInvoke.OpenProcess_SafeHandle(
                (PROCESS_ACCESS_RIGHTS)ProcessQueryLimitedInformation, false, pid);
            if (handle.IsInvalid) return false;

            Span<char> buffer = stackalloc char[260];
            uint largo = (uint)buffer.Length;
            if (!PInvoke.QueryFullProcessImageName(
                    handle, PROCESS_NAME_FORMAT.PROCESS_NAME_WIN32, buffer, ref largo)
                || largo == 0)
                return false;

            ReadOnlySpan<char> ruta = buffer[..(int)largo];
            int barra = ruta.LastIndexOf('\\');
            ReadOnlySpan<char> fichero = barra >= 0 ? ruta[(barra + 1)..] : ruta;
            return fichero.Equals(nombre + ".exe", StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            // Un proceso que no se deja leer no es el shell. Se sigue con la geometria.
            return false;
        }
    }

    /// <summary>
    /// Windows saca las ventanas de la banda topmost sin quitarles el bit, y al
    /// cambiar el area de trabajo (la barra que se oculta) las puede mover. Dos
    /// llamadas para el orden: la primera vuelve a la banda, la segunda sube al
    /// principio de ella. La posicion se clava en el filo, que es donde nacio.
    /// </summary>
    private void Anclar()
    {
        const SET_WINDOW_POS_FLAGS Quieta = SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE;

        PInvoke.SetWindowPos(
            _hwnd, HWND_TOPMOST, _x, _y, _w, (int)Scale(LogicalWindowHeight), Quieta);
        PInvoke.SetWindowPos(_hwnd, HWND.Null, _x, _y, _w, (int)Scale(LogicalWindowHeight), Quieta);
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
        Burbuja();

        // Al CRECER la region se pone ya, o recortaria lo que esta creciendo. Al
        // ENCOGERSE hay que esperar a que el muelle termine, o se recortaria la
        // animacion de cierre a media carrera. La espera la vigila el tic de 120 ms.
        // Crecer no cancela el encogimiento que la isla del aviso pueda tener esperando.
        if (abriendo)
        {
            _regionMain = efectivo;
            AplicarRegion();
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

        // En la tarjeta del aviso solo hay botones, y el que se pulse vuelve por la tuberia a
        // la app que lo mando. Que significa es cosa suya (SEGURIDAD.md s.3.7).
        if (_avisoEstado == Estado.Abierta)
        {
            int boton = _visuals.GolpeBoton(p);
            if (_enTarjeta is not null && boton >= 0 && boton < _enTarjeta.Botones.Count)
                Avisos.Responder(_enTarjeta.Numero, _enTarjeta.Botones[boton].Id);
            return;
        }

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
    private void AplicarRegion()
    {
        _regionBurbuja = (_enTarjeta is not null, HayPrincipal());
        HRGN region = PInvoke.CreateRectRgn(0, 0, 0, 0);
        // Sin linea no hay rectangulo: uno invisible de 140x5 se comeria el borde de la pastilla.
        if (_lineaVisible) Sumar(region, RectDe(_regionMain));
        // La isla del aviso, mientras hay uno: su burbuja -- solo su cuadrado, nada del hueco que
        // la separa de la brasa --, o la pastilla entera si esta desplegada.
        if (_enTarjeta is not null) Sumar(region, _regionAviso == Estado.Brasa ? Burbuja(0) : RectDe(_regionAviso));
        // La ventana se queda la region: no se suelta aqui.
        PInvoke.SetWindowRgn(_hwnd, region, false);
    }

    private RECT RectDe(Estado estado)
    {
        (float w, float h, _, float lift) = Medidas(estado);
        int rw = (int)Scale(w);
        int rx = (_w - rw) / 2;
        int ry = (int)Scale(lift);
        return new RECT { left = rx, right = rx + rw, top = ry, bottom = ry + (int)Scale(h) };
    }

    private static void Sumar(HRGN region, RECT r)
    {
        HRGN otra = PInvoke.CreateRectRgn(r.left, r.top, r.right, r.bottom);
        PInvoke.CombineRgn(region, region, otra, RGN_COMBINE_MODE.RGN_OR);
        PInvoke.DeleteObject(otra);
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
        // Antes de destruir la ventana: un CCW registrado apuntando a un HWND que ya no
        // existe es pedirle a COM que le mande mensajes a un muerto. Y esto se llama
        // tambien al rehacerse, no solo al cerrar.
        Audio.Callar();
        _visuals.Dispose();
        if (!_hwnd.IsNull) PInvoke.DestroyWindow(_hwnd);
        _instancia = null;
    }
}
