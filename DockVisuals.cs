using System.Numerics;
using System.Runtime.InteropServices;
using Windows.Graphics.DirectX;
using Windows.UI.Composition;
using Windows.UI.Composition.Desktop;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Direct2D;
using Windows.Win32.Graphics.Direct2D.Common;
using Windows.Win32.Graphics.Direct3D;
using Windows.Win32.Graphics.Direct3D11;
using Windows.Win32.Graphics.Dxgi;
using Windows.Win32.Graphics.Dxgi.Common;
using Windows.Win32.System.WinRT;
using Windows.Win32.System.WinRT.Composition;
using WinRT;

namespace Dock;

/// <summary>
/// Árbol de visuals del dock, enganchado al HWND propio.
///
/// Se usa Windows.UI.Composition del SISTEMA (no la del WinAppSDK): las
/// animaciones corren en el proceso de DWM, así que son inmunes a que nuestro
/// hilo de UI se bloquee. Ése es el argumento que decidió el stack en la Fase 1.
/// </summary>
internal sealed unsafe class DockVisuals : IDisposable
{
    private const uint D3D11SdkVersion = 7;

    /// El controller hay que conservarlo vivo: si se recoge, el compositor se
    /// queda sin cola de despacho en este hilo.
    private static object? _dispatcherQueueController;

    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _target;
    private readonly ContainerVisual _root;

    private CompositionGraphicsDevice? _graphics;

    /// Geometría en reposo de cada icono, en píxeles físicos. M2 la sustituye por
    /// la curva de magnificación, pero el hit-test ya se apoya en ella.
    private readonly List<(float X, float Size)> _slots = [];

    public DockVisuals(HWND hwnd)
    {
        EnsureDispatcherQueue();

        _compositor = new Compositor();

        // El puente Win32 -> Composition. CsWin32 marshala el puntero COM
        // directamente al tipo proyectado.
        ICompositorDesktopInterop interop = _compositor.As<ICompositorDesktopInterop>();
        interop.CreateDesktopWindowTarget(hwnd, true, out _target);

        _root = _compositor.CreateContainerVisual();
        _root.RelativeSizeAdjustment = Vector2.One;
        _target.Root = _root;
    }

    public Compositor Compositor => _compositor;

    /// <summary>
    /// Windows.UI.Composition exige una DispatcherQueue en el hilo antes de poder
    /// crear el Compositor. Una sola por proceso basta.
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
    /// Device de render para las superficies de iconos. Fuera de XAML no existe
    /// LoadedImageSurface, así que hay que pasar por D3D11 -> D2D -> Composition.
    /// Es el bloque de interop que el plan presupuestó: se monta una vez y no se
    /// vuelve a tocar.
    /// </summary>
    private CompositionGraphicsDevice EnsureGraphicsDevice()
    {
        if (_graphics is not null) return _graphics;

        // HARDWARE: DWM tiene que muestrear estas superficies, asi que lo prudente es
        // que esten en el mismo adaptador que el compositor.
        //
        // WARP tambien funciona y gasta menos (working set privado ~13 MB frente a
        // ~22 MB), porque este device solo sube los pixeles una vez y nunca renderiza
        // un frame. Llegue a culparlo de que el dock se volviera invisible, pero la
        // causa real era otra (el z-order, ver DockWindow.EnsureTopmost) y con WARP
        // nunca se reprobo el bug aislado. Queda como posible ahorro si la memoria
        // llega a apretar; los dos valores estan muy por debajo del limite de 60 MB.
        //
        // BGRA_SUPPORT es obligatorio para poder interoperar con Direct2D.
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

        PInvoke.D2D1CreateDevice((IDXGIDevice)d3dDevice, null, out ID2D1Device d2dDevice)
            .ThrowOnFailure();

        _compositor.As<ICompositorInterop>().CreateGraphicsDevice(d2dDevice, out _graphics);
        return _graphics;
    }

