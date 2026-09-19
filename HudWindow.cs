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
/// La ventana vive <b>escondida</b> y solo se ensena cuando hay algo que decir. Y
/// mientras esta a la vista <b>tampoco estorba</b>: los clics la atraviesan enteros,
/// incluso sobre la capsula. Un HUD que se traga los clics del centro de la pantalla
/// durante segundo y medio despues de cada tecla es peor que el de Windows.
/// </para>
///
/// <para>
/// El nivel sale del sistema. <c>--demo</c> lo sustituye por un guion de valores falsos
/// y no registra las teclas: sirve para afinar muelles sin pelearse con COM al mismo
/// tiempo, que es como se pierde un dia sin saber cual de las dos cosas esta mal.
/// </para>
/// </summary>
internal sealed unsafe class HudWindow
{
    private const string ClassName = "HudVolumen";

    // Los atajos. Los tres de volumen son SEGURIDAD.md §3.1: no es un hook, es pedirle
    // a Windows que mande WM_HOTKEY a NUESTRA ventana cuando se pulse una de tres
    // teclas concretas. Y al registrarlas, Windows deja de verlas -- que es lo que hace
    // que no salga su aviso gris.
    private const int AtajoSalir = 1;
    private const int AtajoSubir = 2;
    private const int AtajoBajar = 3;
    private const int AtajoMudo = 4;

    // Las tres teclas, con su valor, en vez de traerse el enum VIRTUAL_KEY entero de
    // CsWin32. Tres constantes a la vista dicen "solo estas tres" mucho mejor que 250
    // nombres generados, y esto es justo lo que va a mirar un auditor.
    private const uint VK_VOLUME_MUTE = 0xAD;
    private const uint VK_VOLUME_DOWN = 0xAE;
    private const uint VK_VOLUME_UP = 0xAF;

    private const nuint TimerAutoocultar = 1;
    private const nuint TimerEsconder = 2;
    private const nuint TimerDemo = 3;
    private const nuint TimerVigilar = 4;

    /// <summary>
    /// Cada cuanto se mira si el volumen ha cambiado por su cuenta: el mezclador, una
    /// app, el mando de unos auriculares. Nuestras propias pulsaciones no pasan por
    /// aqui, que para eso esta el atajo.
    ///
    /// <para>
    /// ponytail: sondeo a 250 ms en vez de IAudioEndpointVolumeCallback. La isla ya
    /// sondea audio ocho veces por segundo sin coste medible, y el callback de COM
    /// entrega en un hilo ajeno y habria que marshalarlo. Si el retardo llega a
    /// notarse, el techo es implementar la interfaz.
    /// </para>
    /// </summary>
    private const uint MsVigilar = 250;

    /// <summary>
    /// Lo que se tarda en cerrar la capsula. Pasado esto la ventana se esconde de
    /// verdad; antes no, o se cortaria la animacion de salida a media cara.
    /// </summary>
    private const uint MsCierre = 340;

    private const uint MsPasoDemo = 1100;

    // --- mensajes que nos interesan ------------------------------------------------
    // CsWin32 no genera las WM_*, y tenerlas aqui con su valor es mas legible que
    // buscarlas en winuser.h cada vez.
    private const uint WM_DESTROY = 0x0002;
    private const uint WM_NCCALCSIZE = 0x0083;
    private const uint WM_NCACTIVATE = 0x0086;
    private const uint WM_WINDOWPOSCHANGING = 0x0046;
    private const uint WM_MOUSEACTIVATE = 0x0021;
    private const uint WM_TIMER = 0x0113;
    private const uint WM_DISPLAYCHANGE = 0x007E;
    private const uint WM_DPICHANGED = 0x02E0;
    private const uint WM_HOTKEY = 0x0312;

    private const int MA_NOACTIVATE = 3;
    private static readonly HWND HWND_TOPMOST = new(-1);

    // --- medidas, en px logicos (a 96 dpi) -----------------------------------------
    // La capsula es lo que se ve; la holgura es el margen invisible que necesita el
    // squash del tope (morph 4) para no salirse de la ventana y quedarse cortado.
    internal const float AnchoCapsula = 280f;
    internal const float AltoCapsula = 68f;
    private const float Holgura = 24f;

