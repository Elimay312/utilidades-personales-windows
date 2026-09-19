using System.Diagnostics.CodeAnalysis;
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
using Windows.Win32.Graphics.Dxgi.Common;
using Windows.Win32.System.WinRT;
using Windows.Win32.System.WinRT.Composition;
using WinRT;

namespace Isla;

/// <summary>
/// El arbol de composicion de la isla y su movimiento.
///
/// <para>
/// <b>La decision que lo decide todo</b>: el morph son TRES MUELLES sobre la caja, y
/// todo lo demas son expresiones que leen su alto en vivo. El dock embudo su animacion
/// por un CompositionPropertySet porque veinte iconos derivan de una sola posicion del
/// cursor; aqui hay una caja, y para una caja las animaciones de movimiento natural son
/// mejores: arrancan solas desde donde esten y son interrumpibles, que es justo lo que
/// hace falta cuando entras y sales del hover antes de que termine.
/// </para>
///
/// <para>
/// Nada de esto corre en el hilo de UI, asi que el morph no pierde un fotograma aunque
/// el hilo se quede bloqueado.
/// </para>
/// </summary>
internal sealed unsafe class IslaVisuals : IDisposable
{
    // Una DispatcherQueue por proceso, y hay que guardarla viva o el Compositor se
    // queda sin cola.
    private static Windows.System.DispatcherQueueController? _dispatcherQueueController;

    // Muelle al entrar, sin rebote al salir. Un rebote al aparecer es simpatico; al
    // desaparecer es ruido. Es la regla que separa "vivo" de "nervioso".
    private const float DampingAbrir = 0.72f;
    private const float DampingCerrar = 1.0f;
    private static readonly TimeSpan PeriodoAbrir = TimeSpan.FromMilliseconds(45);
    private static readonly TimeSpan PeriodoCerrar = TimeSpan.FromMilliseconds(55);

    // Umbrales del desfase, medidos sobre el ALTO de la caja en unidades logicas.
    // El contenido no empieza a entrar hasta que la caja pasa de 40, o sea el ultimo
    // tramo del recorrido: si entrara al mismo ritmo que la caja, se veria turbio.
    private const float CristalDesde = 40f;
    private const float CristalRango = 80f;

    // --- colocacion del contenido, en unidades logicas sobre el panel abierto -------
    private const float Margen = 16f;
    private const float CaratulaLado = 92f;
    private const float CaratulaRadio = 10f;
    private const float TextoX = 124f;
    private const float TituloY = 22f;
    private const float TituloPx = 14f;
    private const float TituloAncho = 240f;
    private const float ArtistaY = 46f;
    private const float ArtistaPx = 11.5f;
    private const float AppY = 68f;
    private const float AppPx = 10f;

    private const uint D3D11SdkVersion = 7;


    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _target;
    private readonly ContainerVisual _root;
    private readonly ContainerVisual _grupo;
    private readonly ContainerVisual _panel;
    private readonly SpriteVisual _macizo;
    private readonly SpriteVisual _cristal;
    private readonly ContainerVisual _contenido;
    private ContainerVisual _cajaTitulo;
    private SpriteVisual _caratula;
    private SpriteVisual _rotTitulo;
    private SpriteVisual _rotArtista;
    private SpriteVisual _rotApp;
    private readonly CompositionColorBrush _relleno;
    private readonly ShapeVisual _borde;
    private readonly CompositionRoundedRectangleGeometry _forma;
    private readonly CompositionRoundedRectangleGeometry _formaBorde;
    private readonly float _scale;
    private CompositionGraphicsDevice? _graphics;

