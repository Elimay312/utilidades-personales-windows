using System.Runtime.ExceptionServices;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.UI.Shell;

namespace Lanzador;

/// <summary>Pixeles de un icono, en BGRA premultiplicado y top-down.</summary>
/// <remarks>
/// Copiado de dock/Icons.cs con los nombres traducidos. Copiado y no compartido: son repos
/// separados, y este fichero ya venia con tres trampas pagadas —el hilo STA, ICONONLY y el
/// reescalado del shell— que no se tiran a la basura para volver a tropezar con ellas.
/// </remarks>
internal sealed record Icono(int Ancho, int Alto, byte[] Bgra);

internal static class Iconos
{
    // --- cache y carga en segundo plano -------------------------------------------
    //
    // SEGURIDAD.md §3.11: viven en memoria y se pierden al cerrar. Una cache de iconos
    // en disco seria un inventario de lo que tienes.
    //
    // El valor puede ser null y eso NO es "todavia no": es "este destino no tiene
    // icono", y hace falta distinguirlo o se volveria a pedir en cada repintado.
    private static readonly Dictionary<string, Icono?> Cache = new(StringComparer.OrdinalIgnoreCase);
    private static readonly Lock Candado = new();

    /// <summary>El icono si ya esta; null si no ha llegado o si ese destino no tiene.</summary>
    public static Icono? Hay(string destino)
    {
        lock (Candado) return Cache.TryGetValue(destino, out Icono? i) ? i : null;
    }

    /// <summary>
    /// Lo pide si no estaba pedido. <paramref name="cuandoLlegue"/> se llama <b>desde el
    /// hilo de segundo plano</b>: lo unico que debe hacer es avisar a la ventana.
    /// </summary>
    public static void Pedir(string destino, Action cuandoLlegue)
    {
        lock (Candado)
        {
            if (!Cache.TryAdd(destino, null)) return;   // ya esta o ya se pidio
        }

        Task.Run(() =>
        {
            Icono? icono = null;
            try { icono = Extraer(destino); }
            catch (Exception) { /* un destino sin icono se queda con el hueco */ }

            lock (Candado) Cache[destino] = icono;
            if (icono is not null) cuandoLlegue();
        });
    }

    /// <summary>
    /// Se extrae siempre a 256 y se deja que el compositor baje-escale. Así
    /// 100/150/200% salen nítidos sin volver a extraer nada al cambiar de monitor.
    /// </summary>
    public const int LadoExtraccion = 256;

    /// <summary>
    /// A qué tamaño se vuelve a pedir cuando el fichero no da para 256. Es el tamaño de
    /// icono grande de Windows y el último que el shell sirve como icono: por encima
    /// pasa a servir miniatura, que es de donde sale el problema.
    /// </summary>
    private const int LadoDeRespaldo = 48;

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
    /// El apaño va aquí y no en los llamantes porque hay varios sitios
    /// que acaban en esta función: cargar el lanzador, abrir una carpeta y navegar dentro
    /// de ella.
    /// </para>
    /// </summary>
    public static Icono Extraer(string destino)
    {
        if (Thread.CurrentThread.GetApartmentState() == ApartmentState.STA) return ExtraerAqui(destino);

        // ponytail: un hilo por extracción. Crear un hilo son décimas de milisegundo
        // contra las decenas que tarda el shell en devolver el icono, así que no
        // compensa todavía; si algún día los iconos se extraen en caliente, un único
        // hilo STA con cola.
        Icono? icono = null;
        ExceptionDispatchInfo? error = null;

        Thread sta = new(() =>
        {
            try { icono = ExtraerAqui(destino); }
            catch (Exception ex) { error = ExceptionDispatchInfo.Capture(ex); }
        });

        sta.SetApartmentState(ApartmentState.STA);
        sta.Start();
        sta.Join();

        error?.Throw();
        return icono!;
    }

