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
/// El titular de la isla: lo unico que cabe en la pastilla asomada. Si lleva caratula
/// es que habla de musica; si no, el texto se pega al borde izquierdo.
/// </summary>
/// <para>
/// <c>Color</c> distinto de cero es el aviso de otra app (SEGURIDAD.md s.3.7): lleva un punto de
/// su color delante, en vez de la miniatura.
/// </para>
internal readonly record struct Aviso(string Texto, bool ConCaratula, uint Color = 0);

/// <summary>Que hay bajo el raton dentro del panel abierto.</summary>
internal enum Zona
{
    Nada,
    Anterior,
    PlayPausa,
    Siguiente,
    Barra,
    App,
    Pomodoro,
}

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

    // La fila compacta aparece pronto (la pastilla asomada mide 56 de alto) y se va
    // cuando entra el panel entero. El cruce de las dos es lo que hace que parezca que
    // el titular CRECE hasta convertirse en ficha, y no que una cosa tapa a la otra.
    private const float CompactoDesde = 26f;
    private const float CompactoRango = 26f;
    private const float PlenoDesde = 92f;
    private const float PlenoRango = 48f;

    private const float CompMiniLado = 40f;
    private const float CompMiniRadio = 6f;
    private const float CompMargen = 8f;
    private const float CompTextoY = 18f;
    private const float CompTextoPx = 13f;
    private const float CompAncho = 250f;

    // --- colocacion del contenido, en unidades logicas sobre el panel abierto -------
    private const float Margen = 16f;
    private const float CaratulaLado = 92f;
    private const string GlifoNota = "\uEC4F"; // MusicNote, Segoe Fluent Icons
    private const float NotaPx = 34f;
    private const float CaratulaRadio = 10f;
    private const float TextoX = 124f;
    private const float TituloY = 22f;
    private const float TituloPx = 14f;
    private const float TituloAncho = 240f;
    private const float ArtistaY = 46f;
    private const float ArtistaPx = 11.5f;
    private const float AppY = 68f;
    private const float AppPx = 10f;
    // La onda empieza en OndaX. El nombre para antes, con aire: el AUMID de la Store
    // llega entero (SEGURIDAD.md 3.1, se pinta tal cual) y sin este tope se mete en las barras.
    private const float AppAncho = OndaX - TextoX - 10f;

    private const float BarraX = 16f;
    private const float BarraY = 118f;
    private const float BarraAncho = 348f;
    private const float BarraAlto = 4f;
    private const float TiempoY = 128f;
    private const float TiempoPx = 10f;

    private const float BotonCy = 159f;
    private const float BotonPx = 16f;
    private const float PlayPx = 22f;
    // Caja de clic de cada boton. Mas grande que el glifo a proposito: apuntar a 16 px
    // con el raton es una loteria.
    private const float GolpeLado = 36f;
    private static readonly float[] BotonCx = [140f, 190f, 240f];

    // La onda: cuatro barras a la derecha del nombre de la app.
    private const float OndaX = 336f;
    private const float OndaBase = 80f;
    private const float OndaAncho = 3f;
    private const float OndaHueco = 4f;
    private const float OndaAlto = 14f;
    private const float OndaMinima = 0.14f;

    // Inercia distinta por barra: si todas siguieran al pico igual, se moverian como un
    // bloque y pareceria un medidor de VU, no una onda.
    private static readonly float[] Inercia = [0.55f, 0.74f, 0.62f, 0.81f];
    private static readonly float[] Ganancia = [1.00f, 0.82f, 0.94f, 0.70f];

    // Glifos de Segoe Fluent Icons: anterior, siguiente, play y pausa.
    private const string GlifoAnterior = "\uE100";
    private const string GlifoSiguiente = "\uE101";
    private const string GlifoPlay = "\uE102";
    private const string GlifoPausa = "\uE103";
    private const string GlifoReloj = "\uE916"; // Stopwatch

    // El pomodoro en el panel abierto: el hueco libre a la izquierda de los botones.
    private const float PomPx = 12f;
    private const float PomAncho = 96f;
    private const float PomBarraY = BotonCy + 10f;

    private const uint D3D11SdkVersion = 7;

    // --- el aviso de otra app (s.3.7) ------------------------------------------------
    // El punto del titular, y lo que va dentro de la burbuja que queda junto a la brasa
    // mientras el aviso espera respuesta (sus medidas, en IslaWindow): la insignia de la app
    // en su color, o un punto si no mando ninguna.
    private const float PuntoCompacto = 10f;
    private const float InsigniaPx = 13f;
    private const float PuntoBurbuja = 8f;
    private const float AroBurbuja = 1.5f;

    // La tarjeta, sobre el panel abierto de 380x180.
    private const float TarjPuntoY = 22f;
    private const float TarjAppX = 34f;
    private const float TarjAppY = 15f;
    private const float TarjTituloY = 38f;
    private const float TarjLineaY = 66f;
    private const float TarjBotonY = 124f;
    private const float TarjBotonAlto = 34f;
    private const float TarjBotonHueco = 8f;
    private const float TarjBotonPx = 12f;


    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _target;
    private readonly ContainerVisual _root;
    private readonly ContainerVisual _grupo;
    private readonly ContainerVisual _panel;
    private readonly SpriteVisual _sombra;
    private readonly SpriteVisual _macizo;
    private readonly SpriteVisual _cristal;
    private readonly ContainerVisual _contenido;
    private ContainerVisual _cajaTitulo;
    private SpriteVisual _caratula;
    private SpriteVisual _notaCaratula;
    private ContainerVisual _pomodoro;
    private SpriteVisual _pomGlifo;
    private SpriteVisual _pomTexto;
    private SpriteVisual _pomRelleno;
    private readonly SpriteVisual _brasaPomodoro;
    private bool _hayPomodoro;
    private SpriteVisual _rotTitulo;
    private SpriteVisual _rotArtista;
    private SpriteVisual _rotApp;
    private ContainerVisual _barra;
    private SpriteVisual _relleno;
    private SpriteVisual _rotPasado;
    private SpriteVisual _rotTotal;
    private SpriteVisual _botAnterior;
    private SpriteVisual _botPlay;
    private SpriteVisual _botSiguiente;
    private SpriteVisual[] _onda = [];
    private ContainerVisual _compacto;
    private ContainerVisual _cajaCompacta;
    private SpriteVisual _miniatura;
    private SpriteVisual _rotCompacto;
    private string _textoCompacto = string.Empty;
    private readonly float[] _nivel = new float[4];
    private readonly SpriteVisual _aura;
    private readonly CompositionRadialGradientBrush _degradado;
    private Estado _estado = Estado.Brasa;
    private float _latido = 1f;
    private float _medio = 0.05f;
    private readonly CompositionColorBrush _grisCaratula;
    private readonly ShapeVisual _borde;
    private readonly CompositionRoundedRectangleGeometry _forma;
    private readonly CompositionRoundedRectangleGeometry _formaBorde;
    private readonly float _scale;
    private readonly float _anchoVentana;
    private CompositionGraphicsDevice? _graphics;

    private readonly CompositionPropertySet _vista;
    private SpriteVisual _puntoCompacto;

    // La isla del aviso: otra pastilla entera, con su muelle, su aura y su borde, que en reposo
    // es la burbuja. La principal no se entera de que existe.
    private readonly ContainerVisual _grupoAviso;
    private readonly ContainerVisual _panelAviso;
    private readonly CompositionRoundedRectangleGeometry _formaAviso;
    private readonly CompositionRoundedRectangleGeometry _formaBordeAviso;
    private readonly SpriteVisual _auraAviso;
    private readonly CompositionRadialGradientBrush _degradadoAviso;
    private readonly ShapeVisual _bordeAviso;
    private ContainerVisual _filaAviso;
    private SpriteVisual _puntoFilaAviso;
    private SpriteVisual _rotFilaAviso;
    private string _textoFilaAviso = string.Empty;
    private readonly ContainerVisual _burbuja;
    private readonly SpriteVisual _insignia;
    private readonly SpriteVisual _puntoBurbuja;
    private readonly SpriteVisual _aroBurbuja;
    private long _burbujaDe;
    private float _dx = float.NaN;
    private ContainerVisual _tarjeta;
    private SpriteVisual _tarjPunto;
    private SpriteVisual _tarjApp;
    private SpriteVisual _tarjTitulo;
    private SpriteVisual _tarjLinea;
    private readonly List<ContainerVisual> _botones = [];
    private int _numBotones;

    public IslaVisuals(HWND hwnd, float scale, float anchoVentana)
    {
        _scale = scale;
        _anchoVentana = anchoVentana;

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
        // El tercero, que es este: un sprite SIN pincel por debajo del panel -- no tapa
        // nada, que era el fallo del primero -- con un DropShadow cuya mascara es la forma
        // de la pastilla abierta (ver Sombra). Solo con el panel abierto.
        _sombra = Sombra();
        _grupo.Children.InsertAtTop(_sombra);

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

        // El aura: el color que manda en la caratula, difuminado detras de la mitad
        // izquierda. Es lo que hace que cada cancion se sienta distinta sin cambiar
        // nada mas, y cuesta una media de pixeles que ya se hizo en el pool.
        _degradado = _compositor.CreateRadialGradientBrush();
        _degradado.EllipseCenter = new Vector2(0.17f, 0.36f);
        _degradado.EllipseRadius = new Vector2(0.85f, 1.25f);
        _degradado.ColorStops.Insert(0, _compositor.CreateColorGradientStop(0f, Color.FromArgb(0, 0, 0, 0)));
        _degradado.ColorStops.Insert(1, _compositor.CreateColorGradientStop(1f, Color.FromArgb(0, 0, 0, 0)));
        _aura = Capa(_degradado);

        // Sin caratula: este cuadrado con una nota encima (ver Contenido). La nota es un
        // glifo de Segoe Fluent Icons, asi que no hay recurso que empaquetar.
        _grisCaratula = _compositor.CreateColorBrush(Color.FromArgb(38, 255, 255, 255));
        _vista = _compositor.CreatePropertySet();
        _vista.InsertScalar("Dx", 0f);
        _contenido = Contenido();
        _compacto = FilaCompacta();

        // El pomodoro en la brasa: la linea se va vaciando segun pasa. Blanco tenue sobre el
        // negro, del ancho de la brasa y escalado en X desde la izquierda; se apaga en cuanto
        // la pastilla crece, que ahi ya lo cuenta el texto.
        _brasaPomodoro = _compositor.CreateSpriteVisual();
        _brasaPomodoro.Brush = _compositor.CreateColorBrush(Color.FromArgb(120, 255, 255, 255));
        _brasaPomodoro.IsVisible = false;
        _panel.Children.InsertAtTop(_brasaPomodoro);

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

        // --- la isla del aviso -------------------------------------------------------
        // Encima de la principal y con las mismas capas: negro, aura (del color del aviso) y el
        // filo iluminado. Nace invisible; la ensena el primer aviso.
        _grupoAviso = _compositor.CreateContainerVisual();
        _grupoAviso.RelativeSizeAdjustment = Vector2.One;
        _grupoAviso.Opacity = 0f;
        _root.Children.InsertAtTop(_grupoAviso);
        _panelAviso = _compositor.CreateContainerVisual();
        _grupoAviso.Children.InsertAtTop(_panelAviso);
        _formaAviso = _compositor.CreateRoundedRectangleGeometry();
        _panelAviso.Clip = _compositor.CreateGeometricClip(_formaAviso);
        Capa(_compositor.CreateColorBrush(Color.FromArgb(255, 0, 0, 0)), _panelAviso);
        _degradadoAviso = _compositor.CreateRadialGradientBrush();
        _degradadoAviso.EllipseCenter = new Vector2(0.17f, 0.36f);
        _degradadoAviso.EllipseRadius = new Vector2(0.85f, 1.25f);
        _degradadoAviso.ColorStops.Insert(0, _compositor.CreateColorGradientStop(0f, Color.FromArgb(0, 0, 0, 0)));
        _degradadoAviso.ColorStops.Insert(1, _compositor.CreateColorGradientStop(1f, Color.FromArgb(0, 0, 0, 0)));
        _auraAviso = Capa(_degradadoAviso, _panelAviso);
        _tarjeta = Tarjeta();
        _filaAviso = FilaAviso();
        _formaBordeAviso = _compositor.CreateRoundedRectangleGeometry();
        CompositionSpriteShape trazoAviso = _compositor.CreateSpriteShape(_formaBordeAviso);
        trazoAviso.StrokeBrush = _compositor.CreateColorBrush(Color.FromArgb(20, 255, 255, 255));
        trazoAviso.StrokeThickness = S(2f);
        _bordeAviso = _compositor.CreateShapeVisual();
        _bordeAviso.RelativeSizeAdjustment = Vector2.One;
        _bordeAviso.Shapes.Add(trazoAviso);
        _panelAviso.Children.InsertAtTop(_bordeAviso);

        // Lo que se ve cuando la isla del aviso es la burbuja: negra como la brasa y con un aro
        // del color del aviso -- sin el, un circulo negro sobre una pestana oscura es un
        // agujero, y escondida es lo unico que asoma --, y dentro la insignia. Se funde en
        // cuanto la pastilla empieza a crecer.
        float lado = S(IslaWindow.BurbujaLado);
        _burbuja = _compositor.CreateContainerVisual();
        _burbuja.Size = new Vector2(lado, lado);
        // El aro es un circulo de color con el negro encima, 1,5 mas pequeno por cada lado: con
        // un trazo de ShapeVisual dentro de este contenedor no se pintaba nada.
        _aroBurbuja = Circulo(IslaWindow.BurbujaLado);
        _burbuja.Children.InsertAtTop(_aroBurbuja);
        SpriteVisual fondo = Circulo(IslaWindow.BurbujaLado - AroBurbuja * 2f);
        fondo.Offset = new Vector3(S(AroBurbuja), S(AroBurbuja), 0);
        fondo.Brush = _compositor.CreateColorBrush(Color.FromArgb(255, 0, 0, 0));
        _burbuja.Children.InsertAtTop(fondo);
        _insignia = _compositor.CreateSpriteVisual();
        _burbuja.Children.InsertAtTop(_insignia);
        _puntoBurbuja = Circulo(PuntoBurbuja);
        _puntoBurbuja.Offset = new Vector3((lado - S(PuntoBurbuja)) * 0.5f, (lado - S(PuntoBurbuja)) * 0.5f, 0);
        _burbuja.Children.InsertAtTop(_puntoBurbuja);
        _panelAviso.Children.InsertAtTop(_burbuja);

        Expresiones(anchoVentana);
        AvisoGoTo(Estado.Brasa, abriendo: false, fuera: false, instantaneo: true, lift: -IslaWindow.BurbujaLado);
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

        // La sombra calca la caja y solo aparece con la ficha entera.
        Expresion(_sombra, "Size", "P.Size");
        Expresion(_sombra, "Offset", $"Vector3({centro}, 0, 0)");
        Expresion(_sombra, "Opacity", Rampa(PlenoDesde, PlenoRango));

        string entrada = Rampa(CristalDesde, CristalRango);
        Expresion(_cristal, "Opacity", entrada);
        Expresion(_macizo, "Opacity", $"1 - ({entrada})");
        Expresion(_borde, "Opacity", entrada);

        // El titular entra pronto y se va cuando entra la ficha entera.
        string pleno = Rampa(PlenoDesde, PlenoRango);
        Expresion(_compacto, "Opacity", $"{Rampa(CompactoDesde, CompactoRango)} * (1 - ({pleno}))");

        // La brasa del pomodoro: del tamano de la caja y solo mientras es brasa.
        Expresion(_brasaPomodoro, "Size", "P.Size");
        Expresion(_brasaPomodoro, "Opacity", $"1 - Clamp((P.Size.Y - {F(S(5f))}) / {F(S(6f))}, 0, 1)");

        // El contenido entra con la misma rampa y ademas crece un poco. Escalar desde
        // el centro del panel -- que tambien se esta moviendo -- es lo que hace que
        // parezca que sale de dentro y no que aparece pegado encima.
        Expresion(_contenido, "Opacity", pleno);
        EscalaDeEntrada(_contenido, entrada, _panel);

        // La isla del aviso, con las mismas rampas sobre su propio tamano (P es su panel). Nace y
        // muere donde esta su burbuja y vuelve al centro a medida que se ensancha: de 28 de
        // ancho, que es la burbuja, a 320, la asomada.
        string hacia = $"(1 - Clamp((P.Size.X - {F(S(IslaWindow.BurbujaLado))}) / {F(S(320f - IslaWindow.BurbujaLado))}, 0, 1))";
        Expresion(_formaAviso, "Size", "P.Size", _panelAviso);
        Expresion(_formaBordeAviso, "Size", "P.Size", _panelAviso);
        Expresion(_panelAviso, "Offset", $"Vector3({centro} + V.Dx * {hacia}, 0, 0)", _panelAviso);
        Expresion(_bordeAviso, "Opacity", entrada, _panelAviso);
        Expresion(_auraAviso, "Opacity", entrada, _panelAviso);
        // La fila empieza mas tarde que la de la principal: con 28 de alto la burbuja ya pasaria
        // del umbral de 26 y el texto se colaria dentro del circulo.
        Expresion(_filaAviso, "Opacity", $"{Rampa(IslaWindow.BurbujaLado + 4f, 24f)} * (1 - ({pleno}))", _panelAviso);
        Expresion(_tarjeta, "Opacity", pleno, _panelAviso);
        EscalaDeEntrada(_tarjeta, entrada, _panelAviso);
        Expresion(_burbuja, "Opacity",
            $"1 - Clamp((P.Size.X - {F(S(IslaWindow.BurbujaLado))}) / {F(S(16f))}, 0, 1)", _panelAviso);
    }

    private void EscalaDeEntrada(ContainerVisual capa, string entrada, ContainerVisual panel)
    {
        Expresion(capa, "CenterPoint", "Vector3(P.Size.X * 0.5, P.Size.Y * 0.5, 0)", panel);
        Expresion(capa, "Scale", $"Vector3(Lerp(0.92, 1, {entrada}), Lerp(0.92, 1, {entrada}), 1)", panel);
    }

    /// <summary>
    /// El latido del audio. Un solo float por lectura, ver Audio y SEGURIDAD.md §3.3.
    ///
    /// <para>
    /// En brasa mueve el ANCHO de la tira; abierta o asomada, las cuatro barras. Cada
    /// barra lleva su propia inercia y su propia ganancia, porque cuatro barras que
    /// siguen al pico exactamente igual se mueven como un bloque y eso no parece una
    /// onda, parece un vumetro roto.
    /// </para>
    /// </summary>
    public void Pulso(float pico, bool sonando)
    {
        if (!sonando)
        {
            Reposar();
            return;
        }

        // Ganancia automatica: media lenta como centro, y se estira la desviacion
        // RELATIVA a su alrededor.
        //
        // Los dos intentos anteriores fallaron por lo mismo. Dividir por un techo que
        // decae da 1 casi siempre, porque el techo lo acaba de fijar el propio pico; y
        // estirar la banda techo-suelo tampoco, porque los dos persiguen al pico y la
        // banda se cierra. Los numeros lo decian desde el principio: veinte lecturas
        // seguidas de musica continua dieron 0.043 a 0.066, una banda estrecha con una
        // media clara en medio. Lo que se ve es cuanto se separa de esa media.
        _medio += (pico - _medio) * 0.02f;
        float nivel = _medio > 0.002f
            ? Math.Clamp(0.5f + (pico - _medio) / _medio * 3.4f, 0f, 1f)
            : 0f;

        if (_estado == Estado.Brasa)
        {
            // La tira respira entre el 82% y el 100% de su ancho.
            float objetivo = 0.82f + nivel * 0.18f;
            float antes = _latido;
            _latido += (objetivo - _latido) * 0.35f;

            // Solo se escribe si el cambio se nota. Cada asignacion a una propiedad del
            // compositor es una confirmacion que cruza a DWM, y a 8 por segundo eso se
            // ve en el medidor de CPU; medio pixel de 140 no se ve en la pantalla.
            if (Math.Abs(_latido - antes) > 0.004f) _panel.Scale = new Vector3(_latido, 1f, 1f);
            return;
        }

        Enderezar();

        for (int i = 0; i < _onda.Length; i++)
        {
            float objetivo = OndaMinima + Math.Clamp(nivel * Ganancia[i], 0f, 1f) * (1f - OndaMinima);
            float antes = _nivel[i];
            _nivel[i] += (objetivo - _nivel[i]) * (1f - Inercia[i]);
            if (Math.Abs(_nivel[i] - antes) > 0.004f)
                _onda[i].Scale = new Vector3(1f, Math.Max(_nivel[i], OndaMinima), 1f);
        }
    }

    /// <summary>Sin audio: la tira a su ancho entero y las barras al minimo.</summary>
    private void Reposar()
    {
        Enderezar();
        for (int i = 0; i < _onda.Length; i++)
        {
            if (_nivel[i] <= OndaMinima + 0.001f) continue;
            _nivel[i] = OndaMinima;
            _onda[i].Scale = new Vector3(1f, OndaMinima, 1f);
        }
    }

    private void Enderezar()
    {
        if (Math.Abs(_panel.Scale.X - 1f) <= 0.001f) return;
        _latido = 1f;
        _panel.Scale = Vector3.One;
    }

    /// <summary>
    /// Lleva al estado pedido. <paramref name="abriendo"/> elige el muelle: con rebote
    /// al crecer, sin rebote al encogerse.
    /// </summary>
    public void GoTo(Estado estado, bool abriendo, bool instantaneo = false)
    {
        _estado = estado;
        (float w, float h, float r, float lift) = IslaWindow.Medidas(estado);
        // El latido escala el panel en X, asi que el centro tiene que estar en su medio
        // o la tira crece solo hacia la derecha y se descoloca.
        _panel.CenterPoint = new Vector3(S(w) * 0.5f, 0f, 0f);
        Morph(_panel, _forma, _formaBorde, _grupo, (w, h, r, lift), abriendo, instantaneo);
    }

    /// <summary>
    /// La isla del aviso al estado pedido. En <see cref="Estado.Brasa"/> es la burbuja: bajada
    /// entera si <paramref name="fuera"/>, o escondida tras el borde; <paramref name="lift"/>
    /// fuerza su altura, que es como entra, desde detras del borde.
    /// </summary>
    public void AvisoGoTo(Estado estado, bool abriendo, bool fuera, bool instantaneo = false, float? lift = null)
    {
        float lado = IslaWindow.BurbujaLado;
        (float W, float H, float R, float Lift) m = estado == Estado.Brasa
            ? (lado, lado, lado * 0.5f, lift ?? (fuera ? IslaWindow.BurbujaY : IslaWindow.BurbujaAsoma - lado))
            : IslaWindow.Medidas(estado);
        Morph(_panelAviso, _formaAviso, _formaBordeAviso, _grupoAviso, m, abriendo, instantaneo);
    }

    private void Morph(ContainerVisual panel, CompositionRoundedRectangleGeometry forma,
                       CompositionRoundedRectangleGeometry borde, ContainerVisual grupo,
                       (float W, float H, float R, float Lift) m, bool abriendo, bool instantaneo)
    {
        Vector2 tam = new(S(m.W), S(m.H));
        Vector2 radio = new(S(m.R), S(m.R));
        float y = S(m.Lift);

        if (instantaneo)
        {
            // Un muelle a medio camino le ganaria al valor puesto a mano.
            panel.StopAnimation("Size");
            forma.StopAnimation("CornerRadius");
            borde.StopAnimation("CornerRadius");
            grupo.StopAnimation("Offset.Y");
            panel.Size = tam;
            forma.CornerRadius = radio;
            borde.CornerRadius = radio;
            grupo.Offset = new Vector3(0, y, 0);
            return;
        }

        float damping = abriendo ? DampingAbrir : DampingCerrar;
        TimeSpan periodo = abriendo ? PeriodoAbrir : PeriodoCerrar;

        panel.StartAnimation("Size", MuelleV2(tam, damping, periodo));
        forma.StartAnimation("CornerRadius", MuelleV2(radio, damping, periodo));
        borde.StartAnimation("CornerRadius", MuelleV2(radio, damping, periodo));
        grupo.StartAnimation("Offset.Y", MuelleEscalar(y, damping, periodo));
    }

    /// <summary>
    /// Ensena o apaga la isla del aviso. Al ensenarse de nuevo entra desde detras del borde
    /// hasta su sitio de burbuja; al apagarse se funde donde este.
    /// </summary>
    public void AvisoVisible(bool visible, bool fuera)
    {
        if (visible && _grupoAviso.Opacity < 0.5f)
        {
            AvisoGoTo(Estado.Brasa, abriendo: false, fuera, instantaneo: true, lift: -IslaWindow.BurbujaLado);
            _grupoAviso.StopAnimation("Opacity");
            _grupoAviso.Opacity = 1f;
            AvisoGoTo(Estado.Brasa, abriendo: true, fuera);
            return;
        }
        if (!visible) Fundir(_grupoAviso, 0f);
    }

    /// <summary>Donde cae la burbuja respecto al centro, en pixeles: a su lado de la brasa, o en medio.</summary>
    public void AvisoDx(float dx)
    {
        if (dx == _dx) return;
        _dx = dx;
        _vista.InsertScalar("Dx", dx);
    }

    // --- piezas --------------------------------------------------------------------

    private float S(float logical) => logical * _scale;

    // En espanol la coma decimal rompe cada numero de la expresion. Medido en el dock.
    private static string F(float v) => v.ToString("0.###", CultureInfo.InvariantCulture);

    private string Rampa(float desde, float rango) =>
        $"Clamp((P.Size.Y - {F(S(desde))}) / {F(S(rango))}, 0, 1)";

    // P es el panel del que se lee el tamano: el de la principal, o el de la isla del aviso.
    private void Expresion(CompositionObject destino, string propiedad, string expresion,
                           ContainerVisual? panel = null)
    {
        ExpressionAnimation e = _compositor.CreateExpressionAnimation(expresion);
        e.SetReferenceParameter("P", panel ?? _panel);
        e.SetReferenceParameter("V", _vista);
        destino.StartAnimation(propiedad, e);
    }

    private SpriteVisual Capa(CompositionBrush pincel, ContainerVisual? en = null)
    {
        SpriteVisual v = _compositor.CreateSpriteVisual();
        v.RelativeSizeAdjustment = Vector2.One;
        v.Brush = pincel;
        (en ?? _panel).Children.InsertAtTop(v);
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
    [MemberNotNull(nameof(_onda), nameof(_cajaTitulo), nameof(_caratula), nameof(_notaCaratula),
                   nameof(_pomodoro), nameof(_pomGlifo), nameof(_pomTexto), nameof(_pomRelleno),
                   nameof(_rotTitulo), nameof(_rotArtista), nameof(_rotApp),
                   nameof(_barra), nameof(_relleno), nameof(_rotPasado), nameof(_rotTotal),
                   nameof(_botAnterior), nameof(_botPlay), nameof(_botSiguiente))]
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
        _caratula.Brush = _grisCaratula;
        CompositionRoundedRectangleGeometry marco = _compositor.CreateRoundedRectangleGeometry();
        marco.Size = _caratula.Size;
        marco.CornerRadius = new Vector2(S(CaratulaRadio), S(CaratulaRadio));
        _caratula.Clip = _compositor.CreateGeometricClip(marco);
        raiz.Children.InsertAtTop(_caratula);

        // La nota del cuadrado vacio. Se pinta una vez; Mostrar solo la enciende o apaga.
        _notaCaratula = _compositor.CreateSpriteVisual();
        Rotular(_notaCaratula, GlifoNota, NotaPx, false, 0.35f, iconos: true);
        _notaCaratula.Offset = new Vector3((_caratula.Size - _notaCaratula.Size) * 0.5f, 0);
        _caratula.Children.InsertAtTop(_notaCaratula);

        // El titulo va dentro de una caja que lo recorta. Si no cabe se pasea: cortarlo
        // con puntos suspensivos esconde justo la parte que distingue dos canciones del
        // mismo disco.
        _cajaTitulo = _compositor.CreateContainerVisual();
        _cajaTitulo.Size = new Vector2(S(TituloAncho), S(TituloPx) * 1.7f);
        _cajaTitulo.Offset = new Vector3(S(TextoX), S(TituloY), 0);
        _cajaTitulo.Clip = _compositor.CreateInsetClip();
        raiz.Children.InsertAtTop(_cajaTitulo);

        _rotTitulo = Hueco(Vector2.Zero, _cajaTitulo);
        // Caja con clip, como el titulo: Cabe pone los puntos, y el clip impide que un
        // pixel de mas llegue a la onda o al borde redondeado.
        ContainerVisual cajaArtista = Caja(raiz, TextoX, ArtistaY, TituloAncho, ArtistaPx);
        _rotArtista = Hueco(Vector2.Zero, cajaArtista);
        ContainerVisual cajaApp = Caja(raiz, TextoX, AppY, AppAncho, AppPx);
        _rotApp = Hueco(Vector2.Zero, cajaApp);

        // La barra de progreso: carril recortado y un relleno que escala en X desde la
        // izquierda. Escalar y no redimensionar es lo que permite que la anime el
        // compositor sin que nadie toque nada por fotograma.
        _barra = _compositor.CreateContainerVisual();
        _barra.Size = new Vector2(S(BarraAncho), S(BarraAlto));
        _barra.Offset = new Vector3(S(BarraX), S(BarraY), 0);
        CompositionRoundedRectangleGeometry carril = _compositor.CreateRoundedRectangleGeometry();
        carril.Size = _barra.Size;
        carril.CornerRadius = new Vector2(S(BarraAlto * 0.5f), S(BarraAlto * 0.5f));
        _barra.Clip = _compositor.CreateGeometricClip(carril);
        raiz.Children.InsertAtTop(_barra);

        SpriteVisual surco = _compositor.CreateSpriteVisual();
        surco.RelativeSizeAdjustment = Vector2.One;
        surco.Brush = _compositor.CreateColorBrush(Color.FromArgb(46, 255, 255, 255));
        _barra.Children.InsertAtTop(surco);

        _relleno = _compositor.CreateSpriteVisual();
        _relleno.Size = _barra.Size;
        _relleno.Brush = _compositor.CreateColorBrush(Color.FromArgb(235, 255, 255, 255));
        _relleno.Scale = new Vector3(0f, 1f, 1f);
        _barra.Children.InsertAtTop(_relleno);

        _rotPasado = Hueco(new Vector2(S(BarraX), S(TiempoY)), raiz);
        _rotTotal = Hueco(Vector2.Zero, raiz);

        // Las barras de la onda. Escalan en Y desde su base, que esta clavada.
        _onda = new SpriteVisual[Inercia.Length];
        for (int i = 0; i < _onda.Length; i++)
        {
            SpriteVisual barra = _compositor.CreateSpriteVisual();
            barra.Size = new Vector2(S(OndaAncho), S(OndaAlto));
            barra.Offset = new Vector3(S(OndaX + i * (OndaAncho + OndaHueco)), S(OndaBase - OndaAlto), 0);
            barra.CenterPoint = new Vector3(0, S(OndaAlto), 0);
            barra.Scale = new Vector3(1f, OndaMinima, 1f);
            barra.Brush = _compositor.CreateColorBrush(Color.FromArgb(150, 255, 255, 255));
            raiz.Children.InsertAtTop(barra);
            _onda[i] = barra;
        }

        _botAnterior = Hueco(Vector2.Zero, raiz);
        _botPlay = Hueco(Vector2.Zero, raiz);
        _botSiguiente = Hueco(Vector2.Zero, raiz);

        // El pomodoro: reloj, tiempo y una barra de 2 px que se vacia. Oculto sin pomodoro.
        _pomodoro = _compositor.CreateContainerVisual();
        _pomodoro.IsVisible = false;
        raiz.Children.InsertAtTop(_pomodoro);
        _pomGlifo = Hueco(Vector2.Zero, _pomodoro);
        _pomTexto = Hueco(Vector2.Zero, _pomodoro);
        SpriteVisual pomSurco = _compositor.CreateSpriteVisual();
        pomSurco.Size = new Vector2(S(PomAncho), S(2f));
        pomSurco.Offset = new Vector3(S(Margen), S(PomBarraY), 0);
        pomSurco.Brush = _compositor.CreateColorBrush(Color.FromArgb(46, 255, 255, 255));
        _pomodoro.Children.InsertAtTop(pomSurco);
        _pomRelleno = _compositor.CreateSpriteVisual();
        _pomRelleno.Size = pomSurco.Size;
        _pomRelleno.Offset = pomSurco.Offset;
        _pomRelleno.Brush = _compositor.CreateColorBrush(Color.FromArgb(200, 255, 255, 255));
        _pomodoro.Children.InsertAtTop(_pomRelleno);

        return raiz;
    }

    /// <summary>Lo que va a llevar un texto. Nace vacio y lo llena Mostrar.</summary>
    /// <summary>
    /// El titular: una miniatura y una linea. Es lo UNICO que se ve en la pastilla
    /// asomada, que antes salia vacia -- su alto es 56 y la rampa del contenido no
    /// arranca hasta 92, asi que el aviso que existe para decir que ha cambiado no
    /// decia nada.
    /// </summary>
    [MemberNotNull(nameof(_cajaCompacta), nameof(_miniatura), nameof(_rotCompacto), nameof(_puntoCompacto))]
    private ContainerVisual FilaCompacta()
    {
        ContainerVisual raiz = _compositor.CreateContainerVisual();
        raiz.RelativeSizeAdjustment = Vector2.One;
        _panel.Children.InsertAtTop(raiz);

        _miniatura = _compositor.CreateSpriteVisual();
        _miniatura.Size = new Vector2(S(CompMiniLado), S(CompMiniLado));
        _miniatura.Offset = new Vector3(S(CompMargen), S(CompMargen), 0);
        CompositionRoundedRectangleGeometry marco = _compositor.CreateRoundedRectangleGeometry();
        marco.Size = _miniatura.Size;
        marco.CornerRadius = new Vector2(S(CompMiniRadio), S(CompMiniRadio));
        _miniatura.Clip = _compositor.CreateGeometricClip(marco);
        raiz.Children.InsertAtTop(_miniatura);

        _cajaCompacta = _compositor.CreateContainerVisual();
        _cajaCompacta.Size = new Vector2(S(CompAncho), S(CompTextoPx) * 1.7f);
        _cajaCompacta.Clip = _compositor.CreateInsetClip();
        raiz.Children.InsertAtTop(_cajaCompacta);

        _rotCompacto = Hueco(Vector2.Zero, _cajaCompacta);

        _puntoCompacto = Circulo(PuntoCompacto);
        _puntoCompacto.Offset = new Vector3(S(CompMargen * 2f), S(28f - PuntoCompacto * 0.5f), 0);
        raiz.Children.InsertAtTop(_puntoCompacto);
        return raiz;
    }

    private SpriteVisual Circulo(float lado)
    {
        SpriteVisual v = _compositor.CreateSpriteVisual();
        v.Size = new Vector2(S(lado), S(lado));
        CompositionEllipseGeometry g = _compositor.CreateEllipseGeometry();
        g.Center = new Vector2(S(lado) * 0.5f, S(lado) * 0.5f);
        g.Radius = g.Center;
        v.Clip = _compositor.CreateGeometricClip(g);
        return v;
    }

    private CompositionColorBrush Pintura(uint rgb, byte alfa = 255) =>
        _compositor.CreateColorBrush(Color.FromArgb(alfa, (byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb));

    /// <summary>
    /// La tarjeta del aviso de otra app: su punto de color, de quien es, el titulo, la linea y
    /// hasta cuatro botones. Nace vacia y la llena MostrarAviso.
    /// </summary>
    [MemberNotNull(nameof(_tarjPunto), nameof(_tarjApp), nameof(_tarjTitulo), nameof(_tarjLinea))]
    private ContainerVisual Tarjeta()
    {
        ContainerVisual raiz = _compositor.CreateContainerVisual();
        raiz.RelativeSizeAdjustment = Vector2.One;
        raiz.Opacity = 0f;
        _panelAviso.Children.InsertAtTop(raiz);

        _tarjPunto = Circulo(10f);
        _tarjPunto.Offset = new Vector3(S(Margen), S(TarjPuntoY - 5f), 0);
        raiz.Children.InsertAtTop(_tarjPunto);
        _tarjApp = Hueco(new Vector2(S(TarjAppX), S(TarjAppY)), raiz);
        _tarjTitulo = Hueco(new Vector2(S(Margen), S(TarjTituloY)), raiz);
        _tarjLinea = Hueco(new Vector2(S(Margen), S(TarjLineaY)), raiz);

        for (int i = 0; i < 4; i++)
        {
            ContainerVisual boton = _compositor.CreateContainerVisual();
            boton.Clip = _compositor.CreateGeometricClip(_compositor.CreateRoundedRectangleGeometry());
            SpriteVisual fondo = _compositor.CreateSpriteVisual();
            fondo.RelativeSizeAdjustment = Vector2.One;
            boton.Children.InsertAtTop(fondo);
            boton.Children.InsertAtTop(_compositor.CreateSpriteVisual());
            raiz.Children.InsertAtTop(boton);
            _botones.Add(boton);
        }
        return raiz;
    }

    /// <summary>
    /// Pinta el aviso que espera respuesta, o nada. Los botones se reparten el ancho; el primero
    /// es el que se espera que pulses y va mas encendido.
    /// </summary>
    public void MostrarAviso(AvisoApp? a)
    {
        _tarjPunto.Brush = a is null || a.Color == 0 ? null : Pintura(a.Color);
        Rotular(_tarjApp, a?.App ?? string.Empty, AppPx, false, 0.38f);
        float ancho = S(380f - Margen * 2f);
        Rotular(_tarjTitulo, a is null ? string.Empty : Cabe(a.Titulo, ancho, S(TituloPx)), TituloPx, true, 1f);
        Rotular(_tarjLinea, a is null ? string.Empty : Cabe(a.Linea, ancho, S(ArtistaPx)), ArtistaPx, false, 0.62f);

        _numBotones = a?.Botones.Count ?? 0;
        float cada = _numBotones == 0 ? 0f : (380f - Margen * 2f - (_numBotones - 1) * TarjBotonHueco) / _numBotones;
        for (int i = 0; i < _botones.Count; i++)
        {
            ContainerVisual boton = _botones[i];
            SpriteVisual fondo = (SpriteVisual)boton.Children.First();
            SpriteVisual rotulo = (SpriteVisual)boton.Children.Last();
            if (a is null || i >= _numBotones)
            {
                boton.Size = Vector2.Zero;
                Rotular(rotulo, string.Empty, TarjBotonPx, true, 1f);
                continue;
            }

            boton.Size = new Vector2(S(cada), S(TarjBotonAlto));
            boton.Offset = new Vector3(S(Margen + i * (cada + TarjBotonHueco)), S(TarjBotonY), 0);
            var forma = (CompositionRoundedRectangleGeometry)((CompositionGeometricClip)boton.Clip).Geometry;
            forma.Size = boton.Size;
            forma.CornerRadius = new Vector2(S(TarjBotonAlto * 0.5f), S(TarjBotonAlto * 0.5f));
            fondo.Brush = _compositor.CreateColorBrush(Color.FromArgb(i == 0 ? (byte)64 : (byte)26, 255, 255, 255));
            Rotular(rotulo, Cabe(a.Botones[i].Texto, S(cada - 12f), S(TarjBotonPx)), TarjBotonPx, true, i == 0 ? 1f : 0.85f);
            rotulo.Offset = new Vector3((boton.Size.X - rotulo.Size.X) * 0.5f, (boton.Size.Y - rotulo.Size.Y) * 0.5f, 0);
        }
    }

    /// <summary>El boton de la tarjeta bajo el punto, en coordenadas del panel abierto, o -1.</summary>
    public int GolpeBoton(Vector2 p)
    {
        for (int i = 0; i < _numBotones; i++)
        {
            Vector3 o = _botones[i].Offset;
            Vector2 t = _botones[i].Size;
            if (p.X >= o.X && p.X < o.X + t.X && p.Y >= o.Y - S(4f) && p.Y < o.Y + t.Y + S(4f)) return i;
        }
        return -1;
    }

    /// <summary>
    /// La fila de la isla del aviso asomada: su punto de color y una linea. Nace vacia y la
    /// llena <see cref="TitularAviso"/>.
    /// </summary>
    [MemberNotNull(nameof(_puntoFilaAviso), nameof(_rotFilaAviso))]
    private ContainerVisual FilaAviso()
    {
        ContainerVisual raiz = _compositor.CreateContainerVisual();
        raiz.RelativeSizeAdjustment = Vector2.One;
        raiz.Opacity = 0f;
        _panelAviso.Children.InsertAtTop(raiz);

        _puntoFilaAviso = Circulo(PuntoCompacto);
        _puntoFilaAviso.Offset = new Vector3(S(CompMargen * 2f), S(28f - PuntoCompacto * 0.5f), 0);
        raiz.Children.InsertAtTop(_puntoFilaAviso);

        ContainerVisual caja = _compositor.CreateContainerVisual();
        caja.Size = new Vector2(S(CompAncho), S(CompTextoPx) * 1.7f);
        caja.Offset = new Vector3(S(CompMargen * 2f + PuntoCompacto + 8f), S(CompTextoY), 0);
        caja.Clip = _compositor.CreateInsetClip();
        raiz.Children.InsertAtTop(caja);
        _rotFilaAviso = Hueco(Vector2.Zero, caja);
        return raiz;
    }

    /// <summary>Lo que dice la isla del aviso asomada: el titulo y cuando, con su punto.</summary>
    public void TitularAviso(string texto, uint color)
    {
        _puntoFilaAviso.Brush = Pintura(color == 0 ? 0xFFFFFFu : color);
        if (texto == _textoFilaAviso) return;
        _textoFilaAviso = texto;
        Rotular(_rotFilaAviso, Cabe(texto, S(CompAncho), S(CompTextoPx)), CompTextoPx, true, 0.95f);
    }

    /// <summary>
    /// De que aviso es la isla del aviso: su color en el aro y en el aura, y su insignia dentro
    /// de la burbuja, o un punto si no trae. Solo repinta si cambia de aviso.
    /// </summary>
    public void PintarBurbuja(AvisoApp a)
    {
        if (a.Numero == _burbujaDe) return;
        _burbujaDe = a.Numero;
        uint rgb = a.Color == 0 ? 0xFFFFFFu : a.Color;
        _aroBurbuja.Brush = Pintura(rgb, 200);
        // El aura del color del aviso, igual que la de una cancion lleva el de su caratula.
        Color tinte = Color.FromArgb(56, (byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb);
        _degradadoAviso.ColorStops[0].Color = tinte;
        _degradadoAviso.ColorStops[1].Color = Color.FromArgb(0, tinte.R, tinte.G, tinte.B);
        if (_insignia.Brush is CompositionMaskBrush vieja)
        {
            _insignia.Brush = null;
            Soltar(vieja.Mask);
            vieja.Dispose();
        }

        if (a.Insignia.Length == 0)
        {
            _insignia.Size = Vector2.Zero;
            _puntoBurbuja.Brush = Pintura(rgb);
            return;
        }

        // El texto sale blanco de Texto; con el como mascara, se pinta del color del aviso.
        float px = S(InsigniaPx);
        Vector2 tam = Texto.Medir(a.Insignia, px, true, false);
        CompositionMaskBrush pincel = _compositor.CreateMaskBrush();
        pincel.Source = Pintura(rgb);
        pincel.Mask = PincelTexto(a.Insignia, px, true, 1f, tam);
        float lado = S(IslaWindow.BurbujaLado);
        _insignia.Size = tam;
        _insignia.Offset = new Vector3(MathF.Round((lado - tam.X) * 0.5f), MathF.Round((lado - tam.Y) * 0.5f), 0);
        _insignia.Brush = pincel;
        _puntoBurbuja.Brush = null;
    }

    /// <summary>
    /// La isla principal, apagada cuando en la brasa no hay nada suyo que ensenar, o cuando
    /// el aviso ocupa el centro. <paramref name="instantaneo"/> al apagarla: un fundido la
    /// dejaria un instante asomando por encima de la pastilla.
    /// </summary>
    public void Principal(bool visible, bool instantaneo = false)
    {
        float objetivo = visible ? 1f : 0f;
        if (!instantaneo)
        {
            Fundir(_grupo, objetivo);
            return;
        }

        _grupo.StopAnimation("Opacity");
        _grupo.Opacity = objetivo;
    }

    private void Fundir(Visual v, float objetivo)
    {
        if (Math.Abs(v.Opacity - objetivo) < 0.001f) return;
        ScalarKeyFrameAnimation a = _compositor.CreateScalarKeyFrameAnimation();
        a.InsertKeyFrame(1f, objetivo);
        a.Duration = TimeSpan.FromMilliseconds(160);
        v.StartAnimation("Opacity", a);
        v.Opacity = objetivo;
    }

    /// <summary>
    /// Pone el titular. Si no lleva caratula el texto se pega al borde: una miniatura
    /// gris vacia al lado de "Bateria 78 %" no aporta nada y estorba.
    /// </summary>
    public void Compacto(Aviso a, bool hayCaratula)
    {
        bool mini = a.ConCaratula && hayCaratula;
        _miniatura.Brush = mini ? _caratula.Brush : null;
        _puntoCompacto.Brush = a.Color != 0 ? Pintura(a.Color) : null;

        float x = mini ? CompMargen * 2f + CompMiniLado
                : a.Color != 0 ? CompMargen * 2f + PuntoCompacto + 8f
                : CompMargen * 2f;
        _cajaCompacta.Offset = new Vector3(S(x), S(CompTextoY), 0);

        if (a.Texto == _textoCompacto) return;
        _textoCompacto = a.Texto;
        Rotular(_rotCompacto, Cabe(a.Texto, _cajaCompacta.Size.X, S(CompTextoPx)), CompTextoPx, true, 0.95f);
    }

    /// <summary>
    /// Lo que cabe de <paramref name="s"/> en <paramref name="ancho"/>, con puntos
    /// suspensivos si sobra.
    ///
    /// <para>
    /// La caja compacta lleva <c>InsetClip</c> y aqui no hay marquesina -- la que pasea
    /// el titulo solo corre en el estado abierto --, asi que sin esto un aviso largo no
    /// se recorta: se <b>corta</b>, a mitad de letra y sin que se note que falta algo.
    /// Se vio al estrenar el nombre del dispositivo: «LG ULTRAWIDE (NVIDI». Le pasaba
    /// igual a cualquier aviso largo desde el primer dia.
    /// </para>
    ///
    /// <para>
    /// Busqueda binaria y no letra a letra: medir con DirectWrite cruza a la fuente y
    /// esto corre en el hilo de UI. Para 43 caracteres son 6 medidas en vez de 43.
    /// </para>
    /// </summary>
    private static string Cabe(string s, float ancho, float px, bool grueso = true)
    {
        if (string.IsNullOrEmpty(s) || ancho <= 0f) return s;
        if (Texto.Medir(s, px, grueso).X <= ancho) return s;

        const string Puntos = "…";

        // Invariante: bajo siempre cabe, alto+1 nunca. Se busca el ultimo que cabe.
        int bajo = 0, alto = s.Length - 1;
        while (bajo < alto)
        {
            int medio = (bajo + alto + 1) / 2;
            if (Texto.Medir(s[..medio] + Puntos, px, grueso).X <= ancho) bajo = medio;
            else alto = medio - 1;
        }

        return bajo == 0 ? Puntos : s[..bajo].TrimEnd() + Puntos;
    }

    /// <summary>Una caja que recorta el texto a su rectangulo. El hijo se pinta en (0, 0).</summary>
    private ContainerVisual Caja(ContainerVisual padre, float x, float y, float ancho, float px)
    {
        ContainerVisual caja = _compositor.CreateContainerVisual();
        caja.Size = new Vector2(S(ancho), S(px) * 1.7f);
        caja.Offset = new Vector3(S(x), S(y), 0);
        caja.Clip = _compositor.CreateInsetClip();
        padre.Children.InsertAtTop(caja);
        return caja;
    }

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
        Rotular(_rotArtista, Cabe(c.Artista, S(TituloAncho), S(ArtistaPx), grueso: false), ArtistaPx, false, 0.62f);
        LineaApp(c);
        Marquesina(_rotTitulo, _cajaTitulo.Size.X);

        CompositionBrush? arteVieja = _caratula.Brush;
        _caratula.Brush = c.Arte is null ? _grisCaratula : PincelArte(c.Arte);
        _notaCaratula.IsVisible = c.Arte is null;
        Soltar(arteVieja);

        // El aura se tine del color de la caratula. Alfa bajo a proposito: tiene que
        // notarse que la cancion cambio, no leerse como un fondo de color.
        Color tinte = Color.FromArgb(56, (byte)(c.Tinte >> 16), (byte)(c.Tinte >> 8), (byte)c.Tinte);
        _degradado.ColorStops[0].Color = c.Tinte == 0 ? Color.FromArgb(0, 0, 0, 0) : tinte;
        _degradado.ColorStops[1].Color = Color.FromArgb(0, tinte.R, tinte.G, tinte.B);

        Tiempos(c.Posicion, c.Duracion);

        // Un boton que la sesion no admite no se dibuja.
        Icono(_botAnterior, c.PuedeAnterior ? GlifoAnterior : null, BotonPx, BotonCx[0]);
        Icono(_botPlay, c.PuedePlayPausa ? (c.Sonando ? GlifoPausa : GlifoPlay) : null, PlayPx, BotonCx[1]);
        Icono(_botSiguiente, c.PuedeSiguiente ? GlifoSiguiente : null, BotonPx, BotonCx[2]);
    }

    /// <summary>
    /// La linea pequena bajo el artista: el nombre de la app, o el volumen mientras gira
    /// la rueda.
    /// </summary>
    public void LineaApp(string s)
        => Rotular(_rotApp, Cabe(s, S(AppAncho), S(AppPx), grueso: false), AppPx, false, 0.38f);

    /// <summary>
    /// El nombre de la app; con mas de una sesion, entre flechas y un poco mas claro, porque
    /// entonces es un boton (ver <see cref="Zona.App"/>).
    /// </summary>
    public void LineaApp(Cancion c)
    {
        _hayOtras = c.Sesiones > 1;
        if (!_hayOtras) { LineaApp(c.App); return; }
        string app = Cabe(c.App, S(AppAncho - 28f), S(AppPx), grueso: false);
        Rotular(_rotApp, $"‹  {app}  ›", AppPx, false, 0.62f);
    }

    private bool _hayOtras;

    /// <summary>
    /// El pomodoro, en el panel y en la brasa. <paramref name="texto"/> nulo lo apaga;
    /// <paramref name="fraccion"/> es lo que QUEDA, de 1 a 0.
    /// </summary>
    public void Pomodoro(string? texto, float fraccion, bool pausa)
    {
        _hayPomodoro = texto is not null;
        _pomodoro.IsVisible = _hayPomodoro;
        _brasaPomodoro.IsVisible = _hayPomodoro;
        if (texto is null) return;

        fraccion = Math.Clamp(fraccion, 0f, 1f);
        _pomRelleno.Scale = new Vector3(fraccion, 1f, 1f);
        _brasaPomodoro.Scale = new Vector3(fraccion, 1f, 1f);

        // Centrados en la linea de los botones, un poco por encima para dejar sitio a la barra.
        float cy = S(BotonCy - 3f);
        Rotular(_pomGlifo, pausa ? GlifoPausa : GlifoReloj, PomPx, false, pausa ? 0.5f : 0.8f, iconos: true);
        _pomGlifo.Offset = new Vector3(S(Margen), cy - _pomGlifo.Size.Y * 0.5f, 0);
        Rotular(_pomTexto, texto, PomPx, true, pausa ? 0.5f : 0.92f);
        _pomTexto.Offset = new Vector3(S(Margen) + _pomGlifo.Size.X + S(4f), cy - _pomTexto.Size.Y * 0.5f, 0);
    }

    /// <summary>Los dos relojes de los extremos de la barra. Solo al cambiar de cancion.</summary>
    public void Tiempos(TimeSpan pasado, TimeSpan total)
    {
        Transcurrido(pasado);
        Rotular(_rotTotal, Reloj(total), TiempoPx, false, 0.38f);
        // El total se alinea a la derecha, asi que su sitio depende de lo que mida.
        _rotTotal.Offset = new Vector3(S(BarraX + BarraAncho) - _rotTotal.Size.X, S(TiempoY), 0);
    }

    /// <summary>
    /// Solo el reloj de la izquierda, que es el unico que cambia cada segundo. Repintar
    /// tambien la duracion total -- que no se mueve en toda la cancion -- doblaba el
    /// trabajo del unico temporizador que corre con el panel abierto.
    /// </summary>
    public void Transcurrido(TimeSpan pasado)
        => Rotular(_rotPasado, Reloj(pasado), TiempoPx, false, 0.38f);

    /// <summary>
    /// La barra avanza EN EL COMPOSITOR: se le dice donde esta y cuanto queda, y el
    /// interpola el resto. Cero trabajo por fotograma en el hilo de UI, y por eso la
    /// isla puede tener una barra que se mueve sola con 0% de CPU.
    /// </summary>
    public void Progreso(double fraccion, TimeSpan queda, bool sonando)
    {
        float f = (float)Math.Clamp(fraccion, 0d, 1d);
        _relleno.StopAnimation("Scale.X");

        if (!sonando || queda <= TimeSpan.Zero)
        {
            _relleno.Scale = new Vector3(f, 1f, 1f);
            return;
        }

        CompositionEasingFunction lineal = _compositor.CreateLinearEasingFunction();
        ScalarKeyFrameAnimation a = _compositor.CreateScalarKeyFrameAnimation();
        a.InsertKeyFrame(0f, f, lineal);
        a.InsertKeyFrame(1f, 1f, lineal);
        a.Duration = queda;
        _relleno.StartAnimation("Scale.X", a);
    }

    /// <summary>Mientras se arrastra, la barra sigue al dedo y no al reloj.</summary>
    public void VistaPrevia(double fraccion)
    {
        _relleno.StopAnimation("Scale.X");
        _relleno.Scale = new Vector3((float)Math.Clamp(fraccion, 0d, 1d), 1f, 1f);
    }

    /// <summary>Pasa de coordenadas de la ventana a coordenadas del panel abierto.</summary>
    public Vector2 EnPanel(Vector2 cliente, float anchoVentana)
    {
        (float w, _, _, float lift) = IslaWindow.Medidas(Estado.Abierta);
        return new Vector2(cliente.X - (anchoVentana - S(w)) * 0.5f, cliente.Y - S(lift));
    }

    public Zona Golpe(Vector2 p)
    {
        float mitad = S(GolpeLado) * 0.5f;
        for (int i = 0; i < BotonCx.Length; i++)
        {
            if (Math.Abs(p.X - S(BotonCx[i])) <= mitad && Math.Abs(p.Y - S(BotonCy)) <= mitad)
                return (Zona)(i + 1);
        }

        // La barra se coge con mucho mas margen del que mide: acertarle a 4 px de alto
        // con el raton no lo hace nadie.
        if (p.X >= S(BarraX - 6f) && p.X <= S(BarraX + BarraAncho + 6f)
            && p.Y >= S(BarraY - 9f) && p.Y <= S(BarraY + BarraAlto + 9f))
            return Zona.Barra;

        // El pomodoro, a la izquierda de los botones: un clic lo pausa o lo reanuda.
        if (_hayPomodoro && p.X >= S(Margen - 6f) && p.X <= S(Margen + PomAncho + 6f)
            && Math.Abs(p.Y - S(BotonCy)) <= S(GolpeLado * 0.5f))
            return Zona.Pomodoro;

        // El nombre de la app, solo si hay otra sesion a la que pasar. Todo el ancho de su
        // linea y algo de alto: el texto mide 10 px.
        if (_hayOtras && p.X >= S(TextoX - 4f) && p.X <= S(TextoX + AppAncho)
            && p.Y >= S(AppY - 5f) && p.Y <= S(AppY + AppPx + 7f))
            return Zona.App;

        return Zona.Nada;
    }

    public double FraccionEnX(float x) => Math.Clamp((x - S(BarraX)) / S(BarraAncho), 0d, 1d);

    private static string Reloj(TimeSpan t)
    {
        if (t < TimeSpan.Zero) t = TimeSpan.Zero;
        return t.TotalHours >= 1 ? t.ToString(@"h\:mm\:ss") : t.ToString(@"m\:ss");
    }

    private void Icono(SpriteVisual v, string? glifo, float px, float cx)
    {
        if (glifo is null)
        {
            v.Size = Vector2.Zero;
            CompositionBrush? habia = v.Brush;
            v.Brush = null;
            Soltar(habia);
            return;
        }

        float fisico = S(px);
        Vector2 tam = Texto.Medir(glifo, fisico, false, iconos: true);

        CompositionBrush? anterior = v.Brush;
        v.Size = tam;
        v.Brush = PincelTexto(glifo, fisico, false, 0.92f, tam, iconos: true);
        v.Offset = new Vector3(S(cx) - tam.X * 0.5f, S(BotonCy) - tam.Y * 0.5f, 0);
        Soltar(anterior);
    }

    private void Rotular(SpriteVisual v, string s, float px, bool grueso, float alpha, bool iconos = false)
    {
        if (string.IsNullOrWhiteSpace(s))
        {
            // Una superficie de tamano cero no se puede pedir, asi que se deja sin
            // pincel: el visual sigue ahi y no pinta nada.
            v.Size = Vector2.Zero;
            CompositionBrush? habia = v.Brush;
            v.Brush = null;
            Soltar(habia);
            return;
        }

        float fisico = S(px);
        Vector2 tam = Texto.Medir(s, fisico, grueso, iconos);

        CompositionBrush? anterior = v.Brush;
        v.Size = tam;
        v.Brush = PincelTexto(s, fisico, grueso, alpha, tam, iconos);
        Soltar(anterior);
    }

    /// <summary>
    /// Suelta la superficie que habia detras de un pincel que se acaba de sustituir.
    /// Sin esto, el reloj del panel abierto dejaba dos superficies abandonadas POR
    /// SEGUNDO esperando al recolector.
    /// </summary>
    private static void Soltar(CompositionBrush? viejo)
    {
        if (viejo is not CompositionSurfaceBrush pincel) return;
        if (pincel.Surface is CompositionDrawingSurface superficie) superficie.Dispose();
        pincel.Dispose();
    }

    /// <summary>
    /// La caratula que trajo la sesion, ya decodificada a BGRA premultiplicado. Se
    /// decodifico a 192 y aqui se baja al tamano de pantalla, que es donde D2D
    /// interpola mejor que hacerlo a mano.
    /// </summary>
    /// <summary>Radio de la pastilla abierta, que es la unica que lleva sombra.</summary>
    public const float SombraRadio = 28f;

    /// <summary>
    /// El sprite de la sombra. La mascara es la pastilla abierta tal cual -- 380x180, radio 28 --
    /// hecha a mano en BGRA, con la misma subida de pixeles que la caratula: sin API de dibujo
    /// nueva. Se estira con la caja, pero solo se ve con el panel entero, que es su tamano.
    ///
    /// Un nine-grid de solo las esquinas seria lo fino, y se probo: como mascara de DropShadow
    /// no pinta nada. Medido leyendo el borde: plano, igual que sin sombra.
    /// </summary>
    private SpriteVisual Sombra()
    {
        (float wl, float hl, _, _) = IslaWindow.Medidas(Estado.Abierta);
        int w = (int)MathF.Ceiling(S(wl)), h = (int)MathF.Ceiling(S(hl));
        float r = S(SombraRadio);
        byte[] bgra = new byte[w * h * 4];
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                float px = x + 0.5f, py = y + 0.5f;
                float dx = MathF.Max(MathF.Max(r - px, px - (w - r)), 0f);
                float dy = MathF.Max(MathF.Max(r - py, py - (h - r)), 0f);
                float a = Math.Clamp(r - MathF.Sqrt(dx * dx + dy * dy) + 0.5f, 0f, 1f);
                byte alfa = (byte)(a * 255f);
                int i = (y * w + x) * 4;
                bgra[i] = bgra[i + 1] = bgra[i + 2] = bgra[i + 3] = alfa;
            }
        }

        CompositionSurfaceBrush molde = PincelBgra(bgra, w, h, w, h);
        molde.Stretch = CompositionStretch.Fill;

        DropShadow sombra = _compositor.CreateDropShadow();
        sombra.Mask = molde;
        sombra.BlurRadius = S(24f);
        sombra.Offset = new Vector3(0f, S(6f), 0f);
        sombra.Color = Color.FromArgb(255, 0, 0, 0);
        sombra.Opacity = 0.45f;

        SpriteVisual v = _compositor.CreateSpriteVisual();
        v.Shadow = sombra;
        return v;
    }

    private CompositionSurfaceBrush PincelArte(byte[] bgra)
        => PincelBgra(bgra, Medios.ArteLado, Medios.ArteLado, S(CaratulaLado), S(CaratulaLado));

    /// <summary>Sube un BGRA premultiplicado de <paramref name="ancho"/> x <paramref name="alto"/> y lo pinta a <paramref name="destW"/> x <paramref name="destH"/>.</summary>
    private CompositionSurfaceBrush PincelBgra(byte[] bgra, int ancho, int alto, float destW, float destH)
    {
        CompositionDrawingSurface superficie = EnsureGraphicsDevice().CreateDrawingSurface(
            new global::Windows.Foundation.Size(destW, destH),
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

            fixed (byte* datos = bgra)
            {
                ctx.CreateBitmap(
                    new D2D_SIZE_U { width = (uint)ancho, height = (uint)alto },
                    datos, (uint)(ancho * 4), propiedades, out ID2D1Bitmap1 mapa);

                D2D_RECT_F destino = new()
                {
                    left = offset.X,
                    top = offset.Y,
                    right = offset.X + destW,
                    bottom = offset.Y + destH,
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
    private CompositionSurfaceBrush PincelTexto(string s, float px, bool grueso, float alpha, Vector2 tam, bool iconos = false)
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
            Texto.Dibujar(ctx, s, px, grueso, alpha, offset, iconos);
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