    public IslaVisuals(HWND hwnd, float scale, float anchoVentana)
    {
        _scale = scale;

        EnsureDispatcherQueue();
        _compositor = new Compositor();

        ICompositorDesktopInterop interop = _compositor.As<ICompositorDesktopInterop>();
        interop.CreateDesktopWindowTarget(hwnd, true, out _target);

        _root = _compositor.CreateContainerVisual();
        _root.RelativeSizeAdjustment = Vector2.One;
        _target.Root = _root;

        // _grupo lleva el DESPEGUE (Offset.Y). Va en su propio contenedor porque el
        // centrado del panel es una ExpressionAnimation sobre Offset, y una expresion
        // es duena de la propiedad entera: no se puede poner un muelle encima.
        _grupo = _compositor.CreateContainerVisual();
        _grupo.RelativeSizeAdjustment = Vector2.One;
        _root.Children.InsertAtTop(_grupo);

        // SIN SOMBRA, y es una renuncia medida, no un olvido. Dos intentos:
        //
        //   1. Un sprite negro por debajo del panel haciendo de molde. Funcionaba
        //      mientras el panel era opaco, pero al volverlo translucido ese sprite
        //      macizo quedaba justo detras tapando el fondo: con fondo (40,44,52) el
        //      panel daba (12,12,14), que es lo que sale sobre NEGRO.
        //   2. Un LayerVisual con Shadow, que deberia sacar la sombra del alfa real del
        //      contenido. Pinto la VENTANA ENTERA de negro (0,0,0) en los dos estados.
        //
        // La sombra es un adorno; el morph no. Cuando toque, el camino es DropShadow
        // con Mask sobre una superficie con la forma de la pastilla.
        _panel = _compositor.CreateContainerVisual();
        _grupo.Children.InsertAtTop(_panel);

        _forma = _compositor.CreateRoundedRectangleGeometry();
        _panel.Clip = _compositor.CreateGeometricClip(_forma);

        // Dos capas que se cruzan: negro macizo en reposo -> cristal al abrirse.
        //
        // Aqui NO hay acrilico, y no es por pereza. CreateHostBackdropBrush se crea sin
        // lanzar excepcion pero se pinta NEGRO en una app Win32 sin empaquetar: no
        // muestrea nada. Medido poniendo un azul (0,90,220) detras y leyendo el panel,
        // que salio (11,11,13) -- exactamente el tinte sobre negro. Y el control
        // descarta que sea cosa de esta app: la barra del dock, en la misma maquina y
        // sobre el mismo azul, da (48,48,48), que es su tinte blanco de alfa 48 sobre
        // negro. Los comentarios del dock dicen que tiene acrilico; la pantalla dice
        // que no.
        //
        // Asi que el "cristal" es alfa a secas, y POCO: 4%.
        //
        // Empezo en 226 (11% de fondo) y se veia demasiado: con una ventana detras se
        // leian sus botones de minimizar y cerrar a traves del panel. Sin desenfoque
        // que los disuelva, un fantasma de otra interfaz distrae mas de lo que aporta.
        // Lo que de verdad da sensacion de cristal a este tamano no es la transparencia,
        // es el borde iluminado de abajo.
        _macizo = Capa(_compositor.CreateColorBrush(Color.FromArgb(255, 0, 0, 0)));
        _cristal = Capa(_compositor.CreateColorBrush(Color.FromArgb(245, 14, 14, 16)));

        // ponytail: cuadrado gris cuando la cancion no trae caratula. Un icono
        // generico quedaria mejor, pero eso es un recurso que hay que empaquetar.
        _relleno = _compositor.CreateColorBrush(Color.FromArgb(38, 255, 255, 255));
        _contenido = Contenido();

        // Borde interior de 1 px. Sin el, un panel oscuro parece un agujero en la
        // pantalla; con el, parece iluminado. Es lo mas barato que cambia la lectura.
        // Geometria propia, no la del clip: asi no hay que suponer que una geometria se
        // puede compartir entre un clip y una forma.
        _formaBorde = _compositor.CreateRoundedRectangleGeometry();
        CompositionSpriteShape trazo = _compositor.CreateSpriteShape(_formaBorde);
        trazo.StrokeBrush = _compositor.CreateColorBrush(Color.FromArgb(20, 255, 255, 255));
        // Doble grosor porque el clip del panel se come la mitad que cae fuera.
        trazo.StrokeThickness = S(2f);
        _borde = _compositor.CreateShapeVisual();
        _borde.RelativeSizeAdjustment = Vector2.One;
        _borde.Shapes.Add(trazo);
        _panel.Children.InsertAtTop(_borde);

        Expresiones(anchoVentana);
    }

    /// <summary>
    /// Todo lo derivado lee el alto animado de la caja en vivo. Son nueve expresiones
    /// cortas: el limite de longitud que muerde en el dock aqui no se acerca.
    /// </summary>
    private void Expresiones(float anchoVentana)
    {
        // Las dos geometrias siguen al panel.
        Expresion(_forma, "Size", "P.Size");
        Expresion(_formaBorde, "Size", "P.Size");

        // Centrado. La ventana es fija y ancha; la pastilla se estrecha dentro de ella.
        string centro = $"({F(anchoVentana)} - P.Size.X) * 0.5";
        Expresion(_panel, "Offset", $"Vector3({centro}, 0, 0)");

        string entrada = Rampa(CristalDesde, CristalRango);
        Expresion(_cristal, "Opacity", entrada);
        Expresion(_macizo, "Opacity", $"1 - ({entrada})");
        Expresion(_borde, "Opacity", entrada);

        // El contenido entra con la misma rampa y ademas crece un poco. Escalar desde
        // el centro del panel -- que tambien se esta moviendo -- es lo que hace que
        // parezca que sale de dentro y no que aparece pegado encima.
        Expresion(_contenido, "Opacity", entrada);
        Expresion(_contenido, "CenterPoint", "Vector3(P.Size.X * 0.5, P.Size.Y * 0.5, 0)");
        Expresion(_contenido, "Scale",
            $"Vector3(Lerp(0.92, 1, {entrada}), Lerp(0.92, 1, {entrada}), 1)");
    }

