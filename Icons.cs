using System.Runtime.ExceptionServices;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.UI.Shell;

namespace Dock;

/// <summary>Píxeles de un icono, en BGRA premultiplicado y top-down.</summary>
internal sealed record IconBitmap(int Width, int Height, byte[] Bgra);

internal static class Icons
{
    /// <summary>
    /// Se extrae siempre a 256 y se deja que el compositor baje-escale. Así
    /// 100/150/200% salen nítidos sin volver a extraer nada al cambiar de monitor.
    /// </summary>
    public const int ExtractSize = 256;

    /// <summary>
    /// Extrae el icono de un ejecutable o de un elemento del shell.
    /// La doc de Microsoft avisa de que esto "can be time consuming" y que no debe
    /// hacerse en el hilo de UI: la llamada va en background.
    ///
    /// <para>
    /// <b>Y ese background tiene que ser STA.</b> Los manejadores de icono del shell se
    /// registran con <c>ThreadingModel=Apartment</c>; desde un hilo MTA —cualquiera del
    /// pool— <c>GetImage</c> no llega a usarlos y devuelve el icono genérico sin fallar
    /// ni avisar. Medido sobre el .url de un juego de Steam, misma llamada, mismo
    /// fichero: en STA salen 5553 píxeles con alfa (el icono del juego) y en MTA 35789
    /// opacos en un rectángulo vertical, que es la hoja en blanco. Los .exe, .lnk,
    /// carpetas y la papelera dan byte por byte lo mismo en los dos, y por eso el fallo
    /// tardó en verse.
    /// </para>
    ///
    /// <para>
    /// El apaño va aquí y no en los llamantes porque hay tres <c>Task.Run</c> distintos
    /// que acaban en esta función: cargar el dock, abrir una carpeta y navegar dentro
    /// de ella.
    /// </para>
    /// </summary>
    public static IconBitmap Extract(string target)
    {
        if (Thread.CurrentThread.GetApartmentState() == ApartmentState.STA) return ExtractHere(target);

        // ponytail: un hilo por extracción. Crear un hilo son décimas de milisegundo
        // contra las decenas que tarda el shell en devolver el icono, así que no
        // compensa todavía; si algún día los iconos se extraen en caliente, un único
        // hilo STA con cola.
        IconBitmap? icon = null;
        ExceptionDispatchInfo? error = null;

        Thread sta = new(() =>
        {
            try { icon = ExtractHere(target); }
            catch (Exception ex) { error = ExceptionDispatchInfo.Capture(ex); }
        });

        sta.SetApartmentState(ApartmentState.STA);
        sta.Start();
        sta.Join();

        error?.Throw();
        return icon!;
    }

    private static unsafe IconBitmap ExtractHere(string target)
    {
        // Una URL no es un elemento del shell: SHCreateItemFromParsingName la rechaza.
        // Se le pone la cara de la app que la va a abrir, que es lo que el usuario
        // espera ver y lo que enseña el propio Windows en sus accesos directos.
        if (SchemeOf(target) is string scheme && HandlerOf(scheme) is string handler)
        {
            target = handler;
        }

        Guid iid = typeof(IShellItemImageFactory).GUID;
        object item;
        fixed (char* path = target)
        {
            PInvoke.SHCreateItemFromParsingName(new PCWSTR(path), null, &iid, out item).ThrowOnFailure();
        }

        var factory = (IShellItemImageFactory)item;

        HBITMAP hbmp;
        // ICONONLY es obligatorio: por defecto GetImage devuelve el THUMBNAIL, no el
        // icono, así que un .exe con vista previa daría la miniatura.
        // BIGGERSIZEOK deja que el shell devuelva su tamaño nativo mayor en vez de
        // estirarlo con StretchBlt, que da mala calidad.
        factory.GetImage(
            new SIZE(ExtractSize, ExtractSize),
            SIIGBF.SIIGBF_ICONONLY | SIIGBF.SIIGBF_BIGGERSIZEOK,
            &hbmp);

        try
        {
            return ReadPixels(hbmp);
        }
        finally
        {
            PInvoke.DeleteObject((HGDIOBJ)(nint)hbmp);
        }
    }

    /// <summary>El esquema de una URL (<c>https</c>, <c>mailto</c>), o null si no lo es.</summary>
    private static string? SchemeOf(string target)
    {
        int colon = target.IndexOf(':');
        if (colon <= 1) return null;

        string scheme = target[..colon];
        return scheme.All(c => char.IsAsciiLetterOrDigit(c) || c is '+' or '.' or '-')
            && !scheme.Equals("shell", StringComparison.OrdinalIgnoreCase)
            ? scheme
            : null;
    }

