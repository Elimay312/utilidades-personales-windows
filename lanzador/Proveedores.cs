using System.Globalization;

namespace Lanzador;

/// <summary>
/// Lo que no sale del disco: los prefijos web, los sitios del sistema y las cuentas.
/// </summary>
internal static class Proveedores
{
    /// <summary>
    /// Sitios del sistema, que son entradas del indice como cualquier otra: un nombre y
    /// algo que el shell sabe abrir. Ni un P/Invoke nuevo.
    /// <para>
    /// <b>No estan apagar ni reiniciar</b>, y no es un olvido: SEGURIDAD.md §4 lo explica.
    /// Enter sobre una coincidencia difusa no es sitio para perder trabajo. Bloquear si
    /// esta (§3.10) porque no puede salir mal.
    /// </para>
    /// </summary>
    private static readonly (string Nombre, string Destino)[] DelSistema =
    {
        ("Bluetooth y dispositivos",   "ms-settings:bluetooth"),
        ("Wi-Fi",                      "ms-settings:network-wifi"),
        ("Red e internet",             "ms-settings:network"),
        ("Sonido",                     "ms-settings:sound"),
        ("Pantalla",                   "ms-settings:display"),
        ("Energia y bateria",          "ms-settings:powersleep"),
        ("Aplicaciones instaladas",    "ms-settings:appsfeatures"),
        ("Aplicaciones de inicio",     "ms-settings:startupapps"),
        ("Windows Update",             "ms-settings:windowsupdate"),
        ("Impresoras y escaneres",     "ms-settings:printers"),
        ("Almacenamiento",             "ms-settings:storagesense"),
        ("Fecha y hora",               "ms-settings:dateandtime"),
        ("Papelera de reciclaje",      "shell:RecycleBinFolder"),
        ("Descargas",                  "shell:Downloads"),
        ("Carpeta de inicio",          "shell:Startup"),
        ("Fuentes",                    "shell:Fonts"),
        ("Este equipo",                "shell:MyComputerFolder"),
    };

    /// <summary>Destino reservado: no se le pasa al shell, lo atiende la ventana.</summary>
    public const string DestinoBloquear = "lanzador:bloquear";

    public static IEnumerable<Entrada> Sistema()
    {
        foreach ((string nombre, string destino) in DelSistema) yield return new Entrada(nombre, destino);
        yield return new Entrada("Bloquear la sesion", DestinoBloquear);
    }

    /// <summary>
    /// La fila de arriba cuando la consulta no es una busqueda: una cuenta o un prefijo
    /// web. Devuelve null si la consulta es una busqueda normal.
    /// </summary>
    public static Entrada? Especial(string consulta, IReadOnlyDictionary<string, string> web)
    {
        string q = consulta.Trim();
        if (q.Length == 0) return null;

        return Cuenta(q) ?? Web(q, web);
    }

    // --- prefijos web -------------------------------------------------------------------

    /// <summary>
    /// <c>g gatos</c> -> la busqueda de Google. SEGURIDAD.md §3.5: el termino se escapa y
    /// el resultado <b>tiene que ser http o https</b>; si una plantilla produce otro
    /// esquema no se abre, porque eso convertiria el fichero de configuracion en una
    /// forma de ejecutar cualquier cosa.
    /// </summary>
    private static Entrada? Web(string consulta, IReadOnlyDictionary<string, string> plantillas)
    {
        int espacio = consulta.IndexOf(' ');
        if (espacio <= 0) return null;

        string prefijo = consulta[..espacio];
        string termino = consulta[(espacio + 1)..].Trim();
        if (termino.Length == 0) return null;
        if (!plantillas.TryGetValue(prefijo, out string? plantilla)) return null;

        string url = plantilla.Replace("{}", Uri.EscapeDataString(termino));
        if (!Uri.TryCreate(url, UriKind.Absolute, out Uri? destino)) return null;
        if (destino.Scheme != Uri.UriSchemeHttp && destino.Scheme != Uri.UriSchemeHttps) return null;

        return new Entrada($"Buscar \"{termino}\" en {destino.Host}", url);
    }

    // --- la calculadora -------------------------------------------------------------------

    /// <summary>
    /// <c>2+2*7</c> -> una fila con el resultado. Devuelve null si no es una cuenta.
    /// Solo se mira: copiar el resultado necesitaria el portapapeles, que es la regla 14
    /// (SEGURIDAD.md §4 explica cual seria la enmienda).
    /// </summary>
    private static Entrada? Cuenta(string consulta)
    {
        double? valor = Cuentas.Evaluar(consulta);
        if (valor is null) return null;

        return new Entrada(Cuentas.Escribir(valor.Value), consulta, SoloSeMira: true);
    }
}

