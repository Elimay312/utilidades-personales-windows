using System.Numerics;
using System.Runtime.InteropServices;
using Windows.Graphics.DirectX;
using Windows.UI;
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

    /// <summary>
    /// Un solo Compositor y un solo device para todo el proceso, no uno por pantalla.
    ///
    /// Un Compositor admite varios DesktopWindowTarget, que es como cualquier app con
    /// varias ventanas lo hace. Cada dock creaba el suyo con su propio device D3D11
    /// hardware detras: con tres monitores eran tres adaptadores abiertos y unos 22 MB
    /// cada uno, para dibujar los mismos siete iconos.
    /// </summary>
    private static Compositor? _sharedCompositor;
    private static CompositionGraphicsDevice? _graphics;

    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _target;
    private readonly ContainerVisual _root;

    /// <summary>
    /// La ÚNICA entrada de la animación. Todo el dock son expresiones en forma
    /// cerrada sobre estas dos propiedades, así que el hilo de UI solo tiene que
    /// escribir aquí y el compositor hace el resto.
    /// </summary>
    private readonly CompositionPropertySet _props;

    private DockCurve _curve;

    /// <summary>Icono levantado ahora mismo porque se va a soltar algo encima, o -1.</summary>
    private int _dropTarget = -1;

    /// <summary>La zona de "soltar aquí para añadir". Solo se ve durante un arrastre.</summary>
    private ContainerVisual? _addZone;

    /// <summary>Etiqueta con el nombre de cada elemento. Null en los separadores.</summary>
    private readonly List<SpriteVisual?> _labels = [];

    /// <summary>Cuál se está viendo ahora mismo, o -1.</summary>
    private int _labelShown = -1;

    /// <summary>Escala del monitor, para que el texto no salga borroso a 125%.</summary>
    private float _scale = 1f;

    /// <summary>El menú del clic derecho. Se crea la primera vez que hace falta.</summary>
    private DockMenu? _menu;

    /// Un visual por ranura: icono o separador.
    private readonly List<SpriteVisual> _items = [];

    /// El punto de "app abierta" de cada ranura, o null si es un separador.
    private readonly List<SpriteVisual?> _dots = [];

    public DockVisuals(HWND hwnd)
    {
        EnsureDispatcherQueue();

        _compositor = _sharedCompositor ??= new Compositor();

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
    public CompositionSurfaceBrush CreateBitmapBrush(IconBitmap icon)
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
    /// Monta el dock entero: barra de fondo, elementos y sus puntos de estado, cada
    /// uno con sus ExpressionAnimation. A partir de aquí el hilo de UI no vuelve a
    /// tocar la geometría: solo escribe el cursor en el property set.
    /// </summary>
    /// <param name="items">
    /// Un icono por elemento, o null si esa ranura es un separador.
    /// </param>
    public void Build(
        in DockCurve curve,
        IReadOnlyList<IconBitmap?> items,
        IReadOnlyList<string> names,
        float windowWidth,
        float windowHeight,
        float padding,
        float iconSize)
    {
        _curve = curve;
        _root.Children.RemoveAll();
        _items.Clear();
        _dots.Clear();
        _labels.Clear();
        _dropTarget = -1;
        _labelShown = -1;
        _menu?.Close();

        // Los subtérminos compartidos de la curva, calculados una sola vez.
        DockExpressions.Setup(_compositor, _props, curve, windowWidth);

        float barHeight = iconSize + padding * 2f;
        float barTop = windowHeight - barHeight;
        float iconTop = windowHeight - padding - iconSize;

        BuildBar(padding, barTop, barHeight);
        BuildAddZone(padding, barTop, barHeight);

        _scale = iconSize / 48f;
        float dotSize = MathF.Max(4f, padding * 0.4f);
        float dotTop = windowHeight - padding + (padding - dotSize) * 0.5f;

        for (int i = 0; i < items.Count; i++)
        {
            float content = curve.Slot(i).ContentWidth;
            SpriteVisual visual = _compositor.CreateSpriteVisual();

            if (items[i] is IconBitmap icon)
            {
                visual.Brush = CreateBitmapBrush(icon);
                visual.Size = new Vector2(content, iconSize);
            }
            else
            {
                // Separador: una raya fina y discreta, más corta que los iconos.
                visual.Brush = _compositor.CreateColorBrush(Color.FromArgb(60, 255, 255, 255));
                visual.Size = new Vector2(content, iconSize * 0.55f);
            }

            // CenterPoint en el borde INFERIOR izquierdo: el elemento crece hacia
            // arriba y hacia la derecha desde ahí, que es como se comporta el Dock.
            visual.CenterPoint = new Vector3(0f, visual.Size.Y, 0f);

            // El rebote vive en el propio visual, no en el property set compartido:
            // cada icono salta por su cuenta. Va restando en la Y dentro de la misma
            // expresión, así no se pelea con la que ya es dueña de Offset.
            visual.Properties.InsertScalar("Bounce", 0f);

            // Y su gemelo horizontal, para arrastrar y reordenar. Mismo truco: la
            // expresión de Offset lo suma, así que moverlo no le quita la propiedad a
            // la ExpressionAnimation que gobierna la magnificación.
            visual.Properties.InsertScalar("Shift", 0f);

            float top = items[i] is null
                ? windowHeight - padding - visual.Size.Y
                : iconTop;

            ExpressionAnimation offset = _compositor.CreateExpressionAnimation(
                DockExpressions.IconOffset(curve, i, top, "I.Bounce", "I.Shift"));
            offset.SetReferenceParameter(DockExpressions.Props, _props);
            offset.SetReferenceParameter("I", visual);
            visual.StartAnimation("Offset", offset);

            Animate(visual, "Scale", DockExpressions.IconScale(curve, i));
            _root.Children.InsertAtTop(visual);
            _items.Add(visual);

            // Punto de "app abierta". Solo para iconos, y arranca invisible.
            if (items[i] is null)
            {
                _dots.Add(null);
                _labels.Add(null);
                continue;
            }

            SpriteVisual dot = _compositor.CreateSpriteVisual();
            dot.Size = new Vector2(dotSize, dotSize);
            dot.Brush = _compositor.CreateColorBrush(Color.FromArgb(235, 255, 255, 255));
            dot.Opacity = 0f;

            CompositionRoundedRectangleGeometry round = _compositor.CreateRoundedRectangleGeometry();
            round.Size = new Vector2(dotSize, dotSize);
            round.CornerRadius = new Vector2(dotSize * 0.5f);
            dot.Clip = _compositor.CreateGeometricClip(round);

            ExpressionAnimation center = _compositor.CreateExpressionAnimation(
                DockExpressions.ItemCenter(curve, i, dotSize, dotTop, "I.Shift"));
            center.SetReferenceParameter(DockExpressions.Props, _props);
            center.SetReferenceParameter("I", visual);
            dot.StartAnimation("Offset", center);
            _root.Children.InsertAtTop(dot);
            _dots.Add(dot);

            // La etiqueta con el nombre, encima del icono magnificado. Se crea una por
            // elemento en vez de una compartida que se redibuje: así la posición la
            // gobierna una expresión, como todo lo demás, y sigue al icono cuando se
            // magnifica o cuando se arrastra.
            _labels.Add(BuildLabel(curve, i, names[i], padding, iconSize, windowHeight, visual));
        }
    }

    /// <summary>
    /// El rebote al lanzar. Sube y baja un par de veces, cada vez menos, que es lo que
    /// hace el Dock. Corre entero en el compositor: el hilo de UI solo lo dispara.
    /// </summary>
    /// <param name="forever">
    /// Que siga botando hasta que se le diga que pare. Es el icono de una app que se
    /// está abriendo: macOS bota hasta que la ventana aparece, y ese es justo el aviso
    /// que hace falta cuando una app tarda cinco segundos en arrancar.
    /// </param>
    public void Bounce(int index, float height, bool forever = false)
    {
        if (index < 0 || index >= _items.Count) return;

        ScalarKeyFrameAnimation jump = _compositor.CreateScalarKeyFrameAnimation();
        jump.InsertKeyFrame(0f, 0f);
        jump.InsertKeyFrame(0.28f, height);
        jump.InsertKeyFrame(0.52f, 0f);
        jump.InsertKeyFrame(0.74f, height * 0.42f);
        jump.InsertKeyFrame(1f, 0f);
        jump.Duration = TimeSpan.FromMilliseconds(680);

        if (forever) jump.IterationBehavior = AnimationIterationBehavior.Forever;

        _items[index].Properties.StartAnimation("Bounce", jump);
    }

    /// <summary>Para el rebote y deja el icono en su sitio.</summary>
    public void StopBounce(int index)
    {
        if (index < 0 || index >= _items.Count) return;

        _items[index].Properties.StopAnimation("Bounce");
        _items[index].Properties.InsertScalar("Bounce", 0f);
    }

    /// <summary>
    /// Pone el icono donde diga, ya mismo y sin suavizar. Es el que sigue al dedo
    /// mientras se arrastra, así que igual que con el cursor de la magnificación,
    /// interpolar aquí solo añadiría retraso.
    /// </summary>
    public void SetShift(int index, float x)
    {
        if (index < 0 || index >= _items.Count) return;

        // Si venía de hacer sitio con un muelle, hay que pararlo: mientras una
        // animación posee la propiedad, escribirla no hace nada.
        // Si venía de hacer sitio con un muelle, hay que pararlo: mientras una
        // animación posee la propiedad, escribirla no hace nada.
        _items[index].Properties.StopAnimation("Shift");
        _items[index].Properties.InsertScalar("Shift", x);
    }

    /// <summary>
    /// Lleva el icono a su sitio con un muelle. Es el de los que se apartan para hacer
    /// hueco, y el de volver a cero al soltar.
    /// </summary>
    public void SpringShift(int index, float x)
    {
        if (index < 0 || index >= _items.Count) return;

        SpringScalarNaturalMotionAnimation slide = _compositor.CreateSpringScalarAnimation();
        slide.DampingRatio = 0.9f;
        slide.Period = TimeSpan.FromMilliseconds(55);
        slide.FinalValue = x;
        _items[index].Properties.StartAnimation("Shift", slide);
    }

    /// <summary>
    /// Marca el icono que está en la mano.
    ///
    /// Lo primero es subirlo al frente. El orden z de los visuals es el de inserción, o
    /// sea el orden del dock, así que un icono arrastrado hacia la derecha se metía
    /// DEBAJO de sus vecinos y desaparecía de la vista. Se vio arrastrando el Bloc de
    /// notas sobre Paint: el icono seguía ahí, con su Shift correcto, tapado.
    /// </summary>
    public void SetLifted(int index, bool lifted)
    {
        if (index < 0 || index >= _items.Count) return;

        if (lifted)
        {
            _root.Children.Remove(_items[index]);
            _root.Children.InsertAtTop(_items[index]);
        }

        ScalarKeyFrameAnimation fade = _compositor.CreateScalarKeyFrameAnimation();
        fade.InsertKeyFrame(1f, lifted ? 0.85f : 1f);
        fade.Duration = TimeSpan.FromMilliseconds(120);
        _items[index].StartAnimation("Opacity", fade);
    }

    /// <summary>
    /// La etiqueta con el nombre, colgada encima del icono. Nace invisible.
    /// </summary>
    private SpriteVisual? BuildLabel(
        in DockCurve curve, int index, string name, float padding, float iconSize, float windowHeight,
        SpriteVisual icon)
    {
        if (string.IsNullOrWhiteSpace(name)) return null;

        Vector2 size = Labels.Measure(name, _scale);

        SpriteVisual label = _compositor.CreateSpriteVisual();
        label.Size = size;
        label.Opacity = 0f;
        label.Brush = CreateLabelBrush(name, size);

        // Justo encima de donde llega el icono cuando está del todo magnificado, que es
        // el hueco que LogicalLabelRoom reserva en la ventana.
        float top = windowHeight - padding - iconSize * _curve.MaxScale - size.Y - padding * 0.4f;

        // El "I" de la expresión es el visual del ICONO, no el de la etiqueta: así
        // sigue su desplazamiento al arrastrarlo, igual que hace el puntito.
        ExpressionAnimation place = _compositor.CreateExpressionAnimation(
            DockExpressions.ItemCenter(curve, index, size.X, top, "I.Shift"));
        place.SetReferenceParameter(DockExpressions.Props, _props);
        place.SetReferenceParameter("I", icon);
        label.StartAnimation("Offset", place);

        _root.Children.InsertAtTop(label);
        return label;
    }

    /// <summary>El acrílico de la barra, para que el desplegable use el mismo material.</summary>
    public CompositionBrush CreateBackdropBrush() => CreateAcrylicBrush();

    /// <summary>La escala de este monitor, que el desplegable necesita para medir.</summary>
    public float Scale => _scale;

    public CompositionSurfaceBrush CreateLabelBrush(string name, Vector2 size)
    {
        CompositionDrawingSurface surface = EnsureGraphicsDevice().CreateDrawingSurface(
            new global::Windows.Foundation.Size(size.X, size.Y),
            DirectXPixelFormat.B8G8R8A8UIntNormalized,
            DirectXAlphaMode.Premultiplied);

        ICompositionDrawingSurfaceInterop interop = surface.As<ICompositionDrawingSurfaceInterop>();
        Guid iid = typeof(ID2D1DeviceContext).GUID;

        System.Drawing.Point offset;
        interop.BeginDraw(null, &iid, out object contextObject, &offset);
        try
        {
            var context = (ID2D1DeviceContext)contextObject;

            // Igual que con los iconos: el contexto nace con el DPI del escritorio y
            // aquí se trabaja en píxeles, no en DIPs.
            context.SetDpi(96, 96);

            D2D1_COLOR_F transparent = default;
            context.Clear(&transparent);

            Labels.Draw(context, name, _scale, size, offset);
        }
        finally
        {
            interop.EndDraw();
        }

        return _compositor.CreateSurfaceBrush(surface);
    }

    /// <summary>
    /// Enseña la etiqueta del icono que hay bajo el cursor y esconde la anterior.
    /// </summary>
    public void SetLabel(int index)
    {
        if (index == _labelShown) return;

        Fade(_labelShown, 0f);
        _labelShown = index;
        Fade(index, 1f);
    }

    private void Fade(int index, float target)
    {
        if (index < 0 || index >= _labels.Count || _labels[index] is not SpriteVisual label) return;

        ScalarKeyFrameAnimation fade = _compositor.CreateScalarKeyFrameAnimation();
        fade.InsertKeyFrame(1f, target);
        fade.Duration = TimeSpan.FromMilliseconds(target > 0f ? 140 : 90);
        label.StartAnimation("Opacity", fade);
    }

    // --- Menú del clic derecho -------------------------------------------------

    public bool MenuOpen => _menu?.IsOpen == true;

    public void OpenMenu(string[] items, float anchorX, float bottom, float left, float right,
        bool closable = false)
    {
        _menu ??= new DockMenu(_compositor, _root, EnsureGraphicsDevice());
        _menu.Open(items, anchorX, bottom, _scale, left, right, closable);
    }

    public void CloseMenu() => _menu?.Close();

    public int MenuHitTest(float x, float y) => _menu?.HitTest(x, y) ?? -1;

    public int MenuHitTestClose(float x, float y) => _menu?.HitTestClose(x, y) ?? -1;

    public void MenuHot(int index) => _menu?.SetHot(index);

    /// <summary>
    /// Enseña o esconde la zona del "+". Solo tiene sentido mientras se arrastra algo
    /// por encima: el resto del tiempo el dock no debe tener botones de más.
    /// </summary>
    public void SetAddZone(bool visible)
    {
        if (_addZone is null) return;

        ScalarKeyFrameAnimation fade = _compositor.CreateScalarKeyFrameAnimation();
        fade.InsertKeyFrame(1f, visible ? 1f : 0f);
        fade.Duration = TimeSpan.FromMilliseconds(140);
        _addZone.StartAnimation("Opacity", fade);
    }

    /// <summary>Resalta la zona del "+" cuando el cursor está justo encima.</summary>
    public void SetAddZoneHot(bool hot)
    {
        if (_addZone is null) return;

        SpringScalarNaturalMotionAnimation grow = _compositor.CreateSpringScalarAnimation();
        grow.DampingRatio = 0.7f;
        grow.Period = TimeSpan.FromMilliseconds(50);
        grow.FinalValue = hot ? 1.18f : 1f;
        _addZone.StartAnimation("Scale.X", grow);
        _addZone.StartAnimation("Scale.Y", grow);
    }

    /// <summary>
    /// El "+" al que se arrastra algo para añadirlo al dock.
    ///
    /// Un cuadrado con las esquinas redondeadas y dos barritas cruzadas. Se dibuja con
    /// visuals y no con texto porque el dock no tiene nada que renderice texto, y para
    /// un signo de más no merece la pena traerlo.
    /// </summary>
    private void BuildAddZone(float padding, float barTop, float barHeight)
    {
        float size = barHeight * 0.58f;
        float top = barTop + (barHeight - size) * 0.5f;

        ContainerVisual zone = _compositor.CreateContainerVisual();
        zone.Size = new Vector2(size, size);
        zone.CenterPoint = new Vector3(size * 0.5f, size * 0.5f, 0f);
        zone.Opacity = 0f;
        Animate(zone, "Offset", DockExpressions.AddZoneOffset(padding, size * 0.35f, top));

        // Mismo material que la barra, y por un motivo concreto: un chip blanco
        // translúcido con un "+" blanco es invisible sobre un fondo claro, y el primer
        // sitio donde se probó fue encima de una página web en blanco. Con el acrílico
        // debajo se lee sobre lo que sea, y además parece un trocito del propio dock.
        CompositionRoundedRectangleGeometry round = _compositor.CreateRoundedRectangleGeometry();
        round.Size = new Vector2(size, size);
        round.CornerRadius = new Vector2(size * 0.28f);
        zone.Clip = _compositor.CreateGeometricClip(round);

        SpriteVisual material = _compositor.CreateSpriteVisual();
        material.RelativeSizeAdjustment = Vector2.One;
        material.Brush = CreateAcrylicBrush();
        zone.Children.InsertAtBottom(material);

        SpriteVisual tint = _compositor.CreateSpriteVisual();
        tint.RelativeSizeAdjustment = Vector2.One;
        tint.Brush = _compositor.CreateColorBrush(Color.FromArgb(48, 255, 255, 255));
        zone.Children.InsertAtTop(tint);

        float thick = MathF.Max(2f, size * 0.1f);
        float arm = size * 0.46f;
        CompositionColorBrush ink = _compositor.CreateColorBrush(Color.FromArgb(255, 255, 255, 255));

        SpriteVisual across = _compositor.CreateSpriteVisual();
        across.Size = new Vector2(arm, thick);
        across.Offset = new Vector3((size - arm) * 0.5f, (size - thick) * 0.5f, 0f);
        across.Brush = ink;
        zone.Children.InsertAtTop(across);

        SpriteVisual down = _compositor.CreateSpriteVisual();
        down.Size = new Vector2(thick, arm);
        down.Offset = new Vector3((size - thick) * 0.5f, (size - arm) * 0.5f, 0f);
        down.Brush = ink;
        zone.Children.InsertAtTop(down);

        _root.Children.InsertAtTop(zone);
        _addZone = zone;
    }

    /// <summary>
    /// Levanta el icono sobre el que se va a soltar algo, y baja el anterior.
    ///
    /// Se reutiliza la propiedad del rebote en vez de inventar un resaltado nuevo: es
    /// la misma idea de "este es el que va a recibir" y no hace falta ni una expresión
    /// más. Con muelle, que es lo que distingue "se ha levantado" de "ha parpadeado".
    /// </summary>
    public void SetDropTarget(int index, float height)
    {
        if (index == _dropTarget) return;

        Lift(_dropTarget, 0f);
        _dropTarget = index;
        Lift(index, height);
    }

    private void Lift(int index, float height)
    {
        if (index < 0 || index >= _items.Count) return;

        SpringScalarNaturalMotionAnimation rise = _compositor.CreateSpringScalarAnimation();
        rise.DampingRatio = 0.7f;
        rise.Period = TimeSpan.FromMilliseconds(50);
        rise.FinalValue = height;
        _items[index].Properties.StartAnimation("Bounce", rise);
    }

    /// <summary>
    /// El "puf" de quitar un icono del dock. Se va por opacidad y no encogiéndose
    /// porque la escala la posee una ExpressionAnimation: animarla aquí le quitaría el
    /// control a la magnificación y el icono se quedaría clavado a tamaño de reposo.
    /// </summary>
    public void Puff(int index)
    {
        if (index < 0 || index >= _items.Count) return;

        ScalarKeyFrameAnimation fade = _compositor.CreateScalarKeyFrameAnimation();
        fade.InsertKeyFrame(1f, 0f);
        fade.Duration = TimeSpan.FromMilliseconds(180);
        _items[index].StartAnimation("Opacity", fade);

        if (_dots[index] is SpriteVisual dot) dot.StartAnimation("Opacity", fade);
    }

    /// <summary>Enciende o apaga los puntos de "app abierta".</summary>
    public void SetRunning(IReadOnlyList<bool> running)
    {
        for (int i = 0; i < _dots.Count && i < running.Count; i++)
        {
            if (_dots[i] is not SpriteVisual dot) continue;

            float target = running[i] ? 1f : 0f;
            if (MathF.Abs(dot.Opacity - target) < 0.01f) continue;

            ScalarKeyFrameAnimation fade = _compositor.CreateScalarKeyFrameAnimation();
            fade.InsertKeyFrame(1f, target);
            fade.Duration = TimeSpan.FromMilliseconds(180);
            dot.StartAnimation("Opacity", fade);
            dot.Opacity = target;
        }
    }

    /// <summary>
    /// La barra del dock: acrílico con esquinas redondeadas, y crece con la fila igual
    /// que en macOS.
    ///
    /// El material NO se le pide a DWM (DWMWA_SYSTEMBACKDROP_TYPE) a propósito: la
    /// ventana ocupa todo el ancho del monitor y está casi entera transparente, así que
    /// un backdrop de DWM pintaría ese rectángulo completo en vez de solo la barra.
    /// CreateHostBackdropBrush muestrea el escritorio ya desenfocado por el sistema y se
    /// aplica exactamente donde queramos.
    ///
    /// El acrílico se compone como manda la receta: backdrop desenfocado debajo y una
    /// capa de tinte translúcida encima. Sin efectos encadenados, que necesitarían Win2D
    /// y una dependencia más.
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
        // aspecto de cristal esmerilado y mantiene los iconos legibles sobre cualquier
        // cosa que haya detrás.
        SpriteVisual tint = _compositor.CreateSpriteVisual();
        tint.RelativeSizeAdjustment = Vector2.One;
        tint.Brush = _compositor.CreateColorBrush(Color.FromArgb(48, 255, 255, 255));
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
            return _compositor.CreateColorBrush(Color.FromArgb(200, 32, 32, 40));
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
    /// Índice de la ranura que hay en esa coordenada de REPOSO, o -1 si cae fuera.
    /// Se resuelve en coordenadas de reposo y no en pantalla porque ahí las ranuras
    /// son fijas: el cálculo no depende de cuánto esté magnificado el dock.
    /// </summary>
    public int HitTest(float restPosition) => _curve.SlotAt(restPosition);

    /// <summary>
    /// Suelta lo de ESTA pantalla. El Compositor y el device no: son del proceso y los
    /// comparten los demas docks, que siguen vivos.
    /// </summary>
    public void Dispose()
    {
        _target.Root = null;
        _root.Dispose();
        _target.Dispose();
    }
}
