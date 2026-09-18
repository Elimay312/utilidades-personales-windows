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

        Console.WriteLine("Dock M5 - clic derecho sobre el dock para salir");
        Console.WriteLine();

        DockConfig config = DockConfig.Load(DockConfig.DefaultPath);
        Console.WriteLine($"[config] {config.Apps.Count} apps");

        // Un dock por pantalla.
        List<DockWindow> docks = [];
        foreach (var monitor in DockWindow.AllMonitors())
            docks.Add(new DockWindow(monitor, config));

        Console.WriteLine($"[dock] {docks.Count} monitor(es)");
        foreach (DockWindow dock in docks) dock.Show();

        using FileSystemWatcher watcher = WatchConfig(docks);
        DockWindow.RunMessageLoop();

        foreach (DockWindow dock in docks) dock.Dispose();

        Console.WriteLine("[dock] salida limpia");
    }

    /// <summary>
    /// Vigila dock.json y recarga el dock al vuelo.
    ///
    /// El rebote de 250 ms no sobra: los editores no guardan de una sola vez, sino
    /// que disparan varios eventos por guardado (y a veces truncan el archivo antes
    /// de escribirlo). Sin esperar, se leeria un JSON a medias.
    /// </summary>
    private static FileSystemWatcher WatchConfig(List<DockWindow> docks)
    {
        string path = DockConfig.DefaultPath;
        FileSystemWatcher watcher = new(Path.GetDirectoryName(path)!, Path.GetFileName(path))
        {
            NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size | NotifyFilters.FileName,
        };

        // Un unico timer que se reprograma en cada evento: el rebote clasico.
        Timer debounce = new(_ =>
        {
            foreach (DockWindow dock in docks) dock.RequestReload();
        }, null, Timeout.Infinite, Timeout.Infinite);

        void Touched(object? sender, FileSystemEventArgs e) => debounce.Change(250, Timeout.Infinite);

        watcher.Changed += Touched;
        watcher.Created += Touched;
        watcher.Renamed += (_, _) => debounce.Change(250, Timeout.Infinite);
        watcher.EnableRaisingEvents = true;

        Console.WriteLine($"[config] vigilando {path}");
        return watcher;
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
