using Microsoft.Win32;
using System.Text.Json;
using System.Text.Json.Serialization;
using Windows.Win32.UI.Input.KeyboardAndMouse;

namespace Lanzador;

/// <summary>
/// Lo que el usuario puede tocar. <b>Aqui no entra nada de lo que el lanzador aprende</b>:
/// eso vive en uso.json, al lado, y se borra por separado.
/// </summary>
internal sealed record LanzadorConfig
{
    /// <summary>Modificadores y tecla, del estilo <c>Alt+Space</c> o <c>Ctrl+Shift+P</c>.</summary>
    [JsonPropertyName("atajo")]
    public string Atajo { get; init; } = "Alt+Space";

    [JsonPropertyName("maxResultados")]
    public int MaxResultados { get; init; } = 8;

    /// <summary>Prefijo -> plantilla de URL con <c>{}</c> donde va el termino.</summary>
    [JsonPropertyName("web")]
    public Dictionary<string, string> Web { get; init; } = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>
    /// Autoarranque en <c>HKCU\Software\Microsoft\Windows\CurrentVersion\Run</c>.
    /// Por defecto NO: no se escribe en el registro hasta que alguien lo pide aqui.
    /// </summary>
    [JsonPropertyName("autoArranque")]
    public bool AutoArranque { get; init; }
}

internal static class Config
{
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
    /// un <c>dotnet clean</c> se lleva por delante la carpeta de compilacion y con ella la
    /// configuracion del usuario, sin avisar.
    /// </summary>
    public static string Carpeta => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Lanzador");

    public static string Ruta => Path.Combine(Carpeta, "lanzador.json");

    public static LanzadorConfig Cargar()
    {
        try
        {
            Sembrar();
            LanzadorConfig? c = JsonSerializer.Deserialize<LanzadorConfig>(File.ReadAllText(Ruta), Opciones);
            if (c is null) return new LanzadorConfig();
            return c with { MaxResultados = Math.Clamp(c.MaxResultados, 1, 20) };
        }
        catch (Exception ex)
        {
            // Un JSON roto no impide lanzar cosas: se pierden los ajustes, no el programa.
            Console.Error.WriteLine($"[lanzador] lanzador.json no se pudo leer ({ex.Message}); valores por defecto.");
            return new LanzadorConfig();
        }
    }

    /// <summary>La primera vez copia el de al lado del ejecutable. Despues ya no se mira.</summary>
    private static void Sembrar()
    {
        if (File.Exists(Ruta)) return;
        Directory.CreateDirectory(Carpeta);

        string plantilla = Path.Combine(AppContext.BaseDirectory, "lanzador.json");
        if (File.Exists(plantilla)) File.Copy(plantilla, Ruta);
    }

    private const string ClaveRun = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string NombreRun = "Lanzador";

    /// <summary>
    /// SEGURIDAD.md §3.9: <c>HKCU\...\Run</c> y nada mas. Sale en la pestana Inicio del
    /// Administrador de tareas y se puede quitar desde ahi. Nada de tareas programadas,
    /// servicios ni carpeta Startup.
    /// <para>
    /// Estaba en el JSON desde H4 y no lo aplicaba nadie: el documento prometia una cosa
    /// que el codigo no hacia. Encontrado repasando §3.9 contra el codigo en H7.
    /// </para>
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
            Console.Error.WriteLine($"[lanzador] autoarranque: {ex.Message}");
        }
    }

    /// <summary>
    /// <c>"Alt+Space"</c> -> los argumentos de RegisterHotKey. Devuelve false si no se
    /// entiende, y entonces quien llama lo dice por consola: un atajo que no se registra
    /// y no avisa es media hora perdida.
    /// </summary>
    public static bool LeerAtajo(string atajo, out HOT_KEY_MODIFIERS mods, out uint tecla)
    {
        mods = 0;
        tecla = 0;

        foreach (string trozo in atajo.Split('+', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            switch (trozo.ToLowerInvariant())
            {
                case "ctrl" or "control": mods |= HOT_KEY_MODIFIERS.MOD_CONTROL; continue;
                case "alt": mods |= HOT_KEY_MODIFIERS.MOD_ALT; continue;
                case "shift" or "mayus": mods |= HOT_KEY_MODIFIERS.MOD_SHIFT; continue;
                case "win": mods |= HOT_KEY_MODIFIERS.MOD_WIN; continue;
            }

            // La tecla, que tiene que ser la ultima y una sola.
            if (tecla != 0) return false;
            tecla = Tecla(trozo);
            if (tecla == 0) return false;
        }

        // Sin modificador, RegisterHotKey se quedaria una tecla suelta del sistema entero.
        return tecla != 0 && mods != 0;
    }

    private static uint Tecla(string nombre)
    {
        string n = nombre.ToLowerInvariant();

        if (n.Length == 1 && char.IsAsciiLetterOrDigit(n[0])) return char.ToUpperInvariant(n[0]);

        if (n.StartsWith('f') && n.Length <= 3 && int.TryParse(n.AsSpan(1), out int f) && f is >= 1 and <= 24)
        {
            return (uint)(0x70 + f - 1);   // VK_F1 = 0x70
        }

        return n switch
        {
            "space" or "espacio" => 0x20,
            "enter" or "return" => 0x0D,
            "tab" => 0x09,
            "esc" or "escape" => 0x1B,
            "insert" or "ins" => 0x2D,
            "home" or "inicio" => 0x24,
            "end" or "fin" => 0x23,
            _ => 0,
        };
    }
}
