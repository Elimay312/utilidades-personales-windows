using System.Text;
using Windows.Win32;

namespace Renombrar;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        // Compilado como WinExe no hay consola propia. Si nos han lanzado desde una
        // terminal nos enganchamos a la suya; si no, no hay donde escribir y da igual.
        // El guardia evita romper una tuberia cuando la salida esta redirigida.
        if (!Console.IsOutputRedirected) PInvoke.AttachConsole(0xFFFFFFFF);
        Console.OutputEncoding = Encoding.UTF8;

        string modo = args.Length > 0 ? args[0] : string.Empty;
        string ruta = args.Length > 1 ? args[1] : string.Empty;

        return modo switch
        {
            "--check"    => Comprobaciones.Ejecutar(),
            "--previa"   => Previsualizar(ruta, args.Length > 2 ? args[2] : string.Empty),
            "--aplicar"  => Aplicar(ruta),
            "--deshacer" => Deshacer(),
            "--ayuda" or "-h" or "/?" => Ayuda(),
            "" => Ventana(),
            _ => Desconocido(modo),
        };
    }

    private static int Ayuda()
    {
        Console.WriteLine("""
            renombrar [modo]

              --previa CARPETA [PLANTILLA]   la tabla antes -> despues, sin tocar nada
              --aplicar CARPETA              renombra, despues de que confirmes
              --deshacer                     revierte el ultimo lote
              --check                        comprueba el motor y la vista previa
              sin modo                       abre la ventana

            La plantilla admite fichas: {nombre}, {n}, {n:000}, {fecha}, {fecha:yyyy-MM}.

              renombrar --previa "C:\recibos" "Recibo_{fecha}_{n:000}"

            --previa devuelve 1 si alguna fila no se podria renombrar.
            """);
        return 0;
    }

    private static int Desconocido(string modo)
    {
        Console.Error.WriteLine($"[renombrar] no se que es \"{modo}\".");
        Ayuda();
        return 2;
    }

    /// <summary>
    /// La misma tabla que ensena la ventana, en texto. No es una version reducida para
    /// depurar: es la misma llamada a <see cref="Previa.Calcular"/> con los mismos
    /// estados, que es lo que hace que valga como sonda.
    /// </summary>
    private static int Previsualizar(string carpeta, string plantilla)
    {
        if (carpeta.Length == 0)
        {
            Console.Error.WriteLine("[renombrar] --previa necesita una carpeta.");
            return 2;
        }

        if (!Directory.Exists(carpeta))
        {
            Console.Error.WriteLine($"[renombrar] no existe la carpeta {carpeta}");
            return 2;
        }

        string? vetada = Carpeta.Vetada(carpeta);
        if (vetada is not null)
        {
            Console.Error.WriteLine($"[renombrar] esa carpeta no se toca: {vetada}. (SEGURIDAD.md regla 7)");
            return 2;
        }

        List<Fichero> ficheros = Carpeta.Reunir(carpeta);
        if (ficheros.Count == 0)
        {
            Console.WriteLine("[renombrar] la carpeta no tiene ficheros visibles.");
            return 0;
        }

        // Sin plantilla no hay reglas, y la tabla sale entera en SinCambio. Sirve para ver
        // que ficheros entran en el lote y en que orden, que es la mitad de las sorpresas.
        List<Regla> reglas = plantilla.Length > 0
            ? [new Regla(Tipo.Plantilla, plantilla, Desde: 1)]
            : [];

        List<Fila> filas = Previa.Calcular(ficheros, reglas, Carpeta.Ocupados(carpeta));

        int ancho = Math.Min(filas.Max(f => f.Antes.Length), 50);
        foreach (Fila f in filas)
        {
            Console.WriteLine($"  {Marca(f.Estado)} {Recorta(f.Antes, ancho).PadRight(ancho)} -> {f.Despues}");
            if (f.Motivo.Length > 0) Console.WriteLine($"      {f.Motivo}");
        }

        Console.WriteLine();
        foreach (IGrouping<Estado, Fila> g in filas.GroupBy(f => f.Estado).OrderBy(g => g.Key))
            Console.WriteLine($"  {g.Count(),5}  {g.Key}");

        return filas.Any(f => f.Estado is not (Estado.Ok or Estado.SinCambio)) ? 1 : 0;
    }

    private static string Marca(Estado e) => e switch
    {
        Estado.Ok => "ok",
        Estado.SinCambio => " =",
        _ => " X",
    };

    private static string Recorta(string s, int ancho) =>
        s.Length <= ancho ? s : s[..(ancho - 1)] + "…";

    // Cada modo se rellena en su hito. Devuelven 2 —y lo dicen— en vez de fingir que
    // pasaron: una comprobacion que aprueba sin mirar es peor que no tenerla.
    private static int NoTodavia(string que, string hito)
    {
        Console.Error.WriteLine($"[renombrar] {que} llega en {hito}.");
        return 2;
    }

    private static int Aplicar(string _) => NoTodavia("--aplicar", "H2");
    private static int Deshacer() => NoTodavia("--deshacer", "H2");
    private static int Ventana() => NoTodavia("la ventana", "H3");
}