    /// <summary>
    /// Lleva al estado pedido. <paramref name="abriendo"/> elige el muelle: con rebote
    /// al crecer, sin rebote al encogerse.
    /// </summary>
    public void GoTo(Estado estado, bool abriendo, bool instantaneo = false)
    {
        (float w, float h, float r, float lift) = IslaWindow.Medidas(estado);
        Vector2 tam = new(S(w), S(h));
        Vector2 radio = new(S(r), S(r));
        float y = S(lift);

        if (instantaneo)
        {
            _panel.Size = tam;
            _forma.CornerRadius = radio;
            _formaBorde.CornerRadius = radio;
            _grupo.Offset = new Vector3(0, y, 0);
            return;
        }

        float damping = abriendo ? DampingAbrir : DampingCerrar;
        TimeSpan periodo = abriendo ? PeriodoAbrir : PeriodoCerrar;

        _panel.StartAnimation("Size", MuelleV2(tam, damping, periodo));
        _forma.StartAnimation("CornerRadius", MuelleV2(radio, damping, periodo));
        _formaBorde.StartAnimation("CornerRadius", MuelleV2(radio, damping, periodo));
        _grupo.StartAnimation("Offset.Y", MuelleEscalar(y, damping, periodo));
    }

    // --- piezas --------------------------------------------------------------------

    private float S(float logical) => logical * _scale;

    // En espanol la coma decimal rompe cada numero de la expresion. Medido en el dock.
    private static string F(float v) => v.ToString("0.###", CultureInfo.InvariantCulture);

    private string Rampa(float desde, float rango) =>
        $"Clamp((P.Size.Y - {F(S(desde))}) / {F(S(rango))}, 0, 1)";

    private void Expresion(CompositionObject destino, string propiedad, string expresion)
    {
        ExpressionAnimation e = _compositor.CreateExpressionAnimation(expresion);
        e.SetReferenceParameter("P", _panel);
        destino.StartAnimation(propiedad, e);
    }

    private SpriteVisual Capa(CompositionBrush pincel)
    {
        SpriteVisual v = _compositor.CreateSpriteVisual();
        v.RelativeSizeAdjustment = Vector2.One;
        v.Brush = pincel;
        _panel.Children.InsertAtTop(v);
        return v;
    }

    private SpringVector2NaturalMotionAnimation MuelleV2(Vector2 final, float damping, TimeSpan periodo)
    {
        SpringVector2NaturalMotionAnimation m = _compositor.CreateSpringVector2Animation();
        m.FinalValue = final;
        m.DampingRatio = damping;
        m.Period = periodo;
        return m;
    }

    private SpringScalarNaturalMotionAnimation MuelleEscalar(float final, float damping, TimeSpan periodo)
    {
        SpringScalarNaturalMotionAnimation m = _compositor.CreateSpringScalarAnimation();
        m.FinalValue = final;
        m.DampingRatio = damping;
        m.Period = periodo;
        return m;
    }