    private static unsafe Icono ExtraerAqui(string destino)
    {
        // Una URL no es un elemento del shell: SHCreateItemFromParsingName la rechaza.
        // Se le pone la cara de la app que la va a abrir, que es lo que el usuario
        // espera ver y lo que enseña el propio Windows en sus accesos directos.
        if (EsquemaDe(destino) is string scheme && QuienAbre(scheme) is string handler)
        {
            destino = handler;
        }

        Guid iid = typeof(IShellItemImageFactory).GUID;
        object item;
        fixed (char* path = destino)
        {
            PInvoke.SHCreateItemFromParsingName(new PCWSTR(path), null, &iid, out item).ThrowOnFailure();
        }

        var factory = (IShellItemImageFactory)item;
        Icono icono = Imagen(factory, LadoExtraccion);

        // El shell NO agranda un icono pequeño. Si lo más grande que trae el fichero es
        // de 32 o 48, a 256 devuelve ese mismo dibujo a tamaño nativo, centrado en el
        // lienzo y con el marco de miniatura alrededor. En el lanzador eso es un sello
        // diminuto al lado de iconos que llenan su hueco: los .url de Steam de un juego
        // viejo salen así, porque su .ico solo trae 16 y 32.
        //
        // Medido sobre 23 entradas del lanzador, lado de la caja del dibujo sobre el lienzo:
        // los sanos van del 87 % al 100 %, y los pequeños dan 14 %, 18 % y 18 %. No hay
        // nada entre medias, así que el corte a la mitad no roza ningún caso real.
        //
        // Pidiéndolo a 48 —el tamaño de icono grande de Windows, el último que el shell
        // sirve como icono y no como miniatura— sí viene escalado y sin marco: Alice
        // Madness Returns pasa de ocupar el 14 % a ocupar el 83 %.
        if (Lado(icono) * 2 < LadoExtraccion) icono = Imagen(factory, LadoDeRespaldo);

        return icono;
    }

    private static unsafe Icono Imagen(IShellItemImageFactory factory, int size)
    {
        HBITMAP hbmp;
        // ICONONLY es obligatorio: por defecto GetImage devuelve el THUMBNAIL, no el
        // icono, así que un .exe con vista previa daría la miniatura.
        // BIGGERSIZEOK deja que el shell devuelva su tamaño nativo mayor en vez de
        // estirarlo con StretchBlt, que da mala calidad.
        factory.GetImage(
            new SIZE(size, size),
            SIIGBF.SIIGBF_ICONONLY | SIIGBF.SIIGBF_BIGGERSIZEOK,
            &hbmp);

        try
        {
            return LeerPixeles(hbmp);
        }
        finally
        {
            PInvoke.DeleteObject((HGDIOBJ)(nint)hbmp);
        }
    }

    /// <summary>
    /// Lado de la caja que ocupa el dibujo dentro de su lienzo, en píxeles.
    ///
    /// Solo cuenta lo bien opaco a propósito: el marco que el shell pinta alrededor de
    /// una miniatura viene con alfa 38, y contándolo la caja saldría siempre del lienzo
    /// entero y esto no distinguiría nada.
    /// </summary>
    internal static int Lado(Icono icono)
    {
        int minX = icono.Ancho, minY = icono.Alto, maxX = -1, maxY = -1;

        for (int y = 0; y < icono.Alto; y++)
        {
            for (int x = 0; x < icono.Ancho; x++)
            {
                if (icono.Bgra[((y * icono.Ancho) + x) * 4 + 3] < 128) continue;

                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }

        return maxX < 0 ? 0 : Math.Max(maxX - minX + 1, maxY - minY + 1);
    }

    /// <summary>El esquema de una URL (<c>https</c>, <c>mailto</c>), o null si no lo es.</summary>
    private static string? EsquemaDe(string destino)
    {
        int colon = destino.IndexOf(':');
        if (colon <= 1) return null;

        string scheme = destino[..colon];
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
    private static string? QuienAbre(string scheme)
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

    private static unsafe Icono LeerPixeles(HBITMAP hbmp)
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

        Premultiplicar(pixels);
        return new Icono(width, height, pixels);
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
    internal static void Premultiplicar(byte[] bgra)
    {
        bool hace_falta = false;
        for (int i = 0; i < bgra.Length; i += 4)
        {
            byte a = bgra[i + 3];
            if (bgra[i] > a || bgra[i + 1] > a || bgra[i + 2] > a)
            {
                hace_falta = true;
                break;
            }
        }

        if (!hace_falta) return;

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
