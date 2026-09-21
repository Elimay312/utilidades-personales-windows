using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.Storage.Xps;

namespace Dock;

/// <summary>
/// Un fotograma de una ventana ajena, para poder animarlo.
///
/// Sujeto a la enmienda 1 de SEGURIDAD.md: solo se captura la ventana de la app cuyo
/// icono el usuario acaba de clicar, los píxeles van directos a una superficie del
/// compositor, y no se guardan ni salen del proceso.
///
/// <para>
/// Se usa <c>PrintWindow</c> con <c>PW_RENDERFULLCONTENT</c> y no
/// Windows.Graphics.Capture, aunque la documentación diga que el flag no está
/// documentado y que las apps aceleradas dan resultados poco fiables. Se midió antes de
/// decidir: Explorador, Paint y Bloc de notas se capturan íntegros en esta máquina, y
/// dos de ellas son WinUI 3. A cambio nos ahorramos el framepool, la sesión asíncrona y
/// el borde amarillo de captura que Windows 11 dibuja alrededor de lo que se está
/// grabando.
/// </para>
///
/// Si algún día una app devuelve una captura en blanco, el dock lo detecta y se limita a
/// minimizarla sin animación. Degradar es mejor que romperse.
/// </summary>
internal static unsafe class WindowCapture
{
    /// <summary>
    /// Sin este flag, cualquier ventana acelerada por GPU devuelve un rectángulo negro.
    /// No aparece en la documentación de PrintWindow, pero es el único que funciona.
    /// </summary>
    private const PRINT_WINDOW_FLAGS RenderFullContent = (PRINT_WINDOW_FLAGS)0x00000002;

    public static IconBitmap? Capture(HWND window)
    {
        if (window.IsNull) return null;
        if (!PInvoke.GetWindowRect(window, out RECT rect)) return null;

        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        if (width <= 0 || height <= 0) return null;

        HDC screen = PInvoke.GetDC(default);
        HDC memory = PInvoke.CreateCompatibleDC(screen);
        HBITMAP bitmap = default;

        try
        {
            BITMAPINFO info = default;
            info.bmiHeader.biSize = (uint)sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            // Altura negativa: filas de arriba a abajo, que es como las quiere el
            // compositor. Si no, la ventana sale del revés.
            info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = (uint)BI_COMPRESSION.BI_RGB;

            void* bits = null;
            bitmap = PInvoke.CreateDIBSection(memory, &info, DIB_USAGE.DIB_RGB_COLORS, &bits, default, 0);
            if (bitmap.IsNull || bits is null) return null;

            PInvoke.SelectObject(memory, (HGDIOBJ)(nint)bitmap);

            if (!PInvoke.PrintWindow(window, memory, RenderFullContent)) return null;

            byte[] pixels = new byte[width * height * 4];
            new Span<byte>(bits, pixels.Length).CopyTo(pixels);

            if (LooksEmpty(pixels)) return null;

            MakeOpaque(pixels);
            return new IconBitmap(width, height, pixels);
        }
        finally
        {
            if (!bitmap.IsNull) PInvoke.DeleteObject((HGDIOBJ)(nint)bitmap);
            PInvoke.DeleteDC(memory);
            PInvoke.ReleaseDC(default, screen);
        }
    }

    /// <summary>
    /// Una captura fallida no devuelve error: devuelve un rectángulo de un solo color.
    /// Se muestrea en rejilla y si no hay variación se da por perdida.
    /// </summary>
    private static bool LooksEmpty(byte[] bgra)
    {
        if (bgra.Length < 64) return true;

        int step = Math.Max(4, (bgra.Length / 4 / 400) * 4);
        uint first = BitConverter.ToUInt32(bgra, 0);

        for (int i = step; i + 4 <= bgra.Length; i += step)
        {
            if (BitConverter.ToUInt32(bgra, i) != first) return false;
        }

        return true;
    }

    /// <summary>
    /// PrintWindow no rellena el canal alfa de forma fiable: muchas ventanas vuelven con
    /// todo a cero. Como la superficie del compositor es premultiplicada, eso se vería
    /// como nada. Una ventana es opaca, así que se fuerza — pero solo si de verdad vino
    /// vacía, para no estropear las que sí traen alfa bueno.
    /// </summary>
    private static void MakeOpaque(byte[] bgra)
    {
        for (int i = 3; i < bgra.Length; i += 4)
        {
            if (bgra[i] != 0) return;
        }

        for (int i = 3; i < bgra.Length; i += 4)
        {
            bgra[i] = 255;
        }
    }
}
