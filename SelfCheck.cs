namespace QuickLook;

/// <summary>
/// Lo que se puede comprobar sin pantalla, sin Explorador y sin teclado. Sin framework de
/// tests a proposito: falla ruidosamente si la logica se rompe.
///
/// <para>
/// Lo que NO esta aqui, y no por pereza: el filtro del hook necesita ventanas reales con
/// foco real, y sintetizar teclas esta prohibido por la regla 12. Eso se pulsa a mano, y
/// las tres pruebas estan escritas en el README.
/// </para>
/// </summary>
internal static class SelfCheck
{
    public static void Run()
    {
        Premultiplicado();
        Encaje();
        Clasificador();
        Tamanos();
        Texto();

        Console.WriteLine();
        Console.WriteLine("[check] todo bien");
    }

    /// <summary>
    /// Premultiplicar dos veces oscurece la imagen y no da error, asi que este es el tipo
    /// de fallo que se ve meses despues y se achaca a otra cosa.
    /// </summary>
    private static void Premultiplicado()
    {
        // Alfa 128 con canales por encima del alfa: NO estaba premultiplicado.
        byte[] raw = [200, 100, 50, 128];
        Shell.Premultiply(raw);
        Assert(raw[0] == 200 * 128 / 255, $"B premultiplicado mal: {raw[0]}");
        Assert(raw[1] == 100 * 128 / 255, $"G premultiplicado mal: {raw[1]}");
        Assert(raw[2] == 50 * 128 / 255, $"R premultiplicado mal: {raw[2]}");
        Assert(raw[3] == 128, "el alfa no se toca");

        // Ya premultiplicado: ningun canal supera al alfa, asi que no se debe tocar.
        byte[] already = [50, 40, 30, 128];
        byte[] copy = (byte[])already.Clone();
        Shell.Premultiply(already);
        Assert(already.AsSpan().SequenceEqual(copy), "premultiplico algo ya premultiplicado");

        // Opaco: identidad.
        byte[] opaque = [10, 20, 30, 255];
        Shell.Premultiply(opaque);
        Assert(opaque[0] == 10 && opaque[1] == 20 && opaque[2] == 30, "toco un pixel opaco");

        Console.WriteLine("[check] premultiplicado: OK");
    }

    /// <summary>
    /// El encaje del panel. Una miniatura deformada no se reconoce, y un panel mas grande
    /// que el monitor no se puede cerrar con el raton.
    /// </summary>
    private static void Encaje()
    {
        // Area de trabajo de 2000x1000 => caja maxima de 1240x720.
        const int W = 2000, H = 1000;

        // Apaisada: la limita el ancho.
        var wide = Panel.Size(Thumb(4000, 1000), 1f, W, H);
        Assert(Math.Abs((wide.W - 32) / (float)(wide.H - 32 - 46) - 4f) < 0.05f,
            $"una miniatura 4:1 salio {wide.W}x{wide.H}, deformada");

        // Vertical: la limita el alto.
        var tall = Panel.Size(Thumb(1000, 4000), 1f, W, H);
        Assert(tall.H <= (int)(H * 0.72f), $"una miniatura vertical se salio del alto: {tall.H}");
        Assert(Math.Abs((tall.W - 32) / (float)(tall.H - 32 - 46) - 0.25f) < 0.05f,
            $"una miniatura 1:4 salio {tall.W}x{tall.H}, deformada");

        // Diminuta: NO se agranda por encima de su tamano nativo.
        var tiny = Panel.Size(Thumb(64, 64), 1f, W, H);
        Assert(tiny.W == 64 + 32, $"una miniatura de 64 se estiro a {tiny.W}");

        // Y en un monitor al 175% SI se agranda, hasta 1,75x, para verse del mismo tamano
        // aparente. Topar a 1f dejaba la imagen a tamano nativo con el marco a 1,75x: una
        // tarjeta casi cuadrada con la imagen perdida dentro. Medido en el monitor 3.
        var hidpi = Panel.Size(Thumb(256, 192), 1.75f, W, H);
        float imagenW = hidpi.W - 32f * 1.75f;
        Assert(imagenW > 256f * 1.7f, $"al 175% la imagen se quedo en {imagenW} px, sin agrandar");
        Assert(Math.Abs(imagenW / (hidpi.H - (32f + 46f) * 1.75f) - 4f / 3f) < 0.05f,
            $"al 175% la proporcion se deformo: {hidpi.W}x{hidpi.H}");

        // Un video no se topa nunca: se compone a la resolucion que se le pida.
        var video = new Preview(new Pixels(256, 192, new byte[256 * 192 * 4]), true, "v", "", null, 0, 0, "v.mp4", true);
        var lleno = Panel.Size(video, 1f, W, H);
        Assert(lleno.H >= (int)(H * 0.72f) - 1 || lleno.W >= (int)(W * 0.62f) - 1,
            $"un video de 256x192 no lleno la caja: {lleno.W}x{lleno.H}");

        // El caso feo: un monitor mas pequeno que la ficha fija. Tiene que recortarse,
        // no desbordarse.
        var small = Panel.Size(new Preview(null, false, "x", ""), 1f, 300, 200);
        Assert(small.W <= (int)(300 * 0.62f) && small.H <= (int)(200 * 0.72f),
            $"la ficha se salio de un monitor pequeno: {small.W}x{small.H}");

        // Y nunca cero o negativo, que crearia una ventana invisible imposible de cerrar.
        var absurd = Panel.Size(Thumb(4000, 4000), 1f, 10, 10);
        Assert(absurd.W >= 1 && absurd.H >= 1, $"tamano no positivo: {absurd.W}x{absurd.H}");

        Console.WriteLine("[check] encaje del panel: OK");
    }

