using System.Globalization;
using System.Numerics;
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
using Windows.Win32.System.WinRT;
using Windows.Win32.System.WinRT.Composition;
using WinRT;

namespace Hud;

/// <summary>
/// La capsula y sus cuatro morphs, enganchada al HWND propio.
///
/// <para>
/// Se usa Windows.UI.Composition del SISTEMA (no la del WinAppSDK): las animaciones
/// corren en el proceso de DWM, asi que son inmunes a que nuestro hilo de UI se
/// bloquee. En un HUD eso importa mas que en el dock -- sale mientras el ordenador
/// esta ocupado, que es justo cuando nuestro hilo podria estar atascado.
/// </para>
///
/// <para>
/// <b>Todo cuelga de tres escalares</b> en un <c>CompositionPropertySet</c>, y el hilo
/// de UI no escribe nada mas:
/// <list type="bullet">
/// <item><c>V</c> — el nivel, de 0 a 1. Mueve el relleno de la barra.</item>
/// <item><c>A</c> — la apertura, de 0 a 1. Entrada y salida.</item>
/// <item><c>S</c> — el squash del tope. Reposa en 1.</item>
/// </list>
/// Las expresiones tienen un limite de longitud que el dock alcanzo dos veces; aqui
/// son cortas, pero por eso no se meten constantes calculadas dentro de ellas sin
/// mirar.
/// </para>
/// </summary>
internal sealed unsafe class HudVisuals : IDisposable
{
    private const uint D3D11SdkVersion = 7;

    /// <summary>Nombre del property set dentro de las expresiones.</summary>
    private const string P = "P";

    // --- geometria interna de la capsula, en px logicos ------------------------------
    private const float PadLateral = 22f;
    private const float LadoGlifo = 30f;
    private const float HuecoGlifoBarra = 16f;
    private const float AltoBarra = 6f;
    private const float PxGlifo = 21f;

    /// El controller hay que conservarlo vivo: si se recoge, el compositor se queda sin
    /// cola de despacho en este hilo.
    private static object? _dispatcherQueueController;

    // Un solo Compositor y un solo device para todo el proceso. Hoy hay una sola
    // ventana, pero es lo que el dock aprendio midiendo (~22 MB por adaptador D3D11
    // duplicado) y no cuesta nada heredarlo.
    private static Compositor? _sharedCompositor;
    private static CompositionGraphicsDevice? _graphics;

    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _target;
    private readonly ContainerVisual _root;
    private readonly CompositionPropertySet _props;

    private readonly ContainerVisual _capsula;
    private readonly ContainerVisual _contenido;

    /// <summary>Los cinco glifos, apilados. Solo uno esta a opacidad 1.</summary>
    private readonly SpriteVisual[] _glifos = new SpriteVisual[Glifos.Todos.Length];

    private int _glifoVisible = -1;
    private readonly float _escala;

    /// <summary>
    /// Los pinceles de los glifos, para poder soltarlos. Cada uno lleva detras una
    /// superficie de D2D, que es memoria de verdad y no la recoge el GC.
    /// </summary>
    private readonly List<CompositionSurfaceBrush> _pinceles = [];

