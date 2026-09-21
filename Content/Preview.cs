namespace QuickLook;

/// <summary>Lo que hay que enseniar de un archivo.</summary>
/// <param name="Image">Los pixeles, o null si el shell no supo dibujarlo.</param>
/// <param name="IsThumbnail">
/// true si <paramref name="Image"/> es el contenido del archivo, y false si es el icono de
/// su tipo. Decide el aspecto entero del panel: una miniatura manda sobre la caja y llena
/// el panel; un icono se queda pequenio y centrado en una ficha.
/// </param>
/// <param name="Text">
/// El contenido, si es un archivo de texto y se pudo leer. Null en todo lo demas.
/// </param>
/// <param name="PdfPages">
/// Cuantas paginas tiene, si es un PDF. Cero en todo lo demas. Es lo que le dice al panel
/// que la rueda pasa pagina en vez de desplazar.
/// </param>
/// <param name="PdfPage">Que pagina se esta viendo, desde cero.</param>
/// <param name="Media">La ruta a reproducir, si es video o audio. Null en todo lo demas.</param>
/// <param name="IsVideo">
/// true si ademas de sonar hay algo que ver. Un audio no tiene superficie de video: se queda
/// con su caratula, que es la miniatura que el shell ya sabe sacar de las etiquetas.
/// </param>
/// <param name="SelIndex">Por cual de los seleccionados va, desde cero.</param>
/// <param name="SelCount">Cuantos hay marcados. Uno en el caso normal.</param>
internal sealed record Preview(
    Pixels? Image, bool IsThumbnail, string Title, string Detail, string? Text = null,
    int PdfPages = 0, int PdfPage = 0, string? Media = null, bool IsVideo = false,
    int SelIndex = 0, int SelCount = 1)
{
    /// <summary>
    /// El mismo contenido, pero sabiendo que es el numero <paramref name="index"/> de
    /// <paramref name="count"/> marcados.
    ///
    /// <para>
    /// <b>No toca <c>Detail</c>.</b> La primera version pegaba ahi el contador, y
    /// <c>--check</c> cazo lo que eso era: llamarla dos veces dejaba un pie con
    /// "2 de 4 · 3 de 4". El flujo de hoy no encadena dos llamadas, asi que el fallo no se
    /// veia — estaba puesto a esperar. El contador se compone al dibujar, en
    /// <see cref="Caption"/>, y asi da igual cuantas veces se llame.
    /// </para>
    /// </summary>
    public Preview WithSelection(int index, int count) =>
        count > 1
            ? this with { SelIndex = index, SelCount = count }
            : this with { SelIndex = 0, SelCount = count };

    /// <summary>El pie tal y como se dibuja: los datos del archivo y, si hay varios marcados, por cual va.</summary>
    public string Caption => SelCount > 1 ? $"{Detail}  ·  {SelIndex + 1} de {SelCount}" : Detail;

    /// <summary>A que tamanio se pide la miniatura. Ver la nota de ponytail en Kind.</summary>
    private const int ThumbnailSize = 1600;

    /// <summary>
    /// A que ancho se rasteriza una pagina de PDF. Mas que la miniatura porque lo que se
    /// mira en un PDF es texto pequenio, y ahi la resolucion se nota enseguida.
    /// </summary>
    private const int PdfWidth = 1800;

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
        // Documentos que Windows sabe rasterizar. El .pdf NO esta aqui: tiene su propio
        // camino, porque la miniatura del shell de un PDF es la portada a baja resolucion
        // y no deja pasar pagina.
        ".docx", ".xlsx", ".pptx", ".doc", ".xls", ".ppt",
    };

    /// <summary>Lo que se ve y suena.</summary>
    private static readonly HashSet<string> Video = new(StringComparer.OrdinalIgnoreCase)
    {
        ".mp4", ".mkv", ".mov", ".webm", ".avi", ".m4v", ".wmv",
    };

    /// <summary>Lo que solo suena. La caratula la saca el shell de las etiquetas.</summary>
    private static readonly HashSet<string> Audio = new(StringComparer.OrdinalIgnoreCase)
    {
        ".mp3", ".flac", ".wav", ".m4a", ".ogg", ".aac", ".wma", ".opus",
    };

    /// <summary>Lo que se puede leer como texto. Lo usa M5; aqui ya decide que NO es miniatura.</summary>
    private static readonly HashSet<string> Textual = new(StringComparer.OrdinalIgnoreCase)
    {
        ".txt", ".md", ".json", ".xml", ".csv", ".log", ".ini", ".yml", ".yaml", ".toml",
        ".cs", ".js", ".ts", ".py", ".html", ".css", ".sql", ".sh", ".ps1", ".c", ".h", ".cpp", ".rs", ".go",
    };

    /// <summary>Lo que se lee en columnas y quiere fuente monoespaciada.</summary>
    private static readonly HashSet<string> Code = new(StringComparer.OrdinalIgnoreCase)
    {
        ".json", ".xml", ".csv", ".log", ".ini", ".yml", ".yaml", ".toml",
        ".cs", ".js", ".ts", ".py", ".html", ".css", ".sql", ".sh", ".ps1", ".c", ".h", ".cpp", ".rs", ".go",
    };

    /// <summary>
    /// Si esto se dibuja monoespaciado. Un .md o un .txt son prosa y se leen mejor con la
    /// proporcional; un .json alineado con espacios se vuelve ilegible con ella.
    /// Logica pura: lo comprueba <c>--check</c>.
    /// </summary>
    public static bool IsCode(string path) => Code.Contains(System.IO.Path.GetExtension(path));

    /// <summary>
    /// Que trato le toca a esa extension. Es logica pura y determinista: es lo que
    /// comprueba <c>--check</c>.
    /// </summary>
    public static PreviewKind Kind(string path)
    {
        string extension = System.IO.Path.GetExtension(path);
        if (extension.Equals(".pdf", StringComparison.OrdinalIgnoreCase)) return PreviewKind.Pdf;
        if (Video.Contains(extension)) return PreviewKind.Video;
        if (Audio.Contains(extension)) return PreviewKind.Audio;
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

        PreviewKind kind = Kind(path);

        // El texto no pasa por el shell: se lee y se dibuja. Si resulta no ser texto
        // —bytes con extension .txt— cae a la ficha como cualquier otra cosa.
        if (kind == PreviewKind.Text && TextFile.Read(path) is string content)
            return new Preview(null, false, title, DetailOf(path), content);

        if (kind == PreviewKind.Pdf) return Pdf(path, title, 0);

        if (kind is PreviewKind.Video or PreviewKind.Audio)
        {
            bool video = kind == PreviewKind.Video;

            // La miniatura del shell hace doble trabajo aqui: da la forma de la tarjeta
            // —que si no habria que esperar a que el reproductor cargue para saberla— y es
            // lo que se ve mientras el video arranca, en vez de un rectangulo negro. Para
            // un audio es la caratula.
            Pixels? poster = Shell.Image(path, ThumbnailSize, iconOnly: false);
            poster ??= Shell.Image(path, IconSize, iconOnly: true);
            if (poster is not null) Log.Line($"[poster] {poster.Width}x{poster.Height}");

            return new Preview(poster, poster is not null && video, title, DetailOf(path),
                null, 0, 0, path, video);
        }

        bool thumbnail = kind == PreviewKind.Thumbnail;

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

    /// <summary>
    /// Una pagina concreta de un PDF. Lo usa tanto la apertura como pasar pagina con la
    /// rueda, asi que el contador del pie sale siempre del mismo sitio.
    /// </summary>
    public static Preview Pdf(string path, string title, int page)
    {
        (Pixels? image, int count) = PdfFile.Page(path, page, PdfWidth);

        // Un PDF cifrado o roto no es un error: cae a la ficha como cualquier otra cosa
        // que el sistema no sepa dibujar.
        if (image is null || count == 0)
            return new Preview(Shell.Image(path, IconSize, iconOnly: true), false, title, DetailOf(path));

        page = Math.Clamp(page, 0, count - 1);
        Log.Line($"[pdf] pagina {page + 1}/{count}");

        string detail = DetailOf(path);
        if (count > 1) detail += $"  ·  {page + 1} / {count}";

        return new Preview(image, true, title, detail, null, count, page);
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

    /// <summary>Texto y codigo, leidos y dibujados con DirectWrite.</summary>
    Text,

    /// <summary>PDF, rasterizado por Windows y con la rueda para pasar pagina.</summary>
    Pdf,

    /// <summary>Video: suena y se ve, en bucle y mudo.</summary>
    Video,

    /// <summary>Audio: suena, y se ve su caratula.</summary>
    Audio,
}
