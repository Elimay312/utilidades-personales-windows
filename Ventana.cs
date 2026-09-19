using System.Runtime.InteropServices;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.System.Com;
using Windows.Win32.System.Ole;
using Windows.Win32.System.SystemServices;
using Windows.Win32.UI.Shell;
using Windows.Win32.UI.WindowsAndMessaging;

namespace Renombrar;

/// <summary>
/// La ventana. Una ventana normal y corriente: esta SI quiere el foco, al reves que el
/// dock, porque se escribe en ella. Por eso no necesita <c>SetForegroundWindow</c> —
/// Windows se lo da al abrirla— y por eso la regla 14 de SEGURIDAD.md no tiene excepciones.
/// </summary>
internal sealed unsafe class Ventana : IDisposable
{
    private const string Clase = "RenombrarVentana";

    private const uint WM_DESTROY = 0x0002;
    private const uint WM_SIZE = 0x0005;
    private const uint WM_MOUSEWHEEL = 0x020A;
    private const uint WM_DPICHANGED = 0x02E0;

    // El delegado se guarda en un campo estatico a proposito: pasandolo directamente a
    // WNDCLASSEXW, el GC podria recogerlo mientras Windows todavia tiene el puntero, y el
    // fallo aparece mucho despues y en otro sitio.
    private static readonly WNDPROC Thunk = Procedimiento;
    private static ushort _clase;

    // Una sola ventana, asi que el WndProc estatico la encuentra por aqui en vez de montar
    // un diccionario para una sola entrada.
    private static Ventana? _unica;

    private HWND _hwnd;
    private Visuales? _visuales;
    private Soltar? _soltar;

    private string _carpeta = "";
    private List<Fichero> _ficheros = [];
    private List<Regla> _reglas = [];

    internal Ventana(string carpeta, string plantilla)
    {
        AseguraClase();

        fixed (char* clase = Clase)
        fixed (char* titulo = "Renombrar")
        {
            _hwnd = PInvoke.CreateWindowEx(
                default,
                new PCWSTR(clase),
                new PCWSTR(titulo),
                WINDOW_STYLE.WS_OVERLAPPEDWINDOW,
                int.MinValue, int.MinValue, 980, 640,
                default, default, Modulo, null);
        }

        if (_hwnd.IsNull) throw new InvalidOperationException("no se pudo crear la ventana");
        _unica = this;

        _visuales = new Visuales(_hwnd, PInvoke.GetDpiForWindow(_hwnd) / 96f);

        _soltar = new Soltar(this);
        HRESULT hr = PInvoke.RegisterDragDrop(_hwnd, _soltar);
        if (hr.Failed) Console.WriteLine($"[soltar] RegisterDragDrop -> 0x{(uint)hr.Value:X8}");

        if (plantilla.Length > 0) _reglas = [new Regla(Tipo.Plantilla, plantilla, Desde: 1)];
        if (carpeta.Length > 0) Carga(carpeta);

        PInvoke.ShowWindow(_hwnd, SHOW_WINDOW_CMD.SW_SHOW);
        Mide();
    }

    internal HWND Handle => _hwnd;

    private static HINSTANCE Modulo => (HINSTANCE)(nint)PInvoke.GetModuleHandle((PCWSTR)null);

    internal void Bucle()
    {
        MSG msg;
        while (PInvoke.GetMessage(&msg, default, 0, 0).Value > 0)
        {
            PInvoke.TranslateMessage(&msg);
            PInvoke.DispatchMessage(&msg);
        }
    }

    /// <summary>Lo que se ha soltado. Llega desde <see cref="Soltar"/>, ya resuelto a rutas.</summary>
    internal void Recibe(string[] rutas)
    {
        if (rutas.Length == 0) return;

        string carpeta = Directory.Exists(rutas[0]) ? rutas[0] : Path.GetDirectoryName(rutas[0]) ?? "";
        if (carpeta.Length == 0) return;

        string? vetada = Carpeta.Vetada(carpeta);
        if (vetada is not null)
        {
            Console.WriteLine($"[renombrar] esa carpeta no se toca: {vetada}");
            _visuales?.Cabecera($"Esa carpeta no se toca: {vetada}");
            return;
        }

        // Soltar la carpeta entera trae todos sus ficheros; soltar unos cuantos trae solo
        // esos. Es la diferencia entre "renombra esta carpeta" y "renombra estos", y el
        // usuario ya la expreso al arrastrar.
        Carga(carpeta, Directory.Exists(rutas[0]) ? null : rutas);
    }

    private void Carga(string carpeta, string[]? sueltos = null)
    {
        _carpeta = carpeta;
        _ficheros = sueltos is null ? Carpeta.Reunir(carpeta) : Carpeta.Reunir(carpeta, sueltos);
        Recalcula();
    }

    private void Recalcula()
    {
        if (_visuales is null) return;

        if (_ficheros.Count == 0)
        {
            _visuales.Ensena([]);
            _visuales.Cabecera("Suelta aquí archivos o una carpeta");
            return;
        }

        List<Fila> filas = Previa.Calcular(_ficheros, _reglas, Carpeta.Ocupados(_carpeta));
        _visuales.Ensena(filas);

        int cambian = filas.Count(f => f.Estado == Estado.Ok);
        int malas = filas.Count(f => f.Estado is not (Estado.Ok or Estado.SinCambio));
        string cuenta = $"{Path.GetFileName(_carpeta)} — {filas.Count} archivos, {cambian} cambian";
        _visuales.Cabecera(malas == 0 ? cuenta : $"{cuenta}, {malas} no pueden");
    }

