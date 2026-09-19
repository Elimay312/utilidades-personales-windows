using System.Globalization;
using System.Numerics;
using Windows.UI;
using Windows.UI.Composition;
using Windows.UI.Composition.Desktop;
using Windows.Win32;
using Windows.Win32.Foundation;
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

    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _target;
    private readonly ContainerVisual _root;
    private readonly ContainerVisual _grupo;
    private readonly ContainerVisual _panel;
    private readonly SpriteVisual _macizo;
    private readonly SpriteVisual _cristal;
    private readonly ShapeVisual _borde;
    private readonly CompositionRoundedRectangleGeometry _forma;
    private readonly CompositionRoundedRectangleGeometry _formaBorde;
    private readonly float _scale;

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
        // Asi que el "cristal" es alfa a secas: el 12% de lo que hay detras se ve, sin
        // desenfocar. A este tamano cuela, y la isla de macOS es opaca de todas formas.
        _macizo = Capa(_compositor.CreateColorBrush(Color.FromArgb(255, 0, 0, 0)));
        _cristal = Capa(_compositor.CreateColorBrush(Color.FromArgb(226, 14, 14, 16)));

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
