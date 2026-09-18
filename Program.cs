using System.Text;
using Dock;

internal static class Program
{
    // STA: Windows.UI.Composition y las APIs de shell de M1 esperan un hilo STA.
    [STAThread]
    private static void Main(string[] args)
    {
        // La consola de Windows usa la codificacion ANSI del sistema por defecto
        // y destroza los acentos.
        Console.OutputEncoding = Encoding.UTF8;

        if (args.Contains("--check"))
        {
            SelfCheck();
            return;
        }

        Console.WriteLine("Dock M4 - clic derecho sobre el dock para salir");
        Console.WriteLine();

        DockConfig config = DockConfig.Load(DockConfig.DefaultPath);
        Console.WriteLine($"[config] {config.Apps.Count} apps");

        // Un dock por pantalla.
        List<DockWindow> docks = [];
        foreach (var monitor in DockWindow.AllMonitors())
            docks.Add(new DockWindow(monitor, config));

        Console.WriteLine($"[dock] {docks.Count} monitor(es)");
        foreach (DockWindow dock in docks) dock.Show();

        DockWindow.RunMessageLoop();

        foreach (DockWindow dock in docks) dock.Dispose();

        Console.WriteLine("[dock] salida limpia");
    }

    /// <summary>Verificacion sin arrancar la ventana: logica pura + extraccion real.</summary>
    private static void SelfCheck()
    {
        IconsSelfCheck.Run();
        MagnifySelfCheck.Run();

        DockConfig config = DockConfig.Load(DockConfig.DefaultPath);
        Console.WriteLine($"[check] dock.json: {config.Apps.Count} apps validas");

        foreach (DockApp app in config.Apps)
        {
            try
            {
                IconBitmap icon = Icons.Extract(app.Target);
                int opaque = 0, transparent = 0;
                for (int i = 3; i < icon.Bgra.Length; i += 4)
                {
                    if (icon.Bgra[i] == 255) opaque++;
                    else if (icon.Bgra[i] == 0) transparent++;
                }
                Console.WriteLine($"[check] {app.Name,-16} {icon.Width}x{icon.Height} " +
                    $"opacos={opaque} transparentes={transparent}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[check] {app.Name,-16} FALLO: {ex.Message}");
            }
        }
    }
}
