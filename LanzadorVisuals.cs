using System.Numerics;
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

namespace Lanzador;

/// <summary>
/// La lista de resultados, sobre <c>Windows.UI.Composition</c>.
/// <para>
/// <b>Una sola superficie para toda la lista</b>, no una por fila ni dos por fila. Se
/// redibuja entera en cada pulsacion, que es un unico BeginDraw y una unica subida de
/// pixeles; ocho filas con dos superficies cada una serian dieciseis objetos nuevos por
/// tecla. El precio es que mover la seleccion tambien repinta la lista entera, y a este
/// tamano eso no se nota — medido en H4.
/// </para>
/// </summary>
internal sealed unsafe class LanzadorVisuals : IDisposable
{
    private const uint D3D11SdkVersion = 7;

    // Las mismas medidas logicas que usa la ventana. Estan repetidas a proposito y no
    // compartidas en una clase de constantes: son dos cosas distintas que hoy coinciden,
    // y una clase "Medidas" con seis campos es la abstraccion que sobra.
    private const float AltoFila = 44f;
    private const float AltoFranja = 56f;
    private const float MargenLista = 8f;
    private const float MargenTexto = 18f;

    private static Windows.System.DispatcherQueueController? _cola;

    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _target;
    private readonly ContainerVisual _raiz;
    private readonly SpriteVisual _lista;
    private float _escala;
    private float _ancho;
    private CompositionGraphicsDevice? _graficos;

    public LanzadorVisuals(HWND hwnd, float escala, float anchoFisico)
    {
        _escala = escala;
        _ancho = anchoFisico;

        AsegurarCola();
        _compositor = new Compositor();

        ICompositorDesktopInterop interop = _compositor.As<ICompositorDesktopInterop>();
        interop.CreateDesktopWindowTarget(hwnd, true, out _target);

        _raiz = _compositor.CreateContainerVisual();
        _raiz.RelativeSizeAdjustment = Vector2.One;
        _target.Root = _raiz;

        _lista = _compositor.CreateSpriteVisual();
        _lista.Offset = new Vector3(0, S(AltoFranja), 0);
        _raiz.Children.InsertAtTop(_lista);
    }

    /// <summary>
    /// La ventana cambio de pantalla y con ella la escala. Se llama antes de pintar, asi
    /// que no hace falta repintar aqui.
    /// </summary>
    public void Reescalar(float escala, float anchoFisico)
    {
        _escala = escala;
        _ancho = anchoFisico;
        _lista.Offset = new Vector3(0, S(AltoFranja), 0);
    }

    /// <summary>
    /// Redibuja la lista entera. Con cero resultados la deja a tamano cero, que es como
    /// desaparece sin tener que quitarla del arbol.
    /// </summary>
    public void Pintar(IReadOnlyList<Resultado> resultados, int elegido)
    {
        CompositionBrush? viejo = _lista.Brush;

        if (resultados.Count == 0)
        {
            _lista.Brush = null;
            _lista.Size = Vector2.Zero;
            Soltar(viejo);
            return;
        }

        float alto = S(MargenLista) * 2 + resultados.Count * S(AltoFila);
        _lista.Size = new Vector2(_ancho, alto);
        _lista.Brush = PincelLista(resultados, elegido, alto);
        Soltar(viejo);
    }

    private CompositionSurfaceBrush PincelLista(IReadOnlyList<Resultado> resultados, int elegido, float alto)
    {
        CompositionDrawingSurface superficie = AsegurarDispositivo().CreateDrawingSurface(
            new global::Windows.Foundation.Size(_ancho, alto),
            DirectXPixelFormat.B8G8R8A8UIntNormalized,
            DirectXAlphaMode.Premultiplied);

        ICompositionDrawingSurfaceInterop interop = superficie.As<ICompositionDrawingSurfaceInterop>();
        Guid iid = typeof(ID2D1DeviceContext).GUID;

        System.Drawing.Point desplazamiento;
        interop.BeginDraw(null, &iid, out object obj, &desplazamiento);
        try
        {
            var ctx = (ID2D1DeviceContext)obj;

            // Las tres trampas que ya pago la isla: BeginDraw devuelve un OFFSET porque
            // la superficie puede ser un hueco de un atlas compartido; el contexto nace
            // con el DPI del escritorio y hay que fijarlo a 96 o todo sale mas grande
            // que la superficie; y hay que limpiarla, porque ese hueco puede traer los
            // pixeles del inquilino anterior.
            ctx.SetDpi(96, 96);
            D2D1_COLOR_F nada = default;
            ctx.Clear(&nada);

            for (int i = 0; i < resultados.Count; i++)
            {
                float y = desplazamiento.Y + S(MargenLista) + i * S(AltoFila);
                Fila(ctx, resultados[i], desplazamiento.X, y, i == elegido);
            }
        }
        finally
        {
            interop.EndDraw();
        }

        return _compositor.CreateSurfaceBrush(superficie);
    }

