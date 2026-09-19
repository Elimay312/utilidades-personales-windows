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

namespace Renombrar;

/// <summary>
/// El arbol de visuals, enganchado al HWND propio.
///
/// Se usa <c>Windows.UI.Composition</c> del SISTEMA, no la del WinAppSDK: las animaciones
/// corren en el proceso de DWM, asi que el desplazamiento de la lista no se entrecorta
/// aunque el hilo de UI este recalculando la previa. Es el mismo argumento que decidio el
/// stack del dock y de la isla.
/// </summary>
internal sealed unsafe class Visuales : IDisposable
{
    private const uint VersionSdkD3D11 = 7;

    /// <summary>
    /// La banda de los controles, que la fija <see cref="Controles"/>. <b>Composition no
    /// pinta ahi</b>: la raiz va recortada por arriba justo esta altura, porque el arbol de
    /// visuals se compone POR ENCIMA del contenido del HWND y tapaba los controles hijos.
    /// Esa banda la pinta el propio WndProc con una brocha.
    /// </summary>
    internal const float AltoFranja = Controles.AltoFranja;

    /// <summary>La linea de estado, justo debajo de los controles. Esta si es nuestra.</summary>
    private const float AltoEstado = 26f;

    // Si se recoge, el compositor se queda sin cola de despacho en este hilo.
    private static object? _cola;

    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _destino;
    private readonly ContainerVisual _raiz;
    private readonly InsetClip _recorte;
    private readonly SpriteVisual _fondo;
    private readonly SpriteVisual _titulo;
    private readonly ContainerVisual _marco;
    private readonly ContainerVisual _lista;
    private CompositionGraphicsDevice? _graficos;

    /// <summary>Las filas que tienen visual ahora mismo, por indice. Solo las que se ven.</summary>
    private readonly Dictionary<int, SpriteVisual> _pintadas = [];

    private List<Fila> _filas = [];
    private string _cabecera = "Suelta aquí archivos o una carpeta";
    private float _escala = 1f;
    private float _ancho, _alto;
    private float _desplazamiento;

    internal Visuales(HWND hwnd, float escala)
    {
        AseguraCola();
        _escala = escala;

        _compositor = new Compositor();

        // El puente Win32 -> Composition. CsWin32 marshala el puntero COM directamente al
        // tipo proyectado.
        _compositor.As<ICompositorDesktopInterop>().CreateDesktopWindowTarget(hwnd, true, out _destino);

        _raiz = _compositor.CreateContainerVisual();
        _raiz.RelativeSizeAdjustment = Vector2.One;
        // Por la PROPIEDAD y no por los cuatro argumentos de CreateInsetClip: pasandolos
        // en orden, el 52 se fue al lado izquierdo y la tabla aparecia con los primeros
        // cuatro caracteres comidos. Medido en la captura: la franja gris del WndProc
        // llegaba hasta x=52 y la Composition empezaba ahi.
        _recorte = _compositor.CreateInsetClip();
        _recorte.TopInset = AltoFranja * escala;
        _raiz.Clip = _recorte;
        _destino.Root = _raiz;

        // El acrilico va DEBAJO de la franja, no detras: un control EDIT del sistema pinta
        // su propio fondo opaco y encima del acrilico se ve el parche. Es la leccion que
        // el lanzador ya pago, y aqui se hereda antes de tener los controles.
        _fondo = _compositor.CreateSpriteVisual();
        _fondo.Brush = _compositor.CreateHostBackdropBrush();
        _fondo.RelativeSizeAdjustment = new Vector2(1f, 1f);
        _raiz.Children.InsertAtTop(_fondo);

        SpriteVisual velo = _compositor.CreateSpriteVisual();
        velo.Brush = _compositor.CreateColorBrush(Color.FromArgb(150, 18, 18, 22));
        velo.RelativeSizeAdjustment = new Vector2(1f, 1f);
        _raiz.Children.InsertAtTop(velo);

        _titulo = _compositor.CreateSpriteVisual();
        _titulo.Offset = new Vector3(0f, AltoFranja * escala, 0f);
        _titulo.Size = new Vector2(0f, AltoEstado * escala);
        _raiz.Children.InsertAtTop(_titulo);

        // Dos contenedores y no uno: el marco se queda quieto y recorta, y la lista se
        // desplaza dentro. Con uno solo, el recorte viaja con el desplazamiento —un
        // InsetClip recorta contra el TAMANO del visual— y ademas un contenedor sin tamano
        // recorta a cero: la tabla entera se volvia invisible sin que nada fallase.
        _marco = _compositor.CreateContainerVisual();
        _marco.Offset = new Vector3(0f, AltoFranja * escala, 0f);
        _marco.Clip = _compositor.CreateInsetClip();
        _raiz.Children.InsertAtTop(_marco);

        _lista = _compositor.CreateContainerVisual();
        _marco.Children.InsertAtTop(_lista);
    }

