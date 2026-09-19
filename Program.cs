using System.Text;
using QuickLook;

internal static class Program
{
    // STA: Windows.UI.Composition y las APIs de shell esperan un hilo STA, y los
    // handlers de miniatura del shell son ThreadingModel=Apartment — desde un hilo MTA
    // devuelven basura sin fallar, que es peor que fallar.
    [STAThread]
    private static void Main()
    {
        // Compilado como WinExe no hay consola propia. Si nos lanzaron desde una
        // terminal, enganchamos la suya; si se arranco al iniciar sesion no hay donde
        // escribir y da igual. Con la salida ya redirigida no se toca nada: engancharse
        // a otra consola romperia la redireccion.
        if (!Console.IsOutputRedirected)
        {
            const uint AttachParentProcess = 0xFFFFFFFF;
            Windows.Win32.PInvoke.AttachConsole(AttachParentProcess);
        }

        // La consola de Windows usa la codificacion ANSI del sistema por defecto y
        // destroza los acentos.
        try { Console.OutputEncoding = Encoding.UTF8; } catch { /* sin consola */ }

        Console.WriteLine("QuickLook - espacio sobre un archivo del Explorador para verlo");
        Console.WriteLine();

        using HostWindow host = new();
        host.RunMessageLoop();

        Console.WriteLine("[quicklook] salida limpia");
    }
}