    /// <summary>Sube los píxeles de un icono a una superficie del compositor.</summary>
    private CompositionSurfaceBrush CreateIconBrush(IconBitmap icon)
    {
        CompositionDrawingSurface surface = EnsureGraphicsDevice().CreateDrawingSurface(
            new global::Windows.Foundation.Size(icon.Width, icon.Height),
            DirectXPixelFormat.B8G8R8A8UIntNormalized,
            DirectXAlphaMode.Premultiplied);

        ICompositionDrawingSurfaceInterop interop = surface.As<ICompositionDrawingSurfaceInterop>();
        Guid iid = typeof(ID2D1DeviceContext).GUID;

        // BeginDraw puede devolver un hueco dentro de un atlas compartido, así que
        // hay que dibujar en el offset que indica, no en (0,0).
        System.Drawing.Point offset;
        interop.BeginDraw(null, &iid, out object contextObject, &offset);
        try
        {
            var context = (ID2D1DeviceContext)contextObject;

            // Un device context de D2D nace con el DPI del escritorio (aquí 120), y
            // DrawBitmap trabaja en DIPs. Sin fijarlo a 96 el icono se dibujaría un
            // 25% más grande de lo que mide la superficie y saldría recortado.
            context.SetDpi(96, 96);

            // La superficie puede venir de un atlas reutilizado: limpiarla a
            // transparente evita arrastrar los pixeles del inquilino anterior.
            D2D1_COLOR_F transparent = default;
            context.Clear(&transparent);

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

            fixed (byte* pixels = icon.Bgra)
            {
                context.CreateBitmap(
                    new D2D_SIZE_U { width = (uint)icon.Width, height = (uint)icon.Height },
                    pixels,
                    (uint)(icon.Width * 4),
                    properties,
                    out ID2D1Bitmap1 bitmap);

                D2D_RECT_F destination = new()
                {
                    left = offset.X,
                    top = offset.Y,
                    right = offset.X + icon.Width,
                    bottom = offset.Y + icon.Height,
                };

                context.DrawBitmap(
                    bitmap, &destination, 1f,
                    D2D1_BITMAP_INTERPOLATION_MODE.D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                    null);
            }
        }
        finally
        {
            interop.EndDraw();
        }

        CompositionSurfaceBrush brush = _compositor.CreateSurfaceBrush(surface);
        // Por defecto CompositionSurfaceBrush usa Stretch.None: dibujaria la
        // superficie de 256x256 a tamano real dentro de un visual de 60x60, o sea
        // un recorte del centro del icono. Uniform la encaja conservando el aspecto.
        brush.Stretch = CompositionStretch.Uniform;
        return brush;
    }

    /// <summary>
    /// Coloca los iconos en fila, centrados. Devuelve el ancho total en píxeles.
    /// </summary>
    public void BuildIcons(IReadOnlyList<IconBitmap> icons, float iconSize, float spacing, float windowHeight)
    {
        _root.Children.RemoveAll();
        _slots.Clear();

        // Fondo propio, pintado por Composition en vez de delegarlo al backdrop de
        // DWM. Diagnostico del bug "el dock desaparece": si esto sobrevive, el que
        // falla es DWM y no el arbol de visuals.
        SpriteVisual background = _compositor.CreateSpriteVisual();
        background.Brush = _compositor.CreateColorBrush(Windows.UI.Color.FromArgb(200, 32, 32, 40));
        background.RelativeSizeAdjustment = Vector2.One;
        _root.Children.InsertAtBottom(background);

        float x = spacing;
        float y = (windowHeight - iconSize) / 2f;

        foreach (IconBitmap icon in icons)
        {
            SpriteVisual visual = _compositor.CreateSpriteVisual();
            visual.Brush = CreateIconBrush(icon);
            visual.Size = new Vector2(iconSize, iconSize);
            visual.Offset = new Vector3(x, y, 0);
            _root.Children.InsertAtTop(visual);

            _slots.Add((x, iconSize));
            x += iconSize + spacing;
        }

    }

    /// <summary>Índice del icono bajo esa X, o -1.</summary>
    public int HitTest(float x)
    {
        for (int i = 0; i < _slots.Count; i++)
        {
            (float slotX, float size) = _slots[i];
            if (x >= slotX && x < slotX + size) return i;
        }
        return -1;
    }

    public void Dispose()
    {
        _target.Root = null;
        _root.Dispose();
        _target.Dispose();
        _compositor.Dispose();
    }
}
