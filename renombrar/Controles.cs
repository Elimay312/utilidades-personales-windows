using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.UI.WindowsAndMessaging;

namespace Renombrar;

/// <summary>
/// La franja de arriba: tres cajas de texto y dos botones, todos controles del sistema.
///
/// <para>
/// <b>Son controles de verdad, no dibujos.</b> El caret, la seleccion, las teclas muertas,
/// Ctrl+V, el deshacer de la caja y el menu del boton derecho los hace el control. Es la
/// leccion que el lanzador ya pago, y la razon de que la franja vaya en color solido: un
/// EDIT pinta su propio fondo y encima del acrilico se veria el parche.
/// </para>
/// </summary>
internal sealed unsafe class Controles
{
    internal const int Presets = 100;
    private const int Plantilla = 101;
    private const int Buscar = 102;
    private const int Por = 103;
    internal const int Aplicar = 104;
    internal const int Deshacer = 105;

    private const uint WS_CHILD = 0x40000000;
    private const uint WS_VISIBLE = 0x10000000;
    private const uint WS_TABSTOP = 0x00010000;
    private const uint ES_AUTOHSCROLL = 0x0080;
    private const uint CBS_DROPDOWNLIST = 0x0003;
    private const uint WS_VSCROLL = 0x00200000;

    private const uint WM_SETFONT = 0x0030;
    private const uint EM_SETCUEBANNER = 0x1501;
    private const uint CB_ADDSTRING = 0x0143;
    private const uint CB_GETCURSEL = 0x0147;
    private const uint CB_SETCURSEL = 0x014E;

    /// <summary>Alto de los controles y de la franja, a 96 ppp.</summary>
    internal const float AltoControl = 30f;
    internal const float AltoFranja = 52f;

    private readonly HWND _padre;
    private readonly Dictionary<int, HWND> _hijos = [];
    private HFONT _letra;
    private float _escala;

    internal Controles(HWND padre, float escala, IReadOnlyList<Preset> presets)
    {
        _padre = padre;
        _escala = escala;
        _letra = Letra(escala);

        Lista(Presets, presets);
        Caja(Plantilla, "Nombre nuevo, p. ej. Recibo_{fecha}_{n:000}");
        Caja(Buscar, "Buscar");
        Caja(Por, "Reemplazar por");
        Boton(Aplicar, "Aplicar");
        Boton(Deshacer, "Deshacer");
    }

    /// <summary>Las reglas que salen de lo que hay escrito ahora mismo.</summary>
    internal List<Regla> Reglas()
    {
        List<Regla> reglas = [];

        // Buscar va ANTES que la plantilla: si no, la ficha {nombre} traeria el nombre sin
        // arreglar y "quita IMG_ y luego numera" no se podria escribir.
        string buscar = Texto(Buscar);
        if (buscar.Length > 0) reglas.Add(new Regla(Tipo.Reemplazar, buscar, Texto(Por)));

        string plantilla = Texto(Plantilla);
        if (plantilla.Length > 0) reglas.Add(new Regla(Tipo.Plantilla, plantilla, Desde: 1));

        return reglas;
    }

    internal void Activa(int cual, bool si) => PInvoke.EnableWindow(_hijos[cual], si);

    /// <summary>Deja un preset puesto. Es lo que hace que abrir con una plantilla desde la consola ensene lo mismo que si la hubieras tecleado.</summary>
    internal void Escribe(Preset p)
    {
        Pon(Plantilla, p.Plantilla);
        Pon(Buscar, p.Buscar);
        Pon(Por, p.Por);
    }

    /// <summary>Lo que hay escrito ahora, para guardarlo como lo ultimo que usaste.</summary>
    internal Preset Puesto() => new("", Texto(Plantilla), Texto(Buscar), Texto(Por));

    /// <summary>Cual de los presets esta elegido, o -1.</summary>
    internal int Elegido() => (int)PInvoke.SendMessage(_hijos[Presets], CB_GETCURSEL, 0, 0).Value;

    private void Pon(int cual, string texto)
    {
        fixed (char* t = texto)
        {
            PInvoke.SetWindowText(_hijos[cual], new PCWSTR(t));
        }
    }

    /// <summary>
    /// Coloca los controles. A mano y con porcentajes: son cinco, y un motor de layout para
    /// cinco controles es mas codigo que las cinco cuentas.
    /// </summary>
    internal void Coloca(float ancho, float escala)
    {
        if (escala != _escala)
        {
            _escala = escala;
            PInvoke.DeleteObject(_letra);
            _letra = Letra(escala);
            foreach (HWND h in _hijos.Values) PInvoke.SendMessage(h, WM_SETFONT, (WPARAM)(nuint)_letra.Value, 1);
        }

        int margen = (int)(12f * escala);
        int alto = (int)(AltoControl * escala);
        int y = (int)(10f * escala);
        int botones = (int)(88f * escala);
        int presets = (int)(150f * escala);

        int libre = (int)ancho - margen * 6 - botones * 2 - presets;
        int plantilla = (int)(libre * 0.48f);
        int caja = (libre - plantilla) / 2;

        int x = margen;

        // A un combo, el alto que se le da es el de la lista desplegada; la caja cerrada se
        // queda con el alto de su fila. Por eso aqui va un numero grande y no "alto".
        Mueve(Presets, x, y, presets, alto + (int)(200f * escala)); x += presets + margen;

        Mueve(Plantilla, x, y, plantilla, alto); x += plantilla + margen;
        Mueve(Buscar, x, y, caja, alto); x += caja + margen;
        Mueve(Por, x, y, caja, alto); x += caja + margen;
        Mueve(Aplicar, x, y, botones, alto); x += botones + margen;
        Mueve(Deshacer, x, y, botones, alto);
    }