    public HudVisuals(HWND hwnd, float escala, float anchoCapsula, float altoCapsula, float holgura)
    {
        _escala = escala;

        EnsureDispatcherQueue();
        _compositor = _sharedCompositor ??= new Compositor();

        // El puente Win32 -> Composition. CsWin32 marshala el puntero COM directamente
        // al tipo proyectado.
        ICompositorDesktopInterop interop = _compositor.As<ICompositorDesktopInterop>();
        interop.CreateDesktopWindowTarget(hwnd, true, out _target);

        _root = _compositor.CreateContainerVisual();
        _root.RelativeSizeAdjustment = Vector2.One;
        _target.Root = _root;

        _props = _compositor.CreatePropertySet();
        _props.InsertScalar("V", 0f);
        _props.InsertScalar("A", 0f);
        _props.InsertScalar("S", 1f);

        float w = S(anchoCapsula);
        float h = S(altoCapsula);
        float borde = S(holgura);

        _capsula = _compositor.CreateContainerVisual();
        _capsula.Size = new Vector2(w, h);
        // El centro del escalado es el centro de la capsula: si no, al crecer desde la
        // linea fina se abriria hacia abajo a la derecha en vez de hacia fuera.
        _capsula.CenterPoint = new Vector3(w / 2f, h / 2f, 0f);

        // Morph 1, entrada y salida. Tres cosas a la vez sobre la misma A: aparece,
        // sube 18 px, y crece desde una linea fina hasta la capsula entera.
        //
        // El squash del tope (morph 4) entra multiplicando aqui mismo, no en otra
        // animacion: Scale solo tiene un dueno, y dos animaciones sobre la misma
        // propiedad se pisan. Es lo que el dock anoto al modular Amount en vez de
        // Scale.
        Animar(_capsula, "Offset",
            $"Vector3({F(borde)}, {F(borde)} + (1 - {P}.A) * {F(S(18f))}, 0)");
        Animar(_capsula, "Scale",
            $"Vector3((0.9 + 0.1 * {P}.A) * {P}.S, (0.34 + 0.66 * {P}.A) * (2 - {P}.S), 1)");
        Animar(_capsula, "Opacity", $"{P}.A");

        // Esquinas redondeadas recortando en el compositor, con antialiasing. La otra
        // via era SetWindowRgn, que es lo que usan los vecinos, pero recorta sin
        // suavizar y ademas recorta el DIBUJO, asi que habria que acordarse de dejarle
        // sitio al squash del tope. Aqui la ventana no tiene region: los clics los
        // resuelve WS_EX_LAYERED | WS_EX_TRANSPARENT (HudWindow).
        CompositionRoundedRectangleGeometry esquinas = _compositor.CreateRoundedRectangleGeometry();
        esquinas.Size = new Vector2(w, h);
        esquinas.CornerRadius = new Vector2(h / 2f);
        _capsula.Clip = _compositor.CreateGeometricClip(esquinas);

        // Capa 1: el escritorio desenfocado por el sistema.
        SpriteVisual material = _compositor.CreateSpriteVisual();
        material.RelativeSizeAdjustment = Vector2.One;
        material.Brush = PincelAcrilico();
        _capsula.Children.InsertAtBottom(material);

        // Capa 2: el velo oscuro. Lo que tiene que leerse encima es una barra blanca
        // fina y un glifo blanco, y el escritorio de detras puede ser claro.
        SpriteVisual velo = _compositor.CreateSpriteVisual();
        velo.RelativeSizeAdjustment = Vector2.One;
        velo.Brush = _compositor.CreateColorBrush(Color.FromArgb(74, 14, 14, 18));
        _capsula.Children.InsertAtTop(velo);

        // Capa 3: el brillo del cristal, la misma receta que el dock. Con solo el velo
        // la capsula sale casi negra y parece una pastilla opaca en vez de cristal:
        // medido mirandola encima de un fondo claro. Esta capa es la que la levanta.
        SpriteVisual brillo = _compositor.CreateSpriteVisual();
        brillo.RelativeSizeAdjustment = Vector2.One;
        brillo.Brush = _compositor.CreateColorBrush(Color.FromArgb(30, 255, 255, 255));
        _capsula.Children.InsertAtTop(brillo);

        // El contenido entra un poco despues que la capsula. Sin esto, al crecer desde
        // la linea fina el glifo y la barra salen aplastados y se ve el truco.
        _contenido = _compositor.CreateContainerVisual();
        _contenido.RelativeSizeAdjustment = Vector2.One;
        Animar(_contenido, "Opacity", $"Clamp(({P}.A - 0.45) / 0.55, 0, 1)");
        _capsula.Children.InsertAtTop(_contenido);

        ConstruirGlifos(h);
        ConstruirBarra(w, h);

        _root.Children.InsertAtTop(_capsula);
    }

    private float S(float logico) => logico * _escala;

    /// <summary>
    /// Floats dentro de una expresion, siempre asi. En espanol la coma decimal rompe la
    /// expresion sin dar error: se queda quieta y a saber por que. Es la trampa que el
    /// dock dejo anotada en DockExpressions.
    /// </summary>
    private static string F(float v) => v.ToString("G7", CultureInfo.InvariantCulture);

    // --- las piezas -------------------------------------------------------------------

