using System.Diagnostics;
using System.Runtime.InteropServices;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Dwm;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.UI.HiDpi;
using Windows.Win32.UI.Input.KeyboardAndMouse;
using Windows.Win32.UI.Shell;
using Windows.Win32.UI.WindowsAndMessaging;

namespace Lanzador;

internal sealed unsafe class LanzadorWindow : IDisposable
{
    // --- medidas, en unidades logicas (96 ppp) -------------------------------------
    private const int AnchoLogico = 660;
    private const int AltoFranja = 56;      // la caja de texto
    private const int AltoFila = 44;        // cada resultado
    private const int MargenLista = 8;      // aire arriba y abajo de la lista

    /// <summary>A que altura de la pantalla se asoma, en tanto por uno del alto util.</summary>
    private const float AlturaEnPantalla = 0.22f;

    /// <summary>
    /// El color de la franja de arriba, en BGR como lo quiere GDI. Solido y no
    /// transparente: un control EDIT no sabe pintarse sobre el acrilico, y esa es la
    /// renuncia que se acepto al elegirlo en vez de dibujar la edicion de texto a mano.
    /// </summary>
    private const uint ColorFranja = 0x002B2B2B;

    private const string ClassName = "LanzadorVentana";
    private const int HotkeyId = 1;
    private const int EditId = 100;

    // Mensajes. CsWin32 no genera las constantes WM_*, asi que van a mano.
    private const uint WM_DESTROY = 0x0002;
    private const uint WM_SETFONT = 0x0030;
    private const uint WM_GETTEXT = 0x000D;
    private const uint WM_GETTEXTLENGTH = 0x000E;
    private const uint WM_SETTEXT = 0x000C;
    private const uint WM_COMMAND = 0x0111;
    private const uint WM_KEYDOWN = 0x0100;
    private const uint WM_ACTIVATE = 0x0006;
    private const uint WM_HOTKEY = 0x0312;
    private const uint EM_SETSEL = 0x00B1;
    private const uint WM_CTLCOLOREDIT = 0x0133;
    private const int EN_CHANGE = 0x0300;

    private const uint VK_ESCAPE = 0x1B;
    private const uint VK_RETURN = 0x0D;
    private const uint VK_UP = 0x26;
    private const uint VK_DOWN = 0x28;

    // El delegate va en un campo estatico de solo lectura: si se pasa un lambda suelto a
    // RegisterClassEx, el GC se lo lleva y la ventana muere al primer mensaje. Lo
    // aprendio el dock.
    private static readonly WNDPROC WndProcThunk = WndProc;
    private static ushort _classAtom;
    private static LanzadorWindow? _instancia;

    private readonly LanzadorConfig _config;
    private readonly HWND _hwnd;
    private readonly HWND _edit;
    private DeleteObjectSafeHandle _fuente;
    private readonly LanzadorVisuals _visuals;
    private readonly HBRUSH _fondoCaja;
    private readonly List<Entrada> _indice;
    private readonly Uso _uso;
    // No son readonly y no es un descuido: cambian al asomarse en otra pantalla. Ver
    // AplicarDpi, que es donde esta el porque.
    private uint _dpi;
    private int _ancho;

    private List<Resultado> _resultados = [];
    private int _elegido;
    private string _consulta = string.Empty;
    private bool _visible;
    private long _pulsado;   // marca de tiempo del atajo, para medir hasta que se ve

    /// <summary>
    /// Traza de lo que se escribe y lo que sale, con LANZADOR_LOG=1. Como el
    /// DOCK_HOVER_LOG del dock: casi todo lo que puede ir mal aqui se ve en texto, y un
    /// diff de capturas es el ultimo recurso.
    /// <para>
    /// <b>No es un registro</b> (regla 11): va a la consola, no a un fichero, y solo si
    /// lo pides por variable de entorno.
    /// </para>
    /// </summary>
    private static readonly bool Traza = Environment.GetEnvironmentVariable("LANZADOR_LOG") == "1";