    internal float Escala => _escala;

    /// <summary>Alto util de la lista, en pixeles reales.</summary>
    private float Hueco => MathF.Max(_alto - (AltoFranja + AltoEstado) * _escala, 1f);

    private float AltoFila => Texto.AltoFila * _escala;

    internal void Redimensiona(float ancho, float alto, float escala)
    {
        bool cambiaAncho = ancho != _ancho || escala != _escala;

        _ancho = ancho;
        _alto = alto;
        _escala = escala;
        _recorte.TopInset = AltoFranja * escala;
        _titulo.Offset = new Vector3(0f, AltoFranja * escala, 0f);
        _titulo.Size = new Vector2(ancho, AltoEstado * escala);
        _marco.Offset = new Vector3(0f, (AltoFranja + AltoEstado) * escala, 0f);
        _marco.Size = new Vector2(ancho, Hueco);
        _lista.Offset = new Vector3(0f, _desplazamiento, 0f);

        // Las superficies llevan el ancho horneado dentro, asi que un cambio de ancho
        // obliga a repintarlas. Un cambio de alto solo cambia cuantas se ven.
        if (cambiaAncho)
        {
            Suelta();
            Cabecera(_cabecera);
        }

        Limita();
        Sincroniza();
    }

    /// <summary>La linea de la franja: donde estas y cuantos van a cambiar.</summary>
    internal void Cabecera(string texto)
    {
        _cabecera = texto;
        if (_ancho <= 0f) return;

        float alto = AltoEstado * _escala;
        _titulo.Size = new Vector2(_ancho, alto);
        _titulo.Brush = _compositor.CreateSurfaceBrush(
            Dibuja(_ancho, alto, ctx => Texto.Cabecera(ctx, texto, _ancho, alto, _escala)));
    }

    internal void Ensena(List<Fila> filas)
    {
        _filas = filas;
        _desplazamiento = 0f;
        Suelta();
        Sincroniza();
        _lista.Offset = Vector3.Zero;
    }

    /// <summary>
    /// La rueda. El salto se anima con un muelle en vez de escribirse a pelo: la animacion
    /// corre en DWM, asi que la lista sigue deslizandose con inercia mientras este hilo
    /// esta recalculando la previa o creando superficies.
    /// </summary>
    internal void Rueda(float clics)
    {
        if (_filas.Count == 0) return;

        _desplazamiento += clics * AltoFila * 3f;
        Limita();

        // Las filas que van a entrar se pintan YA, con el destino del muelle, no con donde
        // esta la lista ahora: si se esperase a que llegue, se verian huecos en blanco
        // durante todo el deslizamiento.
        Sincroniza();

        Vector3KeyFrameAnimation muelle = _compositor.CreateVector3KeyFrameAnimation();
        muelle.InsertKeyFrame(1f, new Vector3(0f, _desplazamiento, 0f),
                              _compositor.CreateCubicBezierEasingFunction(new Vector2(0.1f, 0.9f), new Vector2(0.2f, 1f)));
        muelle.Duration = TimeSpan.FromMilliseconds(220);
        _lista.StartAnimation("Offset", muelle);
    }

    private void Limita()
    {
        float sobra = MathF.Max(_filas.Count * AltoFila - Hueco, 0f);
        _desplazamiento = Math.Clamp(_desplazamiento, -sobra, 0f);
    }

    /// <summary>
    /// Crea las filas que se ven y suelta las que ya no. Sin esto, una carpeta de 300
    /// recibos serian 300 superficies de ~100 KB cada una para ensenar quince.
    /// </summary>
    private void Sincroniza()
    {
        if (_ancho <= 0f) return;

        int primera = Math.Max((int)(-_desplazamiento / AltoFila) - 4, 0);
        int ultima = Math.Min(primera + (int)(Hueco / AltoFila) + 8, _filas.Count - 1);

        foreach (int i in _pintadas.Keys.Where(i => i < primera || i > ultima).ToArray())
        {
            _lista.Children.Remove(_pintadas[i]);
            _pintadas.Remove(i);
        }

        for (int i = primera; i <= ultima; i++)
        {
            if (_pintadas.ContainsKey(i)) continue;

            SpriteVisual fila = _compositor.CreateSpriteVisual();
            fila.Size = new Vector2(_ancho, AltoFila);
            fila.Offset = new Vector3(0f, i * AltoFila, 0f);
            Fila cual = _filas[i];
            fila.Brush = _compositor.CreateSurfaceBrush(
                Dibuja(_ancho, AltoFila, ctx => Texto.Fila(ctx, cual, _ancho, _escala)));

            _lista.Children.InsertAtTop(fila);
            _pintadas[i] = fila;
        }
    }