    private void ConstruirGlifos(float altoCapsula)
    {
        float caja = S(LadoGlifo);
        float x = S(PadLateral);
        float y = (altoCapsula - caja) / 2f;

        for (int i = 0; i < Glifos.Todos.Length; i++)
        {
            Vector2 medida = Glifos.Medir(Glifos.Todos[i], S(PxGlifo));

            CompositionSurfaceBrush pincel = PincelGlifo(Glifos.Todos[i], medida);
            _pinceles.Add(pincel);

            SpriteVisual v = _compositor.CreateSpriteVisual();
            v.Size = medida;
            v.Brush = pincel;
            // Centrado dentro de su caja: los cinco glifos no miden lo mismo, y sin
            // esto el altavoz saltaria de sitio al cambiar de ondas.
            v.Offset = new Vector3(x + (caja - medida.X) / 2f, y + (caja - medida.Y) / 2f, 0f);
            v.CenterPoint = new Vector3(medida.X / 2f, medida.Y / 2f, 0f);
            v.Opacity = 0f;

            _glifos[i] = v;
            _contenido.Children.InsertAtTop(v);
        }
    }

    private void ConstruirBarra(float anchoCapsula, float altoCapsula)
    {
        float x = S(PadLateral + LadoGlifo + HuecoGlifoBarra);
        float ancho = anchoCapsula - x - S(PadLateral);
        float alto = S(AltoBarra);
        float y = (altoCapsula - alto) / 2f;
        float radio = alto / 2f;

        SpriteVisual pista = _compositor.CreateSpriteVisual();
        pista.Size = new Vector2(ancho, alto);
        pista.Offset = new Vector3(x, y, 0f);
        pista.Brush = _compositor.CreateColorBrush(Color.FromArgb(46, 255, 255, 255));
        pista.Clip = Redondeado(new Vector2(ancho, alto), radio);
        _contenido.Children.InsertAtTop(pista);

        // Morph 2, el relleno. Su ancho es una expresion sobre V, y a V se le escribe
        // con muelle: la inercia esta en el escalar, no en un keyframe.
        SpriteVisual relleno = _compositor.CreateSpriteVisual();
        relleno.Offset = new Vector3(x, y, 0f);
        relleno.Brush = _compositor.CreateColorBrush(Color.FromArgb(242, 255, 255, 255));
        relleno.Size = new Vector2(0f, alto);

        string tamano = $"Vector2({P}.V * {F(ancho)}, {F(alto)})";
        Animar(relleno, "Size", tamano);

        // El recorte del relleno sigue a su propio tamano, no al de la pista. Asi el
        // extremo derecho va redondeado siempre, y a volumen muy bajo el relleno se
        // queda en un punto en vez de en una pestana cuadrada. La geometria clampa el
        // radio a la mitad del lado corto, asi que el degenerado sale gratis.
        CompositionRoundedRectangleGeometry g = _compositor.CreateRoundedRectangleGeometry();
        g.Size = new Vector2(0f, alto);
        g.CornerRadius = new Vector2(radio);
        ExpressionAnimation exp = _compositor.CreateExpressionAnimation(tamano);
        exp.SetReferenceParameter(P, _props);
        g.StartAnimation("Size", exp);
        relleno.Clip = _compositor.CreateGeometricClip(g);

        _contenido.Children.InsertAtTop(relleno);
    }

    private CompositionGeometricClip Redondeado(Vector2 tam, float radio)
    {
        CompositionRoundedRectangleGeometry g = _compositor.CreateRoundedRectangleGeometry();
        g.Size = tam;
        g.CornerRadius = new Vector2(radio);
        return _compositor.CreateGeometricClip(g);
    }

    // --- lo que el hilo de UI puede pedir ---------------------------------------------

    /// <summary>
    /// El nivel, de 0 a 1, con muelle. Nunca un keyframe lineal: la barra tiene que
    /// sentir inercia o parece un indicador de carga.
    /// </summary>
    public void Nivel(float v)
    {
        SpringScalarNaturalMotionAnimation m = _compositor.CreateSpringScalarAnimation();
        m.DampingRatio = 0.78f;
        m.Period = TimeSpan.FromMilliseconds(45);
        m.FinalValue = Math.Clamp(v, 0f, 1f);
        _props.StartAnimation("V", m);
    }

    /// <summary>Morph 1: abrir o cerrar la capsula.</summary>
    public void Abrir(bool abierto)
    {
        SpringScalarNaturalMotionAnimation m = _compositor.CreateSpringScalarAnimation();
        // Al abrir rebota un poco; al cerrar no, que un HUD que se va dando botes
        // llama la atencion justo cuando ya no tiene nada que decir.
        m.DampingRatio = abierto ? 0.72f : 1f;
        m.Period = TimeSpan.FromMilliseconds(abierto ? 55 : 38);
        m.FinalValue = abierto ? 1f : 0f;
        _props.StartAnimation("A", m);
    }

