namespace QuickLook;

/// <summary>Lo que hay que enseniar de un archivo.</summary>
/// <param name="Image">Los pixeles, o null si el shell no supo dibujarlo.</param>
/// <param name="IsThumbnail">
/// true si <paramref name="Image"/> es el contenido del archivo, y false si es el icono de
/// su tipo. Decide el aspecto entero del panel: una miniatura manda sobre la caja y llena
/// el panel; un icono se queda pequenio y centrado en una ficha.
/// </param>
internal sealed record Preview(Pixels? Image, bool IsThumbnail, string Title, string Detail)
{
    /// <summary>A que tamanio se pide la miniatura. Ver la nota de ponytail en Kind.</summary>
    private const int ThumbnailSize = 1600;

    /// <summary>
    /// El icono se pide a 96 y no a 256 a proposito: por encima del tamanio de icono
    /// grande de Windows, el shell deja de servir icono y sirve miniatura, y un .txt
    /// devolveria un sello diminuto centrado en un lienzo enorme con su marco alrededor.
    /// </summary>
    private const int IconSize = 96;

    /// <summary>
    /// Que tipos tienen miniatura de verdad. Se decide por extension y no preguntandole
    /// al shell porque <c>GetImage</c> sin <c>SIIGBF_ICONONLY</c> siempre devuelve algo:
    /// para un .zip devuelve su icono disfrazado de miniatura, con marco de documento
    /// alrededor, y quedaria peor que la ficha.
    /// </summary>
    private static readonly HashSet<string> Thumbnailable = new(StringComparer.OrdinalIgnoreCase)
    {
        // Imagenes
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".tif", ".tiff", ".heic", ".avif", ".ico",
        // Video
        ".mp4", ".mkv", ".mov", ".webm", ".avi", ".m4v", ".wmv",
        // Documentos que Windows sabe rasterizar
        ".pdf", ".docx", ".xlsx", ".pptx", ".doc", ".xls", ".ppt",
    };

    /// <summary>Lo que se puede leer como texto. Lo usa M5; aqui ya decide que NO es miniatura.</summary>
    private static readonly HashSet<string> Textual = new(StringComparer.OrdinalIgnoreCase)
    {
        ".txt", ".md", ".json", ".xml", ".csv", ".log", ".ini", ".yml", ".yaml", ".toml",
        ".cs", ".js", ".ts", ".py", ".html", ".css", ".sql", ".sh", ".ps1", ".c", ".h", ".cpp", ".rs", ".go",
    };

    /// <summary>
    /// Que trato le toca a esa extension. Es logica pura y determinista: es lo que
    /// comprueba <c>--check</c>.
    /// </summary>
    public static PreviewKind Kind(string path)
    {
        string extension = System.IO.Path.GetExtension(path);
        if (Thumbnailable.Contains(extension)) return PreviewKind.Thumbnail;
        if (Textual.Contains(extension)) return PreviewKind.Text;
        return PreviewKind.Card;
    }

    /// <summary>
    /// Lo que se ve al pulsar espacio sobre <paramref name="path"/>. Nunca devuelve null
    /// y nunca lanza: en el peor caso es una ficha sin icono, pero el panel abre.
    /// </summary>
    public static Preview For(string path)
    {
        string title = System.IO.Path.GetFileName(path);
        if (string.IsNullOrEmpty(title)) title = path;

        bool thumbnail = Kind(path) == PreviewKind.Thumbnail;

        // ponytail: la miniatura del shell topa cerca de 1600px. Si una foto de 24MP se
        // ve blanda a pantalla completa, el salto es decodificar con WIC
        // (IWICImagingFactory), que ya esta en el SDK y no anade dependencia.
        Pixels? image = Shell.Image(path, thumbnail ? ThumbnailSize : IconSize, iconOnly: !thumbnail);

        // Si el shell no supo hacer la miniatura, se cae a la ficha en vez de dejar el
        // panel vacio.
        if (image is null && thumbnail)
        {
            image = Shell.Image(path, IconSize, iconOnly: true);
            thumbnail = false;
        }

        return new Preview(image, thumbnail, title, DetailOf(path));
    }

    /// <summary>La linea de debajo del nombre: tipo, tamanio y fecha.</summary>
    private static string DetailOf(string path)
    {
        try
        {
            // Directory.Exists antes que FileInfo: una carpeta tiene nombre y fecha pero
            // no tamanio, y FileInfo.Length lanzaria.
            if (Directory.Exists(path))
                return $"Carpeta  ·  {new DirectoryInfo(path).LastWriteTime:d MMM yyyy, HH:mm}";

            FileInfo info = new(path);
            if (!info.Exists) return "";

            string kind = System.IO.Path.GetExtension(path).TrimStart('.').ToUpperInvariant();
            if (kind.Length == 0) kind = "Archivo";

            return $"{kind}  ·  {Size(info.Length)}  ·  {info.LastWriteTime:d MMM yyyy, HH:mm}";
        }
        catch (Exception ex)
        {
            // Una ruta de red caida, o permisos. No es motivo para no abrir el panel.
            Console.WriteLine($"[preview] sin datos de {path}: {ex.Message}");
            return "";
        }
    }

    /// <summary>Tamanio legible. Logica pura: lo comprueba <c>--check</c>.</summary>
    internal static string Size(long bytes)
    {
        string[] units = ["B", "KB", "MB", "GB", "TB"];

        double value = bytes;
        int unit = 0;
        while (value >= 1024d && unit < units.Length - 1)
        {
            value /= 1024d;
            unit++;
        }

        // Los bytes no llevan decimales; a partir de KB, uno solo: "2,4 MB" se lee de un
        // vistazo y "2,437 MB" no.
        return unit == 0 ? $"{bytes} B" : $"{value:0.#} {units[unit]}";
    }
}

internal enum PreviewKind
{
    /// <summary>Icono grande y datos. El fallback universal.</summary>
    Card,

    /// <summary>Miniatura real del contenido, hecha por el shell.</summary>
    Thumbnail,

    /// <summary>Texto y codigo. Todavia se dibuja como ficha; es el M5.</summary>
    Text,
}
