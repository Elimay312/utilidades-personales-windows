using System.Text;
using Dock;
using Windows.Win32;
using Windows.Win32.Foundation;

internal static class Program
{
    // STA: Windows.UI.Composition y las APIs de shell de M1 esperan un hilo STA.
    [STAThread]
    private static void Main(string[] args)
    {
        // Compilado como WinExe no hay consola propia. Si nos lanzaron desde una
        // terminal, enganchamos la suya para no perder los mensajes; si se arranco al
        // iniciar sesion, simplemente no hay donde escribir y da igual.
        //
        // Si la salida ya viene redirigida (una tuberia), no se toca nada: engancharse
        // a otra consola romperia la redireccion.
        if (!Console.IsOutputRedirected)
        {
            const uint AttachParentProcess = 0xFFFFFFFF;
            Windows.Win32.PInvoke.AttachConsole(AttachParentProcess);
        }

        // OleInitialize LO PRIMERO, antes de tocar nada de COM.
        //
        // No vale CoInitialize: la documentacion de RegisterDragDrop dice que si el
        // hilo se inicializo con CoInitialize/CoInitializeEx, RegisterDragDrop
        // "will always return an E_OUTOFMEMORY error". Y [STAThread] hace que el CLR
        // inicialice COM en cuanto se usa, asi que hay que ganarle la mano.
        HRESULT ole = PInvoke.OleInitialize();
        if (ole.Failed) Console.WriteLine($"[ole] OleInitialize fallo: 0x{(uint)ole.Value:X8}");

        // La consola de Windows usa la codificacion ANSI del sistema por defecto
        // y destroza los acentos.
        try { Console.OutputEncoding = Encoding.UTF8; } catch { /* sin consola */ }

        if (args.Contains("--check"))
        {
            SelfCheck();
            return;
        }

        Console.WriteLine("Dock - clic derecho sobre el dock para salir");
        Console.WriteLine();

        // Cronometro del arranque. El criterio de aceptacion es "menos de un segundo",
        // y sin medirlo no hay forma de saber si se cumple.
        Startup = System.Diagnostics.Stopwatch.StartNew();

        DockConfig.EnsureSeeded();
        DockConfig config = DockConfig.Load(DockConfig.DefaultPath);
        Console.WriteLine($"[config] {config.Apps.Count} apps en dock.json");

        AutoStart.Sync(config.AutoStart);

        // Un dock por pantalla.
        List<DockWindow> docks = [];
        Rebuild(docks, config);

        // Enchufar o quitar una pantalla se atiende tirando los docks y volviendolos a
        // crear. Es bruto, pero un cambio de pantallas invalida los HMONITOR y ya es de
        // por si un momento visualmente aparatoso; hacerlo fino exigiria una identidad
        // de monitor que todavia no existe. Los iconos no se vuelven a extraer: la
        // cache la comparten todos los docks.
        DockWindow.DisplaysChanged = () =>
        {
            Console.WriteLine("[dock] cambiaron las pantallas, reconstruyendo");
            foreach (DockWindow dock in docks) dock.Dispose();
            docks.Clear();
            Rebuild(docks, config);
        };

        using FileSystemWatcher watcher = WatchConfig(docks);
        DockWindow.RunMessageLoop();

        foreach (DockWindow dock in docks) dock.Dispose();

        // Despues de revocar los destinos de sueltas, que se hace en Dispose.
        PInvoke.OleUninitialize();

        Console.WriteLine("[dock] salida limpia");
    }

    /// <summary>Desde que arranca el proceso. Lo lee el dock para dejar constancia.</summary>
    internal static System.Diagnostics.Stopwatch? Startup;

    /// <summary>Crea un dock por pantalla y los muestra.</summary>
    private static void Rebuild(List<DockWindow> docks, DockConfig config)
    {
        foreach (var monitor in DockWindow.AllMonitors())
            docks.Add(new DockWindow(monitor, config));

        Console.WriteLine($"[dock] {docks.Count} monitor(es)");
        foreach (DockWindow dock in docks) dock.Show();
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
        ConfigSelfCheck.Run();
        IconsSelfCheck.Run();
        MagnifySelfCheck.Run();
        GenieSelfCheck.Run();

        // Resuelto para la pantalla principal: sin pantalla, Load ya no aplica ni la
        // validacion ni la superposicion local, que es justo lo que se quiere probar.
        string device = DockWindow.DeviceNameOf(
            PInvoke.MonitorFromPoint(default, Windows.Win32.Graphics.Gdi.MONITOR_FROM_FLAGS.MONITOR_DEFAULTTOPRIMARY));

        DockConfig config = DockConfig.Load(DockConfig.DefaultPath).For(device);
        Console.WriteLine($"[check] dock.json: {config.Apps.Count} apps validas en {device}");

        foreach (DockApp app in config.Apps)
        {
            try
            {
                // Desde el pool y no desde aqui a proposito: Main es STA, y en STA el
                // shell devuelve el icono bueno aunque Extract no se defienda. El dock
                // extrae desde Task.Run, asi que si esto midiera en STA volveria a dar
                // por bueno un icono que en el dock sale en blanco.
                IconBitmap icon = Task.Run(() => Icons.Extract(app.IconSource)).GetAwaiter().GetResult();
                int opaque = 0, transparent = 0;
                for (int i = 3; i < icon.Bgra.Length; i += 4)
                {
                    if (icon.Bgra[i] == 255) opaque++;
                    else if (icon.Bgra[i] == 0) transparent++;
                }
                // El lado que ocupa el dibujo es lo que delata al icono que el shell no
                // supo agrandar: por debajo de la mitad del lienzo sale como un sello.
                Console.WriteLine($"[check] {app.Name,-16} {icon.Width}x{icon.Height} " +
                    $"ocupa={100 * Icons.Side(icon) / icon.Width}% " +
                    $"opacos={opaque} transparentes={transparent}" +
                    (app.Arguments.Length > 0 ? $" args=[{app.Arguments}]" : ""));
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[check] {app.Name,-16} FALLO: {ex.Message}");
            }
        }
    }
}
