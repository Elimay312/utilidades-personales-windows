using System.Diagnostics;
using System.Text;
using Windows.Win32;

namespace Hud;

internal static class Program
{
    // Local\ y no Global\: el ambito es la sesion del usuario, que es donde hay un
    // teclado y una pantalla. Global\ necesitaria permisos que esta app no tiene ni
    // quiere (SEGURIDAD.md regla 1).
    private const string MutexName = @"Local\HudVolumen.instancia";

    [STAThread]
    private static int Main(string[] args)
    {
        // Compilado como WinExe no hay consola propia. Si nos han lanzado desde una
        // terminal nos enganchamos a la suya; si no, no hay donde escribir y da igual.
        // El guardia evita romper una tuberia cuando la salida esta redirigida.
        if (!Console.IsOutputRedirected) PInvoke.AttachConsole(0xFFFFFFFF);
        Console.OutputEncoding = Encoding.UTF8;

        // Logica pura, sin ventana. Va dentro del binario y no en un proyecto de tests
        // aparte: lo que se comprueba aqui es lo que se rompe en silencio.
        if (args.Contains("--check"))
        {
            HudWindow.SelfCheck();
            Console.WriteLine("[hud] --check OK");
            return 0;
        }

        // Dos HUD serian dos capsulas exactamente en el mismo sitio, y los dos
        // peleandose por registrar las mismas tres teclas.
        using Mutex unica = new(true, MutexName, out bool primera);
        if (!primera)
        {
            Console.WriteLine("[hud] ya hay uno corriendo.");
            return 1;
        }

        Stopwatch reloj = Stopwatch.StartNew();

        HudConfig config = Config.Cargar();
        Config.AplicarAutoArranque(config.AutoArranque);

        if (HudWindow.Create(config) is null)
        {
            Console.Error.WriteLine("[hud] no se pudo crear la ventana.");
            return 2;
        }

        Console.WriteLine($"[hud] arrancado en {reloj.ElapsedMilliseconds} ms");
        Console.WriteLine("[hud] Ctrl+Alt+H para salir.");

        HudWindow.RunMessageLoop();
        return 0;
    }
}
