using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Dock;

/// <summary>Una entrada del dock.</summary>
internal sealed class DockApp
{
    /// <summary>Nombre visible. De momento solo sirve para los mensajes de error.</summary>
    public string Name { get; init; } = "";

    /// <summary>
    /// Ruta a un ejecutable, o un nombre del espacio de nombres del shell como
    /// <c>shell:AppsFolder\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App</c>.
    ///
    /// Una sola cadena para los dos casos porque SHCreateItemFromParsingName
    /// entiende ambos: así la extracción de iconos tiene una única ruta de código
    /// para apps Win32 y para apps MSIX.
    /// </summary>
    public string Target { get; init; } = "";

    public bool IsShellItem => Target.StartsWith("shell:", StringComparison.OrdinalIgnoreCase);

    /// <summary>
    /// Lanza la app delegando en el shell. Sin P/Invoke y sin CreateProcess con
    /// flags raros: es API de .NET pura (ver Fase 2).
    /// </summary>
    public void Launch()
    {
        // Una sola ruta para todo. UseShellExecute acaba en ShellExecuteEx, que
        // entiende tanto una ruta de archivo como un moniker "shell:", así que las
        // apps MSIX no necesitan nada especial.
        //
        // Antes se lanzaban con explorer.exe como proceso intermedio, y eso dejaba
        // un explorer.exe suelto apareciendo en Alt+Tab.
        Process.Start(new ProcessStartInfo(Target) { UseShellExecute = true });
    }
}

/// <summary>Contenido de dock.json.</summary>
internal sealed class DockConfig
{
    /// <summary>Tamaño del icono en reposo, en unidades lógicas (96 DPI).</summary>
    public int IconSize { get; init; } = 48;

    /// <summary>Separación entre iconos, en unidades lógicas.</summary>
    public int IconSpacing { get; init; } = 16;

    /// <summary>
    /// Si el dock se esconde solo, dejando asomar una franja en el borde inferior.
    /// </summary>
    public bool AutoHide { get; init; } = true;

    public List<DockApp> Apps { get; init; } = [];

    private static readonly JsonSerializerOptions Options = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
    };

    public static string DefaultPath =>
        Path.Combine(AppContext.BaseDirectory, "dock.json");

    public static DockConfig Load(string path)
    {
        DockConfig config = JsonSerializer.Deserialize<DockConfig>(File.ReadAllText(path), Options)
            ?? throw new InvalidDataException($"{path} está vacío");

        // Una entrada mala no debe tumbar el dock: se avisa y se omite.
        List<DockApp> valid = [];
        foreach (DockApp app in config.Apps)
        {
            if (string.IsNullOrWhiteSpace(app.Target))
            {
                Console.WriteLine($"[config] omitida '{app.Name}': sin target");
                continue;
            }

            // SHCreateItemFromParsingName NO acepta barras normales: son nombres de
            // parsing del shell, no rutas de archivo. File.Exists sí las acepta, así
            // que sin normalizar aquí la entrada pasa la validación y revienta
            // después con E_INVALIDARG.
            //
            // Se normalizan todas, también las de shell:AppsFolder, para que en el
            // JSON nunca haga falta escapar una barra invertida. Los AppUserModelID
            // no contienen barras normales, así que no hay nada que romper.
            string target = app.Target.Replace('/', '\\');

            if (!app.IsShellItem && !File.Exists(target))
            {
                Console.WriteLine($"[config] omitida '{app.Name}': no existe {target}");
                continue;
            }

            valid.Add(new DockApp { Name = app.Name, Target = target });
        }

        return new DockConfig
        {
            IconSize = config.IconSize,
            IconSpacing = config.IconSpacing,
            AutoHide = config.AutoHide,
            Apps = valid,
        };
    }
}
