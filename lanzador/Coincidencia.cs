using System.Globalization;
using System.Text;

namespace Lanzador;

/// <summary>
/// De donde sale cada punto. Solo se calcula para las filas que se ensenan: afinar los
/// pesos mirando el total y nada mas es adivinar.
/// </summary>
internal sealed record Desglose(int Total, int Letras, int Prefijo, int Longitud, int[] Donde);

/// <summary>
/// Una entrada y lo que puntuo para la consulta de ahora, con el total repartido: cuanto
/// puso el texto y cuanto puso lo que sueles abrir.
/// </summary>
internal sealed record Resultado(Entrada Entrada, int Puntos, int Texto, int Costumbre);

/// <summary>
/// El algoritmo. Dos pasadas, porque una sola es o lenta o tonta:
/// <list type="number">
///   <item>un filtro O(m) que descarta lo que ni siquiera contiene las letras en orden;</item>
///   <item>una puntuacion por programacion dinamica sobre los que sobreviven.</item>
/// </list>
/// </summary>
internal static class Coincidencia
{
    // Los ocho pesos, juntos y con nombre a proposito: afinarlos es lo que se hace con
    // --buscar, y vale mas verlos los ocho de golpe que repartidos por el algoritmo.
    private const int BaseLetra    = 16;  // por letra que coincide, antes de bonos
    private const int BonoInicio   = 32;  // la letra cae en la posicion 0
    private const int BonoPalabra  = 24;  // la letra cae detras de un separador
    private const int BonoMayuscula = 16; // la letra es la mayuscula de un camelCase
    private const int BonoSeguida  = 20;  // la letra va pegada a la anterior
    private const int PenaHueco    = 4;   // por cada letra saltada entre dos coincidencias
    private const int BonoPrefijo  = 64;  // el candidato empieza por la consulta entera
    private const int PenaLongitud = 1;   // por letra del candidato: a igualdad, gana el corto
    private const int PenaFichero  = 40;  // un fichero de Everything, frente a una aplicacion

    /// <summary>Lo que devuelve el nucleo cuando una letra no puede caer en esa posicion.</summary>
    private const int Imposible = int.MinValue / 2;

    public const int NoCoincide = int.MinValue;

    /// <summary>
    /// Los mejores de todo el indice. La consulta se normaliza aqui una vez, no una vez
    /// por candidato.
    /// <para>
    /// Con <paramref name="uso"/> a null se puntua solo el texto, que es como corre
    /// <c>--check</c>: asi los casos del algoritmo no cambian de resultado segun lo que
    /// hayas abierto hoy.
    /// </para>
    /// </summary>
    public static List<Resultado> Buscar(IReadOnlyList<Entrada> indice, string consulta, int cuantos,
                                         Uso? uso = null, DateTimeOffset ahora = default)
    {
        string q = Normalizar(consulta).ToLowerInvariant().Trim();
        List<Resultado> vivos = new(64);
        if (q.Length == 0) return vivos;

        string? fijado = uso?.Fijado(q);

        foreach (Entrada e in indice)
        {
            if (!Contiene(e.Buscable, q)) continue;
            int texto = Puntuar(e.Buscable, q);
            if (texto == NoCoincide) continue;

            if (e.EsFichero) texto -= PenaFichero;

            int costumbre = uso?.Refuerzo(e.Destino, ahora) ?? 0;
            if (fijado is not null && e.Destino == fijado) costumbre += Uso.BonoDeFijado;

            vivos.Add(new Resultado(e, texto + costumbre, texto, costumbre));
        }

        // A igualdad de puntos gana el nombre mas corto y luego el alfabetico: sin el
        // desempate, el orden lo decidiria el del indice, que cambia entre arranques.
        vivos.Sort((a, b) =>
        {
            int c = b.Puntos.CompareTo(a.Puntos);
            if (c != 0) return c;
            c = a.Entrada.Nombre.Length.CompareTo(b.Entrada.Nombre.Length);
            return c != 0 ? c : string.Compare(a.Entrada.Nombre, b.Entrada.Nombre, StringComparison.OrdinalIgnoreCase);
        });

        if (vivos.Count > cuantos) vivos.RemoveRange(cuantos, vivos.Count - cuantos);
        return vivos;
    }

