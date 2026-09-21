using Windows.Data.Pdf;
using Windows.Graphics.Imaging;
using Windows.Storage;
using Windows.Storage.Streams;

namespace QuickLook;

/// <summary>
/// Una pagina de un PDF, rasterizada por el propio Windows. Ver SEGURIDAD.md §3.3.
///
/// <para>
/// <b>Sin dependencia nueva.</b> <c>Windows.Data.Pdf</c> viene en el SDK y es el mismo
/// renderizador que usa el visor de Edge. No hace falta PDFium ni PDFBox ni nada que anada
/// un binario propio al paquete, que es justo lo que la regla 8 no quiere.
/// </para>
///
/// <para>
/// <b>Se pide la pagina al tamano del panel</b>, no a un tamano fijo: un PDF rasterizado a
/// 300 px y luego estirado se ve como una fotocopia. Y solo la pagina visible esta en
/// memoria — un PDF de 400 paginas no puede convertirse en 400 mapas de bits.
/// </para>
/// </summary>
internal static class PdfFile
{
    /// <summary>
    /// La pagina <paramref name="index"/> a <paramref name="width"/> pixeles de ancho, y
    /// cuantas paginas tiene el documento.
    ///
    /// Devuelve <c>(null, 0)</c> si no se pudo abrir: un PDF cifrado, uno roto, o uno que
    /// otro programa tiene bloqueado. El panel cae a la ficha.
    /// </summary>
    public static (Pixels? Page, int Count) Page(string path, int index, int width)
    {
        try
        {
            // Todo el trabajo asincrono va a un hilo del pool y se espera ahi. Bloquear el
            // hilo de UI sobre un await de WinRT es como se montan los interbloqueos mas
            // tontos, y este hilo ademas es el que despacha la DispatcherQueue del
            // compositor.
            return Task.Run(async () => await RenderAsync(path, index, width)).GetAwaiter().GetResult();
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[pdf] no se pudo abrir {System.IO.Path.GetFileName(path)}: {ex.Message}");
            return (null, 0);
        }
    }

    private static async Task<(Pixels?, int)> RenderAsync(string path, int index, int width)
    {
        // ponytail: el documento se abre y se cierra en cada pagina. Pasar pagina cuesta
        // unas decenas de milisegundos de mas, pero no hay que llevar la cuenta de quien
        // es dueno de un objeto WinRT abierto entre gestos, ni cerrarlo en los cinco
        // caminos por los que el panel puede morir. Si pasar pagina se nota lento, ese es
        // el salto.
        StorageFile file = await StorageFile.GetFileFromPathAsync(path);
        PdfDocument document = await PdfDocument.LoadFromFileAsync(file);

        int count = (int)document.PageCount;
        if (count == 0) return (null, 0);

        index = Math.Clamp(index, 0, count - 1);

        using PdfPage page = document.GetPage((uint)index);
        using InMemoryRandomAccessStream stream = new();

        await page.RenderToStreamAsync(stream, new PdfPageRenderOptions
        {
            DestinationWidth = (uint)Math.Max(1, width),
        });

        BitmapDecoder decoder = await BitmapDecoder.CreateAsync(stream);

        // Premultiplicado y BGRA: es lo que espera Composition, y pedirselo al decodificador
        // sale mas barato y mas correcto que convertirlo a mano despues.
        PixelDataProvider data = await decoder.GetPixelDataAsync(
            BitmapPixelFormat.Bgra8,
            BitmapAlphaMode.Premultiplied,
            new BitmapTransform(),
            ExifOrientationMode.IgnoreExifOrientation,
            ColorManagementMode.DoNotColorManage);

        return (new Pixels((int)decoder.PixelWidth, (int)decoder.PixelHeight, data.DetachPixelData()), count);
    }
}
