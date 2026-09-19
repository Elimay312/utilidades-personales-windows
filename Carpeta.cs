using Windows.Win32;

namespace Renombrar;

/// <summary>
/// De donde salen los ficheros. Es el unico sitio que mira el disco para reunir el lote,
/// y el unico que aplica la regla 7 de SEGURIDAD.md: hay carpetas que no se renombran
/// aunque las sueltes a proposito.
/// </summary>
internal static class Carpeta
{
    /// <summary>
    /// Las carpetas que se rechazan enteras. No es una lista de sitios peligrosos: es que
    /// nadie renombra en lote dentro de System32 queriendo, asi que quien lo intente se ha
    /// equivocado de ventana y lo que toca es decirselo, no obedecer.
    /// </summary>
    private static readonly string[] Sistema =
    [
        Environment.GetFolderPath(Environment.SpecialFolder.Windows),
        Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
        Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
        Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
    ];

    /// <summary>El motivo por el que no se puede trabajar en esa carpeta, o <c>null</c> si se puede.</summary>
    internal static string? Vetada(string carpeta)
    {
        string ruta = Path.TrimEndingDirectorySeparator(Path.GetFullPath(carpeta));

        // La raiz de una unidad: ahi viven los ficheros de arranque y las carpetas del
        // sistema, y un lote con "todos los de C:\" no es una operacion que nadie quiera.
        if (Path.TrimEndingDirectorySeparator(Path.GetPathRoot(ruta) ?? "") == ruta)
            return "es la raiz de una unidad";

        foreach (string s in Sistema)
        {
            if (s.Length == 0) continue;
            string sis = Path.TrimEndingDirectorySeparator(s);
            if (ruta.Equals(sis, StringComparison.OrdinalIgnoreCase) ||
                ruta.StartsWith(sis + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                return $"esta dentro de {sis}";
        }

        return null;
    }

    /// <summary>
    /// Los ficheros directos de la carpeta, en el orden del Explorador. <b>Sin bajar a
    /// subcarpetas</b> (SEGURIDAD.md regla 5).
    /// </summary>
    internal static List<Fichero> Reunir(string carpeta)
    {
        List<Fichero> ficheros = [];
        foreach (string ruta in Directory.GetFiles(carpeta))
        {
            FileInfo i = new(ruta);

            // Ocultos y de sistema fuera: ahi viven desktop.ini y Thumbs.db, que no se ven
            // en el Explorador y que renombrar rompe la personalizacion de la carpeta. Si
            // no lo ves en la ventana de al lado, no lo renombra este programa.
            if ((i.Attributes & (FileAttributes.Hidden | FileAttributes.System)) != 0) continue;

            ficheros.Add(new Fichero(ruta, i.Name, i.CreationTime, i.LastWriteTime));
        }

        ficheros.Sort(Orden);
        return ficheros;
    }

    /// <summary>Todos los nombres que hay en la carpeta, ocultos incluidos: un destino ocupado por un fichero oculto esta igual de ocupado.</summary>
    internal static HashSet<string> Ocupados(string carpeta) =>
        new(Directory.GetFiles(carpeta).Select(Path.GetFileName)!, StringComparer.OrdinalIgnoreCase);

    /// <summary>
    /// El orden del Explorador, que no es el alfabetico: <c>foto2</c> va antes que
    /// <c>foto10</c> porque compara los numeros como numeros. Importa de verdad porque es
    /// el orden en el que la ficha <c>{n}</c> reparte la numeracion, y renumerar 100
    /// facturas en orden alfabetico las deja barajadas.
    /// </summary>
    private static readonly Comparison<Fichero> Orden =
        (a, b) => PInvoke.StrCmpLogical(a.Nombre, b.Nombre);
}
