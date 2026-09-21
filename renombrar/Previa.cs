using System.Text.RegularExpressions;

namespace Renombrar;

/// <summary>
/// Por que una fila no se va a renombrar, o por que si. El orden importa: se queda el
/// primero que se cumple, de peor a mejor.
/// </summary>
internal enum Estado
{
    /// <summary>Se renombra.</summary>
    Ok,
    /// <summary>El nombre nuevo es igual al viejo. No se toca.</summary>
    SinCambio,
    /// <summary>Dos ficheros del lote acabarian con el mismo nombre.</summary>
    Colision,
    /// <summary>El destino ya existe en la carpeta y no es uno de los del lote.</summary>
    YaExiste,
    /// <summary>Windows no acepta ese nombre.</summary>
    Invalido,
    /// <summary>La ruta se pasa de 260 y la original no lo hacia.</summary>
    Largo,
}

/// <summary>Lo que se sabe de un fichero antes de tocarlo. <c>Nombre</c> incluye la extension.</summary>
internal sealed record Fichero(string Ruta, string Nombre, DateTime Creacion, DateTime Modificacion);

internal sealed record Fila(Fichero Fichero, string Despues, Estado Estado, string Motivo = "")
{
    internal string Antes => Fichero.Nombre;
}

/// <summary>
/// Ficheros + reglas -> lo que va a pasar. <b>No toca el disco</b> (SEGURIDAD.md §5.1):
/// ni siquiera pregunta si un destino existe, se le pasa la lista de nombres
/// <c>ocupados</c> de la carpeta. Por eso <c>--check</c> puede comprobar los seis estados
/// sin crear un solo fichero, incluido el que depende de lo que ya hay en el disco.
/// </summary>
internal static class Previa
{
    // Nombres de dispositivo de DOS. Siguen reservados cuarenta anos despues y con
    // extension tambien: CON.txt no se puede crear. Windows devuelve un error raro en vez
    // de decirlo, asi que lo dice la previa.
    private static readonly HashSet<string> Reservados = new(StringComparer.OrdinalIgnoreCase)
    {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };

    private static readonly char[] Prohibidos = Path.GetInvalidFileNameChars();

    /// <summary>El limite clasico de ruta. Sigue vigente para cualquier programa que no active rutas largas, y este no las activa.</summary>
    internal const int RutaMaxima = 260;