    /// <summary>
    /// Morph 4: el tope. Subir estando al 100% no cambia el nivel, asi que sin esto no
    /// habria ninguna respuesta a la tecla. La capsula se ensancha un 5% y vuelve.
    /// </summary>
    public void Tope()
    {
        SpringScalarNaturalMotionAnimation m = _compositor.CreateSpringScalarAnimation();
        m.InitialValue = 1.05f;
        m.FinalValue = 1f;
        m.DampingRatio = 0.35f;
        m.Period = TimeSpan.FromMilliseconds(48);
        _props.StartAnimation("S", m);
    }

    /// <summary>
    /// Morph 3: el glifo. No se cambia el icono, se transforma — el que sale se encoge
    /// y se va, el que entra llega grande y asienta con muelle. Si ya es el que toca,
    /// no se hace nada: volver a lanzar la animacion en cada pulsacion lo dejaria
    /// temblando mientras subes el volumen.
    /// </summary>
    public void Glifo(int indice)
    {
        if (indice == _glifoVisible) return;

        if (_glifoVisible >= 0) Desvanecer(_glifos[_glifoVisible]);
        Aparecer(_glifos[indice]);
        _glifoVisible = indice;
    }

    private void Desvanecer(SpriteVisual v)
    {
        ScalarKeyFrameAnimation fade = _compositor.CreateScalarKeyFrameAnimation();
        fade.InsertKeyFrame(1f, 0f);
        fade.Duration = TimeSpan.FromMilliseconds(130);
        v.StartAnimation("Opacity", fade);
        v.Opacity = 0f;

        SpringVector3NaturalMotionAnimation encoge = _compositor.CreateSpringVector3Animation();
        encoge.FinalValue = new Vector3(0.78f, 0.78f, 1f);
        encoge.DampingRatio = 1f;
        encoge.Period = TimeSpan.FromMilliseconds(40);
        v.StartAnimation("Scale", encoge);
    }

    private void Aparecer(SpriteVisual v)
    {
        ScalarKeyFrameAnimation fade = _compositor.CreateScalarKeyFrameAnimation();
        fade.InsertKeyFrame(1f, 1f);
        fade.Duration = TimeSpan.FromMilliseconds(130);
        v.StartAnimation("Opacity", fade);
        v.Opacity = 1f;

        SpringVector3NaturalMotionAnimation entra = _compositor.CreateSpringVector3Animation();
        entra.InitialValue = new Vector3(1.22f, 1.22f, 1f);
        entra.FinalValue = Vector3.One;
        entra.DampingRatio = 0.55f;
        entra.Period = TimeSpan.FromMilliseconds(45);
        v.StartAnimation("Scale", entra);
    }

    // --- fontaneria --------------------------------------------------------------------

    private void Animar(CompositionObject destino, string propiedad, string expresion)
    {
        ExpressionAnimation a = _compositor.CreateExpressionAnimation(expresion);
        a.SetReferenceParameter(P, _props);
        destino.StartAnimation(propiedad, a);
    }

    /// <summary>
    /// Acrilico, con caida a color solido si el sistema no lo soporta. El HUD sigue
    /// siendo util en ese caso: solo se ve mas plano.
    ///
    /// No se le pide a DWM con DWMWA_SYSTEMBACKDROP_TYPE a proposito: eso pintaria la
    /// ventana entera, y la nuestra es mas grande que la capsula (la holgura del
    /// squash), asi que se veria un rectangulo alrededor.
    /// </summary>
    private CompositionBrush PincelAcrilico()
    {
        try
        {
            return _compositor.CreateHostBackdropBrush();
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[hud] acrilico no disponible, color solido: {ex.Message}");
            return _compositor.CreateColorBrush(Color.FromArgb(214, 28, 28, 34));
        }
    }