    /// <summary>
    /// Lo que se ve dentro del panel. Las posiciones son fijas respecto a su esquina
    /// superior izquierda, asi que no hace falta ninguna expresion para colocarlas.
    /// </summary>
    [MemberNotNull(nameof(_cajaTitulo), nameof(_caratula),
                   nameof(_rotTitulo), nameof(_rotArtista), nameof(_rotApp))]
    private ContainerVisual Contenido()
    {
        ContainerVisual raiz = _compositor.CreateContainerVisual();
        raiz.RelativeSizeAdjustment = Vector2.One;
        _panel.Children.InsertAtTop(raiz);

        // La caratula. El clip redondeado se pone una vez y se queda: lo que cambia
        // en cada cancion es solo el pincel.
        _caratula = _compositor.CreateSpriteVisual();
        _caratula.Size = new Vector2(S(CaratulaLado), S(CaratulaLado));
        _caratula.Offset = new Vector3(S(Margen), S(Margen), 0);
        _caratula.Brush = _relleno;
        CompositionRoundedRectangleGeometry marco = _compositor.CreateRoundedRectangleGeometry();
        marco.Size = _caratula.Size;
        marco.CornerRadius = new Vector2(S(CaratulaRadio), S(CaratulaRadio));
        _caratula.Clip = _compositor.CreateGeometricClip(marco);
        raiz.Children.InsertAtTop(_caratula);

        // El titulo va dentro de una caja que lo recorta. Si no cabe se pasea: cortarlo
        // con puntos suspensivos esconde justo la parte que distingue dos canciones del
        // mismo disco.
        _cajaTitulo = _compositor.CreateContainerVisual();
        _cajaTitulo.Size = new Vector2(S(TituloAncho), S(TituloPx) * 1.7f);
        _cajaTitulo.Offset = new Vector3(S(TextoX), S(TituloY), 0);
        _cajaTitulo.Clip = _compositor.CreateInsetClip();
        raiz.Children.InsertAtTop(_cajaTitulo);

        _rotTitulo = Hueco(Vector2.Zero, _cajaTitulo);
        _rotArtista = Hueco(new Vector2(S(TextoX), S(ArtistaY)), raiz);
        _rotApp = Hueco(new Vector2(S(TextoX), S(AppY)), raiz);

        return raiz;
    }

    /// <summary>Lo que va a llevar un texto. Nace vacio y lo llena Mostrar.</summary>
    private SpriteVisual Hueco(Vector2 en, ContainerVisual padre)
    {
        SpriteVisual v = _compositor.CreateSpriteVisual();
        v.Offset = new Vector3(en.X, en.Y, 0);
        padre.Children.InsertAtTop(v);
        return v;
    }

    /// <summary>Pinta lo que suena. Se llama desde el hilo de UI, nunca desde el pool.</summary>
    public void Mostrar(Cancion c)
    {
        Rotular(_rotTitulo, c.Titulo, TituloPx, true, 1f);
        Rotular(_rotArtista, c.Artista, ArtistaPx, false, 0.62f);
        Rotular(_rotApp, c.App, AppPx, false, 0.38f);
        Marquesina(_rotTitulo, _cajaTitulo.Size.X);

        _caratula.Brush = c.Arte is null ? _relleno : PincelArte(c.Arte);
    }

    private void Rotular(SpriteVisual v, string s, float px, bool grueso, float alpha)
    {
        if (string.IsNullOrWhiteSpace(s))
        {
            // Una superficie de tamano cero no se puede pedir, asi que se deja sin
            // pincel: el visual sigue ahi y no pinta nada.
            v.Size = Vector2.Zero;
            v.Brush = null;
            return;
        }

        float fisico = S(px);
        Vector2 tam = Texto.Medir(s, fisico, grueso);
        v.Size = tam;
        v.Brush = PincelTexto(s, fisico, grueso, alpha, tam);
    }

    /// <summary>
    /// La caratula que trajo la sesion, ya decodificada a BGRA premultiplicado. Se
    /// decodifico a 192 y aqui se baja al tamano de pantalla, que es donde D2D
    /// interpola mejor que hacerlo a mano.
    /// </summary>
    private CompositionSurfaceBrush PincelArte(byte[] bgra)
    {
        float lado = S(CaratulaLado);

        CompositionDrawingSurface superficie = EnsureGraphicsDevice().CreateDrawingSurface(
            new global::Windows.Foundation.Size(lado, lado),
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

            D2D1_BITMAP_PROPERTIES1 propiedades = new()
            {
                pixelFormat = new D2D1_PIXEL_FORMAT
                {
                    format = DXGI_FORMAT.DXGI_FORMAT_B8G8R8A8_UNORM,
                    alphaMode = D2D1_ALPHA_MODE.D2D1_ALPHA_MODE_PREMULTIPLIED,
                },
                dpiX = 96,
                dpiY = 96,
            };

            fixed (byte* pixeles = bgra)
            {
                ctx.CreateBitmap(
                    new D2D_SIZE_U { width = (uint)Medios.ArteLado, height = (uint)Medios.ArteLado },
                    pixeles, (uint)(Medios.ArteLado * 4), propiedades, out ID2D1Bitmap1 mapa);

                D2D_RECT_F destino = new()
                {
                    left = offset.X,
                    top = offset.Y,
                    right = offset.X + lado,
                    bottom = offset.Y + lado,
                };

                ctx.DrawBitmap(mapa, &destino, 1f,
                    D2D1_BITMAP_INTERPOLATION_MODE.D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, null);
            }
        }
        finally
        {
            interop.EndDraw();
        }

        return _compositor.CreateSurfaceBrush(superficie);
    }

