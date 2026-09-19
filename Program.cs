using System.Text;
using QuickLook;
using Windows.Win32.Foundation;

internal static class Program
{
    // STA: Windows.UI.Composition y las APIs de shell esperan un hilo STA, y los
    // handlers de miniatura del shell son ThreadingModel=Apartment — desde un hilo MTA
    // devuelven basura sin fallar, que es peor que fallar.
    [STAThread]
    private static void Main(string[] args)
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

        // OleInitialize LO PRIMERO, antes de tocar nada de COM: [STAThread] hace que el
        // CLR inicialice COM en cuanto se usa, y hay que ganarle la mano.
        HRESULT ole = Windows.Win32.PInvoke.OleInitialize();
        if (ole.Failed) Console.WriteLine($"[ole] OleInitialize fallo: 0x{(uint)ole.Value:X8}");

        // La consola de Windows usa la codificacion ANSI del sistema por defecto y
        // destroza los acentos.
        try { Console.OutputEncoding = Encoding.UTF8; } catch { /* sin consola */ }

        if (args.Contains("--check"))
        {
            SelfCheck.Run();
            Windows.Win32.PInvoke.OleUninitialize();
            return;
        }

        // Un solo QuickLook por sesion. Cada instancia instala SU hook, asi que dos
        // copias se comen el espacio dos veces y abren dos paneles — y desde fuera parece
        // que el filtro del hook esta roto. Local\ y no Global\: esto es por sesion de
        // usuario, no de maquina, y Global\ pediria permisos que no hacen falta.
        using Mutex instance = new(true, @"Local\QuickLook.SingleInstance", out bool first);
        if (!first)
        {
            Console.WriteLine("[quicklook] ya hay una instancia corriendo");
            Windows.Win32.PInvoke.OleUninitialize();
            return;
        }

        Console.WriteLine("QuickLook - espacio sobre un archivo del Explorador para verlo");
        Console.WriteLine();

        using HostWindow host = new();

        // El hook despues de la ventana: necesita a donde mandar el aviso. Y si no se
        // puede instalar, se dice y se sale, porque sin el no hay programa.
        using Hook hook = new(host.Handle);
        Console.WriteLine("[hook] instalado, solo mira la barra espaciadora");

        host.RunMessageLoop();

        Windows.Win32.PInvoke.OleUninitialize();

        Console.WriteLine("[quicklook] salida limpia");
    }
}
