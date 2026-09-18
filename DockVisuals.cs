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

    /// <summary>
    /// La ÚNICA entrada de la animación. Todo el dock son expresiones en forma
    /// cerrada sobre estas dos propiedades, así que el hilo de UI solo tiene que
    /// escribir aquí y el compositor hace el resto.
    /// </summary>
    private readonly CompositionPropertySet _props;

    private DockCurve _curve;

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

        _props = _compositor.CreatePropertySet();
        _props.InsertScalar("C", 0f);
        _props.InsertScalar("Amount", 0f);
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
    /// Monta el dock entero: barra de fondo e iconos, cada uno con sus dos
    /// ExpressionAnimation. A partir de aquí el hilo de UI no vuelve a tocar la
    /// geometría: solo escribe el cursor en el property set.
    /// </summary>
    public void Build(
        in DockCurve curve,
        IReadOnlyList<IconBitmap> icons,
        float windowWidth,
        float windowHeight,
        float padding)
    {
        _curve = curve;
        _root.Children.RemoveAll();

        // Los subterminos compartidos de la curva, calculados una sola vez.
        DockExpressions.Setup(_compositor, _props, curve, windowWidth);

        float barHeight = curve.IconSize + padding * 2f;
        float barTop = windowHeight - barHeight;
        float iconTop = windowHeight - padding - curve.IconSize;

        BuildBar(padding, barTop, barHeight);

        for (int i = 0; i < icons.Count; i++)
        {
            SpriteVisual visual = _compositor.CreateSpriteVisual();
            visual.Brush = CreateIconBrush(icons[i]);
            visual.Size = new Vector2(curve.IconSize, curve.IconSize);

            // CenterPoint en el borde INFERIOR izquierdo: el icono crece hacia arriba
            // y hacia la derecha desde ahí, que es como se comporta el Dock.
            visual.CenterPoint = new Vector3(0f, curve.IconSize, 0f);

            Animate(visual, "Offset", DockExpressions.IconOffset(curve, i, iconTop));
            Animate(visual, "Scale", DockExpressions.IconScale(curve, i));

            _root.Children.InsertAtTop(visual);
        }
    }

    /// <summary>
    /// La barra del dock: acrílico con esquinas redondeadas, y crece con la fila
    /// igual que en macOS.
    ///
    /// El material NO se le pide a DWM (DWMWA_SYSTEMBACKDROP_TYPE) a propósito: la
    /// ventana es del tamaño MÁXIMO que puede llegar a ocupar el dock magnificado,
    /// así que un backdrop de DWM pintaría ese rectángulo entero en vez de solo la
    /// barra. CreateHostBackdropBrush muestrea el escritorio ya desenfocado por el
    /// sistema y se aplica exactamente donde queramos.
    ///
    /// El acrílico se compone como manda la receta: backdrop desenfocado debajo y una
    /// capa de tinte translúcida encima. Sin efectos encadenados, que necesitarían
    /// Win2D y una dependencia más.
    /// </summary>
    private void BuildBar(float padding, float top, float height)
    {
        ContainerVisual bar = _compositor.CreateContainerVisual();
        Animate(bar, "Offset", DockExpressions.BarOffset(padding, top));
        Animate(bar, "Size", DockExpressions.BarSize(padding, height));

        // Esquinas redondeadas recortando en el compositor, con antialiasing.
        // SetWindowRgn habría sido la otra vía, pero recorta sin suavizar y además
        // expulsa a la ventana de la categoría que DWM redondea y compone.
        CompositionRoundedRectangleGeometry corners = _compositor.CreateRoundedRectangleGeometry();
        corners.CornerRadius = new Vector2(height * 0.28f);
        // La geometría del recorte tiene su propio tamaño: sigue al de la barra.
        StartExpression(corners, "Size", DockExpressions.BarSizeOnly(padding));
        bar.Clip = _compositor.CreateGeometricClip(corners);

        // Capa 1: el escritorio desenfocado por el sistema.
        SpriteVisual material = _compositor.CreateSpriteVisual();
        material.RelativeSizeAdjustment = Vector2.One;
        material.Brush = CreateAcrylicBrush();
        bar.Children.InsertAtBottom(material);

        // Capa 2: el tinte. Sin él el acrílico es solo un desenfoque y sobre un fondo
        // oscuro queda casi negro; el tinte claro a baja opacidad es lo que da el
        // aspecto de cristal esmerilado y mantiene los iconos legibles sobre
        // cualquier cosa que haya detrás.
        SpriteVisual tint = _compositor.CreateSpriteVisual();
        tint.RelativeSizeAdjustment = Vector2.One;
        tint.Brush = _compositor.CreateColorBrush(Windows.UI.Color.FromArgb(48, 255, 255, 255));
        bar.Children.InsertAtTop(tint);

        _root.Children.InsertAtBottom(bar);
    }

    /// <summary>
    /// Acrílico, con caída a color sólido si el sistema no lo soporta. El dock sigue
    /// siendo usable en ese caso: solo se ve más plano.
    /// </summary>
    private CompositionBrush CreateAcrylicBrush()
    {
        try
        {
            return _compositor.CreateHostBackdropBrush();
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[acrilico] no disponible, se usa color sólido: {ex.Message}");
            return _compositor.CreateColorBrush(Windows.UI.Color.FromArgb(200, 32, 32, 40));
        }
    }

    private void StartExpression(CompositionObject target, string property, string expression)
    {
        ExpressionAnimation animation = _compositor.CreateExpressionAnimation(expression);
        animation.SetReferenceParameter(DockExpressions.Props, _props);
        target.StartAnimation(property, animation);
    }

    private void Animate(Visual visual, string property, string expression)
    {
        ExpressionAnimation animation = _compositor.CreateExpressionAnimation(expression);
        animation.SetReferenceParameter(DockExpressions.Props, _props);
        visual.StartAnimation(property, animation);
    }

    /// <summary>
    /// Posición del cursor, en coordenadas de reposo. Es lo único que el hilo de UI
    /// escribe por cada movimiento de ratón: un InsertScalar, sin layout ni repintado.
    /// </summary>
    public void SetCursor(float restPosition) => _props.InsertScalar("C", restPosition);

    /// <summary>
    /// Entrada y salida del hover, con un muelle para que no dé un salto.
    ///
    /// El suavizado va sobre Amount y no sobre Scale a propósito: las
    /// ExpressionAnimation ya son dueñas de Scale y Offset, y una animación implícita
    /// encima chocaría con ellas. Modulando Amount, el muelle entra dentro de la
    /// propia expresión.
    ///
    /// C no se suaviza: tiene que seguir al puntero exactamente, o el icono de debajo
    /// del cursor iría con retraso, que es peor que el jitter que quitaría.
    /// </summary>
    public void SetHover(bool hovering)
    {
        SpringScalarNaturalMotionAnimation spring = _compositor.CreateSpringScalarAnimation();
        spring.DampingRatio = 0.85f;
        spring.Period = TimeSpan.FromMilliseconds(40);
        spring.FinalValue = hovering ? 1f : 0f;
        _props.StartAnimation("Amount", spring);
    }

    /// <summary>
    /// Desliza el dock dentro o fuera de la pantalla.
    ///
    /// Se mueve el árbol de visuals, no la ventana: mover la ventana sería animar
    /// desde el hilo de UI, mientras que esto lo interpola el compositor. La ventana
    /// se queda quieta y el hit-test se encarga de que los clics la atraviesen
    /// mientras está escondida (ver DockWindow.OnHitTest).
    /// </summary>
    public void SetHidden(bool hidden, float hiddenOffset)
    {
        SpringScalarNaturalMotionAnimation slide = _compositor.CreateSpringScalarAnimation();
        slide.DampingRatio = 1f;
        slide.Period = TimeSpan.FromMilliseconds(70);
        slide.FinalValue = hidden ? hiddenOffset : 0f;
        _root.StartAnimation("Offset.Y", slide);
    }

    /// <summary>
    /// Índice del icono que hay en esa coordenada de REPOSO, o -1 si cae en un hueco.
    /// Se resuelve en coordenadas de reposo y no en pantalla porque ahí las ranuras
    /// son una rejilla regular: el cálculo es exacto y no depende de la magnificación.
    /// </summary>
    public int HitTest(float restPosition)
    {
        if (_curve.Count == 0) return -1;

        int index = (int)MathF.Floor(restPosition / _curve.SlotWidth);
        if (index < 0 || index >= _curve.Count) return -1;

        return restPosition >= _curve.RestLeft(index) && restPosition < _curve.RestRight(index)
            ? index
            : -1;
    }

    public void Dispose()
    {
        _target.Root = null;
        _root.Dispose();
        _target.Dispose();
        _compositor.Dispose();
    }
}
