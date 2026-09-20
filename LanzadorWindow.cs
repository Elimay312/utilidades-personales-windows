using System.Diagnostics;
using System.Runtime.InteropServices;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Dwm;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.UI.HiDpi;
using Windows.Win32.UI.Input.KeyboardAndMouse;
using Windows.Win32.System.DataExchange;
using Windows.Win32.UI.Shell;
using Windows.Win32.UI.WindowsAndMessaging;

namespace Lanzador;

internal sealed unsafe class LanzadorWindow : IDisposable
{
    // --- medidas, en unidades logicas (96 ppp) -------------------------------------
    private const int AnchoLogico = 660;
    private const int AltoFranja = 64;      // la caja de texto
    private const int AltoFila = 48;        // cada resultado
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

    // --- Everything -----------------------------------------------------------------
    private const nuint TemporizadorEverything = 1;
    private const nuint TemporizadorIconos = 2;

    /// <summary>
    /// Cuanto se espera antes de pedir los iconos de lo que se ve.
    /// <para>
    /// Sin rebote, escribir "micro" pedia los iconos de "m", "mi", "mic", "micr" y
    /// "micro": cuarenta extracciones para usar ocho. Y cada extraccion carga en nuestro
    /// proceso el manejador de iconos de esa aplicacion, que ya no se descarga — medido
    /// en H8, +107 MB con iconos frente a +30 MB sin ellos.
    /// </para>
    /// </summary>
    private const uint ReboteIconosMs = 110;

    /// <summary>
    /// Cuanto se espera desde la ultima tecla antes de preguntar. Sin rebote, escribir
    /// "documento" serian nueve preguntas y ocho respuestas que se tiran.
    /// </summary>
    private const uint ReboteMs = 60;

    /// <summary>Cuantos ficheros se piden. Se ensenan 8 como mucho, pero el ranking necesita elegir.</summary>
    private const uint FicherosQuePedir = 30;

    /// <summary>
    /// Marca de nuestras respuestas. El header deja elegir el dwData con el que Everything
    /// contesta, asi que le metemos un numero de serie en los 16 bits bajos: si llega la
    /// respuesta de una consulta que ya no es la de ahora, se tira sin mirarla. Sin esto,
    /// escribir rapido hace que la lista parpadee con resultados viejos.
    /// </summary>
    private const uint MarcaRespuesta = 0x4C5A0000;

    // Mensajes. CsWin32 no genera las constantes WM_*, asi que van a mano.
    private const uint WM_DESTROY = 0x0002;
    private const uint WM_SETFONT = 0x0030;
    private const uint WM_GETTEXT = 0x000D;
    private const uint WM_GETTEXTLENGTH = 0x000E;
    private const uint WM_SETTEXT = 0x000C;
    private const uint WM_COMMAND = 0x0111;
    private const uint WM_KEYDOWN = 0x0100;
    private const uint WM_KEYUP = 0x0101;
    private const uint WM_MOUSEMOVE = 0x0200;
    private const uint WM_LBUTTONUP = 0x0202;
    private const uint WM_ACTIVATE = 0x0006;
    private const uint WM_TIMER = 0x0113;

    /// <summary>WM_APP + 1. Lo manda el hilo que carga iconos para que se repinte.</summary>
    private const uint WM_APP_ICONO = 0x8001;

    /// <summary>WM_APP + 2. Lo manda el hilo que construye el indice cuando termina.</summary>
    private const uint WM_APP_INDICE = 0x8002;
    private const uint WM_HOTKEY = 0x0312;
    private const uint EM_SETSEL = 0x00B1;
    private const uint EM_SETMARGINS = 0x00D3;
    private const uint WM_CTLCOLOREDIT = 0x0133;
    private const uint WM_CTLCOLORSTATIC = 0x0138;
    private const int EN_CHANGE = 0x0300;

    private const uint VK_ESCAPE = 0x1B;
    private const uint VK_RETURN = 0x0D;
    private const uint VK_UP = 0x26;
    private const uint VK_DOWN = 0x28;
    private const uint VK_CONTROL = 0x11;

    /// <summary>Cada cuanto se vuelve a construir el indice al asomarse.</summary>
    private static readonly TimeSpan IndiceCaduca = TimeSpan.FromMinutes(5);