    /// <summary>
    /// Quita los acentos y deja la caja como estaba. Lo primero hace que "configuracion"
    /// encuentre "Configuración", que escribiendo rapido en espanol es la norma; lo
    /// segundo hace falta porque el bono de camelCase necesita ver las mayusculas.
    /// </summary>
    public static string Normalizar(string texto)
    {
        // Atajo para lo que ya es ASCII, que es la mayoria del indice: Normalize asigna
        // una cadena nueva aunque no haya nada que quitar.
        bool hayQueTocar = false;
        foreach (char c in texto)
        {
            if (c > 127) { hayQueTocar = true; break; }
        }
        if (!hayQueTocar) return texto;

        string descompuesto = texto.Normalize(NormalizationForm.FormD);
        StringBuilder sb = new(descompuesto.Length);
        foreach (char c in descompuesto)
        {
            if (CharUnicodeInfo.GetUnicodeCategory(c) != UnicodeCategory.NonSpacingMark) sb.Append(c);
        }
        return sb.ToString().Normalize(NormalizationForm.FormC);
    }

    /// <summary>
    /// El filtro barato: estan todas las letras de la consulta, en orden, en el candidato?
    /// No puntua nada, solo descarta. La consulta tiene que llegar ya en minusculas.
    /// </summary>
    public static bool Contiene(string candidato, string consulta)
    {
        if (consulta.Length == 0) return true;
        if (consulta.Length > candidato.Length) return false;

        int i = 0;
        for (int j = 0; j < candidato.Length; j++)
        {
            if (char.ToLowerInvariant(candidato[j]) == consulta[i] && ++i == consulta.Length) return true;
        }
        return false;
    }

    /// <summary>
    /// Cuanto puntua el candidato para esa consulta, o <see cref="NoCoincide"/>. La
    /// consulta tiene que llegar normalizada y en minusculas.
    /// </summary>
    public static int Puntuar(string candidato, string consulta)
    {
        int letras = Nucleo(candidato, consulta, null);
        return letras == NoCoincide ? NoCoincide : letras + Extras(candidato, consulta);
    }

    /// <summary>
    /// Lo mismo, pero contando de donde sale cada punto y en que letras cayo. Cuesta la
    /// matriz entera, asi que solo se llama para las filas que se ensenan.
    /// </summary>
    public static Desglose? Explicar(string candidato, string consulta)
    {
        if (consulta.Length == 0) return null;

        int[] matriz = new int[consulta.Length * candidato.Length];
        int letras = Nucleo(candidato, consulta, matriz);
        if (letras == NoCoincide) return null;

        int prefijo = candidato.StartsWith(consulta, StringComparison.OrdinalIgnoreCase) ? BonoPrefijo : 0;
        int longitud = -PenaLongitud * candidato.Length;
        return new Desglose(letras + prefijo + longitud, letras, prefijo, longitud,
                            Camino(candidato, consulta, matriz));
    }

    private static int Extras(string candidato, string consulta)
    {
        int extra = -PenaLongitud * candidato.Length;
        if (candidato.StartsWith(consulta, StringComparison.OrdinalIgnoreCase)) extra += BonoPrefijo;
        return extra;
    }

