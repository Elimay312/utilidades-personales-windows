using System.Text.Json;
using System.Text.Json.Serialization;

namespace Lanzador;

/// <summary>Una cosa que abriste: cuantas veces y cuando fue la ultima.</summary>
/// <remarks>
/// SEGURIDAD.md §3.6, corte 3: <b>una fecha, no una lista</b>. Se guarda la ultima vez y no
/// un registro de todas las veces, para que de aqui no salga una linea temporal de tu dia.
/// </remarks>
internal sealed record Lanzamiento
{
    public int Veces { get; init; }
    public DateTimeOffset Ultimo { get; init; }
}

/// <summary>
/// Lo que el lanzador aprende de ti, y lo unico que escribe a disco.
/// <para>
/// Esto es justo lo que la isla prohibe en su regla 12, asi que se gano por escrito en
/// <c>SEGURIDAD.md §3.6</c> con cinco cortes. El que mas manda: <b>solo entra aqui lo que
/// acabo en Enter</b>. Una consulta que escribiste y descartaste no deja rastro.
/// </para>
/// </summary>
internal sealed class Uso
{
    // El refuerzo maximo que puede dar el uso. Esta calibrado contra los pesos de
    // Coincidencia: una puntuacion de texto va de ~30 a ~210, asi que 120 deja que lo que
    // usas a diario adelante a una coincidencia algo mejor, pero no a una mucho mejor.
    private const int BonoUso = 12;
    private const int VecesQueCuentan = 10;

    /// <summary>A los 30 dias sin abrir algo, su refuerzo vale la mitad.</summary>
    private const double SemividaDias = 30.0;

    /// <summary>
    /// Lo que elegiste para una consulta exacta gana a cualquier puntuacion de texto. Son
    /// diez lineas y es lo que hace que un lanzador parezca que te lee la mente: si para
    /// "br" elegiste Brave, "br" da Brave siempre.
    /// </summary>
    private const int BonoFijado = 1000;

    [JsonPropertyName("lanzamientos")]
    public Dictionary<string, Lanzamiento> Lanzamientos { get; init; } =
        new(StringComparer.OrdinalIgnoreCase);

    [JsonPropertyName("elecciones")]
    public Dictionary<string, string> Elecciones { get; init; } =
        new(StringComparer.OrdinalIgnoreCase);

    private static readonly JsonSerializerOptions Opciones = new()
    {
        PropertyNameCaseInsensitive = true,
        WriteIndented = true,
    };

    public static string Ruta => Path.Combine(Config.Carpeta, "uso.json");

    /// <summary>
    /// Cuanto suma el uso para este destino. Decaimiento exponencial con semivida de 30
    /// dias, y las veces saturan a las 10: sin saturar, algo abierto trescientas veces
    /// sepultaria para siempre a todo lo demas y el lanzador dejaria de aprender.
    /// </summary>
    public int Refuerzo(string destino, DateTimeOffset ahora)
    {
        if (!Lanzamientos.TryGetValue(destino, out Lanzamiento? l)) return 0;

        double dias = (ahora - l.Ultimo).TotalDays;
        if (dias < 0) dias = 0;   // el reloj puede ir hacia atras; no se premia por ello

        double decaimiento = Math.Pow(0.5, dias / SemividaDias);
        return (int)Math.Round(BonoUso * Math.Min(l.Veces, VecesQueCuentan) * decaimiento);
    }

    /// <summary>Lo que se eligio la ultima vez para esa consulta exacta, si hubo algo.</summary>
    public string? Fijado(string consulta) =>
        Elecciones.TryGetValue(consulta, out string? destino) ? destino : null;

    public static int BonoDeFijado => BonoFijado;

    /// <summary>
    /// Se llama <b>al lanzar</b>, no al escribir. Es la linea que separa "guardar lo que
    /// abriste" de "guardar lo que escribes" (regla 11).
    /// </summary>
    public void Registrar(string consulta, string destino, DateTimeOffset ahora)
    {
        Lanzamientos.TryGetValue(destino, out Lanzamiento? antes);
        Lanzamientos[destino] = new Lanzamiento { Veces = (antes?.Veces ?? 0) + 1, Ultimo = ahora };

        if (consulta.Length > 0) Elecciones[consulta] = destino;
    }

    public static Uso Cargar()
    {
        try
        {
            if (!File.Exists(Ruta)) return new Uso();
            return JsonSerializer.Deserialize<Uso>(File.ReadAllText(Ruta), Opciones) ?? new Uso();
        }
        catch (Exception ex)
        {
            // Un uso.json roto no tumba el lanzador: se pierde el ranking, que se vuelve a
            // aprender solo, y se dice. Negarse a abrir por una coma seria peor.
            Console.Error.WriteLine($"[lanzador] uso.json no se pudo leer ({ex.Message}); se empieza de cero.");
            return new Uso();
        }
    }

    public void Guardar()
    {
        try
        {
            Directory.CreateDirectory(Config.Carpeta);
            File.WriteAllText(Ruta, JsonSerializer.Serialize(this, Opciones));
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[lanzador] uso.json no se pudo guardar ({ex.Message}).");
        }
    }

    /// <summary>SEGURIDAD.md §3.6, corte 4: se borra entero y de una vez.</summary>
    public static bool Olvidar()
    {
        if (!File.Exists(Ruta)) return false;
        File.Delete(Ruta);
        return true;
    }
}
