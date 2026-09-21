using System.Text.Json;
using System.Text.Json.Serialization;

namespace QuickLook;

/// <summary>
/// Lo que se puede ajustar, en <c>%LOCALAPPDATA%\QuickLook\quicklook.json</c>.
///
/// <para>
/// <b>De solo lectura, y eso no es una limitacion: es el diseno.</b> El unico estado que
/// este programa necesita recordar entre sesiones es si arranca solo, y el sitio canonico de
/// eso es la clave <c>Run</c> del registro, que ademas sale en la pestana Inicio del
/// Administrador de tareas. Todo lo demas —el tamano del panel, si el video suena— lo
/// escribe el usuario y el programa solo lo lee.
/// </para>
///
/// <para>
/// Asi que <b>el programa no escribe ni un archivo</b>, y la regla de auditoria que prohibe
/// escribir archivos puede seguir siendo un grep a secas, sin excepciones. El diseno anterior
/// preveia un <c>quicklook.local.json</c> para lo que el programa cambiara; al no cambiar
/// nada, sobra.
/// </para>
///
/// <para>
/// Se relee sola cuando el archivo cambia de fecha: no hace falta vigilante ni reiniciar,
/// porque lo unico que la consulta es abrir un panel, y eso pasa cuando el usuario lo pide.
/// </para>
/// </summary>
internal sealed record Config(
    [property: JsonPropertyName("autoStart")] bool AutoStart = false,
    [property: JsonPropertyName("panelWidth")] float PanelWidth = 0.62f,
    [property: JsonPropertyName("panelHeight")] float PanelHeight = 0.72f,
    [property: JsonPropertyName("videoMuted")] bool VideoMuted = true,
    [property: JsonPropertyName("audioPlays")] bool AudioPlays = true)
{
    private static readonly string Path =
        System.IO.Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "QuickLook", "quicklook.json");

    private static readonly JsonSerializerOptions Options = new()
    {
        AllowTrailingCommas = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        PropertyNameCaseInsensitive = true,
    };

    private static Config _current = new();
    private static DateTime _stamp = DateTime.MinValue;
    private static bool _announced;

    /// <summary>
    /// La config de ahora mismo. Se relee si el archivo cambio desde la ultima vez.
    ///
    /// Los valores se recortan a rangos con sentido: un <c>panelWidth</c> de 5 escrito a
    /// mano daria un panel mas grande que la pantalla, imposible de cerrar con el raton.
    /// </summary>
    public static Config Current
    {
        get
        {
            try
            {
                if (!File.Exists(Path))
                {
                    if (!_announced)
                    {
                        _announced = true;
                        Console.WriteLine($"[config] sin {Path}, valores por defecto");
                    }
                    return _current;
                }

                DateTime stamp = File.GetLastWriteTimeUtc(Path);
                if (stamp == _stamp) return _current;

                _stamp = stamp;
                Config? read = JsonSerializer.Deserialize<Config>(File.ReadAllText(Path), Options);
                if (read is null) return _current;

                _current = read with
                {
                    PanelWidth = Math.Clamp(read.PanelWidth, 0.2f, 0.95f),
                    PanelHeight = Math.Clamp(read.PanelHeight, 0.2f, 0.95f),
                };

                Console.WriteLine($"[config] leido {Path}");
                return _current;
            }
            catch (Exception ex)
            {
                // Un JSON a medio guardar, o con una coma de mas. Se sigue con lo anterior
                // en vez de dejar al usuario sin programa por un error de tecleo.
                Console.WriteLine($"[config] no se pudo leer: {ex.Message}");
                return _current;
            }
        }
    }
}