    /// <summary>
    /// Que trato le toca a cada extension. Si esto se rompe, un .png cae a ficha y el
    /// programa parece roto sin que falle nada.
    /// </summary>
    private static void Clasificador()
    {
        Assert(Preview.Kind("foto.png") == PreviewKind.Thumbnail, "un .png tiene miniatura");
        Assert(Preview.Kind("FOTO.JPG") == PreviewKind.Thumbnail, "la extension no distingue mayusculas");
        Assert(Preview.Kind("video.mp4") == PreviewKind.Video, "un .mp4 se reproduce");
        Assert(Preview.Kind("cancion.mp3") == PreviewKind.Audio, "un .mp3 suena");
        Assert(Preview.Kind("sonido.WAV") == PreviewKind.Audio, "la extension no distingue mayusculas");
        Assert(Preview.Kind("manual.pdf") == PreviewKind.Pdf, "un .pdf va por su propio camino");
        Assert(Preview.Kind("MANUAL.PDF") == PreviewKind.Pdf, "la extension no distingue mayusculas");
        Assert(Preview.Kind("hoja.xlsx") == PreviewKind.Thumbnail, "Office sigue yendo por miniatura");
        Assert(Preview.Kind("notas.md") == PreviewKind.Text, "un .md es texto");
        Assert(Preview.Kind("Program.cs") == PreviewKind.Text, "un .cs es texto");
        Assert(Preview.Kind("cosas.zip") == PreviewKind.Card, "un .zip cae a la ficha");
        Assert(Preview.Kind("sin_extension") == PreviewKind.Card, "sin extension, ficha");
        Assert(Preview.Kind("archivo.PNG.zip") == PreviewKind.Card, "manda la ultima extension");

        Console.WriteLine("[check] clasificador de extension: OK");
    }

    private static void Tamanos()
    {
        Assert(Preview.Size(0) == "0 B", $"cero: {Preview.Size(0)}");
        Assert(Preview.Size(512) == "512 B", $"bytes: {Preview.Size(512)}");
        Assert(Preview.Size(1024).StartsWith('1') && Preview.Size(1024).EndsWith("KB"), $"1 KB: {Preview.Size(1024)}");
        Assert(Preview.Size(1024L * 1024 * 1024 * 5).EndsWith("GB"), $"5 GB: {Preview.Size(1024L * 1024 * 1024 * 5)}");

        // Los bytes nunca llevan decimales.
        Assert(!Preview.Size(999).Contains(','), $"los bytes no llevan decimales: {Preview.Size(999)}");

        Console.WriteLine("[check] tamano legible: OK");
    }

    /// <summary>
    /// Lo que decide si un archivo se dibuja como texto o cae a la ficha. Si esto se
    /// equivoca, un .png se intenta leer como texto o un .txt en UTF-16 se da por binario.
    /// </summary>
    private static void Texto()
    {
        Assert(!TextFile.IsBinary("hola mundo"u8.ToArray(), 10), "texto plano no es binario");

        // Un byte cero es la senal. Cualquier formato binario lo suelta enseguida.
        byte[] png = [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D];
        Assert(TextFile.IsBinary(png, png.Length), "un PNG es binario");

        // UTF-16 lleva ceros por todas partes: si se mirase el cero sin descartar antes su
        // BOM, un .txt del Bloc de notas caeria a la ficha. Es el caso que se escapa.
        byte[] utf16 = [0xFF, 0xFE, 0x68, 0x00, 0x6F, 0x00, 0x6C, 0x00, 0x61, 0x00];
        Assert(!TextFile.IsBinary(utf16, utf16.Length), "un .txt en UTF-16 SI es texto");
        Assert(TextFile.Bom(utf16, utf16.Length) == System.Text.Encoding.Unicode, "BOM de UTF-16 LE");

        byte[] utf8 = [0xEF, 0xBB, 0xBF, 0x68, 0x6F, 0x6C, 0x61];
        Assert(TextFile.Bom(utf8, utf8.Length) == System.Text.Encoding.UTF8, "BOM de UTF-8");
        Assert(TextFile.Bom("hola"u8.ToArray(), 4) is null, "sin BOM, null");

        // Monoespaciado solo para lo que se lee en columnas.
        Assert(Preview.IsCode("datos.json"), "un .json va monoespaciado");
        Assert(Preview.IsCode("Program.cs"), "un .cs va monoespaciado");
        Assert(!Preview.IsCode("notas.md"), "un .md es prosa, proporcional");
        Assert(!Preview.IsCode("leeme.txt"), "un .txt es prosa, proporcional");

        Console.WriteLine("[check] texto: OK");
    }

    private static Preview Thumb(int width, int height) =>
        new(new Pixels(width, height, new byte[width * height * 4]), true, "prueba", "");

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException($"self-check: {message}");
    }
}
