using System.Diagnostics;
using System.Text;
using Windows.Win32;

namespace Isla;

internal static class Program
{
    // Local\ y no Global\: el ambito es la sesion del usuario, que es donde hay una
    // pantalla. Global\ necesitaria permisos que esta app no tiene ni quiere.
    private const string MutexName = @"Local\IslaDinamica.instancia";

    [STAThread]
    private static int Main()
    {
        // Compilado como WinExe no hay consola propia. Si nos han lanzado desde una
        // terminal nos enganchamos a la suya; si no, no hay donde escribir y da igual.
        // El guardia evita romper una tuberia cuando la salida esta redirigida.
        if (!Console.IsOutputRedirected) PInvoke.AttachConsole(0xFFFFFFFF);
        Console.OutputEncoding = Encoding.UTF8;

        // Una sola isla, a diferencia del dock, que tiene una ventana por pantalla.
        // Dos procesos serian dos pastillas exactamente en el mismo sitio.
        using Mutex unica = new(true, MutexName, out bool primera);
        if (!primera)
        {
            Console.WriteLine("[isla] ya hay una corriendo.");
            return 1;
        }

        Stopwatch reloj = Stopwatch.StartNew();

        IslaWindow? isla = IslaWindow.Create();
        if (isla is null)
        {
            Console.Error.WriteLine("[isla] no se pudo crear la ventana.");
            return 2;
        }

        Console.WriteLine($"[isla] arrancada en {reloj.ElapsedMilliseconds} ms");
        Console.WriteLine("[isla] Ctrl+Alt+I rota brasa -> asomada -> abierta.");

        IslaWindow.RunMessageLoop();
        isla.Dispose();
        return 0;
    }
}