    private void Mide()
    {
        if (_visuales is null) return;

        RECT r;
        PInvoke.GetClientRect(_hwnd, &r);
        _visuales.Redimensiona(r.right - r.left, r.bottom - r.top, PInvoke.GetDpiForWindow(_hwnd) / 96f);
    }

    private static LRESULT Procedimiento(HWND hwnd, uint msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
            case WM_SIZE:
                _unica?.Mide();
                return new LRESULT(0);

            // Un clic de rueda son 120 unidades, y puede llegar fraccionado desde un
            // trackpad de precision: se pasa tal cual y que la lista lo reparta.
            case WM_MOUSEWHEEL:
                _unica?._visuales?.Rueda((short)(wParam.Value >> 16) / 120f);
                return new LRESULT(0);

            // Arrastrar la ventana a otra pantalla. El rectangulo sugerido viene en
            // lParam y hay que obedecerlo: es el mismo tamano fisico en la escala nueva.
            case WM_DPICHANGED:
            {
                RECT* nuevo = (RECT*)lParam.Value;
                PInvoke.SetWindowPos(hwnd, default, nuevo->left, nuevo->top,
                                     nuevo->right - nuevo->left, nuevo->bottom - nuevo->top,
                                     SET_WINDOW_POS_FLAGS.SWP_NOZORDER | SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE);
                _unica?.Mide();
                return new LRESULT(0);
            }

            case WM_DESTROY:
                PInvoke.PostQuitMessage(0);
                return new LRESULT(0);
        }

        return PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);
    }

    private static void AseguraClase()
    {
        if (_clase != 0) return;

        fixed (char* nombre = Clase)
        {
            WNDCLASSEXW wc = new()
            {
                cbSize = (uint)Marshal.SizeOf<WNDCLASSEXW>(),
                lpfnWndProc = Thunk,
                hInstance = Modulo,
                lpszClassName = new PCWSTR(nombre),
                hCursor = PInvoke.LoadCursor(default, PInvoke.IDC_ARROW),
            };

            _clase = PInvoke.RegisterClassEx(in wc);
        }
    }

    public void Dispose()
    {
        if (!_hwnd.IsNull && _soltar is not null) PInvoke.RevokeDragDrop(_hwnd);
        _soltar = null;

        _visuales?.Dispose();
        _visuales = null;

        if (!_hwnd.IsNull)
        {
            PInvoke.DestroyWindow(_hwnd);
            _hwnd = default;
        }

        _unica = null;
    }
}

/// <summary>
/// Recibe lo que se suelta encima. Es la unica entrada del programa (SEGURIDAD.md §3.1) y
/// es puramente receptivo: solo ve lo que el usuario suelta, con su gesto.
///
/// <para>
/// El hilo que registra esto tiene que estar bombeando mensajes. Si se bloquea, la app que
/// este arrastrando por encima <b>se cuelga hasta que nosotros cerremos</b>: es el fallo
/// que la documentacion de RegisterDragDrop describe con esas palabras. Por eso aqui solo
/// se extraen las rutas.
/// </para>
/// </summary>
internal sealed unsafe class Soltar(Ventana ventana) : IDropTarget
{
    public void DragEnter(IDataObject datos, MODIFIERKEYS_FLAGS teclas, POINTL donde, DROPEFFECT* efecto)
        => *efecto = DROPEFFECT.DROPEFFECT_LINK;

    public void DragOver(MODIFIERKEYS_FLAGS teclas, POINTL donde, DROPEFFECT* efecto)
        => *efecto = DROPEFFECT.DROPEFFECT_LINK;

    public void DragLeave() { }

    public void Drop(IDataObject datos, MODIFIERKEYS_FLAGS teclas, POINTL donde, DROPEFFECT* efecto)
    {
        // LINK y no COPY: el cursor tiene que decir que aqui no se copia nada. Lo unico
        // que se va a hacer con esos ficheros es mirarles el nombre.
        *efecto = DROPEFFECT.DROPEFFECT_LINK;

        // AQUI y no despues: la documentacion dice que los data objects pasados a
        // IDropTarget dejan de ser validos en cuanto termina la suelta.
        ventana.Recibe(Rutas(datos));
    }

    /// <summary>
    /// Las rutas de disco de lo soltado. Se usa <c>SHCreateShellItemArrayFromDataObject</c>
    /// en vez de leer los formatos a mano porque entiende de una vez <c>CF_HDROP</c> y
    /// <c>CFSTR_SHELLIDLIST</c>, que es lo que Microsoft recomienda.
    ///
    /// <para>
    /// Un <c>.lnk</c> se queda como lo que es, un fichero con su nombre. El dock resuelve
    /// los accesos directos a su destino porque lo que quiere es la app; aqui lo que se
    /// renombra es justo el fichero que has soltado.
    /// </para>
    /// </summary>
    private static string[] Rutas(IDataObject datos)
    {
        Guid iid = typeof(IShellItemArray).GUID;
        if (PInvoke.SHCreateShellItemArrayFromDataObject(
                (System.Runtime.InteropServices.ComTypes.IDataObject)(object)datos, &iid, out object obj).Failed)
        {
            return [];
        }

        var lista = (IShellItemArray)obj;
        lista.GetCount(out uint cuantos);

        List<string> rutas = [];
        for (uint i = 0; i < cuantos; i++)
        {
            lista.GetItemAt(i, out IShellItem item);
            try
            {
                item.GetDisplayName(SIGDN.SIGDN_FILESYSPATH, out PWSTR nombre);
                string ruta = nombre.ToString();
                Marshal.FreeCoTaskMem((nint)nombre.Value);
                if (ruta.Length > 0) rutas.Add(ruta);
            }
            catch (COMException)
            {
                // Sin ruta de disco: algo virtual del shell. Aqui no hay nada que renombrar.
            }
        }

        return [.. rutas];
    }
}
