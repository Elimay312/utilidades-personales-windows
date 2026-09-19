using System.Text.Json;
using Microsoft.Win32;

namespace Isla;

/// <summary>
/// Lo que el usuario puede tocar. <b>Aqui no entra nada de lo que la isla lee</b>:
/// SEGURIDAD.md regla 12 prohibe guardar un historial, asi que este fichero lleva
/// ajustes y nunca contenido.
/// </summary>
internal sealed record IslaConfig
{
    /// <summary>Nombre de dispositivo de la pantalla, del estilo <c>\\.\DISPLAY2</c>. Vacio = la principal.</summary>
    public string Pantalla { get; init; } = string.Empty;

    public int PomodoroMinutos { get; init; } = 25;

    /// <summary>
    /// Si un cambio de volumen hace asomar la isla. Windows ya ensena el suyo arriba a
    /// la izquierda y NO se puede quitar sin trucos prohibidos, asi que con esto en
    /// false se evita el aviso por duplicado.
    /// </summary>
    public bool VolumenAsoma { get; init; } = true;

    /// <summary>
    /// Autoarranque en <c>HKCU\Software\Microsoft\Windows\CurrentVersion\Run</c>.
    /// Por defecto NO: no se escribe en el registro hasta que alguien lo pide aqui.
    /// </summary>
    public bool AutoArranque { get; init; }
}

internal static class Config
{
    private const string ClaveRun = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string NombreRun = "Isla";

    // Admite comentarios y comas finales: el fichero es para leerlo y editarlo a mano,
    // no para que lo escriba un programa.
    private static readonly JsonSerializerOptions Opciones = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
    };

    /// <summary>
    /// Vive en %LOCALAPPDATA%, no junto al ejecutable. El dock aprendio por las malas
    /// que un <c>dotnet clean</c> se lleva por delante la carpeta de compilacion y con
    /// ella la configuracion del usuario, sin avisar.
    /// </summary>
    public static string Carpeta => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Isla");

    public static string Ruta => Path.Combine(Carpeta, "isla.json");

    public static IslaConfig Cargar()
    {
        try
        {
            Sembrar();
            IslaConfig? c = JsonSerializer.Deserialize<IslaConfig>(File.ReadAllText(Ruta), Opciones);
            if (c is null) return new IslaConfig();
            return c with { PomodoroMinutos = Math.Clamp(c.PomodoroMinutos, 1, 24 * 60) };
        }
        catch (Exception ex)
        {
            // Un JSON roto NO tumba la isla, a diferencia del dock. Esto es un adorno
            // del escritorio: negarse a arrancar por una coma de mas seria peor que
            // arrancar con los valores de siempre y decirlo.
            Console.Error.WriteLine($"[isla] isla.json no se pudo leer ({ex.Message}); valores por defecto.");
            return new IslaConfig();
        }
    }

    /// <summary>La primera vez copia el de al lado del ejecutable. Despues ya no se mira.</summary>
    private static void Sembrar()
    {
        if (File.Exists(Ruta)) return;
        Directory.CreateDirectory(Carpeta);

        string plantilla = Path.Combine(AppContext.BaseDirectory, "isla.json");
        if (File.Exists(plantilla)) File.Copy(plantilla, Ruta);
    }

    /// <summary>
    /// SEGURIDAD.md §3.6: <c>HKCU\...\Run</c> y nada mas. Sale en la pestana Inicio del
    /// Administrador de tareas y se puede quitar desde ahi. Nada de tareas programadas,
    /// servicios ni carpeta Startup.
    /// </summary>
    public static void AplicarAutoArranque(bool encender)
    {
        try
        {
            using RegistryKey? run = Registry.CurrentUser.OpenSubKey(ClaveRun, writable: true);
            if (run is null) return;

            string? actual = run.GetValue(NombreRun) as string;
            string quiero = $"\"{Environment.ProcessPath}\"";

            if (encender)
            {
                if (actual != quiero) run.SetValue(NombreRun, quiero);
            }
            else if (actual is not null)
            {
                run.DeleteValue(NombreRun, throwOnMissingValue: false);
            }
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[isla] autoarranque: {ex.Message}");
        }
    }
}
