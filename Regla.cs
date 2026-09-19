using System.Globalization;
using System.Text.RegularExpressions;

namespace Renombrar;

/// <summary>
/// Lo que se puede hacerle a un nombre. Cinco operaciones y tres formas del mismo cambio
/// de caja, porque meter la caja en el enum sale mas barato que un campo aparte que solo
/// tiene sentido para un tipo.
///
/// <para>
/// <b>La numeracion y la fecha no son tipos de regla.</b> El primer borrador tenia
/// <c>Numerar</c> e <c>Insertar</c> por separado y cada una necesitaba su posicion, su
/// formato y su origen; con fichas dentro de <c>Plantilla</c> —<c>{n:000}</c>,
/// <c>{fecha}</c>, <c>{nombre}</c>— las cuatro se quedan en una y encima se combinan, que
/// es justo lo que pide un recibo: <c>Recibo_{fecha}_{n:000}</c>.
/// </para>
/// </summary>
internal enum Tipo
{
    /// <summary>El nombre pasa a ser la plantilla <c>A</c>, con sus fichas expandidas.</summary>
    Plantilla,
    /// <summary>Cambia <c>A</c> por <c>B</c>. Si <c>Regex</c>, <c>A</c> es una expresion y <c>B</c> puede usar <c>$1</c>.</summary>
    Reemplazar,
    /// <summary><c>Cuenta</c> caracteres a partir de <c>Desde</c> (negativo: desde el final).</summary>
    Quitar,
    Minusculas,
    Mayusculas,
    Titulo,
    /// <summary>La extension pasa a ser <c>A</c>. Vacia, se queda sin extension.</summary>
    Extension,
}

/// <summary>El nombre partido en lo que cambian las reglas y lo que solo cambia <see cref="Tipo.Extension"/>.</summary>
internal readonly record struct Trozos(string Nombre, string Ext)
{
    public override string ToString() => Nombre + Ext;
}

/// <summary>Lo que una regla sabe del fichero ademas de su nombre. Fechas, no <c>FileInfo</c>: asi el motor no toca el disco y <c>--check</c> puede comprobarlo entero sin crear nada.</summary>
internal readonly record struct Datos(int Indice, DateTime Creacion, DateTime Modificacion);

/// <summary>
/// Una regla. Un solo record plano con un <c>switch</c>, y no una jerarquia con siete
/// hijas: los presets se guardan en <c>renombrar.json</c> y una jerarquia obliga a montar
/// serializacion polimorfica con el generador, que es mas ceremonia que el <c>switch</c>.
/// Si algun dia una regla necesita estado propio, entonces se parte.
///
/// <para>Que significa cada campo depende del tipo, y esta es la tabla:</para>
/// <code>
///                A                     B          Desde        Cuenta   Regex  Modificacion
/// Plantilla      la plantilla          -          primer {n}   -        -      {fecha} usa la de modificacion
/// Reemplazar     que buscar            por que    -            -        si     -
/// Quitar         -                     -          posicion     cuantos  -      -
/// Min/May/Tit    -                     -          -            -        -      -
/// Extension      la nueva (con o sin punto)       -            -        -      -
/// </code>
/// </summary>
internal sealed record Regla(
    Tipo Tipo,
    string A = "",
    string B = "",
    int Desde = 0,
    int Cuenta = 0,
    bool Regex = false,
    bool Modificacion = false)
{
    // SEGURIDAD.md §3.5: la expresion la escribe el usuario mientras teclea, asi que
    // siempre hay un momento en que esta a medias y puede ser catastrofica. Sin timeout,
    // ese momento cuelga la ventana pensando.
    private static readonly TimeSpan Paciencia = TimeSpan.FromSeconds(1);

    // Las fichas de Plantilla: {nombre}, {n}, {fecha}, con formato opcional tras dos
    // puntos. El formato no se valida aqui: si no vale, ToString lo dice y la fila sale
    // marcada, que es donde el usuario puede verlo.
    private static readonly Regex Fichas = new(@"\{(nombre|n|fecha)(?::([^}]*))?\}",
                                               RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

    internal Trozos Aplicar(Trozos t, Datos d) => Tipo switch
    {
        Tipo.Plantilla  => t with { Nombre = Expandir(t.Nombre, d) },
        Tipo.Reemplazar => t with { Nombre = Cambiar(t.Nombre) },
        Tipo.Quitar     => t with { Nombre = Quita(t.Nombre) },
        Tipo.Minusculas => t with { Nombre = t.Nombre.ToLower(CultureInfo.CurrentCulture) },
        Tipo.Mayusculas => t with { Nombre = t.Nombre.ToUpper(CultureInfo.CurrentCulture) },

        // ToTitleCase deja "RECIBO ACME" tal cual: solo toca las palabras que ya vienen
        // en minuscula. Bajarlo antes es lo que hace que funcione sobre un nombre a gritos.
        Tipo.Titulo     => t with { Nombre = CultureInfo.CurrentCulture.TextInfo.ToTitleCase(t.Nombre.ToLower(CultureInfo.CurrentCulture)) },

        Tipo.Extension  => t with { Ext = ConPunto(A) },
        _ => t,
    };

    private string Expandir(string nombre, Datos d) => Fichas.Replace(A, f =>
    {
        string formato = f.Groups[2].Value;
        return f.Groups[1].Value.ToLowerInvariant() switch
        {
            "nombre" => nombre,
            "n" => (Desde + d.Indice).ToString(formato.Length > 0 ? formato : "0", CultureInfo.InvariantCulture),
            _ => (Modificacion ? d.Modificacion : d.Creacion)
                 .ToString(formato.Length > 0 ? formato : "yyyy-MM-dd", CultureInfo.InvariantCulture),
        };
    });

    private string Cambiar(string nombre)
    {
        if (A.Length == 0) return nombre;
        if (!Regex) return nombre.Replace(A, B, StringComparison.CurrentCultureIgnoreCase);
        return System.Text.RegularExpressions.Regex.Replace(nombre, A, B, RegexOptions.None, Paciencia);
    }

    private string Quita(string nombre)
    {
        if (Cuenta <= 0) return nombre;
        int desde = Math.Clamp(Desde < 0 ? nombre.Length + Desde : Desde, 0, nombre.Length);
        return nombre.Remove(desde, Math.Min(Cuenta, nombre.Length - desde));
    }

    private static string ConPunto(string ext) =>
        ext.Length == 0 || ext.StartsWith('.') ? ext : "." + ext;
}
