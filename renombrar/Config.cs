using System.Text.Json;
using System.Text.Json.Serialization;

namespace Renombrar;

/// <summary>Una cadena de reglas con nombre. Lo que en la oficina se hace siempre igual.</summary>
internal sealed record Preset(string Nombre, string Plantilla = "", string Buscar = "", string Por = "");

internal sealed record Ajustes(List<Preset> Presets, Preset Ultimo);

[JsonSourceGenerationOptions(WriteIndented = true)]
[JsonSerializable(typeof(Ajustes))]
internal sealed partial class AjustesJson : JsonSerializerContext;

/// <summary>
/// <c>renombrar.json</c>, junto al ejecutable, igual que el dock y la isla. Son
/// <b>datos</b>: unas cadenas que alimentan un <c>switch</c>. No se evalua nada y no se
/// lanza nada (SEGURIDAD.md §3.5).
///
/// <para>
/// Se edita a mano y con el bloc de notas. Un editor de presets dentro de la ventana seria
/// una pantalla entera —nombrar, guardar, borrar, reordenar— para algo que en esta oficina
/// se escribe una vez al ano.
/// </para>
/// </summary>
internal static class Config
{
    private static readonly Preset Nada = new("", "", "", "");

    private static string Fichero =>
        Path.Combine(AppContext.BaseDirectory, "renombrar.json");

    internal static Ajustes Cargar()
    {
        try
        {
            if (File.Exists(Fichero) &&
                JsonSerializer.Deserialize(File.ReadAllText(Fichero), AjustesJson.Default.Ajustes) is { } a)
            {
                return a with { Presets = a.Presets ?? [], Ultimo = a.Ultimo ?? Nada };
            }
        }
        catch (Exception e) when (e is IOException or JsonException)
        {
            // Un JSON a medias no puede impedir abrir el programa: se dice y se sigue con
            // los valores de fabrica, que es lo que el usuario puede arreglar mirando.
            Console.WriteLine($"[config] {Fichero}: {e.Message}");
        }

        return new Ajustes([], Nada);
    }

    /// <summary>
    /// Guarda lo ultimo que escribiste, y nada mas. Los presets se quedan como estaban:
    /// este fichero lo escribe una persona y el programa no tiene por que reordenarselo.
    /// </summary>
    internal static void Guarda(Ajustes ajustes, Preset ultimo)
    {
        try
        {
            File.WriteAllText(Fichero, JsonSerializer.Serialize(ajustes with { Ultimo = ultimo },
                                                                AjustesJson.Default.Ajustes));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Console.WriteLine($"[config] no se pudo guardar: {e.Message}");
        }
    }
}