    private void Fila(ID2D1DeviceContext ctx, Resultado r, float x, float y, bool elegida)
    {
        if (elegida)
        {
            // Un rectangulo claro a poca opacidad: sobre el acrilico oscuro se lee como
            // un realce y no tapa lo de detras.
            D2D1_COLOR_F realce = new() { r = 1f, g = 1f, b = 1f, a = 0.14f };
            ctx.CreateSolidColorBrush(&realce, null, out ID2D1SolidColorBrush pincel);

            D2D1_ROUNDED_RECT caja = new()
            {
                rect = new D2D_RECT_F
                {
                    left = x + S(MargenTexto) - S(8f),
                    top = y + S(2f),
                    right = x + _ancho - S(MargenTexto) + S(8f),
                    bottom = y + S(AltoFila) - S(2f),
                },
                radiusX = S(8f),
                radiusY = S(8f),
            };
            ctx.FillRoundedRectangle(caja, pincel);
        }

        float izquierda = x + S(MargenTexto);

        // El nombre a plena opacidad; el destino debajo y apagado, que es donde miras
        // solo cuando dos resultados se llaman parecido.
        Texto.Dibujar(ctx, r.Entrada.Nombre, S(15f), grueso: true, 1f,
                      new System.Drawing.Point((int)izquierda, (int)(y + S(4f))));

        Texto.Dibujar(ctx, Acortar(r.Entrada.Destino), S(11f), grueso: false, 0.55f,
                      new System.Drawing.Point((int)izquierda, (int)(y + S(24f))));
    }

    /// <summary>
    /// Una ruta larga por el final, que es donde esta el nombre del fichero. Cortar por
    /// el principio dejaria ocho resultados que empiezan igual y no se distinguen.
    /// </summary>
    private static string Acortar(string destino)
    {
        const int Cabe = 78;
        return destino.Length <= Cabe ? destino : "…" + destino[^Cabe..];
    }

    private float S(float logico) => logico * _escala;

    private static void Soltar(CompositionBrush? viejo)
    {
        if (viejo is not CompositionSurfaceBrush pincel) return;
        if (pincel.Surface is CompositionDrawingSurface superficie) superficie.Dispose();
        pincel.Dispose();
    }

    /// <summary>
    /// Fuera de XAML no existe LoadedImageSurface: para pintar cualquier cosa que no sea
    /// un color plano hay que montar D3D11 -> D2D -> Composition.
    ///
    /// WARP y no HARDWARE, como la isla: este device solo SUBE pixeles y no renderiza un
    /// fotograma en su vida. BGRA_SUPPORT es obligatorio para interoperar con Direct2D.
    /// </summary>
    private CompositionGraphicsDevice AsegurarDispositivo()
    {
        if (_graficos is not null) return _graficos;

        PInvoke.D3D11CreateDevice(
            null, D3D_DRIVER_TYPE.D3D_DRIVER_TYPE_WARP, default,
            D3D11_CREATE_DEVICE_FLAG.D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            null, 0, D3D11SdkVersion,
            out ID3D11Device d3d, null, out _).ThrowOnFailure();

        PInvoke.D2D1CreateDevice((IDXGIDevice)d3d, null, out ID2D1Device d2d).ThrowOnFailure();
        _compositor.As<ICompositorInterop>().CreateGraphicsDevice(d2d, out _graficos);
        return _graficos;
    }

    /// <summary>
    /// Windows.UI.Composition exige una DispatcherQueue en el hilo antes de poder crear
    /// el Compositor. Con DQTYPE_THREAD_CURRENT la doc exige DQTAT_COM_NONE.
    /// </summary>
    private static void AsegurarCola()
    {
        if (_cola is not null) return;

        DispatcherQueueOptions opciones = new()
        {
            dwSize = (uint)sizeof(DispatcherQueueOptions),
            threadType = DISPATCHERQUEUE_THREAD_TYPE.DQTYPE_THREAD_CURRENT,
            apartmentType = DISPATCHERQUEUE_THREAD_APARTMENTTYPE.DQTAT_COM_NONE,
        };

        PInvoke.CreateDispatcherQueueController(opciones, out Windows.System.DispatcherQueueController c);
        _cola = c;
    }

    public void Dispose()
    {
        Soltar(_lista.Brush);
        _target.Root = null;
        _raiz.Dispose();
        _target.Dispose();
    }
}