    /// <param name="ocupados">Todos los nombres que ya hay en la carpeta, los del lote incluidos.</param>
    internal static List<Fila> Calcular(IReadOnlyList<Fichero> ficheros,
                                        IReadOnlyList<Regla> reglas,
                                        IReadOnlySet<string> ocupados)
    {
        List<Fila> filas = new(ficheros.Count);

        // Los que van a dejar su nombre libre. Sin esto, intercambiar A y B se marcaria
        // como "ya existe" cuando es justo el caso que Aplicar sabe resolver con un
        // temporal.
        //
        // Ojo: es "los que se mueven", no "los que estan en el lote". Con lo segundo,
        // 1.txt -> 2.txt salia Ok aunque 2.txt se quedase donde esta, y al aplicarlo
        // Windows daba el error y el lote se paraba a mitad. Lo encontro escribir el caso
        // en --check, no leer el codigo.
        HashSet<string> liberados = new(StringComparer.OrdinalIgnoreCase);

        // Cuantas veces sale cada destino: uno es normal, dos o mas es colision.
        Dictionary<string, int> destinos = new(StringComparer.OrdinalIgnoreCase);

        for (int i = 0; i < ficheros.Count; i++)
        {
            Fichero f = ficheros[i];
            Trozos t = new(Path.GetFileNameWithoutExtension(f.Nombre), Path.GetExtension(f.Nombre));
            Datos d = new(i, f.Creacion, f.Modificacion);

            string nuevo;
            try
            {
                foreach (Regla r in reglas) t = r.Aplicar(t, d);
                nuevo = t.ToString();
            }
            catch (RegexParseException e)
            {
                // La expresion la esta escribiendo el usuario: a medias es invalida casi
                // siempre. Es un estado de la fila, no una excepcion que mate la previa.
                filas.Add(new Fila(f, f.Nombre, Estado.Invalido, $"expresion regular: {e.Message}"));
                continue;
            }
            catch (RegexMatchTimeoutException)
            {
                filas.Add(new Fila(f, f.Nombre, Estado.Invalido, "la expresion regular tarda demasiado"));
                continue;
            }
            catch (FormatException e)
            {
                filas.Add(new Fila(f, f.Nombre, Estado.Invalido, $"formato de la ficha: {e.Message}"));
                continue;
            }

            filas.Add(new Fila(f, nuevo, Estado.Ok));
            if (!nuevo.Equals(f.Nombre, StringComparison.Ordinal))
            {
                destinos[nuevo] = destinos.GetValueOrDefault(nuevo) + 1;
                liberados.Add(f.Nombre);
            }
        }

        for (int i = 0; i < filas.Count; i++)
        {
            Fila fila = filas[i];
            if (fila.Estado != Estado.Ok) continue;

            string nuevo = fila.Despues;

            // SinCambio va primero, antes incluso que Invalido: si el nombre no cambia no
            // hay nada que validar, ya existe en el disco. Al reves, una carpeta con un
            // CON.pdf dentro salia entera en rojo aunque el lote no fuese a tocarlo, y
            // --previa devolvia 1 sin tener nada que decir.
            if (nuevo.Equals(fila.Antes, StringComparison.Ordinal))
            {
                filas[i] = fila with { Estado = Estado.SinCambio };
                continue;
            }

            // Y de aqui abajo, de peor a mejor. Un nombre invalido tambien puede
            // colisionar, pero lo que hay que arreglar primero es que sea imposible.
            string? mal = Invalido(nuevo);
            if (mal is not null)
                filas[i] = fila with { Estado = Estado.Invalido, Motivo = mal };
            else if (destinos.GetValueOrDefault(nuevo) > 1)
                filas[i] = fila with { Estado = Estado.Colision, Motivo = "otro fichero del lote acaba igual" };
            else if (ocupados.Contains(nuevo) && !liberados.Contains(nuevo))
                filas[i] = fila with { Estado = Estado.YaExiste, Motivo = "ya hay un fichero con ese nombre" };
            else if (DemasiadoLarga(fila.Fichero.Ruta, nuevo))
                filas[i] = fila with { Estado = Estado.Largo, Motivo = $"la ruta pasaria de {RutaMaxima} caracteres" };
        }

        return filas;
    }

    /// <summary>El motivo por el que Windows no aceptaria el nombre, o <c>null</c> si lo acepta.</summary>
    internal static string? Invalido(string nombre)
    {
        if (nombre.Trim().Length == 0) return "el nombre se queda vacio";

        int malo = nombre.IndexOfAny(Prohibidos);
        if (malo >= 0)
        {
            char c = nombre[malo];
            return char.IsControl(c)
                ? "lleva un caracter de control"
                : $"lleva un caracter que Windows no admite: {c}";
        }

        // Un punto o un espacio al final no dan error: Windows los recorta en silencio y
        // acabas con un fichero que no se llama como pediste. Peor que un error.
        if (nombre.EndsWith('.') || nombre.EndsWith(' ')) return "termina en punto o espacio";

        int corte = nombre.IndexOf('.');
        string cuerpo = corte < 0 ? nombre : nombre[..corte];
        if (Reservados.Contains(cuerpo)) return $"{cuerpo} es un nombre reservado de Windows";

        return null;
    }

    /// <summary>Si el nombre nuevo se pasa del limite de ruta y el viejo no lo hacia. Lo segundo importa: una carpeta que ya estaba por encima no es culpa del renombrado.</summary>
    internal static bool DemasiadoLarga(string ruta, string nuevo)
    {
        if (ruta.Length > RutaMaxima) return false;
        int carpeta = ruta.Length - Path.GetFileName(ruta).Length;
        return carpeta + nuevo.Length > RutaMaxima;
    }
}