    /// <summary>Separacion del borde de la pantalla. 110 deja libre la barra de tareas.</summary>
    private const float Margen = 110f;

    private static readonly WNDPROC WndProcThunk = WndProc;
    private static ushort _classAtom;
    private static HudWindow? _instancia;

    private readonly HudConfig _config;
    private readonly HWND _hwnd;
    private readonly bool _demo;
    private HudVisuals? _visuals;
    private uint _dpi;
    private bool _atajoSalir;
    private bool _teclas;

    /// <summary>Si la ventana esta a la vista ahora mismo.</summary>
    private bool _enPantalla;

    private int _porcentaje = -1;
    private bool _silenciado;
    private int _pasoDemo;

    /// <summary>Donde esta puesta la ventana, para no repetir la traza en cada aparicion.</summary>
    private int _x, _y, _w, _h;

    private HudWindow(HudConfig config, bool demo)
    {
        _config = config;
        _demo = demo;

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
                // LAYERED + TRANSPARENT es lo que deja pasar los clics. La isla midio
                // que TRANSPARENT A SECAS no basta y se fue a SetWindowRgn; aqui se
                // volvio a medir con los dos puestos y SI pasa -- WindowFromPoint sobre
                // el centro de la capsula devuelve la ventana de detras, igual que con
                // el HUD cerrado. Y el arbol de Composition se sigue pintando: 68 px de
                // capsula en la columna central, empezando donde toca.
                //
                // Eso ahorra la region entera: SetWindowRgn tambien recorta el DIBUJO,
                // asi que habria que acordarse de que cubra el squash del tope.
                WINDOW_EX_STYLE.WS_EX_NOACTIVATE
                    | WINDOW_EX_STYLE.WS_EX_TOOLWINDOW
                    | WINDOW_EX_STYLE.WS_EX_TOPMOST
                    | WINDOW_EX_STYLE.WS_EX_LAYERED
                    | WINDOW_EX_STYLE.WS_EX_TRANSPARENT,
                new PCWSTR(clase), new PCWSTR(titulo),
                // Sin WS_VISIBLE: nace escondida y se ensena cuando haga falta.
                WINDOW_STYLE.WS_POPUP,
                x, y, w, h,
                default, default, ModuleHandle, null);
        }

        if (_hwnd.IsNull) throw new InvalidOperationException("CreateWindowEx fallo");
        _instancia = this;

        // WS_EX_TRANSPARENT solo surte efecto si la ventana es layered. A alfa 255 no
        // cambia nada de como se ve: es el interruptor que hace que los clics pasen.
        PInvoke.SetLayeredWindowAttributes(_hwnd, default, 255, LAYERED_WINDOW_ATTRIBUTES_FLAGS.LWA_ALPHA);

        _visuals = new HudVisuals(_hwnd, _dpi / 96f, AnchoCapsula, AltoCapsula, Holgura);

        // Ctrl+Alt+H para salir. Mientras no haya icono de bandeja es la unica forma
        // limpia de cerrarlo, y hace falta: el HUD se traga las teclas de volumen
        // (SEGURIDAD.md §3.1), asi que sin forma de cerrarlo un fallo suyo te deja sin
        // volumen hasta el Administrador de tareas.
        _atajoSalir = PInvoke.RegisterHotKey(_hwnd, AtajoSalir,
            HOT_KEY_MODIFIERS.MOD_CONTROL | HOT_KEY_MODIFIERS.MOD_ALT, 'H');
        if (!_atajoSalir)
        {
            // Un atajo global que falla en silencio es media hora perdida. El dock tiene
            // Ctrl+Alt+D y la isla Ctrl+Alt+I, asi que hay vecinos compitiendo.
            Console.Error.WriteLine("[hud] Ctrl+Alt+H ya esta cogido; se cierra desde el Administrador de tareas.");
        }

        // En --demo no se registran las teclas ni se vigila nada: el modo de afinar
        // muelles no tiene por que quedarse con las teclas de volumen de nadie.
        if (!_demo)
        {
            _teclas = RegistrarVolumen();

            Estado inicial = LeerVolumen();
            if (inicial.Hay)
            {
                // Se guarda sin ensenar nada: el HUD no tiene por que saltar al arrancar.
                _porcentaje = inicial.Porcentaje;
                _silenciado = inicial.Mudo;
                Console.WriteLine($"[hud] volumen al {inicial.Porcentaje}%{(inicial.Mudo ? " (silenciado)" : "")}");
            }
            else
            {
                Console.Error.WriteLine("[hud] no hay dispositivo de salida; se reintenta solo.");
            }

            PInvoke.SetTimer(_hwnd, TimerVigilar, MsVigilar, null);
        }

        (_x, _y, _w, _h) = (x, y, w, h);
        Console.WriteLine($"[hud] {pantalla} al {_dpi * 100 / 96}%, ventana {w}x{h} en {x},{y}");
        Console.WriteLine($"[hud] capsula {_config.Posicion}, paso {_config.PasoVolumen}%, {_config.MsAutoocultar} ms");

        if (_demo)
        {
            Console.WriteLine($"[hud] --demo: niveles falsos cada {MsPasoDemo} ms. Ctrl+Alt+H para salir.");
            PInvoke.SetTimer(_hwnd, TimerDemo, MsPasoDemo, null);
        }
    }

    public static HudWindow? Create(HudConfig config, bool demo)
    {
        try
        {
            return new HudWindow(config, demo);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[hud] {ex.Message}");
            return null;
        }
    }

    // --- el volumen -------------------------------------------------------------------

    private readonly record struct Estado(bool Hay, int Porcentaje, bool Mudo);

    private static Estado LeerVolumen()
    {
        var v = Volumen.Leer();
        return v is null ? default : new Estado(true, v.Value.Porcentaje, v.Value.Mudo);
    }

    /// <summary>
    /// Las tres teclas, sin modificadores y <b>sin MOD_NOREPEAT</b>: al mantenerla
    /// pulsada tiene que repetir, como hace Windows.
    ///
    /// <para>
    /// Registrarlas es lo que hace que no salga el aviso gris del sistema: el shell
    /// deja de recibir la pulsacion. Si fallan, el HUD sigue arrancando y se limita a
    /// reflejar lo que pase -- pero entonces si se veran dos avisos, y por eso se dice
    /// en la consola en vez de fallar en silencio.
    /// </para>
    /// </summary>
    private bool RegistrarVolumen()
    {
        bool arriba = PInvoke.RegisterHotKey(_hwnd, AtajoSubir, 0, VK_VOLUME_UP);
        bool abajo = PInvoke.RegisterHotKey(_hwnd, AtajoBajar, 0, VK_VOLUME_DOWN);
        bool mudo = PInvoke.RegisterHotKey(_hwnd, AtajoMudo, 0, VK_VOLUME_MUTE);

        if (arriba && abajo && mudo) return true;

        Console.Error.WriteLine(
            $"[hud] teclas de volumen no registradas (subir={arriba}, bajar={abajo}, silenciar={mudo}). " +
            "Otro programa las tiene cogidas: el HUD solo reflejara los cambios, y saldra tambien el aviso de Windows.");
        return arriba || abajo || mudo;
    }

    /// <summary>
    /// Una de las tres teclas. El nivel se lee del sistema cada vez en vez de confiar
    /// en el nuestro: entre pulsacion y pulsacion ha podido cambiarlo otro.
    /// </summary>
    private void OnTeclaVolumen(nuint atajo)
    {
        Estado e = LeerVolumen();
        if (!e.Hay) return;

        if (atajo == AtajoMudo)
        {
            bool mudo = !e.Mudo;
            Volumen.Silenciar(mudo);
            Mostrar(e.Porcentaje, mudo);
            return;
        }

        int destino = Volumen.Paso(e.Porcentaje, _config.PasoVolumen, atajo == AtajoSubir);
        if (destino != e.Porcentaje) Volumen.Poner(destino);

        // Tocar el volumen quita el silencio, como hace Windows. Bajar a cero no: eso
        // es bajar a cero, no silenciar.
        bool sigueMudo = e.Mudo;
        if (e.Mudo && destino > 0)
        {
            Volumen.Silenciar(false);
            sigueMudo = false;
        }

        // Si no ha cambiado nada -- ya estabas en el tope -- Mostrar da el squash.
        Mostrar(destino, sigueMudo);
    }

    /// <summary>
    /// Lo ha cambiado otro: el mezclador, una app, el mando de unos auriculares. El
    /// HUD sale igual, que es algo que el de Windows no hace.
    /// </summary>
    private void Vigilar()
    {
        Estado e = LeerVolumen();
        if (!e.Hay) return;
        if (e.Porcentaje == _porcentaje && e.Mudo == _silenciado) return;

        Mostrar(e.Porcentaje, e.Mudo);
    }

    // --- ensenar y esconder -----------------------------------------------------------

    /// <summary>
    /// El unico camino por el que el HUD aparece: lo llaman la tecla, el vigilante y
    /// <c>--demo</c>, y ninguno mas.
    ///
    /// <para>
    /// Si el nivel no ha cambiado --has subido estando ya al 100%-- no hay nada que
    /// animar, asi que se da el squash del tope. Sin eso, pulsar en el tope no tendria
    /// ninguna respuesta y parecerian teclas rotas.
    /// </para>
    /// </summary>
    public void Mostrar(int porcentaje, bool silenciado)
    {
        if (_visuals is null) return;

        bool cambio = porcentaje != _porcentaje || silenciado != _silenciado;
        _porcentaje = porcentaje;
        _silenciado = silenciado;

        // Si estaba cerrandose, este timer la esconderia a media animacion de entrada.
        PInvoke.KillTimer(_hwnd, TimerEsconder);

        if (!_enPantalla)
        {
            // Se recoloca al aparecer, no al arrancar: el HUD sale en el monitor donde
            // esta el cursor AHORA, no donde estaba cuando se lanzo el programa.
            Recolocar();
            PInvoke.ShowWindow(_hwnd, SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE);
            _enPantalla = true;
        }

        _visuals.Abrir(true);
        _visuals.Nivel(silenciado ? 0f : porcentaje / 100f);
        _visuals.Glifo(Glifos.Indice(porcentaje, silenciado));
        if (!cambio) _visuals.Tope();

        PInvoke.SetTimer(_hwnd, TimerAutoocultar, (uint)_config.MsAutoocultar, null);
    }

    private void Ocultar()
    {
        if (!_enPantalla) return;

        PInvoke.KillTimer(_hwnd, TimerAutoocultar);
        _visuals?.Abrir(false);
        _enPantalla = false;

        // La ventana no se esconde ya: primero tiene que verse la salida. El timer es
        // lo que separa "la capsula se ha ido" de "la ventana ya no existe para el
        // raton".
        PInvoke.SetTimer(_hwnd, TimerEsconder, MsCierre, null);
    }

    private void Esconder()
    {
        PInvoke.KillTimer(_hwnd, TimerEsconder);
        if (!_enPantalla) PInvoke.ShowWindow(_hwnd, SHOW_WINDOW_CMD.SW_HIDE);
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
        // HUD que no ves. GetCursorPos devuelve un punto y nada mas.
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
    /// Vuelve a medir y se mueve. Se llama cada vez que el HUD va a aparecer, para
    /// seguir al cursor entre pantallas, y al cambiar de monitores o de escala.
    /// </summary>
    private void Recolocar()
    {
        uint dpiAntes = _dpi;
        (int x, int y, int w, int h) = MedirEnElMonitorDelCursor(out _dpi, out string pantalla);
        PInvoke.SetWindowPos(_hwnd, HWND_TOPMOST, x, y, w, h, SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE);

        // Solo cuando de verdad se mueve de sitio, no en cada aparicion. Con tres
        // pantallas a tres escalas distintas, saber en cual se ha puesto y con que
        // escala es la diferencia entre diagnosticar un fallo de colocacion en un
        // minuto o a base de capturas.
        if (x != _x || y != _y || w != _w || h != _h)
        {
            _x = x; _y = y; _w = w; _h = h;
            Console.WriteLine($"[hud] {pantalla} al {_dpi * 100 / 96}%, ventana {w}x{h} en {x},{y}");
        }

        if (_dpi == dpiAntes) return;

        // Un cambio de escala invalida todas las medidas en pixeles, y estan horneadas
        // en los visuals. Se rehacen enteros, que es bruto pero es una sola capsula y
        // pasa una vez al arrastrar entre pantallas de distinto DPI.
        _visuals?.Dispose();
        _visuals = new HudVisuals(_hwnd, _dpi / 96f, AnchoCapsula, AltoCapsula, Holgura);
        _visuals.Abrir(_enPantalla);
        if (_porcentaje >= 0)
        {
            _visuals.Nivel(_silenciado ? 0f : _porcentaje / 100f);
            _visuals.Glifo(Glifos.Indice(_porcentaje, _silenciado));
        }
    }

    // --- demo --------------------------------------------------------------------------

    /// <summary>
    /// El guion de <c>--demo</c>. Pasa por los cuatro cortes de glifo, por el silencio,
    /// y repite el 100 dos veces seguidas para que se vea el squash del tope.
    /// </summary>
    private static readonly (int Pct, bool Mudo)[] Guion =
    [
        (0, false), (12, false), (33, false), (34, false), (50, false),
        (66, false), (67, false), (85, false), (100, false), (100, false),
        (100, true), (100, false), (20, false),
    ];

    private void PasoDemo()
    {
        (int pct, bool mudo) = Guion[_pasoDemo % Guion.Length];
        _pasoDemo++;
        Mostrar(pct, mudo);
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

            case WM_TIMER:
                if (wParam.Value == TimerAutoocultar) hud?.Ocultar();
                else if (wParam.Value == TimerEsconder) hud?.Esconder();
                else if (wParam.Value == TimerVigilar) hud?.Vigilar();
                else if (wParam.Value == TimerDemo) hud?.PasoDemo();
                return new LRESULT(0);

            case WM_HOTKEY:
                if (wParam.Value == AtajoSalir) Salir();
                else hud?.OnTeclaVolumen(wParam.Value);
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

        // Devolver las teclas importa: mientras el HUD las tiene, Windows no las ve.
        // Si se saliera sin soltarlas quedarian muertas hasta cerrar sesion.
        if (hud._teclas)
        {
            PInvoke.UnregisterHotKey(hud._hwnd, AtajoSubir);
            PInvoke.UnregisterHotKey(hud._hwnd, AtajoBajar);
            PInvoke.UnregisterHotKey(hud._hwnd, AtajoMudo);
            hud._teclas = false;
        }

        hud._visuals?.Dispose();
        hud._visuals = null;
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
    /// Lo que se rompe en silencio: la colocacion, y la relacion entre constantes que
    /// viven en ficheros distintos. Un HUD tres pixeles descentrado no falla, solo se ve mal, y
    /// eso no lo detecta nadie hasta que lleva un mes asi.
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

        // El squash del tope ensancha la capsula un 5% desde su centro, asi que se sale
        // 7 px por lado en horizontal. La ventana tiene que tener holgura de sobra o el
        // borde se corta justo en el momento en que el HUD llama la atencion. Son dos
        // constantes que viven en ficheros distintos (el 5% esta en HudVisuals.Tope) y
        // nadie se entera si alguien toca una.
        Assert(AnchoCapsula * 0.05f / 2f <= Holgura, "el squash horizontal no cabe en la holgura");
        Assert(AltoCapsula * 0.05f / 2f <= Holgura, "el squash vertical no cabe en la holgura");

        Console.WriteLine("[hud] colocacion: 8 comprobaciones OK");
    }

    private static void Assert(bool condicion, string queFallo)
    {
        if (!condicion) throw new InvalidOperationException($"colocacion: {queFallo}");
    }
}
