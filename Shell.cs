using System.Runtime.ExceptionServices;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.UI.Shell;

namespace QuickLook;

/// <summary>Pixeles de una imagen, en BGRA premultiplicado y top-down.</summary>
internal sealed record Pixels(int Width, int Height, byte[] Bgra);

/// <summary>
/// Pixeles de un archivo, preguntandoselos al shell. Ver SEGURIDAD.md §3.3.
///
/// <para>
/// <b>La misma llamada sirve para las dos cosas</b>, y esa es la razon de que este
/// programa no necesite un decodificador de imagenes propio.
/// <c>IShellItemImageFactory::GetImage</c> con <c>SIIGBF_ICONONLY</c> da el icono del tipo
/// de archivo; <b>sin</b> esa bandera da la MINIATURA REAL del contenido, usando los
/// handlers que el shell ya tiene registrados: imagenes, PDFs, videos y documentos de
/// Office de una sola vez.
/// </para>
///
/// <para>
/// <b>Tiene que ser STA.</b> Los handlers de miniatura del shell se registran con
/// <c>ThreadingModel=Apartment</c>; desde un hilo MTA —cualquiera del pool—
/// <c>GetImage</c> no llega a usarlos y devuelve el icono generico <b>sin fallar ni
/// avisar</b>. Esto ya estaba medido en el dock y costo encontrarlo alli: los .exe, .lnk y
/// carpetas dan byte por byte lo mismo en los dos, asi que el fallo solo aparece con los
/// archivos que de verdad importan aqui.
/// </para>
/// </summary>
internal static class Shell
{
    /// <summary>
    /// Los pixeles de <paramref name="path"/>, o null si el shell no sabe dibujarlo.
    ///
    /// Con <paramref name="iconOnly"/> se pide el icono del tipo; sin el, la miniatura
    /// del contenido.
    /// </summary>
    public static Pixels? Image(string path, int size, bool iconOnly)
    {
        if (Thread.CurrentThread.GetApartmentState() == ApartmentState.STA) return Here(path, size, iconOnly);

        // ponytail: un hilo por extraccion. Crear un hilo son decimas de milisegundo
        // contra las decenas que tarda el shell, asi que no compensa todavia; si esto
        // acaba llamandose en caliente al cambiar de seleccion, un unico hilo STA con
        // cola.
        Pixels? result = null;
        ExceptionDispatchInfo? error = null;

        Thread sta = new(() =>
        {
            try { result = Here(path, size, iconOnly); }
            catch (Exception ex) { error = ExceptionDispatchInfo.Capture(ex); }
        });

        sta.SetApartmentState(ApartmentState.STA);
        sta.Start();
        sta.Join();

        error?.Throw();
        return result;
    }

    private static unsafe Pixels? Here(string path, int size, bool iconOnly)
    {
        try
        {
            Guid iid = typeof(IShellItemImageFactory).GUID;
            object item;
            fixed (char* p = path)
            {
                PInvoke.SHCreateItemFromParsingName(new PCWSTR(p), null, &iid, out item).ThrowOnFailure();
            }

            var factory = (IShellItemImageFactory)item;

            // BIGGERSIZEOK deja que el shell devuelva su tamano nativo mayor en vez de
            // estirarlo con StretchBlt, que da mala calidad.
            SIIGBF flags = SIIGBF.SIIGBF_BIGGERSIZEOK;
            if (iconOnly) flags |= SIIGBF.SIIGBF_ICONONLY;

            HBITMAP hbmp;
            factory.GetImage(new SIZE(size, size), flags, &hbmp);

            try { return ReadPixels(hbmp); }
            finally { PInvoke.DeleteObject((HGDIOBJ)(nint)hbmp); }
        }
        catch (Exception ex)
        {
            // Un archivo sin handler de miniatura, o uno que se esta escribiendo. No es
            // un error: el panel cae a la ficha.
            Console.WriteLine($"[shell] sin imagen para {System.IO.Path.GetFileName(path)}: {ex.Message}");
            return null;
        }
    }

    private static unsafe Pixels ReadPixels(HBITMAP hbmp)
    {
        BITMAP header;
        if (PInvoke.GetObject((HGDIOBJ)(nint)hbmp, sizeof(BITMAP), &header) == 0)
            throw new InvalidOperationException("GetObject fallo sobre el HBITMAP");

        int width = header.bmWidth;
        int height = header.bmHeight;
        byte[] pixels = new byte[width * height * 4];

        BITMAPINFO bmi = default;
        bmi.bmiHeader.biSize = (uint)sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        // Altura negativa: filas de arriba a abajo, que es como las quiere Composition.
        // Si no, la imagen sale del reves.
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
                    throw new InvalidOperationException("GetDIBits fallo sobre el HBITMAP");
            }
        }
        finally
        {
            PInvoke.ReleaseDC(default, screen);
        }

        Premultiply(pixels);
        return new Pixels(width, height, pixels);
    }

    /// <summary>
    /// El HBITMAP del shell puede venir con alfa SIN premultiplicar, mientras que
    /// Composition espera PARGB. Componer sin premultiplicar deja halos negros en los
    /// bordes.
    ///
    /// Se detecta en vez de asumirlo: si algun canal de color supera al alfa, es
    /// imposible que ya estuviera premultiplicado. Premultiplicar dos veces oscureceria
    /// la imagen, asi que la comprobacion no sobra.
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
