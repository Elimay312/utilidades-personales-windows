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

        IslaConfig config = Config.Cargar();
        Config.AplicarAutoArranque(config.AutoArranque);

        if (IslaWindow.Create(config) is null)
        {
            Console.Error.WriteLine("[isla] no se pudo crear la ventana.");
            return 2;
        }

        using FileSystemWatcher vigilante = Vigilar();

        Console.WriteLine($"[isla] arrancada en {reloj.ElapsedMilliseconds} ms");
        Console.WriteLine("[isla] Ctrl+Alt+I rota brasa -> asomada -> abierta.");

        IslaWindow.RunMessageLoop();
        IslaWindow.Cerrar();
        return 0;
    }

    /// <summary>
    /// Recarga isla.json al guardarlo, con rebote de 250 ms: los editores disparan
    /// varios eventos por guardado y a veces truncan el fichero antes de escribirlo,
    /// asi que sin esperar se leeria un JSON a medias.
    /// </summary>
    private static FileSystemWatcher Vigilar()
    {
        FileSystemWatcher vigilante = new(Config.Carpeta, "isla.json")
        {
            NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size | NotifyFilters.FileName,
            EnableRaisingEvents = true,
        };

        Timer? espera = null;
        void Cambio()
        {
            espera?.Dispose();
            espera = new Timer(_ => IslaWindow.Recargar(), null, 250, Timeout.Infinite);
        }

        vigilante.Changed += (_, _) => Cambio();
        vigilante.Created += (_, _) => Cambio();
        vigilante.Renamed += (_, _) => Cambio();
        return vigilante;
    }
}