    private void Suelta()
    {
        _lista.Children.RemoveAll();
        _pintadas.Clear();
    }

    /// <summary>Una superficie del tamano pedido, con lo que el que llama quiera pintar dentro.</summary>
    private CompositionDrawingSurface Dibuja(float ancho, float alto, Action<ID2D1DeviceContext> pinta)
    {
        CompositionDrawingSurface superficie = Graficos().CreateDrawingSurface(
            new global::Windows.Foundation.Size(ancho, alto),
            DirectXPixelFormat.B8G8R8A8UIntNormalized,
            DirectXAlphaMode.Premultiplied);

        ICompositionDrawingSurfaceInterop interop = superficie.As<ICompositionDrawingSurfaceInterop>();
        Guid iid = typeof(ID2D1DeviceContext).GUID;

        // BeginDraw devuelve un HUECO dentro de un atlas compartido, no una superficie
        // propia que empiece en (0,0). Dibujando en cero, las filas salen vacias: todo va a
        // parar al trozo de la primera superficie, que ademas es la que se lleva la cabecera.
        System.Drawing.Point hueco;
        interop.BeginDraw(null, &iid, out object contexto, &hueco);
        try
        {
            var ctx = (ID2D1DeviceContext)contexto;

            // Un device context de D2D nace con el DPI del escritorio y DrawTextLayout
            // trabaja en DIPs. Sin fijarlo a 96, todo saldria otra vez a escala y el texto
            // se dibujaria al 125% de lo que mide la superficie.
            ctx.SetDpi(96, 96);

            D2D1_COLOR_F nada = default;
            ctx.Clear(&nada);

            // Se traslada el origen y el que pinta sigue creyendo que empieza en (0,0).
            // MEDIDO: sin esta traslacion, las filas salen VACIAS —0 pixeles de texto en
            // la captura, frente a 110 por fila con ella—, porque todo se dibuja en el
            // trozo de atlas de la primera superficie. Clear no hace falta acotarlo: con
            // un recorte explicito al hueco propio salen exactamente los mismos pixeles.
            Matrix3x2 donde = Matrix3x2.CreateTranslation(hueco.X, hueco.Y);
            ctx.SetTransform((D2D_MATRIX_3X2_F*)&donde);

            pinta(ctx);
        }
        finally
        {
            interop.EndDraw();
        }

        return superficie;
    }

    /// <summary>
    /// Windows.UI.Composition exige una DispatcherQueue en el hilo antes de poder crear el
    /// Compositor. Una sola por proceso basta.
    /// </summary>
    private static void AseguraCola()
    {
        if (_cola is not null) return;

        DispatcherQueueOptions opciones = new()
        {
            dwSize = (uint)sizeof(DispatcherQueueOptions),
            threadType = DISPATCHERQUEUE_THREAD_TYPE.DQTYPE_THREAD_CURRENT,
            // Con DQTYPE_THREAD_CURRENT la documentacion exige DQTAT_COM_NONE.
            apartmentType = DISPATCHERQUEUE_THREAD_APARTMENTTYPE.DQTAT_COM_NONE,
        };

        PInvoke.CreateDispatcherQueueController(opciones, out global::Windows.System.DispatcherQueueController controlador);
        _cola = controlador;
    }

    /// <summary>Fuera de XAML no existe LoadedImageSurface, asi que para dibujar hay que pasar por D3D11 -> D2D -> Composition.</summary>
    private CompositionGraphicsDevice Graficos()
    {
        if (_graficos is not null) return _graficos;

        // BGRA_SUPPORT es obligatorio para poder interoperar con Direct2D.
        PInvoke.D3D11CreateDevice(
            null, D3D_DRIVER_TYPE.D3D_DRIVER_TYPE_HARDWARE, default,
            D3D11_CREATE_DEVICE_FLAG.D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            null, 0, VersionSdkD3D11,
            out ID3D11Device d3d, null, out _).ThrowOnFailure();

        PInvoke.D2D1CreateDevice((IDXGIDevice)d3d, null, out ID2D1Device d2d).ThrowOnFailure();

        _compositor.As<ICompositorInterop>().CreateGraphicsDevice(d2d, out _graficos);
        return _graficos;
    }

    public void Dispose()
    {
        Suelta();
        _destino.Dispose();
        _compositor.Dispose();
    }
}
