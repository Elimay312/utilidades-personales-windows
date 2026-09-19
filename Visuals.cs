using System.Numerics;
using Windows.Graphics.DirectX;
using Windows.UI;
using Windows.UI.Composition;
using Windows.Win32;
using Windows.Win32.Graphics.Direct2D;
using Windows.Win32.Graphics.Direct2D.Common;
using Windows.Win32.Graphics.Direct3D;
using Windows.Win32.Graphics.Direct3D11;
using Windows.Win32.Graphics.Dxgi;
using Windows.Win32.Graphics.Dxgi.Common;
using Windows.Win32.System.WinRT;
using Windows.Win32.System.WinRT.Composition;
using WinRT;

namespace QuickLook;

/// <summary>
/// El compositor del proceso.
///
/// <para>
/// Es <c>Windows.UI.Composition</c> del sistema, no la del WinAppSDK: las animaciones
/// corren en el proceso de DWM, asi que son inmunes a que nuestro hilo se bloquee
/// decodificando un PDF. Ese es el argumento que decidio el stack en el dock y vale
/// igual aqui.
/// </para>
///
/// <para>
/// Un solo <c>Compositor</c> para todo el proceso. Admite varios
/// <c>DesktopWindowTarget</c>, que es como cualquier app con varias ventanas lo hace.
/// </para>
/// </summary>
internal static unsafe class Visuals
{
    // Hay que conservarlo vivo: si se recoge, el compositor se queda sin cola de
    // despacho en este hilo.
    private static object? _dispatcherQueueController;

    private static Compositor? _compositor;
    private static CompositionGraphicsDevice? _graphics;

    private const uint D3D11SdkVersion = 7;

    /// <summary>
    /// El compositor del proceso.
    ///
    /// <para>
    /// <b>Dentro de esta clase se usa <see cref="Ensure"/>, nunca esta propiedad.</b> El
    /// nombre <c>Compositor</c> designa a la vez a esta propiedad y al TIPO
    /// <c>Windows.UI.Composition.Compositor</c>, que esta importado arriba. En
    /// <c>Compositor.As&lt;ICompositorInterop&gt;()</c> el compilador lo resuelve contra
    /// el tipo, compila sin una sola advertencia, y el QI revienta en tiempo de ejecucion
    /// con <c>InvalidCastException</c> — que parece un E_NOINTERFACE del compositor y
    /// manda a buscar el fallo donde no esta. MEDIDO: exactamente el mismo codigo con una
    /// variable local por delante funciona. Desde fuera no pasa, porque se escribe
    /// <c>Visuals.Compositor</c> y ahi no hay ambiguedad.
    /// </para>
    /// </summary>
    public static Compositor Compositor => Ensure();

    private static Compositor Ensure()
    {
        EnsureDispatcherQueue();
        return _compositor ??= new Compositor();
    }

    /// <summary>
    /// Windows.UI.Composition exige una DispatcherQueue en el hilo antes de poder crear
    /// el Compositor. Una sola por proceso basta.
    /// </summary>
    private static void EnsureDispatcherQueue()
    {
        if (_dispatcherQueueController is not null) return;

        DispatcherQueueOptions options = new()
        {
            dwSize = (uint)sizeof(DispatcherQueueOptions),
            threadType = DISPATCHERQUEUE_THREAD_TYPE.DQTYPE_THREAD_CURRENT,
            // Con DQTYPE_THREAD_CURRENT la doc exige DQTAT_COM_NONE.
            apartmentType = DISPATCHERQUEUE_THREAD_APARTMENTTYPE.DQTAT_COM_NONE,
        };

        PInvoke.CreateDispatcherQueueController(options, out var controller);
        _dispatcherQueueController = controller;
    }

    /// <summary>
    /// Device de render para las superficies. Fuera de XAML no existe
    /// LoadedImageSurface, asi que hay que pasar por D3D11 -> D2D -> Composition. Se
    /// monta una vez y no se vuelve a tocar.
    /// </summary>
    private static CompositionGraphicsDevice Graphics()
    {
        if (_graphics is not null) return _graphics;

        // HARDWARE: DWM tiene que muestrear estas superficies, asi que lo prudente es
        // que esten en el mismo adaptador que el compositor. BGRA_SUPPORT es obligatorio
        // para poder interoperar con Direct2D.
        PInvoke.D3D11CreateDevice(
            null,
            D3D_DRIVER_TYPE.D3D_DRIVER_TYPE_HARDWARE,
            default,
            D3D11_CREATE_DEVICE_FLAG.D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            null,
            0,
            D3D11SdkVersion,
            out ID3D11Device d3dDevice,
            null,
            out _).ThrowOnFailure();

        PInvoke.D2D1CreateDevice((IDXGIDevice)d3dDevice, null, out ID2D1Device d2dDevice).ThrowOnFailure();

        Ensure().As<ICompositorInterop>().CreateGraphicsDevice(d2dDevice, out _graphics);
        return _graphics;
    }