    // El delegate va en un campo estatico de solo lectura: si se pasa un lambda suelto a
    // RegisterClassEx, el GC se lo lleva y la ventana muere al primer mensaje. Lo
    // aprendio el dock.
    private static readonly WNDPROC WndProcThunk = WndProc;
    private static ushort _classAtom;
    private static LanzadorWindow? _instancia;

    private readonly LanzadorConfig _config;
    private readonly HWND _hwnd;
    private readonly HWND _franja;
    private readonly HWND _edit;
    private DeleteObjectSafeHandle _fuente;
    private readonly LanzadorVisuals _visuals;
    private readonly HBRUSH _fondoCaja;
    private List<Entrada> _indice;
    private readonly Uso _uso;
    // No son readonly y no es un descuido: cambian al asomarse en otra pantalla. Ver
    // AplicarDpi, que es donde esta el porque.
    private uint _dpi;
    private int _ancho;

    private List<Entrada> _ficheros = [];
    private volatile List<Entrada>? _indiceReciente;
    private DateTimeOffset _indiceCuando = DateTimeOffset.MinValue;
    private bool _indexando;

    /// <summary>
    /// Si Control esta pulsado, sabido <b>por los mensajes de nuestra propia ventana</b>.
    /// No se consulta el estado del teclado: GetAsyncKeyState y compania estan prohibidos
    /// por la regla 3, y no hacen falta — el WM_KEYDOWN de Control ya nos llega cuando la
    /// caja tiene el foco, que es el unico momento en que esto importa.
    /// </summary>
    private bool _control;
    private bool _sinEverything;

    /// <summary>
    /// 0 = no hay repintado pendiente. Ocho iconos que llegan casi a la vez son ocho
    /// repintados, y cada repintado pide una superficie nueva de casi 2 MB. Con esto son
    /// uno.
    /// </summary>
    private int _repintadoPedido;
    private ushort _serie;
    private long _preguntado;
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

