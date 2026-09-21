using System.Text.Json;
using System.Text.Json.Serialization;

namespace Renombrar;

/// <summary>Un renombrado, tal y como se hizo. Es lo que se guarda para poder deshacerlo.</summary>
internal sealed record Par(string Viejo, string Nuevo);

/// <summary>El ultimo lote. Un fichero, sobrescrito cada vez (SEGURIDAD.md §3.4).</summary>
internal sealed record Lote(DateTime Fecha, string Carpeta, List<Par> Pares);

[JsonSourceGenerationOptions(WriteIndented = true)]
[JsonSerializable(typeof(Lote))]
internal sealed partial class LoteJson : JsonSerializerContext;

internal sealed record Resultado(List<Par> Hechos, List<string> Problemas);

/// <summary>
/// El unico sitio del programa que mueve ficheros. <c>File.Move</c> sin tercer argumento y
/// dentro de la misma carpeta, nada mas: no hay borrado, no hay sobrescritura y no hay
/// copia (SEGURIDAD.md §1.1). <c>auditar.ps1</c> falla si <c>File.Move</c> aparece en otro
/// fichero.
/// </summary>
internal static class Aplicar
{
    // El sufijo del paso intermedio. Lleva el nombre del programa a proposito: si algo se
    // va al traste en el peor momento, lo que queda en la carpeta dice quien lo dejo ahi.
    private const string Temporal = ".renombrar-tmp";

    internal static string DiarioPorDefecto => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Renombrar", "ultimo-lote.json");

    /// <summary>Renombra las filas que se pueden renombrar y guarda el diario con lo que se hizo de verdad.</summary>
    internal static Resultado Ejecutar(string carpeta, IReadOnlyList<Fila> filas, string diario)
    {
        List<Par> pares = [.. filas.Where(f => f.Estado == Estado.Ok)
                                   .Select(f => new Par(f.Antes, f.Despues))];

        Resultado r = Mover(carpeta, pares);
        if (r.Hechos.Count > 0) Guardar(new Lote(DateTime.Now, carpeta, r.Hechos), diario);
        return r;
    }

    /// <summary>Le da la vuelta al ultimo lote. Devuelve <c>null</c> si no hay ninguno guardado.</summary>
    internal static Lote? Ultimo(string diario)
    {
        if (!File.Exists(diario)) return null;
        return JsonSerializer.Deserialize(File.ReadAllText(diario), LoteJson.Default.Lote);
    }

    /// <summary>
    /// Deshacer es el mismo motor con los pares del reves y en orden inverso. No hace
    /// falta nada mas: un intercambio deshecho vuelve a ser un intercambio, y el temporal
    /// lo resuelve igual.
    /// </summary>
    internal static Resultado Revertir(Lote lote, string diario)
    {
        List<Par> alreves = [.. lote.Pares.AsEnumerable().Reverse().Select(p => new Par(p.Nuevo, p.Viejo))];

        Resultado r = Mover(lote.Carpeta, alreves);

        // Lo que quede por deshacer sigue siendo el ultimo lote, asi que el diario se
        // reescribe con lo que NO se pudo revertir. Si se deshizo entero, se queda vacio.
        HashSet<string> vueltos = new(r.Hechos.Select(p => p.Nuevo), StringComparer.OrdinalIgnoreCase);
        Guardar(lote with { Pares = [.. lote.Pares.Where(p => !vueltos.Contains(p.Viejo))] }, diario);

        return r;
    }

    /// <summary>
    /// Las dos pasadas. Solo pasan por un temporal los ficheros cuyo destino es el nombre
    /// de alguien del lote —el caso <c>A→B, B→A</c>, y el de cambiar solo mayusculas, que
    /// es el mismo caso—; el resto se mueve directo.
    ///
    /// <para>
    /// A la primera que falla se para (SEGURIDAD.md §3.3). Lo que si se deshace es el paso
    /// intermedio: un fichero no puede quedarse llamandose <c>.renombrar-tmp</c> porque
    /// otro se atascase. Eso no es revertir el lote a ciegas, es no dejar la mesa a medias.
    /// </para>
    /// </summary>
    private static Resultado Mover(string carpeta, IReadOnlyList<Par> pares)
    {
        HashSet<string> origenes = new(pares.Select(p => p.Viejo), StringComparer.OrdinalIgnoreCase);
        List<(string Actual, Par Par)> directos = [];
        List<(string Actual, Par Par)> temporales = [];
        List<Par> hechos = [];
        List<string> problemas = [];

        foreach (Par p in pares)
        {
            string actual = Path.Combine(carpeta, p.Viejo);

            if (!origenes.Contains(p.Nuevo))
            {
                directos.Add((actual, p));
                continue;
            }

            string tmp = actual + Temporal;
            if (!Mueve(actual, tmp, p.Viejo, problemas)) break;
            temporales.Add((tmp, p));
        }

        // Los que pasaron por temporal van AL FINAL, y no es un detalle: en una cadena
        // x->y, y->z, la x esta esperando a que y suelte su nombre, y la y no paso por
        // temporal porque su destino no era de nadie. Moviendo primero los directos, el
        // nombre que espera un temporal siempre esta libre cuando le toca: o lo solto un
        // directo que ya se movio, o lo solto la pasada 1.
        List<(string Actual, Par Par)> pendientes = [.. directos, .. temporales];

        // Si la pasada 1 ya se paro, aqui no se mueve nada mas: solo se devuelven los
        // temporales que dejo por el camino.
        bool parado = problemas.Count > 0;

        foreach ((string actual, Par p) in pendientes)
        {
            if (!parado && Mueve(actual, Path.Combine(carpeta, p.Nuevo), p.Viejo, problemas))
            {
                hechos.Add(p);
                continue;
            }

            parado = true;
            if (actual.EndsWith(Temporal, StringComparison.Ordinal))
                Mueve(actual, Path.Combine(carpeta, p.Viejo), p.Viejo, problemas);
        }

        return new Resultado(hechos, problemas);
    }

    private static bool Mueve(string desde, string hasta, string quien, List<string> problemas)
    {
        try
        {
            File.Move(desde, hasta);
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            problemas.Add($"{quien}: {e.Message}");
            return false;
        }
    }

    private static void Guardar(Lote lote, string diario)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(diario)!);
        File.WriteAllText(diario, JsonSerializer.Serialize(lote, LoteJson.Default.Lote));
    }
}
