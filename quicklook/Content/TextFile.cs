using System.Text;

namespace QuickLook;

/// <summary>
/// Leer un archivo de texto para echarle un vistazo. Ver SEGURIDAD.md §3.3.
///
/// <para>
/// <b>Solo lectura, y con techo.</b> Se abre con <c>FileAccess.Read</c> y
/// <c>FileShare.ReadWrite</c> —para poder mirar un log que alguien esta escribiendo ahora
/// mismo— y se leen los primeros 256 KB y ni un byte mas. Esto es un vistazo, no un editor:
/// nadie va a leerse 40 MB en un panel que se cierra con la barra espaciadora, y cargarlos
/// en RAM para ensenar la primera pantalla seria absurdo.
/// </para>
/// </summary>
internal static class TextFile
{
    /// <summary>El techo de lectura. Ver la nota de arriba.</summary>
    private const int Limit = 256 * 1024;

    /// <summary>Cuanto se mira para decidir si esto es texto o son bytes.</summary>
    private const int Sniff = 8 * 1024;

    /// <summary>
    /// El contenido de <paramref name="path"/> como texto, o null si resulta no serlo.
    ///
    /// Devolver null no es un error: el panel cae a la ficha, que es lo que toca para un
    /// .bin al que alguien le puso extension .txt.
    /// </summary>
    public static string? Read(string path)
    {
        try
        {
            using FileStream file = new(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);

            byte[] buffer = new byte[(int)Math.Min(file.Length, Limit)];
            int read = file.ReadAtLeast(buffer, buffer.Length, throwOnEndOfStream: false);
            if (read == 0) return "";

            if (IsBinary(buffer, read)) return null;

            return Decode(buffer, read);
        }
        catch (Exception ex)
        {
            // Bloqueado por otro proceso, una ruta de red caida, permisos. El panel cae a
            // la ficha y ya esta.
            Console.WriteLine($"[texto] no se pudo leer {System.IO.Path.GetFileName(path)}: {ex.Message}");
            return null;
        }
    }

    /// <summary>
    /// Un byte cero en la primera parte del archivo. Es la senal clasica y es la correcta
    /// aqui: UTF-8 y las codificaciones de un byte no lo producen nunca, y cualquier
    /// formato binario lo suelta enseguida.
    ///
    /// <para>
    /// UTF-16 SI lleva ceros, asi que se comprueba DESPUES de descartar su BOM: un .txt
    /// guardado en UTF-16 desde el Bloc de notas es texto perfectamente legible y no puede
    /// caer a la ficha por esto. Logica pura: lo comprueba <c>--check</c>.
    /// </para>
    /// </summary>
    internal static bool IsBinary(byte[] bytes, int length)
    {
        if (Bom(bytes, length) is not null) return false;

        int end = Math.Min(length, Sniff);
        for (int i = 0; i < end; i++)
            if (bytes[i] == 0) return true;

        return false;
    }

    /// <summary>La codificacion que declara el BOM, o null si no hay BOM.</summary>
    internal static Encoding? Bom(byte[] bytes, int length)
    {
        if (length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) return Encoding.UTF8;
        if (length >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) return Encoding.Unicode;
        if (length >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) return Encoding.BigEndianUnicode;
        return null;
    }

    private static string Decode(byte[] bytes, int length)
    {
        Encoding encoding = Bom(bytes, length) ?? new UTF8Encoding(false);

        // GetString se come el BOM si esta, asi que no hay que saltarlo a mano — salvo en
        // UTF-8, donde deja el carater invisible delante y se ve como un hueco raro en la
        // primera linea.
        string text = encoding.GetString(bytes, 0, length).TrimStart('﻿');

        // El corte a 256 KB cae donde cae, casi nunca en un final de linea. Se tira la
        // ultima linea a medias, que es menos feo que ensenarla partida por la mitad.
        if (length >= Limit)
        {
            int lastBreak = text.LastIndexOf('\n');
            if (lastBreak > 0) text = text[..lastBreak];
            text += "\n\n…";
        }

        // Los retornos sueltos de los archivos viejos de Mac dejarian todo en una linea.
        return text.Replace("\r\n", "\n").Replace('\r', '\n');
    }
}