    public static LanzadorWindow? Crear(LanzadorConfig config, Uso uso)
    {
        try { return new LanzadorWindow(config, uso); }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[lanzador] {ex.GetType().Name}: {ex.Message}");
            return null;
        }
    }

    private LanzadorWindow(LanzadorConfig config, Uso uso)
    {
        _config = config;
        _indice = [];     // llega en cuanto el hilo del indice acabe
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
        // MAQUETA: sin EDIT ni STATIC. Toda la franja la dibuja Composition, que es lo
        // unico que puede ser translucido. En la maqueta el texto es falso.
        _franja = default;
        _edit = default;
        _visuals = new LanzadorVisuals(_hwnd, _dpi / 96f, _ancho, config.MaxResultados);

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

        // El hilo que extrae iconos avisa desde fuera; solo pide un repintado, y solo si
        // no habia ya uno pedido.
        Iconos.Arrancar(() =>
        {
            if (Interlocked.Exchange(ref _repintadoPedido, 1) == 0)
            {
                PInvoke.PostMessage(_hwnd, WM_APP_ICONO, 0, 0);
            }
        });

        // Lo ultimo: hasta aqui, un mensaje que llegase durante la construccion
        // encontraria la mitad de los campos sin asignar. Con _instancia a null el
        // WndProc cae a DefWindowProc, que es lo correcto mientras no estemos montados.
        _instancia = this;
    }

    /// <summary>
    /// Se llama <b>desde el hilo del indice</b>. Deja el indice a un lado y avisa; el
    /// cambio lo hace la ventana en su hilo, que es quien lo lee al buscar.
    /// </summary>
    public void RecibirIndice(List<Entrada> indice)
    {
        _indiceReciente = indice;
        PInvoke.PostMessage(_hwnd, WM_APP_INDICE, 0, 0);
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
        Reindexar();

        Colocar();
        _consulta = Environment.GetEnvironmentVariable("LANZADOR_MAQUETA") ?? "conf";
        Refrescar();

        PInvoke.ShowWindow(_hwnd, SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE);

        // SEGURIDAD.md Â§3.4: la unica llamada del programa, y sobre el handle propio.
        // Windows autoriza a ponerse delante al proceso que acaba de recibir WM_HOTKEY;
        // sin esto la ventana sale sin foco y no recibe lo que escribes.
        PInvoke.SetForegroundWindow(_hwnd);
        if (!_edit.IsNull) PInvoke.SetFocus(_edit);

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
        _control = false;   // si se solto fuera, no nos enteramos; se olvida al esconder

        // La consulta se vacia al esconder: no hay ninguna lista de consultas anteriores
        // en ningun sitio (SEGURIDAD.md Â§5).
        _consulta = string.Empty;
        _resultados = [];
        _ficheros = [];
        _elegido = 0;
        PInvoke.KillTimer(_hwnd, TemporizadorEverything);
        PInvoke.KillTimer(_hwnd, TemporizadorIconos);
    }

    /// <summary>
    /// Vuelve a construir el indice si ya tiene sus anos, en segundo plano. Sin esto,
    /// instalabas algo y no aparecia hasta reiniciar el lanzador.
    /// <para>
    /// Al asomarse y no con un temporizador: un temporizador despertaria el proceso cada
    /// pocos minutos para nada. Aqui solo se paga cuando ibas a buscar algo de todas
    /// formas, y la lista de antes sigue sirviendo mientras llega la nueva.
    /// </para>
    /// </summary>
    private void Reindexar()
    {
        if (_indexando || DateTimeOffset.UtcNow - _indiceCuando < IndiceCaduca) return;

        _indexando = true;
        Indice.EnSegundoPlano(RecibirIndice);
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

        // Lo que sigue es del control EDIT, que en la maqueta no existe. El reescalado
        // de los visuales va DESPUES y no debe saltarse: sin el, la lista se dibuja a
        // escala 1 dentro de una ventana a 1,25 y sobran 147 px de panel vacio abajo.
        if (!_edit.IsNull)
        {
            DeleteObjectSafeHandle vieja = _fuente;
            _fuente = CrearFuente();
            PInvoke.SendMessage(_edit, WM_SETFONT, (nuint)_fuente.DangerousGetHandle(), 1);
            vieja.Dispose();
        }

        if (!_edit.IsNull)
        {
            PInvoke.SetWindowPos(_franja, default, 0, 0, _ancho, Escalar(AltoFranja),
                SET_WINDOW_POS_FLAGS.SWP_NOZORDER | SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE);

            PInvoke.SetWindowPos(_edit, default,
                0, Escalar((AltoFranja - LanzadorVisuals.AltoDelTexto) / 2),
                _ancho, Escalar(LanzadorVisuals.AltoDelTexto),
                SET_WINDOW_POS_FLAGS.SWP_NOZORDER | SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE);
            Sangrar(_edit);
        }

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
        if (_edit.IsNull) { _consulta = texto; return; }

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
        if (_edit.IsNull) return _consulta;
        int largo = (int)PInvoke.SendMessage(_edit, WM_GETTEXTLENGTH, 0, 0).Value;
        if (largo <= 0) return string.Empty;

        char[] buffer = new char[largo + 1];
        fixed (char* b = buffer) PInvoke.SendMessage(_edit, WM_GETTEXT, (nuint)(largo + 1), (nint)b);
        return new string(buffer, 0, largo);
    }

    private void Refrescar()
    {
        _resultados = [];

        if (_consulta.Trim().Length > 0)
        {
            // La cuenta y el prefijo web van arriba del todo y sin puntuar: si escribes
            // "2+2" no hay nada que rankear, has pedido una cosa concreta.
            Entrada? especial = Proveedores.Especial(_consulta, _config.Web);
            if (especial is not null) _resultados.Add(new Resultado(especial, 0, 0, 0));

            // SEGURIDAD.md §3.7 promete que si Everything no esta, "se dice en la lista".
            // Hasta H7 no lo decia en ningun sitio que el usuario pudiera ver: el aviso
            // salia por consola, que con la ventana delante no mira nadie. Y la primera
            // version tampoco valia: se anadia al final, pero las 8 aplicaciones ya
            // llenaban el cupo y la fila quedaba recortada fuera de la ventana. Por eso
            // se le RESERVA el sitio antes de buscar.
            bool avisar = _sinEverything && _consulta.Trim().Length >= 3;

            int hueco = _config.MaxResultados - _resultados.Count - (avisar ? 1 : 0);
            if (hueco > 0)
            {
                _resultados.AddRange(
                    Coincidencia.Buscar(Candidatos(), _consulta, hueco, _uso, DateTimeOffset.UtcNow));
            }

            if (avisar)
            {
                _resultados.Add(new Resultado(
                    new Entrada("Everything no esta abierto", "sin el solo se buscan aplicaciones",
                                SoloSeMira: true),
                    0, 0, 0));
            }
        }

        _elegido = 0;
        _visuals.Consulta = _consulta;

        // Los iconos se piden con rebote; la lista se pinta ya, con los que hubiera.
        PInvoke.KillTimer(_hwnd, TemporizadorIconos);
        if (_resultados.Count > 0) PInvoke.SetTimer(_hwnd, TemporizadorIconos, ReboteIconosMs, null);

        _visuals.Pintar(_resultados, _elegido);

        if (Traza)
        {
            Console.WriteLine($"[traza] consulta \"{_consulta}\" -> {_resultados.Count} resultados" +
                              (_resultados.Count > 0 ? $", 1o {_resultados[0].Entrada.Nombre}" : ""));
        }

        // Sin mirar si es visible: al asomarse, Colocar() dimensiona ANTES de que haya
        // resultados, y con el guardia puesto la ventana se quedaba con el alto de la
        // franja hasta que escribieras la primera letra. Redimensionar una ventana
        // escondida no cuesta nada.
        PInvoke.SetWindowPos(_hwnd, default, 0, 0, _ancho, AltoActual(),
            SET_WINDOW_POS_FLAGS.SWP_NOMOVE | SET_WINDOW_POS_FLAGS.SWP_NOZORDER
            | SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE);
    }

    /// <summary>
    /// Pide los iconos que falten de las filas que se ven. Se cargan en segundo plano
    /// (SEGURIDAD.md §3.11) y cuando llega uno se repinta, no antes: pedirlos en el hilo
    /// de la ventana congelaria la lista mientras el shell los resuelve.
    /// </summary>
    private void PedirIconos()
    {
        PInvoke.KillTimer(_hwnd, TemporizadorIconos);

        foreach (Resultado r in _resultados)
        {
            if (r.Entrada.SoloSeMira) continue;
            Iconos.Pedir(r.Entrada.Destino);
        }
    }

    /// <summary>Las aplicaciones y, si ya llegaron, los ficheros de la consulta de ahora.</summary>
    private List<Entrada> Candidatos()
    {
        if (_ficheros.Count == 0) return _indice;

        List<Entrada> todos = new(_indice.Count + _ficheros.Count);
        todos.AddRange(_indice);
        todos.AddRange(_ficheros);
        return todos;
    }

    /// <summary>
    /// Programa la pregunta a Everything, o la cancela. Con menos de tres letras no se
    /// pregunta: devolveria medio disco y ninguno seria lo que buscas.
    /// </summary>
    private void PedirFicheros()
    {
        PInvoke.KillTimer(_hwnd, TemporizadorEverything);

        if (_consulta.Trim().Length < 3)
        {
            _ficheros = [];
            return;
        }

        PInvoke.SetTimer(_hwnd, TemporizadorEverything, ReboteMs, null);
    }

    private void Preguntar()
    {
        PInvoke.KillTimer(_hwnd, TemporizadorEverything);

        // Se busca el buzon en cada consulta y no una vez al arrancar: Everything puede
        // abrirse despues que nosotros, y cachear el handle dejaria los ficheros muertos
        // hasta reiniciar el lanzador.
        HWND buzon = Everything.Buzon();
        _sinEverything = buzon.IsNull;
        if (_sinEverything)
        {
            if (Traza) Console.WriteLine("[traza] Everything no esta corriendo");
            _ficheros = [];
            Refrescar();       // para que salga la fila que lo dice
            return;
        }

        _serie++;
        _preguntado = Stopwatch.GetTimestamp();
        bool aceptada = Everything.Preguntar(buzon, _hwnd, MarcaRespuesta | _serie, _consulta.Trim(), FicherosQuePedir);

        // Cuanto se queda bloqueado NUESTRO hilo dentro del SendMessage. Es el numero que
        // importa: si Everything hiciera la busqueda ahi dentro, escribir daria tirones.
        if (Traza) Console.WriteLine($"[traza] SendMessage devolvio en {Milisegundos(_preguntado):0.0} ms");

        if (!aceptada)
        {
            // Aqui es donde se veria el bloqueo por UIPI si Everything corriera elevado.
            Console.Error.WriteLine("[lanzador] Everything no acepto la consulta " +
                                    "(corre elevado y nosotros no?). Solo aplicaciones.");
            _ficheros = [];
        }
    }

    /// <summary>
    /// Deshace la respuesta de Everything. Devuelve false si el mensaje no era nuestro,
    /// para que siga su camino en vez de comerselo.
    /// </summary>
    private bool Respuesta(LPARAM lParam)
    {
        COPYDATASTRUCT* sobre = (COPYDATASTRUCT*)(nint)lParam.Value;
        if (sobre is null) return false;

        uint marca = (uint)sobre->dwData;
        if ((marca & 0xFFFF0000) != MarcaRespuesta) return false;

        double ms = Milisegundos(_preguntado);

        // Una respuesta de una consulta que ya no es la de ahora se tira sin mirarla.
        if ((ushort)(marca & 0xFFFF) != _serie)
        {
            if (Traza) Console.WriteLine($"[traza] respuesta vieja (serie {marca & 0xFFFF}, voy por {_serie})");
            return true;
        }

        _ficheros = Everything.Leer(sobre->lpData, sobre->cbData);
        if (Traza) Console.WriteLine($"[traza] Everything: {_ficheros.Count} ficheros en {ms:0.0} ms");

        Refrescar();
        return true;
    }

    /// <summary>La Y del raton dentro de la ventana, sacada del lParam.</summary>
    private static int Alto(LPARAM lParam) => (short)((lParam.Value >> 16) & 0xFFFF);

    /// <summary>
    /// Selecciona la fila que hay a esa altura. Devuelve false si el raton no esta sobre
    /// ninguna, que es lo que evita que un clic en la franja de arriba lance algo.
    /// </summary>
    private bool Sobre(int y)
    {
        int dentro = y - Escalar(AltoFranja) - Escalar(MargenLista);
        if (dentro < 0) return false;

        int fila = dentro / Escalar(AltoFila);
        if (fila < 0 || fila >= _resultados.Count) return false;

        if (fila != _elegido)
        {
            _elegido = fila;
            _visuals.Pintar(_resultados, _elegido);
        }

        return true;
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
        if (que.SoloSeMira) return;   // una cuenta: se lee, no se abre

        string consulta = Coincidencia.Normalizar(_consulta).ToLowerInvariant().Trim();

        Esconder();

        // SEGURIDAD.md §3.10: bloquear la sesion es lo mismo que Win+L. Apagar y
        // reiniciar NO estan, y no por descuido: §4 dice por que.
        if (que.Destino == Proveedores.DestinoBloquear)
        {
            _uso.Registrar(consulta, que.Destino, DateTimeOffset.UtcNow);
            _uso.Guardar();
            PInvoke.LockWorkStation();
            return;
        }

        if (!Abrir(que.Destino)) return;

        // Solo despues de abrirlo de verdad. Lo que se guarda es lo que lanzaste, no lo
        // que escribiste (regla 11).
        _uso.Registrar(consulta, que.Destino, DateTimeOffset.UtcNow);
        _uso.Guardar();
    }

    /// <summary>
    /// Se lo pasa al shell con el verbo por defecto, que es lo que hace un doble clic.
    /// Nunca <c>runas</c> (regla 17): si algo necesita administrador, lo pedira el.
    /// </summary>
    private static bool Abrir(string destino)
    {
        SHELLEXECUTEINFOW info = new()
        {
            cbSize = (uint)sizeof(SHELLEXECUTEINFOW),
            fMask = 0x00000040,   // SEE_MASK_NOCLOSEPROCESS: no esperamos ni vigilamos
            nShow = (int)SHOW_WINDOW_CMD.SW_SHOWNORMAL,
        };

        fixed (char* que = destino)
        {
            info.lpFile = new PCWSTR(que);
            if (PInvoke.ShellExecuteEx(ref info)) return true;
        }

        Console.Error.WriteLine($"[lanzador] no se pudo abrir {destino}");
        return false;
    }

    /// <summary>
    /// Ctrl+Enter: abre la carpeta que contiene el resultado, en vez del resultado.
    /// <para>
    /// Se le pasa al shell la <b>ruta de la carpeta</b>, que es una entrada del indice
    /// tanto como el fichero (SEGURIDAD.md §3.8). Nada de montar un
    /// <c>explorer /select</c>, que seria componer un comando y es la regla 10.
    /// </para>
    /// </summary>
    private void AbrirCarpeta()
    {
        if (_elegido >= _resultados.Count) return;

        Entrada que = _resultados[_elegido].Entrada;
        if (que.SoloSeMira || !que.EsFichero) return;   // solo tiene sentido para ficheros

        string? carpeta = Path.GetDirectoryName(que.Destino);
        if (string.IsNullOrEmpty(carpeta) || !Directory.Exists(carpeta)) return;

        if (Traza) Console.WriteLine($"[traza] abriendo la carpeta {carpeta}");
        Esconder();
        _ = Abrir(carpeta);
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
            // Control se sigue por sus propios mensajes, que llegan a la caja como
            // cualquier otra tecla. Ni un P/Invoke nuevo y ni rozar la regla 3.
            if (_instancia is not null && msg.hwnd == _instancia._edit
                && (uint)msg.wParam.Value == VK_CONTROL)
            {
                _instancia._control = msg.message == WM_KEYDOWN;
            }

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
            case VK_RETURN: if (_control) AbrirCarpeta(); else Lanzar(); return true;
            case VK_UP: Mover(-1); return true;
            case VK_DOWN: Mover(+1); return true;
            default: return false;
        }
    }

    /// <summary>
    /// De donde sale la memoria. Se mide el monton administrado Y el conjunto de trabajo:
    /// si crecen juntos es de los iconos guardados, y si solo crece el segundo es de las
    /// superficies y mapas de D2D, que no son objetos de .NET. Adivinarlo sin este
    /// desglose seria tocar el sitio que no es.
    /// </summary>
    private static void Memoria()
    {
        (int cuantos, long bytes) = Iconos.Cuenta();
        using Process yo = Process.GetCurrentProcess();
        Console.WriteLine($"[traza] iconos {cuantos} ({bytes / 1048576.0:0.0} MB)   " +
                          $"monton {GC.GetTotalMemory(false) / 1048576.0:0.0} MB   " +
                          $"trabajo {yo.WorkingSet64 / 1048576.0:0.0} MB   " +
                          $"hilos {yo.Threads.Count}");
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
                v._ficheros = [];     // los de la consulta anterior ya no valen
                v.Refrescar();
                v.PedirFicheros();
                return new LRESULT(0);

            // Al perder el foco se esconde, que es lo que espera cualquiera de un
            // lanzador: haces clic fuera y desaparece.
            case WM_ACTIVATE when v is not null && (wParam.Value & 0xFFFF) == 0:
                v.Esconder();
                return new LRESULT(0);

            // Llego un icono. Solo se repinta: la lista y la seleccion no cambian.
            case WM_APP_INDICE when v is not null:
                if (v._indiceReciente is List<Entrada> nuevo)
                {
                    v._indice = nuevo;
                    v._indiceReciente = null;
                    v._indiceCuando = DateTimeOffset.UtcNow;
                    v._indexando = false;
                    if (Traza) Console.WriteLine($"[traza] indice nuevo: {nuevo.Count} entradas");
                    if (v._visible) v.Refrescar();   // por si ya estabas escribiendo
                }
                return new LRESULT(0);

            // El raton: pasar por encima selecciona, soltar el boton lanza. Las filas
            // estan a alturas fijas, asi que la fila es una division.
            case WM_MOUSEMOVE when v is not null && v._visible:
                v.Sobre(Alto(lParam));
                return new LRESULT(0);

            case WM_LBUTTONUP when v is not null && v._visible:
                if (v.Sobre(Alto(lParam))) v.Lanzar();
                return new LRESULT(0);

            case WM_APP_ICONO when v is not null:
                Interlocked.Exchange(ref v._repintadoPedido, 0);
                if (v._visible) v._visuals.Pintar(v._resultados, v._elegido);
                if (Traza) Memoria();
                return new LRESULT(0);

            case WM_TIMER when v is not null && (nuint)wParam.Value == TemporizadorEverything:
                v.Preguntar();
                return new LRESULT(0);

            case WM_TIMER when v is not null && (nuint)wParam.Value == TemporizadorIconos:
                v.PedirIconos();
                return new LRESULT(0);

            // La respuesta de Everything. Es el unico WM_COPYDATA que esperamos, y solo
            // se mira si lleva nuestra marca y el numero de serie de la consulta de ahora.
            case PInvoke.WM_COPYDATA when v is not null:
                return v.Respuesta(lParam) ? new LRESULT(1) : PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);

            // El EDIT pregunta de que color pintarse justo antes de hacerlo. Se le
            // contesta con el pincel de la franja, que es el mismo color solido.
            // Los dos hermanos de la franja piden su color por mensajes distintos, y los
            // dos tienen que dar el mismo o se ve la costura.
            case WM_CTLCOLOREDIT or WM_CTLCOLORSTATIC when v is not null:
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

        // DWMWCP_ROUND, y no una region propia: MEDIDO en la maqueta, SetWindowRgn NO
        // recorta el backdrop de DWM. Con region y DWMWCP_DONOTROUND las esquinas salian
        // cuadradas; con esto salen redondeadas y suavizadas. El radio es el que da
        // Windows y no se puede subir sin renunciar al acrilico del sistema.
        uint redondas = 2;
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
        lf.lfHeight = -Escalar(22);
        return PInvoke.CreateFontIndirect(lf);
    }

    /// <summary>
    /// La caja ocupa todo el ancho y el texto se mete hacia dentro con EM_SETMARGINS,
    /// para que quede alineado con la columna de nombres de los resultados.
    /// <para>
    /// De alto <b>solo lo que mide el texto</b>, y centrada a mano: un EDIT de una linea
    /// no centra su contenido en vertical si el control es mucho mas alto, se pega
    /// arriba. Medido en la captura de H4. El color de la franja de borde a borde lo pone
    /// un visual de Composition detras, no este control.
    /// </para>
    /// </summary>
    private HWND CrearCaja()
    {
        HWND edit;
        fixed (char* clase = "EDIT")
        fixed (char* vacio = "")
        {
            edit = PInvoke.CreateWindowEx(
                0, new PCWSTR(clase), new PCWSTR(vacio),
                WINDOW_STYLE.WS_CHILD | WINDOW_STYLE.WS_VISIBLE
                    | (WINDOW_STYLE)0x0080,    // ES_AUTOHSCROLL
                0, Escalar((AltoFranja - LanzadorVisuals.AltoDelTexto) / 2),
                _ancho, Escalar(LanzadorVisuals.AltoDelTexto),
                _hwnd, (HMENU)(nint)EditId, Modulo, null);
        }

        if (edit.IsNull) throw new InvalidOperationException("no se pudo crear la caja de texto");
        PInvoke.SendMessage(edit, WM_SETFONT, (nuint)_fuente.DangerousGetHandle(), 1);
        Sangrar(edit);
        return edit;
    }

    /// <summary>
    /// El fondo solido de la franja, como ventana hermana y no como visual de
    /// Composition: lo que dibuja Composition tapa a las ventanas hijas, asi que un
    /// SpriteVisual ahi encima hacia desaparecer lo que escribes. Medido en H4.
    /// </summary>
    private HWND CrearFranja()
    {
        HWND fondo;
        fixed (char* clase = "STATIC")
        fixed (char* vacio = "")
        {
            fondo = PInvoke.CreateWindowEx(
                0, new PCWSTR(clase), new PCWSTR(vacio),
                WINDOW_STYLE.WS_CHILD | WINDOW_STYLE.WS_VISIBLE,
                0, 0, _ancho, Escalar(AltoFranja),
                _hwnd, default, Modulo, null);
        }

        if (fondo.IsNull) throw new InvalidOperationException("no se pudo crear la franja");
        return fondo;
    }

    private void Sangrar(HWND edit)
    {
        int izq = Escalar((int)LanzadorVisuals.Sangria);
        int der = Escalar(16);
        PInvoke.SendMessage(edit, EM_SETMARGINS, 3, (izq & 0xFFFF) | (der << 16));
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
