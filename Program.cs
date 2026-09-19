using System.Diagnostics;
using System.Text;
using Windows.Win32;

namespace Lanzador;

internal static class Program
{
    // Local\ y no Global\: el ambito es la sesion del usuario, que es donde hay un
    // teclado y un atajo. Global\ necesitaria permisos que esta app no tiene ni quiere.
    private const string MutexName = @"Local\Lanzador.instancia";

    [STAThread]
    private static int Main(string[] args)
    {
        // Compilado como WinExe no hay consola propia. Si nos han lanzado desde una
        // terminal nos enganchamos a la suya; si no, no hay donde escribir y da igual.
        // El guardia evita romper una tuberia cuando la salida esta redirigida.
        if (!Console.IsOutputRedirected) PInvoke.AttachConsole(0xFFFFFFFF);
        Console.OutputEncoding = Encoding.UTF8;

        // Los modos de consola no abren ventana ni registran el atajo, asi que no
        // compiten con la instancia que ya este corriendo y no piden el mutex.
        switch (args.Length > 0 ? args[0] : string.Empty)
        {
            case "--check":   return Check();
            case "--indice":  return VolcarIndice(args.Length > 1 && args[1] == "--todo");
            case "--buscar":  return Buscar(args.Length > 1 ? args[1] : string.Empty);
            case "--olvidar": return Olvidar();
            case "--ayuda" or "-h" or "/?": return Ayuda();
        }

        // Un solo lanzador: dos procesos serian dos ventanas peleandose por el mismo
        // atajo, y RegisterHotKey se lo daria solo al primero.
        using Mutex unica = new(true, MutexName, out bool primera);
        if (!primera)
        {
            Console.WriteLine("[lanzador] ya hay uno corriendo.");
            return 1;
        }

        Console.Error.WriteLine("[lanzador] todavia no hay ventana: H0 es solo el andamio.");
        return Ayuda();
    }

    private static int Ayuda()
    {
        Console.WriteLine("""
            lanzador [modo]

              --check          comprueba el algoritmo, el decaimiento y la calculadora
              --indice         vuelca las aplicaciones encontradas y cuanto costo
              --buscar TEXTO   los mejores resultados, con su puntuacion desglosada
              --olvidar        borra uso.json entero
              sin modo         arranca y se queda esperando el atajo
            """);
        return 0;
    }

    // Los cuatro modos de consola se van rellenando en su hito. Devuelven 2 —y lo dicen—
    // en vez de fingir que pasaron: una comprobacion que aprueba sin mirar es peor que
    // no tenerla.
    private static int NoTodavia(string que, string hito)
    {
        Console.Error.WriteLine($"[lanzador] {que} llega en {hito}.");
        return 2;
    }

    private static int Check()   => NoTodavia("--check", "H2");
    private static int Olvidar() => NoTodavia("--olvidar", "H3");

    /// <summary>
    /// Cuantas aplicaciones hay, de donde salen y cuanto costo. El desglose por fuente se
    /// queda: es lo que contesta "me falta una app" sin tener que adivinar en cual de las
    /// dos deberia estar.
    /// </summary>
    private static int VolcarIndice(bool todo)
    {
        Stopwatch reloj = Stopwatch.StartNew();
        List<Entrada> appsFolder = Indice.AppsFolder();
        long msFolder = reloj.ElapsedMilliseconds;

        reloj.Restart();
        List<Entrada> menus = Indice.MenusInicio();
        long msMenus = reloj.ElapsedMilliseconds;

        HashSet<string> enFolder = new(appsFolder.Select(e => e.Nombre), StringComparer.OrdinalIgnoreCase);
        int soloEnMenus = menus.Count(e => !enFolder.Contains(e.Nombre));

        Console.WriteLine($"shell:AppsFolder   {appsFolder.Count,5}   {msFolder,4} ms");
        Console.WriteLine($"menus Inicio .lnk  {menus.Count,5}   {msMenus,4} ms   ({soloEnMenus} no estan en AppsFolder)");
        Console.WriteLine($"indice             {appsFolder.Count + soloEnMenus,5}   {msFolder + msMenus,4} ms");
        Console.WriteLine();

        List<Entrada> indice = Indice.Construir();
        int cuantas = todo ? indice.Count : 40;
        foreach (Entrada e in indice.OrderBy(e => e.Nombre, StringComparer.OrdinalIgnoreCase).Take(cuantas))
        {
            Console.WriteLine($"  {e.Nombre,-45} {e.Destino}");
        }
        if (cuantas < indice.Count) Console.WriteLine($"  ... y {indice.Count - cuantas} mas (--indice --todo)");

        return 0;
    }

    private static int Buscar(string consulta)
    {
        _ = consulta;
        return NoTodavia("--buscar", "H2");
    }
}
