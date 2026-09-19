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
            "--previa"   => Previsualizar(ruta, args.Length > 2 ? args[2] : string.Empty, false),
            "--aplicar"  => Previsualizar(ruta, args.Length > 2 ? args[2] : string.Empty, true),
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
              --aplicar CARPETA [PLANTILLA]  la misma tabla, y renombra si escribes "si"
              --deshacer                     devuelve los nombres del ultimo lote
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
    private static int Previsualizar(string carpeta, string plantilla, bool aplicar)
    {
        if (carpeta.Length == 0)
        {
            Console.Error.WriteLine("[renombrar] hace falta una carpeta.");
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
        Tabla(filas);

        bool hayLios = filas.Any(f => f.Estado is not (Estado.Ok or Estado.SinCambio));
        if (!aplicar) return hayLios ? 1 : 0;

        int cuantos = filas.Count(f => f.Estado == Estado.Ok);
        if (cuantos == 0)
        {
            Console.WriteLine("[renombrar] no hay nada que renombrar.");
            return hayLios ? 1 : 0;
        }

        // Las filas en rojo no paran el lote: se quedan fuera y se dice cuantas. Parar por
        // una colision en una carpeta de 300 ficheros seria obligar a arreglarlo todo
        // antes de poder hacer nada.
        if (hayLios) Console.WriteLine($"[renombrar] {filas.Count - cuantos - filas.Count(f => f.Estado == Estado.SinCambio)} fila(s) se quedan fuera.");
        if (!Confirma($"Renombrar {cuantos} fichero(s)")) return 2;

        Resultado r = Aplicar.Ejecutar(carpeta, filas, Aplicar.DiarioPorDefecto);
        Console.WriteLine($"[renombrar] {r.Hechos.Count} renombrado(s).");
        foreach (string p in r.Problemas) Console.Error.WriteLine($"  {p}");
        if (r.Hechos.Count > 0) Console.WriteLine("[renombrar] deshazlo con: renombrar --deshacer");

        return r.Problemas.Count > 0 ? 1 : 0;
    }

    private static int Deshacer()
    {
        Lote? lote = Aplicar.Ultimo(Aplicar.DiarioPorDefecto);
        if (lote is null || lote.Pares.Count == 0)
        {
            Console.WriteLine("[renombrar] no hay ningun lote que deshacer.");
            return 0;
        }

        Console.WriteLine($"El lote de {lote.Fecha:g} en {lote.Carpeta}:");
        Console.WriteLine();
        foreach (Par p in lote.Pares) Console.WriteLine($"     {p.Nuevo} -> {p.Viejo}");
        Console.WriteLine();

        if (!Confirma($"Devolver {lote.Pares.Count} nombre(s)")) return 2;

        Resultado r = Aplicar.Revertir(lote, Aplicar.DiarioPorDefecto);
        Console.WriteLine($"[renombrar] {r.Hechos.Count} devuelto(s).");
        foreach (string p in r.Problemas) Console.Error.WriteLine($"  {p}");

        return r.Problemas.Count > 0 ? 1 : 0;
    }

    /// <summary>La tabla, que es la misma que ensena la ventana porque sale de la misma llamada a <see cref="Previa.Calcular"/>.</summary>
    private static void Tabla(List<Fila> filas)
    {
        int ancho = Math.Min(filas.Max(f => f.Antes.Length), 50);
        foreach (Fila f in filas)
        {
            Console.WriteLine($"  {Marca(f.Estado)} {Recorta(f.Antes, ancho).PadRight(ancho)} -> {f.Despues}");
            if (f.Motivo.Length > 0) Console.WriteLine($"      {f.Motivo}");
        }

        Console.WriteLine();
        foreach (IGrouping<Estado, Fila> g in filas.GroupBy(f => f.Estado).OrderBy(g => g.Key))
            Console.WriteLine($"  {g.Count(),5}  {g.Key}");
        Console.WriteLine();
    }

    /// <summary>
    /// Escribir "si" entero, y no una tecla. Es la ultima puerta antes de tocar el disco y
    /// no debe poder cruzarse pulsando Enter por inercia.
    /// </summary>
    private static bool Confirma(string que)
    {
        // Sin guardia para la entrada redirigida: escribir "si" en una tuberia es tan
        // deliberado como teclearlo, y la tabla se ha impreso antes en los dos casos. Lo
        // que no puede pasar es cruzar esta puerta pulsando Enter por inercia, y de eso se
        // encarga que haya que escribir la palabra.
        Console.Write($"{que}? escribe si: ");
        if (Console.ReadLine()?.Trim().ToLowerInvariant() is "si" or "sí") return true;

        Console.WriteLine("[renombrar] no se ha tocado nada.");
        return false;
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

    private static int Ventana() => NoTodavia("la ventana", "H3");
}