    /// <summary>
    /// Sube un glifo ya pintado a una superficie del compositor.
    ///
    /// Tres trampas, las tres heredadas de los vecinos: BeginDraw devuelve un OFFSET
    /// porque la superficie puede ser un hueco dentro de un atlas compartido; el
    /// contexto de D2D nace con el DPI del escritorio y hay que fijarlo a 96 o todo
    /// sale mas grande que la superficie; y hay que limpiarla, porque ese hueco puede
    /// traer los pixeles del inquilino anterior.
    /// </summary>
    private CompositionSurfaceBrush PincelGlifo(string glifo, Vector2 tam)
    {
        CompositionDrawingSurface superficie = EnsureGraphicsDevice().CreateDrawingSurface(
            new global::Windows.Foundation.Size(tam.X, tam.Y),
            DirectXPixelFormat.B8G8R8A8UIntNormalized,
            DirectXAlphaMode.Premultiplied);

        ICompositionDrawingSurfaceInterop interop = superficie.As<ICompositionDrawingSurfaceInterop>();
        Guid iid = typeof(ID2D1DeviceContext).GUID;

        System.Drawing.Point offset;
        interop.BeginDraw(null, &iid, out object obj, &offset);
        try
        {
            var ctx = (ID2D1DeviceContext)obj;
            ctx.SetDpi(96, 96);
            D2D1_COLOR_F nada = default;
            ctx.Clear(&nada);
            Glifos.Dibujar(ctx, glifo, S(PxGlifo), offset);
        }
        finally
        {
            interop.EndDraw();
        }

        return _compositor.CreateSurfaceBrush(superficie);
    }

    /// <summary>
    /// Windows.UI.Composition exige una DispatcherQueue en el hilo antes de poder crear
    /// el Compositor. Una sola por proceso basta.
    /// </summary>
    private static void EnsureDispatcherQueue()
    {
        if (_dispatcherQueueController is not null) return;

        DispatcherQueueOptions opciones = new()
        {
            dwSize = (uint)sizeof(DispatcherQueueOptions),
            threadType = DISPATCHERQUEUE_THREAD_TYPE.DQTYPE_THREAD_CURRENT,
            // Con DQTYPE_THREAD_CURRENT la doc exige DQTAT_COM_NONE.
            apartmentType = DISPATCHERQUEUE_THREAD_APARTMENTTYPE.DQTAT_COM_NONE,
        };

        PInvoke.CreateDispatcherQueueController(opciones, out var controller);
        _dispatcherQueueController = controller;
    }

    /// <summary>
    /// Fuera de XAML no existe LoadedImageSurface: para pintar cualquier cosa que no
    /// sea un color plano hay que montar D3D11 -> D2D -> Composition.
    ///
    /// WARP y no HARDWARE, como la isla: este device sube cinco glifos una vez y no
    /// renderiza un fotograma en su vida. El dock midio ~13 MB de working set privado
    /// frente a ~22 MB y se quedo con HARDWARE por prudencia; aqui se empieza por el
    /// barato y la vuelta atras es cambiar una constante.
    ///
    /// BGRA_SUPPORT es obligatorio para poder interoperar con Direct2D.
    /// </summary>
    private CompositionGraphicsDevice EnsureGraphicsDevice()
    {
        if (_graphics is not null) return _graphics;

        PInvoke.D3D11CreateDevice(
            null,
            D3D_DRIVER_TYPE.D3D_DRIVER_TYPE_WARP,
            default,
            D3D11_CREATE_DEVICE_FLAG.D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            null,
            0,
            D3D11SdkVersion,
            out ID3D11Device d3d,
            null,
            out _).ThrowOnFailure();

        PInvoke.D2D1CreateDevice((IDXGIDevice)d3d, null, out ID2D1Device d2d).ThrowOnFailure();
        _compositor.As<ICompositorInterop>().CreateGraphicsDevice(d2d, out _graphics);
        return _graphics;
    }

    /// <summary>
    /// Se llama de verdad, y mas de lo que parece: cada vez que el HUD salta a una
    /// pantalla con otra escala se rehace entero, porque las medidas en pixeles estan
    /// horneadas en los visuals. Con tres pantallas a tres escalas distintas eso pasa
    /// varias veces por minuto.
    ///
    /// <para>
    /// Por eso hay que soltar las superficies y no solo el target: cada glifo lleva
    /// detras una <c>CompositionDrawingSurface</c>, que es memoria de D2D y no la
    /// recoge el GC. Cinco por reconstruccion se acumulan rapido.
    /// </para>
    /// </summary>
    public void Dispose()
    {
        foreach (CompositionSurfaceBrush pincel in _pinceles)
        {
            if (pincel.Surface is CompositionDrawingSurface superficie) superficie.Dispose();
            pincel.Dispose();
        }
        _pinceles.Clear();

        // El Compositor y el device son del proceso y no se tiran: solo el target, que
        // es lo unico atado a este HWND.
        _target.Dispose();
    }
}
