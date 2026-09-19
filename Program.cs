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
            "--check"    => Comprobar(),
            "--previa"   => Previa(ruta),
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

              --previa CARPETA    la tabla antes -> despues, sin tocar nada
              --aplicar CARPETA   renombra, despues de que confirmes
              --deshacer          revierte el ultimo lote
              --check             comprueba el motor y la vista previa
              sin modo            abre la ventana
            """);
        return 0;
    }

    private static int Desconocido(string modo)
    {
        Console.Error.WriteLine($"[renombrar] no se que es \"{modo}\".");
        return Ayuda() + 2;
    }

    // Cada modo se rellena en su hito. Devuelven 2 —y lo dicen— en vez de fingir que
    // pasaron: una comprobacion que aprueba sin mirar es peor que no tenerla.
    private static int NoTodavia(string que, string hito)
    {
        Console.Error.WriteLine($"[renombrar] {que} llega en {hito}.");
        return 2;
    }

    private static int Comprobar() => NoTodavia("--check", "H1");
    private static int Previa(string _) => NoTodavia("--previa", "H1");
    private static int Aplicar(string _) => NoTodavia("--aplicar", "H2");
    private static int Deshacer() => NoTodavia("--deshacer", "H2");
    private static int Ventana() => NoTodavia("la ventana", "H3");
}
