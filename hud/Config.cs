using System.Text.Json;
using Microsoft.Win32;

namespace Hud;

/// <summary>
/// Lo que el usuario puede tocar. <b>Aqui no entra nada de lo que el HUD lee</b>:
/// SEGURIDAD.md regla 12 prohibe guardar historial, asi que este fichero lleva ajustes
/// y nunca estado.
/// </summary>
internal sealed record HudConfig
{
    // Aqui hubo un "ocultarFlyoutNativo" y ya no hace falta: capturar las teclas con
    // RegisterHotKey suprime el aviso nativo de volumen por si solo, porque el shell
    // nunca llega a ver la pulsacion. SEGURIDAD.md §1 cuenta la medicion entera.

    /// <summary>
    /// Cuanto sube o baja el volumen por pulsacion, en porcentaje. Windows usa 2 y no se
    /// puede cambiar; como las teclas las registramos nosotros (§3.1), aqui si.
    /// </summary>
    public int PasoVolumen { get; init; } = 2;

    public int MsAutoocultar { get; init; } = 1600;

    /// <summary>"abajo" como macOS, o "arriba".</summary>
    public string Posicion { get; init; } = "abajo";

    /// <summary>
    /// Autoarranque en <c>HKCU\Software\Microsoft\Windows\CurrentVersion\Run</c>.
    /// Por defecto NO: no se escribe en el registro hasta que alguien lo pide aqui.
    /// </summary>
    public bool AutoArranque { get; init; }

    public bool Abajo => !string.Equals(Posicion, "arriba", StringComparison.OrdinalIgnoreCase);
}

internal static class Config
{
    private const string ClaveRun = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string NombreRun = "Hud";

    // Admite comentarios y comas finales: el fichero es para leerlo y editarlo a mano,
    // no para que lo escriba un programa.
    private static readonly JsonSerializerOptions Opciones = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
    };

    /// <summary>
    /// Vive en %LOCALAPPDATA%, no junto al ejecutable. El dock aprendio por las malas que
    /// un <c>dotnet clean</c> se lleva por delante la carpeta de compilacion y con ella
    /// la configuracion del usuario, sin avisar.
    /// </summary>
    public static string Carpeta => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Hud");

    public static string Ruta => Path.Combine(Carpeta, "hud.json");

    public static HudConfig Cargar()
    {
        try
        {
            Sembrar();
            HudConfig? c = JsonSerializer.Deserialize<HudConfig>(File.ReadAllText(Ruta), Opciones);
            if (c is null) return new HudConfig();

            return c with
            {
                // Un paso de 0 dejaria las teclas registradas sin hacer nada, que es peor
                // que no registrarlas. Uno de 50 deja el volumen en tres escalones.
                PasoVolumen = Math.Clamp(c.PasoVolumen, 1, 25),
                MsAutoocultar = Math.Clamp(c.MsAutoocultar, 300, 10_000),
            };
        }
        catch (Exception ex)
        {
            // Un JSON roto NO tumba el HUD. Esto se queda entre el usuario y sus teclas
            // de volumen: negarse a arrancar por una coma de mas seria peor que arrancar
            // con los valores de siempre y decirlo.
            Console.Error.WriteLine($"[hud] hud.json no se pudo leer ({ex.Message}); valores por defecto.");
            return new HudConfig();
        }
    }

    /// <summary>La primera vez copia el de al lado del ejecutable. Despues ya no se mira.</summary>
    private static void Sembrar()
    {
        if (File.Exists(Ruta)) return;
        Directory.CreateDirectory(Carpeta);

        string plantilla = Path.Combine(AppContext.BaseDirectory, "hud.json");
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
            Console.Error.WriteLine($"[hud] autoarranque: {ex.Message}");
        }
    }
}