    /// <summary>
    /// Qué ejecutable tiene asociado ese protocolo. Es LECTURA de las asociaciones del
    /// shell: no se toca ninguna, y ni siquiera se lee el registro a mano — lo contesta
    /// la API que existe para preguntarlo.
    /// </summary>
    private static string? HandlerOf(string scheme)
    {
        // Primera llamada para saber el tamaño. Un protocolo sin asociar devuelve error
        // y se descarta solo.
        uint length = 0;
        if (PInvoke.AssocQueryString(
                ASSOCF.ASSOCF_NONE, ASSOCSTR.ASSOCSTR_EXECUTABLE,
                scheme, null, default, ref length).Failed || length == 0)
        {
            return null;
        }

        Span<char> buffer = new char[length];
        if (PInvoke.AssocQueryString(
                ASSOCF.ASSOCF_NONE, ASSOCSTR.ASSOCSTR_EXECUTABLE,
                scheme, null, buffer, ref length).Failed)
        {
            return null;
        }

        string path = new string(buffer).TrimEnd('\0');
        return File.Exists(path) ? path : null;
    }

    private static unsafe IconBitmap ReadPixels(HBITMAP hbmp)
    {
        BITMAP header;
        if (PInvoke.GetObject((HGDIOBJ)(nint)hbmp, sizeof(BITMAP), &header) == 0)
            throw new InvalidOperationException("GetObject falló sobre el HBITMAP del icono");

        int width = header.bmWidth;
        int height = header.bmHeight;
        byte[] pixels = new byte[width * height * 4];

        BITMAPINFO bmi = default;
        bmi.bmiHeader.biSize = (uint)sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        // Altura negativa: filas de arriba a abajo, que es como las quiere
        // Composition. Si no, el icono sale del revés.
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = (uint)BI_COMPRESSION.BI_RGB;

        HDC screen = PInvoke.GetDC(default);
        try
        {
            fixed (byte* buffer = pixels)
            {
                if (PInvoke.GetDIBits(screen, hbmp, 0, (uint)height, buffer, &bmi, DIB_USAGE.DIB_RGB_COLORS) == 0)
                    throw new InvalidOperationException("GetDIBits falló sobre el HBITMAP del icono");
            }
        }
        finally
        {
            PInvoke.ReleaseDC(default, screen);
        }

        Premultiply(pixels);
        return new IconBitmap(width, height, pixels);
    }

    /// <summary>
    /// El HBITMAP del shell puede venir con alfa SIN premultiplicar, mientras que
    /// Composition espera PARGB. Componer sin premultiplicar deja halos negros en
    /// los bordes del icono.
    ///
    /// Se detecta en vez de asumirlo: si algún canal de color supera al alfa, es
    /// imposible que ya estuviera premultiplicado. Premultiplicar dos veces
    /// oscurecería el icono, así que la comprobación no sobra.
    /// </summary>
    internal static void Premultiply(byte[] bgra)
    {
        bool needed = false;
        for (int i = 0; i < bgra.Length; i += 4)
        {
            byte a = bgra[i + 3];
            if (bgra[i] > a || bgra[i + 1] > a || bgra[i + 2] > a)
            {
                needed = true;
                break;
            }
        }

        if (!needed) return;

        for (int i = 0; i < bgra.Length; i += 4)
        {
            int a = bgra[i + 3];
            if (a == 255) continue;
            bgra[i] = (byte)(bgra[i] * a / 255);
            bgra[i + 1] = (byte)(bgra[i + 1] * a / 255);
            bgra[i + 2] = (byte)(bgra[i + 2] * a / 255);
        }
    }
}

internal static class IconsSelfCheck
{
    /// <summary>
    /// Comprobacion minima de la logica de premultiplicado. Sin framework de tests:
    /// falla ruidosamente si la logica se rompe.
    /// </summary>
    public static void Run()
    {
        // Alfa 128 con canales por encima del alfa: NO estaba premultiplicado, hay
        // que premultiplicar.
        byte[] raw = [200, 100, 50, 128];
        Icons.Premultiply(raw);
        Assert(raw[0] == 200 * 128 / 255, $"B premultiplicado mal: {raw[0]}");
        Assert(raw[1] == 100 * 128 / 255, $"G premultiplicado mal: {raw[1]}");
        Assert(raw[2] == 50 * 128 / 255, $"R premultiplicado mal: {raw[2]}");
        Assert(raw[3] == 128, "el alfa no se toca");

        // Ya premultiplicado (ningun canal supera al alfa): no se debe tocar, o el
        // icono saldria oscurecido dos veces.
        byte[] already = [50, 40, 30, 128];
        byte[] copy = (byte[])already.Clone();
        Icons.Premultiply(already);
        Assert(already.AsSpan().SequenceEqual(copy), "premultiplico algo ya premultiplicado");

        // Opaco: identidad.
        byte[] opaque = [10, 20, 30, 255];
        Icons.Premultiply(opaque);
        Assert(opaque[0] == 10 && opaque[1] == 20 && opaque[2] == 30, "toco un pixel opaco");

        Console.WriteLine("[check] premultiplicado: OK");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException($"self-check: {message}");
    }
}
