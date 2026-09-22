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

    private const string ClassName = "LanzadorVentana";
    private const int HotkeyId = 1;

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
    private const uint WM_KEYDOWN = 0x0100;
    private const uint WM_CHAR = 0x0102;
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
    private const uint WM_LBUTTONDOWN = 0x0201;

    private const uint VK_ESCAPE = 0x1B;
    private const uint VK_RETURN = 0x0D;
    private const uint VK_UP = 0x26;
    private const uint VK_DOWN = 0x28;
    private const uint VK_CONTROL = 0x11;
    private const uint VK_SHIFT = 0x10;
    private const uint VK_LEFT = 0x25;
    private const uint VK_RIGHT = 0x27;
    private const uint VK_HOME = 0x24;
    private const uint VK_END = 0x23;
    private const uint VK_BACK = 0x08;
    private const uint VK_DELETE = 0x2E;
    private const uint VK_A = 0x41;
    private const uint VK_V = 0x56;

    /// <summary>El unico formato de portapapeles que se pide (SEGURIDAD.md 3.12).</summary>
    private const uint CF_UNICODETEXT = 13;

    private const nuint TemporizadorCaret = 3;

    /// <summary>Lo que tarda el cursor en parpadear. 530 ms es el valor de Windows.</summary>
    private const uint ParpadeoMs = 530;

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
    /// <summary>
    /// Lo que escribes. Sin nada de Win32 dentro a proposito, para poder comprobarlo
    /// desde --check sin abrir una ventana.
    /// </summary>
    private readonly Caja _caja = new();
    private bool _mayus;
    private bool _caretEncendido = true;
    private bool _arrastrandoTexto;

    /// <summary>
    /// Donde estaba el puntero la ultima vez que se le hizo caso, en coordenadas de
    /// PANTALLA. Ver <see cref="RatonSeMovio"/>: es lo que impide que el raton quieto se
    /// quede con la seleccion.
    /// </summary>
    private System.Drawing.Point _ultimoRaton;
    private readonly LanzadorVisuals _visuals;
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
                //
                // NOREDIRECTIONBITMAP: sin superficie de redireccion, lo que Composition no
                // pinta es transparente. Es lo que deja ver solo la pildora y el panel.
                WINDOW_EX_STYLE.WS_EX_TOOLWINDOW | WINDOW_EX_STYLE.WS_EX_TOPMOST
                | WINDOW_EX_STYLE.WS_EX_NOREDIRECTIONBITMAP,
                new PCWSTR(clase), new PCWSTR(titulo),
                WINDOW_STYLE.WS_POPUP,
                0, 0, _ancho, Escalar(AltoFranja),
                default, default, Modulo, null);
        }

        if (_hwnd.IsNull) throw new InvalidOperationException("CreateWindowEx fallo");

        QuitarMarco();
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
        _caja.Vaciar();
        _caretEncendido = true;

        // Se apunta donde esta el puntero ANTES de ensenar nada: asi el WM_MOUSEMOVE que
        // Windows manda por aparecer la ventana debajo de el no cuenta como movimiento.
        PInvoke.GetCursorPos(out _ultimoRaton);
        Refrescar();

        PInvoke.ShowWindow(_hwnd, SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE);

        // SEGURIDAD.md Â§3.4: la unica llamada del programa, y sobre el handle propio.
        // Windows autoriza a ponerse delante al proceso que acaba de recibir WM_HOTKEY;
        // sin esto la ventana sale sin foco y no recibe lo que escribes.
        // Sin SetFocus a ninguna hija, porque ya no hay: al estar en primer plano las
        // teclas llegan directas a nuestro WndProc.
        PInvoke.SetForegroundWindow(_hwnd);
        PInvoke.SetTimer(_hwnd, TemporizadorCaret, ParpadeoMs, null);

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
        _caja.Vaciar();
        _consulta = string.Empty;
        _resultados = [];
        _ficheros = [];
        _elegido = 0;
        _control = false;
        _mayus = false;
        _arrastrandoTexto = false;
        PInvoke.KillTimer(_hwnd, TemporizadorEverything);
        PInvoke.KillTimer(_hwnd, TemporizadorIconos);
        PInvoke.KillTimer(_hwnd, TemporizadorCaret);
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
        _visuals.Caja = _caja;
        _visuals.CaretEncendido = _caretEncendido;

        // El icono de la PRIMERA fila se pide ya, sin esperar al rebote: es la que vas a
        // abrir con Enter, y verla completa al instante es casi toda la sensacion de
        // rapidez. Son como mucho una extraccion por tecla, y casi siempre ya esta hecha.
        if (_resultados.Count > 0 && !_resultados[0].Entrada.SoloSeMira)
        {
            Iconos.Pedir(_resultados[0].Entrada.Destino);
        }

        // Las demas, con rebote: si no, escribir "micro" pediria los iconos de cinco
        // listas distintas para ensenar solo la ultima.
        PInvoke.KillTimer(_hwnd, TemporizadorIconos);
        if (_resultados.Count > 0) PInvoke.SetTimer(_hwnd, TemporizadorIconos, ReboteIconosMs, null);

        _visuals.Pintar(_resultados, _elegido);

        if (Traza)
        {
            Console.WriteLine($"[traza] consulta \"{_consulta}\" -> {_resultados.Count} resultados" +
                              (_resultados.Count > 0 ? $", elegida la {_elegido}: {_resultados[_elegido].Entrada.Nombre}" : ""));
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

    /// <summary>La X del raton dentro de la ventana.</summary>
    private static int Ancho(LPARAM lParam) => (short)(lParam.Value & 0xFFFF);

    /// <summary>Cuanto se tiene que mover el puntero para que cuente, en pixeles.</summary>
    private const int TemblorDelRaton = 5;

    /// <summary>
    /// Si el puntero se ha movido de verdad desde la ultima vez que se le hizo caso.
    /// <para>
    /// Hace falta porque <b>el teclado manda</b>. Windows manda WM_MOUSEMOVE cuando una
    /// ventana aparece o cambia de tamano debajo del puntero, aunque nadie lo haya
    /// tocado: al asomarse con el raton encima de la lista, la fila elegida dejaba de ser
    /// la primera, y al crecer la ventana con cada letra iba saltando sola. Escribiendo
    /// tres letras y pulsando Enter, abrias lo que no era.
    /// </para>
    /// <para>
    /// Se compara en coordenadas de <b>pantalla</b> y no de ventana: al redimensionarse,
    /// las de ventana cambian aunque el puntero siga clavado en el mismo sitio.
    /// </para>
    /// </summary>
    private bool RatonSeMovio()
    {
        PInvoke.GetCursorPos(out System.Drawing.Point ahora);

        int dx = Math.Abs(ahora.X - _ultimoRaton.X);
        int dy = Math.Abs(ahora.Y - _ultimoRaton.Y);
        if (dx + dy < TemblorDelRaton) return false;

        _ultimoRaton = ahora;
        return true;
    }

    /// <summary>
    /// Una letra escrita. Se apunta el texto de antes para saber si hay que volver a
    /// buscar: mover el cursor o seleccionar no cambia los resultados y no debe disparar
    /// una consulta a Everything.
    /// </summary>
    private void Teclear(char c)
    {
        _caja.Escribir(c);
        TrasEscribir(cambio: true);
    }

    /// <summary>Control y Mayusculas, seguidos por sus propios mensajes (regla 3).</summary>
    private void Modificador(uint tecla, bool pulsada)
    {
        if (tecla == VK_CONTROL) _control = pulsada;
        else if (tecla == VK_SHIFT) _mayus = pulsada;
    }

    private void Tecla(uint tecla)
    {
        string antes = _caja.Texto;

        switch (tecla)
        {
            case VK_ESCAPE: Esconder(); return;
            case VK_RETURN: if (_control) AbrirCarpeta(); else Lanzar(); return;
            case VK_UP: Mover(-1); return;
            case VK_DOWN: Mover(+1); return;

            case VK_LEFT: _caja.Mover(-1, _mayus, _control); break;
            case VK_RIGHT: _caja.Mover(+1, _mayus, _control); break;
            case VK_HOME: _caja.AlBorde(-1, _mayus); break;
            case VK_END: _caja.AlBorde(+1, _mayus); break;
            case VK_BACK: _caja.Borrar(haciaAtras: true, _control); break;
            case VK_DELETE: _caja.Borrar(haciaAtras: false, _control); break;

            case VK_A when _control: _caja.Todo(); break;
            case VK_V when _control: Pegar(); break;

            default: return;
        }

        TrasEscribir(_caja.Texto != antes);
    }

    /// <summary>
    /// Lo que hay que hacer despues de tocar la caja. El cursor vuelve a encenderse y el
    /// parpadeo se reinicia: un cursor que se apaga justo mientras escribes parece que se
    /// ha colgado algo.
    /// </summary>
    private void TrasEscribir(bool cambio)
    {
        _caretEncendido = true;
        PInvoke.SetTimer(_hwnd, TemporizadorCaret, ParpadeoMs, null);

        if (!cambio) { Repintar(); return; }

        _consulta = _caja.Texto;
        _ficheros = [];              // los de la consulta anterior ya no valen
        Refrescar();
        PedirFicheros();
    }

    /// <summary>Repinta sin volver a buscar: el cursor y la seleccion no cambian nada.</summary>
    private void Repintar() => _visuals.Pintar(_resultados, _elegido);

    /// <summary>
    /// SEGURIDAD.md §3.12, y es la unica lectura del portapapeles de todo el programa.
    /// Solo se llega aqui desde Ctrl+V, solo se pide CF_UNICODETEXT, y lo que entra va a
    /// la consulta, que no se guarda.
    /// </summary>
    private void Pegar()
    {
        if (!PInvoke.OpenClipboard(_hwnd)) return;

        try
        {
            HANDLE dato = PInvoke.GetClipboardData(CF_UNICODETEXT);
            if (dato.IsNull) return;

            void* p = PInvoke.GlobalLock((HGLOBAL)(nint)dato.Value);
            if (p is null) return;

            try { _caja.Pegar(new string((char*)p)); }
            finally { PInvoke.GlobalUnlock((HGLOBAL)(nint)dato.Value); }
        }
        finally
        {
            PInvoke.CloseClipboard();
        }
    }

    /// <summary>
    /// Un clic. Dentro de la pildora pone el cursor y empieza a arrastrar; por debajo, es
    /// cosa de las filas y lo atiende el soltar el boton.
    /// </summary>
    private void Pinchar(int x, int y)
    {
        if (y > Escalar(AltoFranja)) return;

        _caja.Poner(_visuals.IndiceEn(x), arrastrando: false);
        _arrastrandoTexto = true;
        PInvoke.SetCapture(_hwnd);
        TrasEscribir(cambio: false);
    }

    private void Arrastrar(int x)
    {
        _caja.Poner(_visuals.IndiceEn(x), arrastrando: true);
        Repintar();
    }

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
            if (Traza) Console.WriteLine($"[traza] el raton elige la {fila}");
            _visuals.Pintar(_resultados, _elegido);
        }

        return true;
    }

    private void Mover(int cuanto)
    {
        if (_resultados.Count == 0) return;
        _elegido = Math.Clamp(_elegido + cuanto, 0, _resultados.Count - 1);
        if (Traza) Console.WriteLine($"[traza] las flechas eligen la {_elegido}");
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
    /// El bucle, sin interceptar nada: ya no hay ventana hija a la que le lleguen las
    /// teclas antes que a nosotros, asi que todo pasa por el WndProc.
    /// <para>
    /// <b>TranslateMessage hace falta</b> y no es ceremonia: es lo que convierte las
    /// pulsaciones en WM_CHAR, y con ello lo que hace que las teclas muertas compongan
    /// la tilde de "configuracion" sin que nosotros sepamos nada del teclado.
    /// </para>
    /// </summary>
    public static void Bucle()
    {
        MSG msg;
        while (PInvoke.GetMessage(out msg, default, 0, 0))
        {
            PInvoke.TranslateMessage(&msg);
            PInvoke.DispatchMessage(&msg);
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

            // Al perder el foco se esconde, que es lo que espera cualquiera de un
            // lanzador: haces clic fuera y desaparece.
            case WM_ACTIVATE when v is not null && (wParam.Value & 0xFFFF) == 0:
                v.Esconder();
                return new LRESULT(0);

            // Llego un icono. Solo se repinta: la lista y la seleccion no cambian.
            // Una letra. TranslateMessage ya compuso las teclas muertas, asi que aqui
            // llega la a con tilde hecha y no hay que saber nada del teclado.
            case WM_CHAR when v is not null && v._visible:
                if (wParam.Value >= 32) v.Teclear((char)wParam.Value);
                return new LRESULT(0);

            case WM_KEYDOWN when v is not null:
                v.Modificador((uint)wParam.Value, true);
                if (v._visible) v.Tecla((uint)wParam.Value);
                return new LRESULT(0);

            case WM_KEYUP when v is not null:
                v.Modificador((uint)wParam.Value, false);
                return new LRESULT(0);

            case WM_LBUTTONDOWN when v is not null && v._visible:
                v.Pinchar(Ancho(lParam), Alto(lParam));
                return new LRESULT(0);

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
                if (v._arrastrandoTexto) v.Arrastrar(Ancho(lParam));
                else if (v.RatonSeMovio()) v.Sobre(Alto(lParam));
                return new LRESULT(0);

            case WM_LBUTTONUP when v is not null && v._visible:
                if (v._arrastrandoTexto) { v._arrastrandoTexto = false; PInvoke.ReleaseCapture(); }
                else if (v.Sobre(Alto(lParam))) v.Lanzar();
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

            case WM_TIMER when v is not null && (nuint)wParam.Value == TemporizadorCaret:
                v._caretEncendido = !v._caretEncendido;
                if (v._visible) v.Repintar();
                return new LRESULT(0);

            // La respuesta de Everything. Es el unico WM_COPYDATA que esperamos, y solo
            // se mira si lleva nuestra marca y el numero de serie de la consulta de ahora.
            case PInvoke.WM_COPYDATA when v is not null:
                return v.Respuesta(lParam) ? new LRESULT(1) : PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);

            // El EDIT pregunta de que color pintarse justo antes de hacerlo. Se le
            // contesta con el pincel de la franja, que es el mismo color solido.
            // Los dos hermanos de la franja piden su color por mensajes distintos, y los
            // dos tienen que dar el mismo o se ve la costura.
            case WM_DESTROY:
                PInvoke.PostQuitMessage(0);
                return new LRESULT(0);
        }

        return PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);
    }

    // --- montaje ----------------------------------------------------------------------

    /// <summary>
    /// La ventana en si no se ve: solo la pildora y el panel de resultados, que los
    /// dibujamos nosotros. Aqui se le quita a DWM todo lo que pintaria alrededor.
    /// <para>
    /// <b>Sin el acrilico del sistema</b>, y no por gusto. Con DWMSBT_TRANSIENTWINDOW la
    /// ventana entera era un rectangulo oscuro con la pildora dentro, y ese rectangulo no
    /// se puede hacer pildora: la maqueta midio que SetWindowRgn no recorta el backdrop de
    /// DWM, y CreateHostBackdropBrush pinta negro en una app Win32 sin empaquetar (lo midio
    /// la isla). Asi que el fondo lo pone la pildora, casi opaco y sin desenfoque.
    /// </para>
    /// </summary>
    private void QuitarMarco()
    {
        // DONOTROUND: con ROUND, DWM dibuja borde y sombra alrededor del rectangulo de la
        // ventana, que ahora es invisible y dejaria un marco flotando.
        uint cuadradas = 1;
        PInvoke.DwmSetWindowAttribute(_hwnd, DWMWINDOWATTRIBUTE.DWMWA_WINDOW_CORNER_PREFERENCE,
            &cuadradas, sizeof(uint));
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
        if (!_hwnd.IsNull) PInvoke.DestroyWindow(_hwnd);
    }
}