    /// <summary>
    /// La programacion dinamica, O(n*m). <c>mejor[i][j]</c> es la mejor puntuacion de
    /// encajar las primeras i+1 letras de la consulta con la letra i cayendo justo en la
    /// posicion j del candidato.
    /// <para>
    /// Con <paramref name="matriz"/> a null se lleva solo dos filas, que es como corre en
    /// cada pulsacion. Con matriz se guarda entera para poder reconstruir el camino. Es el
    /// mismo codigo en los dos casos <b>a proposito</b>: dos implementaciones del mismo
    /// algoritmo acaban divergiendo y entonces el desglose explica algo que no paso.
    /// </para>
    /// </summary>
    private static int Nucleo(string candidato, string consulta, int[]? matriz)
    {
        int n = consulta.Length;
        int m = candidato.Length;
        if (n == 0 || n > m) return NoCoincide;

        int[] anterior = new int[m];
        int[] actual = new int[m];

        for (int i = 0; i < n; i++)
        {
            char busca = consulta[i];

            // Mejor puntuacion de la letra anterior cayendo en algun sitio k <= j-2, ya
            // descontado el hueco hasta j. Se lleva incremental porque hacerlo con un
            // bucle interno convertiria esto en O(n*m^2).
            int conHueco = Imposible;

            for (int j = 0; j < m; j++)
            {
                int previo;
                if (i == 0)
                {
                    // La primera letra puede caer donde sea: el hueco por delante no se
                    // penaliza, o buscar por el final de un nombre seria inutil.
                    previo = 0;
                }
                else if (j == 0)
                {
                    previo = Imposible;
                }
                else
                {
                    int seguida = anterior[j - 1] <= Imposible ? Imposible : anterior[j - 1] + BonoSeguida;
                    previo = Math.Max(seguida, conHueco);
                }

                actual[j] = previo <= Imposible || !Igual(candidato[j], busca)
                    ? Imposible
                    : previo + BaseLetra + Bono(candidato, j);

                if (matriz is not null) matriz[i * m + j] = actual[j];

                // Preparar conHueco para j+1: max(conHueco, anterior[j-1]) - PenaHueco.
                if (i > 0 && j >= 1)
                {
                    int mejor = Math.Max(conHueco, anterior[j - 1]);
                    conHueco = mejor <= Imposible ? Imposible : mejor - PenaHueco;
                }
            }

            (anterior, actual) = (actual, anterior);
        }

        int total = Imposible;
        for (int j = 0; j < m; j++) total = Math.Max(total, anterior[j]);
        return total <= Imposible ? NoCoincide : total;
    }

    /// <summary>Que premia caer en esta posicion del candidato.</summary>
    private static int Bono(string candidato, int j)
    {
        if (j == 0) return BonoInicio;

        char anterior = candidato[j - 1];
        if (EsSeparador(anterior)) return BonoPalabra;
        if (char.IsLower(anterior) && char.IsUpper(candidato[j])) return BonoMayuscula;
        return 0;
    }

    private static bool EsSeparador(char c) =>
        c is ' ' or '-' or '_' or '.' or '/' or '\\' or '(' or ')' or '[' or ']' or ':' or ',' or '+';

    private static bool Igual(char a, char b) => char.ToLowerInvariant(a) == b;

    /// <summary>
    /// En que letras cayo la consulta, deshaciendo la matriz desde el final. Solo para
    /// ensenarlo; no influye en la puntuacion.
    /// </summary>
    private static int[] Camino(string candidato, string consulta, int[] matriz)
    {
        int n = consulta.Length;
        int m = candidato.Length;
        int[] donde = new int[n];

        int fin = 0;
        for (int j = 1; j < m; j++)
        {
            if (matriz[(n - 1) * m + j] > matriz[(n - 1) * m + fin]) fin = j;
        }
        donde[n - 1] = fin;

        for (int i = n - 1; i > 0; i--)
        {
            int j = donde[i];
            int objetivo = matriz[i * m + j] - BaseLetra - Bono(candidato, j);

            int mejorK = -1;
            int mejorValor = Imposible;
            for (int k = 0; k < j; k++)
            {
                int v = matriz[(i - 1) * m + k];
                if (v <= Imposible) continue;
                v += k == j - 1 ? BonoSeguida : -PenaHueco * (j - 1 - k);
                if (v > mejorValor || (v == mejorValor && v == objetivo)) { mejorValor = v; mejorK = k; }
            }

            donde[i - 1] = mejorK < 0 ? 0 : mejorK;
        }

        return donde;
    }
}