    private void Mueve(int cual, int x, int y, int ancho, int alto) =>
        PInvoke.SetWindowPos(_hijos[cual], default, x, y, ancho, alto,
                             SET_WINDOW_POS_FLAGS.SWP_NOZORDER | SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE);

    private string Texto(int cual)
    {
        HWND h = _hijos[cual];
        int largo = PInvoke.GetWindowTextLength(h);
        if (largo == 0) return "";

        Span<char> buzon = largo < 512 ? stackalloc char[largo + 1] : new char[largo + 1];
        fixed (char* p = buzon)
        {
            int leidos = PInvoke.GetWindowText(h, new PWSTR(p), largo + 1);
            return new string(p, 0, leidos);
        }
    }

    private void Caja(int id, string pista)
    {
        HWND h = Crea("EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, id);

        // La pista va DENTRO de la caja, en gris, y desaparece al escribir. Es una etiqueta
        // que no ocupa sitio ni necesita otro control que colocar.
        fixed (char* texto = pista)
        {
            PInvoke.SendMessage(h, EM_SETCUEBANNER, 1, (LPARAM)(nint)texto);
        }

        // ponytail: las cajas salen BLANCAS y este tema no basta. Para que un EDIT se pinte
        // oscuro hace falta ademas que el proceso se declare en modo oscuro, y eso solo
        // existe como funcion sin documentar de uxtheme importada por ordinal (135). En un
        // proyecto cuya gracia es que NativeMethods.txt sea una lista auditable, eso es
        // justo lo que no se mete. Techo: blanco sobre franja oscura, y se lee bien. Si
        // algun dia Microsoft lo documenta, esta es la linea que cambia.
        fixed (char* tema = "DarkMode_CFD")
        {
            PInvoke.SetWindowTheme(h, new PCWSTR(tema), null);
        }
    }

    /// <summary>El desplegable de presets. Si no hay ninguno en el JSON, se queda vacio y no estorba.</summary>
    private void Lista(int id, IReadOnlyList<Preset> presets)
    {
        HWND h = Crea("COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, id);

        foreach (Preset p in presets)
        {
            fixed (char* nombre = p.Nombre)
            {
                PInvoke.SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(nint)nombre);
            }
        }

        if (presets.Count > 0) PInvoke.SendMessage(h, CB_SETCURSEL, unchecked((nuint)(-1)), 0);

        fixed (char* tema = "DarkMode_CFD")
        {
            PInvoke.SetWindowTheme(h, new PCWSTR(tema), null);
        }
    }

    private void Boton(int id, string texto)
    {
        HWND h = Crea("BUTTON", texto, WS_CHILD | WS_VISIBLE | WS_TABSTOP, id);

        fixed (char* tema = "DarkMode_Explorer")
        {
            PInvoke.SetWindowTheme(h, new PCWSTR(tema), null);
        }
    }

    private HWND Crea(string clase, string texto, uint estilo, int id)
    {
        HWND h;
        fixed (char* c = clase)
        fixed (char* t = texto)
        {
            h = PInvoke.CreateWindowEx(default, new PCWSTR(c), new PCWSTR(t), (WINDOW_STYLE)estilo,
                                       0, 0, 10, 10, _padre, (HMENU)(nint)id,
                                       (HINSTANCE)(nint)PInvoke.GetModuleHandle((PCWSTR)null), null);
        }

        PInvoke.SendMessage(h, WM_SETFONT, (WPARAM)(nuint)_letra.Value, 1);
        _hijos[id] = h;
        return h;
    }

    /// <summary>
    /// La letra de la interfaz de Windows, a la escala de ESTA pantalla. Sin la version
    /// "ForDpi", las cajas salen con la letra del monitor principal y en una pantalla al
    /// 150% el texto no le cabe al control.
    /// </summary>
    private static HFONT Letra(float escala)
    {
        NONCLIENTMETRICSW metricas = new() { cbSize = (uint)sizeof(NONCLIENTMETRICSW) };
        PInvoke.SystemParametersInfoForDpi((uint)SYSTEM_PARAMETERS_INFO_ACTION.SPI_GETNONCLIENTMETRICS, metricas.cbSize,
                                           &metricas, 0, (uint)(96f * escala));
        return PInvoke.CreateFontIndirect(&metricas.lfMessageFont);
    }
}