    /// <summary>
    /// Va y vuelve, con parada en los dos extremos. Ida y vuelta, y no bucle en un solo
    /// sentido, porque el salto de vuelta se ve: una isla que da tirones deja de
    /// parecer parte de la maquina.
    /// </summary>
    private void Marquesina(SpriteVisual v, float anchoCaja)
    {
        // Al cambiar de cancion hay que parar la anterior y volver al principio, o
        // el titulo nuevo arranca por donde se quedo el viejo.
        v.StopAnimation("Offset.X");
        v.Offset = new Vector3(0, v.Offset.Y, 0);

        float sobra = v.Size.X - anchoCaja;
        if (sobra <= 1f) return;

        CompositionEasingFunction lineal = _compositor.CreateLinearEasingFunction();
        ScalarKeyFrameAnimation a = _compositor.CreateScalarKeyFrameAnimation();
        a.InsertKeyFrame(0.00f, 0f, lineal);
        a.InsertKeyFrame(0.12f, 0f, lineal);
        a.InsertKeyFrame(0.45f, -sobra, lineal);
        a.InsertKeyFrame(0.62f, -sobra, lineal);
        a.InsertKeyFrame(0.95f, 0f, lineal);
        a.InsertKeyFrame(1.00f, 0f, lineal);
        a.Duration = TimeSpan.FromMilliseconds(3200 + sobra * 18);
        a.IterationBehavior = AnimationIterationBehavior.Forever;

        // ponytail: sigue andando aunque la isla este cerrada. A opacidad cero no se ve,
        // y es una interpolacion escalar. Si el reposo llega a costar CPU, se para
        // desde GoTo.
        v.StartAnimation("Offset.X", a);
    }

    /// <summary>
    /// Sube una cadena ya pintada a una superficie del compositor.
    ///
    /// Tres trampas, todas sabidas de antemano: BeginDraw devuelve un OFFSET porque la
    /// superficie puede ser un hueco dentro de un atlas compartido; el contexto de D2D
    /// nace con el DPI del escritorio y hay que fijarlo a 96 o todo sale mas grande que
    /// la superficie; y hay que limpiarla, porque ese hueco puede traer los pixeles del
    /// inquilino anterior.
    /// </summary>
    private CompositionSurfaceBrush PincelTexto(string s, float px, bool grueso, float alpha, Vector2 tam)
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
            Texto.Dibujar(ctx, s, px, grueso, alpha, offset);
        }
        finally
        {
            interop.EndDraw();
        }

        return _compositor.CreateSurfaceBrush(superficie);
    }

    /// <summary>
    /// Fuera de XAML no existe LoadedImageSurface: para pintar cualquier cosa que no
    /// sea un color plano hay que montar D3D11 -> D2D -> Composition.
    ///
    /// WARP y no HARDWARE: este device solo SUBE pixeles y no renderiza un fotograma en
    /// su vida. El dock midio la diferencia en su caso, ~13 MB de working set privado
    /// frente a ~22 MB, y se quedo con HARDWARE por prudencia. Aqui se empieza por el
    /// barato; si DWM diera problemas muestreando estas superficies, la vuelta atras es
    /// cambiar una constante.
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
            null, 0, D3D11SdkVersion,
            out ID3D11Device d3d, null, out _).ThrowOnFailure();

        PInvoke.D2D1CreateDevice((IDXGIDevice)d3d, null, out ID2D1Device d2d).ThrowOnFailure();
        _compositor.As<ICompositorInterop>().CreateGraphicsDevice(d2d, out _graphics);
        return _graphics;
    }

    /// <summary>
    /// Windows.UI.Composition exige una DispatcherQueue en el hilo antes de poder crear
    /// el Compositor. Con DQTYPE_THREAD_CURRENT la doc exige DQTAT_COM_NONE.
    /// </summary>
    private static void EnsureDispatcherQueue()
    {
        if (_dispatcherQueueController is not null) return;

        DispatcherQueueOptions options = new()
        {
            dwSize = (uint)sizeof(DispatcherQueueOptions),
            threadType = DISPATCHERQUEUE_THREAD_TYPE.DQTYPE_THREAD_CURRENT,
            apartmentType = DISPATCHERQUEUE_THREAD_APARTMENTTYPE.DQTAT_COM_NONE,
        };

        PInvoke.CreateDispatcherQueueController(options, out Windows.System.DispatcherQueueController controller);
        _dispatcherQueueController = controller;
    }

    public void Dispose()
    {
        _target.Root = null;
        _root.Dispose();
        _target.Dispose();
    }
}