    public static LanzadorWindow? Crear(LanzadorConfig config, List<Entrada> indice, Uso uso)
    {
        try { return new LanzadorWindow(config, indice, uso); }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[lanzador] {ex.GetType().Name}: {ex.Message}");
            return null;
        }
    }

    private LanzadorWindow(LanzadorConfig config, List<Entrada> indice, Uso uso)
    {
        _config = config;
        _indice = indice;
        _uso = uso;

        // El DPI de la pantalla principal para crear; al asomarse se recoloca en la del
        // cursor. Reescalar la fuente por pantalla llega si hace falta.
        HMONITOR principal = PInvoke.MonitorFromPoint(default, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTOPRIMARY);
        PInvoke.GetDpiForMonitor(principal, MONITOR_DPI_TYPE.MDT_EFFECTIVE_DPI, out uint dpiX, out _);
        _dpi = dpiX;
        _ancho = Escalar(AnchoLogico);

        RegistrarClase();

        fixed (char* clase = ClassName)
        fixed (char* titulo = "Lanzador")
        {
            _hwnd = PInvoke.CreateWindowEx(
                // TOOLWINDOW: fuera del Alt+Tab, de la barra de tareas y del dock.
                // TOPMOST: por encima de lo que estes mirando.
                //
                // Aqui NO va WS_EX_NOACTIVATE, al reves que en la isla y el dock: este
                // programa SI tiene que recibir el foco, porque hay que escribir en el.
                WINDOW_EX_STYLE.WS_EX_TOOLWINDOW | WINDOW_EX_STYLE.WS_EX_TOPMOST,
                new PCWSTR(clase), new PCWSTR(titulo),
                WINDOW_STYLE.WS_POPUP,
                0, 0, _ancho, Escalar(AltoFranja),
                default, default, Modulo, null);
        }

        if (_hwnd.IsNull) throw new InvalidOperationException("CreateWindowEx fallo");

        Acristalar();
        _fuente = CrearFuente();

        // El EDIT pinta su propio fondo con los colores del sistema, que en claro es
        // blanco: sobre el acrilico oscuro quedaria una banda cegadora. WM_CTLCOLOREDIT
        // es la forma documentada de darle otros, y el pincel tiene que sobrevivir a la
        // llamada -- por eso es un campo y no una variable local.
        _fondoCaja = PInvoke.CreateSolidBrush(new COLORREF(ColorFranja));
        _edit = CrearCaja();
        _visuals = new LanzadorVisuals(_hwnd, _dpi / 96f, _ancho);

        if (!Config.LeerAtajo(config.Atajo, out HOT_KEY_MODIFIERS mods, out uint tecla))
        {
            Console.Error.WriteLine($"[lanzador] no entiendo el atajo \"{config.Atajo}\"; uso Alt+Space.");
            mods = HOT_KEY_MODIFIERS.MOD_ALT;
            tecla = 0x20;
        }

        if (!PInvoke.RegisterHotKey(_hwnd, HotkeyId, mods, tecla))
        {
            // Un atajo global que falla en silencio es media hora perdida. Esta fue la
            // razon de que el atajo sea configurable: para poder salir del choque sin
            // recompilar.
            Console.Error.WriteLine($"[lanzador] {config.Atajo} ya lo tiene otro programa. " +
                                    $"Cambia \"atajo\" en {Config.Ruta} y reinicia.");
        }
        else
        {
            Console.WriteLine($"[lanzador] {config.Atajo} registrado.");
        }

        // Lo ultimo: hasta aqui, un mensaje que llegase durante la construccion
        // encontraria la mitad de los campos sin asignar. Con _instancia a null el
        // WndProc cae a DefWindowProc, que es lo correcto mientras no estemos montados.
        _instancia = this;
    }

    // --- mostrar y esconder ---------------------------------------------------------

    /// <summary>
    /// La ventana ya existe y solo se ensena. Crear un HWND, montar Composition y pedir
    /// una superficie cuesta cientos de milisegundos; hacerlo al pulsar el atajo es lo
    /// que hace que un lanzador se sienta lento por mucho que la busqueda sea instantanea.
    /// </summary>
    private void Asomar()
    {
        _pulsado = Stopwatch.GetTimestamp();

        Colocar();
        Escribir(string.Empty);
        Refrescar();

        PInvoke.ShowWindow(_hwnd, SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE);

        // SEGURIDAD.md Â§3.4: la unica llamada del programa, y sobre el handle propio.
        // Windows autoriza a ponerse delante al proceso que acaba de recibir WM_HOTKEY;
        // sin esto la ventana sale sin foco y no recibe lo que escribes.
        PInvoke.SetForegroundWindow(_hwnd);
        PInvoke.SetFocus(_edit);

        _visible = true;

        // Donde y cuanto tardo. La posicion no es adorno: con tres pantallas, "aparece
        // en la que no es" es el fallo tipico y desde fuera no se distingue de "no
        // aparece".
        PInvoke.GetWindowRect(_hwnd, out RECT r);
        Console.WriteLine($"[lanzador] visible en {Milisegundos(_pulsado):0.0} ms " +
                          $"en {r.left},{r.top} de {r.right - r.left}x{r.bottom - r.top} " +
                          $"(pedidos {_ancho}x{AltoActual()} al {_dpi * 100 / 96}%), " +
                          $"hwnd {(nint)_hwnd.Value:X}");
    }

    /// <summary>
    /// Se esconde y <b>no toca el foco de nadie</b>: Windows se lo devuelve solo a quien
    /// lo tenia. No hay ningun SetForegroundWindow sobre una ventana ajena, ni siquiera
    /// para "devolver" el foco.
    /// </summary>
    private void Esconder()
    {
        PInvoke.ShowWindow(_hwnd, SHOW_WINDOW_CMD.SW_HIDE);
        _visible = false;

        // La consulta se vacia al esconder: no hay ninguna lista de consultas anteriores
        // en ningun sitio (SEGURIDAD.md Â§5).
        _consulta = string.Empty;
        _resultados = [];
        _elegido = 0;
    }

    /// <summary>En la pantalla donde esta el cursor, centrado y a un tercio de arriba.</summary>
    private void Colocar()
    {
        PInvoke.GetCursorPos(out System.Drawing.Point raton);
        HMONITOR m = PInvoke.MonitorFromPoint(raton, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTONEAREST);

        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        if (!PInvoke.GetMonitorInfo(m, &info)) return;

        // rcWork y no rcMonitor: por debajo de la barra de tareas, que es donde espera
        // verlo cualquiera.
        int anchoUtil = info.rcWork.right - info.rcWork.left;
        int altoUtil = info.rcWork.bottom - info.rcWork.top;

        // El DPI de ESTA pantalla, antes de calcular nada. Si no, la ventana sale con la
        // escala de la principal: medido con las tres puestas, en la de 125% se veia un
        // 20% pequena. Es el fallo que el dock avisa en su CLAUDE.md y que solo aparece
        // con mas de un monitor.
        PInvoke.GetDpiForMonitor(m, MONITOR_DPI_TYPE.MDT_EFFECTIVE_DPI, out uint dpiAhi, out _);
        AplicarDpi(dpiAhi);

        int x = info.rcWork.left + (anchoUtil - _ancho) / 2;
        int y = info.rcWork.top + (int)(altoUtil * AlturaEnPantalla);

        PInvoke.SetWindowPos(_hwnd, default, x, y, _ancho, AltoActual(),
            SET_WINDOW_POS_FLAGS.SWP_NOZORDER | SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE);
    }

    /// <summary>
    /// Reescala todo a la pantalla donde se va a asomar: el ancho, la fuente de la caja y
    /// la escala con la que dibuja Composition.
    /// <para>
    /// No se atiende WM_DPICHANGED a proposito: esta ventana no se puede arrastrar y se
    /// esconde en cuanto pierde el foco, asi que el unico momento en que puede cambiar de
    /// pantalla es justo antes de asomarse, que es aqui.
    /// </para>
    /// </summary>
    private void AplicarDpi(uint dpi)
    {
        if (dpi == _dpi) return;

        _dpi = dpi;
        _ancho = Escalar(AnchoLogico);

        DeleteObjectSafeHandle vieja = _fuente;
        _fuente = CrearFuente();
        PInvoke.SendMessage(_edit, WM_SETFONT, (nuint)_fuente.DangerousGetHandle(), 1);
        vieja.Dispose();

        int margen = Escalar(18);
        PInvoke.SetWindowPos(_edit, default, margen, Escalar(14),
            _ancho - margen * 2, Escalar(28),
            SET_WINDOW_POS_FLAGS.SWP_NOZORDER | SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE);

        _visuals.Reescalar(dpi / 96f, _ancho);
    }

    private int AltoActual()
    {
        int filas = Math.Min(_resultados.Count, _config.MaxResultados);
        return filas == 0
            ? Escalar(AltoFranja)
            : Escalar(AltoFranja + MargenLista * 2 + filas * AltoFila);
    }

    // --- la consulta ------------------------------------------------------------------

    private void Escribir(string texto)
    {
        fixed (char* t = texto) PInvoke.SendMessage(_edit, WM_SETTEXT, 0, (nint)t);
        PInvoke.SendMessage(_edit, EM_SETSEL, (nuint)texto.Length, texto.Length);
        _consulta = texto;
    }

    /// <summary>
    /// SEGURIDAD.md Â§3.2: el texto se pide con WM_GETTEXT. No se usa GetWindowText â€”
    /// que auditar.ps1 prohibe por la regla 15â€” ni ninguna API de portapapeles: Ctrl+V
    /// lo resuelve el propio control por dentro.
    /// </summary>
    private string LeerCaja()
    {
        int largo = (int)PInvoke.SendMessage(_edit, WM_GETTEXTLENGTH, 0, 0).Value;
        if (largo <= 0) return string.Empty;

        char[] buffer = new char[largo + 1];
        fixed (char* b = buffer) PInvoke.SendMessage(_edit, WM_GETTEXT, (nuint)(largo + 1), (nint)b);
        return new string(buffer, 0, largo);
    }

    private void Refrescar()
    {
        _resultados = _consulta.Length == 0
            ? []
            : Coincidencia.Buscar(_indice, _consulta, _config.MaxResultados, _uso, DateTimeOffset.UtcNow);

        _elegido = 0;
        _visuals.Pintar(_resultados, _elegido);

        if (Traza)
        {
            Console.WriteLine($"[traza] consulta \"{_consulta}\" -> {_resultados.Count} resultados" +
                              (_resultados.Count > 0 ? $", 1o {_resultados[0].Entrada.Nombre}" : ""));
        }

        if (_visible)
        {
            PInvoke.SetWindowPos(_hwnd, default, 0, 0, _ancho, AltoActual(),
                SET_WINDOW_POS_FLAGS.SWP_NOMOVE | SET_WINDOW_POS_FLAGS.SWP_NOZORDER
                | SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE);
        }
    }

    private void Mover(int cuanto)
    {
        if (_resultados.Count == 0) return;
        _elegido = Math.Clamp(_elegido + cuanto, 0, _resultados.Count - 1);
        _visuals.Pintar(_resultados, _elegido);
    }

    /// <summary>
    /// SEGURIDAD.md Â§1.1 y Â§3.8: se lanza <b>la fila que estas viendo seleccionada</b>,
    /// nunca la cadena que escribiste. Y solo desde aqui, que solo se llama desde Enter.
    /// </summary>
    private void Lanzar()
    {
        if (_elegido >= _resultados.Count) return;

        Entrada que = _resultados[_elegido].Entrada;
        string consulta = Coincidencia.Normalizar(_consulta).ToLowerInvariant().Trim();

        Esconder();

        SHELLEXECUTEINFOW info = new()
        {
            cbSize = (uint)sizeof(SHELLEXECUTEINFOW),
            fMask = 0x00000040,   // SEE_MASK_NOCLOSEPROCESS: no esperamos ni vigilamos
            nShow = (int)SHOW_WINDOW_CMD.SW_SHOWNORMAL,
        };

        // Sin verbo: el que ponga el shell por defecto, que es lo que hace un doble clic.
        // Nunca "runas" (regla 17): si algo necesita administrador, lo pedira el.
        fixed (char* destino = que.Destino)
        {
            info.lpFile = new PCWSTR(destino);
            if (!PInvoke.ShellExecuteEx(ref info))
            {
                Console.Error.WriteLine($"[lanzador] no se pudo abrir {que.Destino}");
                return;
            }
        }

        // Solo despues de abrirlo de verdad. Lo que se guarda es lo que lanzaste, no lo
        // que escribiste (regla 11).
        _uso.Registrar(consulta, que.Destino, DateTimeOffset.UtcNow);
        _uso.Guardar();
    }

    // --- el bucle y el WndProc --------------------------------------------------------

    /// <summary>
    /// Las teclas de navegacion se cazan <b>aqui</b> y no subclasando el EDIT. El bucle
    /// es nuestro, asi que mirar el mensaje antes de despacharlo sale gratis y ahorra
    /// SetWindowSubclass entero: menos codigo y una entrada menos en NativeMethods.txt.
    /// </summary>
    public static void Bucle()
    {
        MSG msg;
        while (PInvoke.GetMessage(out msg, default, 0, 0))
        {
            if (_instancia is not null && _instancia._visible && msg.message == WM_KEYDOWN
                && msg.hwnd == _instancia._edit && _instancia.Navegar((uint)msg.wParam.Value))
            {
                continue;
            }

            PInvoke.TranslateMessage(&msg);
            PInvoke.DispatchMessage(&msg);
        }
    }

    /// <summary>true si la tecla era nuestra y el EDIT no debe verla.</summary>
    private bool Navegar(uint tecla)
    {
        switch (tecla)
        {
            case VK_ESCAPE: Esconder(); return true;
            case VK_RETURN: Lanzar(); return true;
            case VK_UP: Mover(-1); return true;
            case VK_DOWN: Mover(+1); return true;
            default: return false;
        }
    }

    private static LRESULT WndProc(HWND hwnd, uint msg, WPARAM wParam, LPARAM lParam)
    {
        LanzadorWindow? v = _instancia;

        switch (msg)
        {
            case WM_HOTKEY when v is not null:
                if (v._visible) { v.Esconder(); Console.WriteLine("[lanzador] escondido por el atajo"); }
                else v.Asomar();
                return new LRESULT(0);

            case WM_COMMAND when v is not null:
                if (Traza) Console.WriteLine($"[traza] WM_COMMAND aviso {(wParam.Value >> 16)}");
                if ((wParam.Value >> 16) != EN_CHANGE) break;
                v._consulta = v.LeerCaja();
                v.Refrescar();
                return new LRESULT(0);

            // Al perder el foco se esconde, que es lo que espera cualquiera de un
            // lanzador: haces clic fuera y desaparece.
            case WM_ACTIVATE when v is not null && (wParam.Value & 0xFFFF) == 0:
                v.Esconder();
                return new LRESULT(0);

            // El EDIT pregunta de que color pintarse justo antes de hacerlo. Se le
            // contesta con el pincel de la franja, que es el mismo color solido.
            case WM_CTLCOLOREDIT when v is not null:
                PInvoke.SetTextColor(new HDC((nint)wParam.Value), new COLORREF(0x00F0F0F0));
                PInvoke.SetBkColor(new HDC((nint)wParam.Value), new COLORREF(ColorFranja));
                return new LRESULT((nint)v._fondoCaja.Value);

            case WM_DESTROY:
                PInvoke.PostQuitMessage(0);
                return new LRESULT(0);
        }

        return PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);
    }

    // --- montaje ----------------------------------------------------------------------

    /// <summary>
    /// El acrilico lo pone DWM, no nosotros: el sistema ya sabe difuminar lo que hay
    /// detras mejor y mas barato de lo que lo hariamos aqui. Hace falta extender el
    /// marco al area de cliente para que ese fondo llegue hasta el borde.
    /// </summary>
    private void Acristalar()
    {
        // SIN DwmExtendFrameIntoClientArea, y es una correccion medida, no un olvido.
        // Con el marco extendido a toda la ventana, lo que pinta GDI queda con alfa cero
        // y DWM lo mezcla: el EDIT salia gris #7F7F7F en vez del color que se le daba, y
        // el cuerpo un #545454 plano. El backdrop de Windows 11 no lo necesita â€” basta
        // con que la ventana no pinte un fondo opaco, y por eso hbrBackground es null.
        uint acrilico = 3;   // DWMSBT_TRANSIENTWINDOW: el de los menus, no el de las ventanas
        PInvoke.DwmSetWindowAttribute(_hwnd, DWMWINDOWATTRIBUTE.DWMWA_SYSTEMBACKDROP_TYPE,
            &acrilico, sizeof(uint));

        uint oscuro = 1;
        PInvoke.DwmSetWindowAttribute(_hwnd, DWMWINDOWATTRIBUTE.DWMWA_USE_IMMERSIVE_DARK_MODE,
            &oscuro, sizeof(uint));

        uint redondas = 2;   // DWMWCP_ROUND
        PInvoke.DwmSetWindowAttribute(_hwnd, DWMWINDOWATTRIBUTE.DWMWA_WINDOW_CORNER_PREFERENCE,
            &redondas, sizeof(uint));
    }

    /// <summary>La fuente del sistema, al tamano de la caja. No se inventa ninguna.</summary>
    private DeleteObjectSafeHandle CrearFuente()
    {
        NONCLIENTMETRICSW metricas = new() { cbSize = (uint)sizeof(NONCLIENTMETRICSW) };
        PInvoke.SystemParametersInfo(SYSTEM_PARAMETERS_INFO_ACTION.SPI_GETNONCLIENTMETRICS,
            metricas.cbSize, &metricas, 0);

        LOGFONTW lf = metricas.lfMessageFont;
        lf.lfHeight = -Escalar(20);
        return PInvoke.CreateFontIndirect(lf);
    }

    private HWND CrearCaja()
    {
        int margen = Escalar(18);

        HWND edit;
        fixed (char* clase = "EDIT")
        fixed (char* vacio = "")
        {
            edit = PInvoke.CreateWindowEx(
                0, new PCWSTR(clase), new PCWSTR(vacio),
                WINDOW_STYLE.WS_CHILD | WINDOW_STYLE.WS_VISIBLE
                    | (WINDOW_STYLE)0x0080     // ES_AUTOHSCROLL
                    | (WINDOW_STYLE)0x0000,    // ES_LEFT
                margen, Escalar(14), _ancho - margen * 2, Escalar(28),
                _hwnd, (HMENU)(nint)EditId, Modulo, null);
        }

        if (edit.IsNull) throw new InvalidOperationException("no se pudo crear la caja de texto");
        PInvoke.SendMessage(edit, WM_SETFONT, (nuint)_fuente.DangerousGetHandle(), 1);
        return edit;
    }

    private static void RegistrarClase()
    {
        if (_classAtom != 0) return;

        fixed (char* clase = ClassName)
        {
            WNDCLASSEXW wc = new()
            {
                // Marshal.SizeOf y no sizeof: WNDCLASSEXW lleva un delegate dentro, asi
                // que es un tipo administrado y sizeof da CS8500.
                cbSize = (uint)Marshal.SizeOf<WNDCLASSEXW>(),
                lpfnWndProc = WndProcThunk,
                hInstance = Modulo,
                lpszClassName = new PCWSTR(clase),
                hCursor = PInvoke.LoadCursor(default, PInvoke.IDC_ARROW),
            };

            _classAtom = PInvoke.RegisterClassEx(wc);
        }

        if (_classAtom == 0) throw new InvalidOperationException("RegisterClassEx fallo");
    }

    private static HMODULE Modulo => PInvoke.GetModuleHandle((PCWSTR)null);

    private int Escalar(int logico) => (int)Math.Round(logico * _dpi / 96.0);

    private static double Milisegundos(long desde) =>
        (Stopwatch.GetTimestamp() - desde) * 1000.0 / Stopwatch.Frequency;

    public void Dispose()
    {
        PInvoke.UnregisterHotKey(_hwnd, HotkeyId);
        _visuals.Dispose();
        _fuente.Dispose();
        if (!_fondoCaja.IsNull) PInvoke.DeleteObject(_fondoCaja);
        if (!_hwnd.IsNull) PInvoke.DestroyWindow(_hwnd);
    }
}