/// <summary>
/// Un evaluador de expresiones de descenso recursivo. Sin dependencias y sin
/// <c>DataTable.Compute</c>, que ademas de lento acepta cosas que no son cuentas.
/// <para>
/// <b>No hay operador <c>%</c></b> a proposito: no se sabe si quien lo escribe quiere un
/// porcentaje o un modulo, y una calculadora que adivina mal es peor que una que no tiene
/// esa tecla. Esta en SEGURIDAD.md §4 para que no parezca un olvido.
/// </para>
/// </summary>
internal static class Cuentas
{
    /// <summary>El valor de la expresion, o null si no es una cuenta que se pueda hacer.</summary>
    public static double? Evaluar(string expresion)
    {
        if (expresion.Length == 0 || expresion.Length > 120) return null;

        // Sin al menos un operador, "2" seria una cuenta y saldria una fila con un 2
        // cada vez que escribes un numero. Y sin al menos un digito, "a+b" tambien.
        if (!expresion.Any(char.IsAsciiDigit)) return null;
        if (!expresion.Any(c => c is '+' or '-' or '*' or 'x' or '×' or '/' or ':' or '^' or '(')) return null;

        int i = 0;
        double? valor = Suma(expresion, ref i);
        if (valor is null) return null;

        Espacios(expresion, ref i);
        if (i != expresion.Length) return null;          // sobraba texto: no era una cuenta

        // Una division por cero da infinito, y un infinito en la primera fila no informa
        // de nada. Se trata como "esto no es una cuenta" y no sale la fila.
        return double.IsFinite(valor.Value) ? valor : null;
    }

    /// <summary>Con la coma decimal de aqui y sin separador de miles, que confundiria.</summary>
    public static string Escribir(double valor) =>
        valor.ToString("0.##########", CultureInfo.CurrentCulture);

    private static double? Suma(string s, ref int i)
    {
        double? izq = Producto(s, ref i);
        if (izq is null) return null;

        while (true)
        {
            Espacios(s, ref i);
            if (i >= s.Length || (s[i] != '+' && s[i] != '-')) return izq;

            char op = s[i++];
            double? der = Producto(s, ref i);
            if (der is null) return null;
            izq = op == '+' ? izq + der : izq - der;
        }
    }

    private static double? Producto(string s, ref int i)
    {
        double? izq = Potencia(s, ref i);
        if (izq is null) return null;

        while (true)
        {
            Espacios(s, ref i);
            if (i >= s.Length || s[i] is not ('*' or 'x' or '×' or '/' or ':')) return izq;

            char op = s[i++];
            double? der = Potencia(s, ref i);
            if (der is null) return null;
            izq = op is '*' or 'x' or '×' ? izq * der : izq / der;
        }
    }

    /// <summary>Asociativa por la derecha: <c>2^3^2</c> son 512, no 64.</summary>
    private static double? Potencia(string s, ref int i)
    {
        double? baseN = Unario(s, ref i);
        if (baseN is null) return null;

        Espacios(s, ref i);
        if (i >= s.Length || s[i] != '^') return baseN;

        i++;
        double? exp = Potencia(s, ref i);
        return exp is null ? null : Math.Pow(baseN.Value, exp.Value);
    }

    private static double? Unario(string s, ref int i)
    {
        Espacios(s, ref i);
        if (i >= s.Length) return null;

        if (s[i] == '-') { i++; double? v = Unario(s, ref i); return v is null ? null : -v; }
        if (s[i] == '+') { i++; return Unario(s, ref i); }

        if (s[i] == '(')
        {
            i++;
            double? dentro = Suma(s, ref i);
            Espacios(s, ref i);
            if (dentro is null || i >= s.Length || s[i] != ')') return null;
            i++;
            return dentro;
        }

        return Numero(s, ref i);
    }

    private static double? Numero(string s, ref int i)
    {
        int desde = i;
        while (i < s.Length && (char.IsAsciiDigit(s[i]) || s[i] == '.' || s[i] == ',')) i++;
        if (i == desde) return null;

        // La coma es el separador decimal de aqui, asi que se acepta y se convierte. No
        // se admite como separador de miles: "1,5" tiene que ser uno y medio, no quince.
        string texto = s[desde..i].Replace(',', '.');
        return double.TryParse(texto, NumberStyles.Float, CultureInfo.InvariantCulture, out double v) ? v : null;
    }

    private static void Espacios(string s, ref int i)
    {
        while (i < s.Length && s[i] == ' ') i++;
    }
}