    /// <summary>
    /// Una superficie del tamano pedido, dibujada por <paramref name="draw"/>.
    ///
    /// Las dos trampas estan aqui dentro y no en cada llamante: BeginDraw puede devolver
    /// un HUECO dentro de un atlas compartido, asi que hay que dibujar en el offset que
    /// indica y no en (0,0); y el contexto nace con el DPI del escritorio, asi que sin
    /// fijarlo a 96 todo saldria un 25% mas grande a 120 ppp y recortado.
    /// </summary>
    private static CompositionSurfaceBrush Surface(int width, int height, Action<ID2D1DeviceContext, System.Drawing.Point> draw)
    {
        CompositionDrawingSurface surface = Graphics().CreateDrawingSurface(
            new global::Windows.Foundation.Size(width, height),
            DirectXPixelFormat.B8G8R8A8UIntNormalized,
            DirectXAlphaMode.Premultiplied);

        ICompositionDrawingSurfaceInterop interop = surface.As<ICompositionDrawingSurfaceInterop>();
        Guid iid = typeof(ID2D1DeviceContext).GUID;

        System.Drawing.Point offset;
        interop.BeginDraw(null, &iid, out object contextObject, &offset);
        try
        {
            var context = (ID2D1DeviceContext)contextObject;
            context.SetDpi(96, 96);

            // La superficie puede venir de un atlas reutilizado: limpiarla a
            // transparente evita arrastrar los pixeles del inquilino anterior.
            D2D1_COLOR_F transparent = default;
            context.Clear(&transparent);

            draw(context, offset);
        }
        finally
        {
            interop.EndDraw();
        }

        return Ensure().CreateSurfaceBrush(surface);
    }

    /// <summary>Sube unos pixeles del shell a una superficie del compositor.</summary>
    public static CompositionSurfaceBrush CreateBitmapBrush(Pixels image) =>
        Surface(image.Width, image.Height, (context, offset) =>
        {
            D2D1_BITMAP_PROPERTIES1 properties = new()
            {
                pixelFormat = new D2D1_PIXEL_FORMAT
                {
                    format = DXGI_FORMAT.DXGI_FORMAT_B8G8R8A8_UNORM,
                    alphaMode = D2D1_ALPHA_MODE.D2D1_ALPHA_MODE_PREMULTIPLIED,
                },
                dpiX = 96,
                dpiY = 96,
            };

            fixed (byte* pixels = image.Bgra)
            {
                context.CreateBitmap(
                    new D2D_SIZE_U { width = (uint)image.Width, height = (uint)image.Height },
                    pixels,
                    (uint)(image.Width * 4),
                    properties,
                    out ID2D1Bitmap1 bitmap);

                D2D_RECT_F destination = new()
                {
                    left = offset.X,
                    top = offset.Y,
                    right = offset.X + image.Width,
                    bottom = offset.Y + image.Height,
                };

                context.DrawBitmap(
                    bitmap, &destination, 1f,
                    D2D1_BITMAP_INTERPOLATION_MODE.D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                    null);
            }
        });

    /// <summary>
    /// El titulo y el detalle del pie, en una sola superficie. Dos lineas: el nombre del
    /// archivo arriba y los datos debajo, mas apagados.
    /// </summary>
    public static CompositionSurfaceBrush CreateCaptionBrush(string title, string detail, Vector2 size, float scale)
    {
        int w = (int)MathF.Ceiling(size.X);
        int h = (int)MathF.Ceiling(size.Y);

        return Surface(w, h, (context, offset) =>
        {
            Text.Draw(context, title, 15f, scale, bold: true, size.X, offset, 0.96f);

            System.Drawing.Point below = new(offset.X, offset.Y + (int)(22f * scale));
            Text.Draw(context, detail, 12f, scale, bold: false, size.X, below, 0.62f);
        });
    }

    /// <summary>
    /// Acrilico, con caida a color solido si el sistema no lo soporta. El panel sigue
    /// siendo usable en ese caso: solo se ve mas plano.
    /// </summary>
    public static CompositionBrush CreateAcrylicBrush()
    {
        try
        {
            return Ensure().CreateHostBackdropBrush();
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[acrilico] no disponible, se usa color solido: {ex.Message}");
            return Ensure().CreateColorBrush(Color.FromArgb(220, 28, 28, 32));
        }
    }
}
