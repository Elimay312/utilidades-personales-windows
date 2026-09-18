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

        Console.WriteLine("Dock M3 - clic derecho sobre el dock para salir");
        Console.WriteLine();

        DockConfig config = DockConfig.Load(DockConfig.DefaultPath);
        Console.WriteLine($"[config] {config.Apps.Count} apps");

        // M1: una sola ventana en el monitor principal. En M4 pasa a una por pantalla.
        using var dock = new DockWindow(DockWindow.PrimaryMonitor, config);
        dock.Show();
        DockWindow.RunMessageLoop();

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
