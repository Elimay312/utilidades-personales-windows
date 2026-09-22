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

namespace Lanzador;

/// <summary>
/// La lista de resultados, sobre <c>Windows.UI.Composition</c>.
/// <para>
/// <b>Una sola superficie para toda la lista</b>, no una por fila ni dos por fila: ocho
/// filas con dos superficies cada una serian dieciseis objetos nuevos por tecla.
/// </para>
/// <para>
/// Y <b>la misma superficie siempre</b>, reutilizada. Pedir una nueva en cada repintado
/// parecia inofensivo porque la anterior se soltaba, pero el compositor no las devuelve a
/// la vez: medido en H8, cincuenta repintados dejaban ~105 MB de conjunto de trabajo que
/// el monton administrado no explicaba. Se crea una del tamano maximo y se redibuja
/// encima; lo que sobra queda fuera de la ventana y no se ve.
/// </para>
/// </summary>
internal sealed unsafe class LanzadorVisuals : IDisposable
{
    private const uint D3D11SdkVersion = 7;

    // Las mismas medidas logicas que usa la ventana. Estan repetidas a proposito y no
    // compartidas en una clase de constantes: son dos cosas distintas que hoy coinciden,
    // y una clase "Medidas" con seis campos es la abstraccion que sobra.
    private const float AltoFila = 48f;
    private const float AltoFranja = 64f;
    private const float MargenLista = 8f;

    /// <summary>Donde empieza el icono: deja aire entre el realce y el borde del panel.</summary>
    private const float MargenTexto = 20f;

    /// <summary>
    /// El hueco del icono. Se reserva aunque el icono no haya llegado todavia: asi el
    /// texto no se mueve cuando aparece, y la columna de nombres queda alineada con lo
    /// que se escribe arriba.
    /// </summary>
    private const float LadoIcono = 32f;
    private const float HuecoIcono = 14f;

    /// <summary>Donde empieza el texto de cada fila. La caja de busqueda usa el mismo.</summary>
    public const float Sangria = MargenTexto + LadoIcono + HuecoIcono;

    /// <summary>Alto de la franja, que la ventana necesita para colocar el raton.</summary>
    public const int AltoDeLaFranja = (int)AltoFranja;

    // --- la pildora de busqueda -------------------------------------------
    private const float MargenPildora = 10f;   // aire entre la pildora y el borde del panel
    private const float AltoPildora = 44f;
    private const float LupaAncho = 30f;       // hueco de la lupa dentro de la pildora

    private static Windows.System.DispatcherQueueController? _cola;

    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _target;
    private readonly ContainerVisual _raiz;
    private readonly SpriteVisual _lista;
    private float _escala;
    private float _ancho;
    private CompositionGraphicsDevice? _graficos;
    private CompositionDrawingSurface? _superficie;
    private readonly int _filasMaximas;

    public LanzadorVisuals(HWND hwnd, float escala, float anchoFisico, int filasMaximas)
    {
        _escala = escala;
        _ancho = anchoFisico;
        _filasMaximas = filasMaximas;

        AsegurarCola();
        _compositor = new Compositor();

        ICompositorDesktopInterop interop = _compositor.As<ICompositorDesktopInterop>();
        interop.CreateDesktopWindowTarget(hwnd, true, out _target);

        _raiz = _compositor.CreateContainerVisual();
        _raiz.RelativeSizeAdjustment = Vector2.One;
        _target.Root = _raiz;

        // Un solo visual para todo, desde arriba del todo: la pildora de busqueda y las
        // filas. Ya no hay ninguna ventana hija a la que respetarle el sitio.
        _lista = _compositor.CreateSpriteVisual();
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
        // Cambio la escala, asi que la superficie ya no mide lo que tiene que medir.
        _superficie?.Dispose();
        _superficie = null;
        _lista.Brush = null;
    }

    /// <summary>Lo que se escribe, con su cursor y su seleccion. Lo pone la ventana.</summary>
    public Caja? Caja { get; set; }

    /// <summary>Si al cursor le toca estar encendido en este parpadeo.</summary>
    public bool CaretEncendido { get; set; } = true;

    /// <summary>
    /// Cuanto se ha corrido el texto hacia la izquierda porque no cabia. Se guarda entre
    /// repintados a proposito: recalcularlo desde cero haria que el texto diese un salto
    /// cada vez que el cursor se acerca a un borde.
    /// </summary>
    private float _corrido;

    public void Pintar(IReadOnlyList<Resultado> resultados, int elegido)
    {
        // Aqui ya no se sale con cero resultados: la pildora tiene que dibujarse igual,
        // porque es donde esta el cursor mientras no has escrito nada.

        // El visual mide siempre el maximo y la ventana mide lo que hay: lo que sobra de
        // la superficie cae por debajo del borde de la ventana y no se ve. Asi la
        // superficie no cambia de tamano nunca.
        _lista.Size = new Vector2(_ancho, AltoMaximo());
        Dibujar(resultados, elegido);
    }

    /// <summary>
    /// La pildora de busqueda, dibujada por nosotros. Es lo que un control EDIT no puede
    /// ser: con el radio que queramos y sin nada alrededor.
    /// </summary>
    private void Pildora(ID2D1DeviceContext ctx, float x, float y, string consulta)
    {
        float izq = x + S(MargenPildora);
        float der = x + _ancho - S(MargenPildora);
        float arr = y + (S(AltoFranja) - S(AltoPildora)) / 2f;
        float aba = arr + S(AltoPildora);

        // El radio es la mitad del alto, que es lo que la hace pildora y no caja.
        Fondo(ctx, izq, arr, der, aba, S(AltoPildora) / 2f);

        float dentro = izq + S(18f);

        // La lupa: glifo E721 de Segoe Fluent Icons, con su escape y no con el
        // caracter, que en el codigo fuente es invisible.
        Texto.Dibujar(ctx, "\uE721", S(14f), grueso: false, 0.65f,
                      new System.Drawing.Point((int)dentro, (int)(arr + S(13f))), iconos: true);

        float x0 = dentro + S(LupaAncho);
        float x1 = der - S(18f);
        float yTexto = arr + S(10f);

        if (consulta.Length == 0)
        {
            Texto.Dibujar(ctx, "Buscar", TamTexto(), grueso: false, 0.35f,
                          new System.Drawing.Point((int)x0, (int)yTexto));
            _corrido = 0f;
            DibujarCaret(ctx, x0, arr);
            return;
        }

        // El texto se recorta a la pildora: si no cabe, lo que sobra no debe salirse por
        // el borde ni pisar la lupa.
        D2D_RECT_F recorte = new() { left = x0, top = arr, right = x1, bottom = aba };
        ctx.PushAxisAlignedClip(recorte, D2D1_ANTIALIAS_MODE.D2D1_ANTIALIAS_MODE_ALIASED);

        try
        {
            float hastaCursor = Ancho(consulta, Caja?.Cursor ?? consulta.Length);
            float cabe = x1 - x0;

            // Se mueve lo justo para que el cursor vuelva a verse, y por eso _corrido se
            // guarda entre repintados: recentrarlo siempre haria que el texto se moviese
            // con cada tecla.
            if (hastaCursor - _corrido > cabe) _corrido = hastaCursor - cabe;
            if (hastaCursor - _corrido < 0f) _corrido = hastaCursor;

            float sobra = Ancho(consulta, consulta.Length) - cabe;
            _corrido = Math.Clamp(_corrido, 0f, Math.Max(0f, sobra));

            if (Caja is { HaySeleccion: true } caja)
            {
                float a = x0 + Ancho(consulta, caja.Desde) - _corrido;
                float b = x0 + Ancho(consulta, caja.Hasta) - _corrido;
                Redondeado(ctx, a, arr + S(8f), b, aba - S(8f), S(3f), 0.40f, 0.60f, 1f, 0.45f);
            }

            Texto.Dibujar(ctx, consulta, TamTexto(), grueso: false, 0.95f,
                          new System.Drawing.Point((int)(x0 - _corrido), (int)yTexto));

            DibujarCaret(ctx, x0 + hastaCursor - _corrido, arr);
        }
        finally
        {
            ctx.PopAxisAlignedClip();
        }
    }

    private float TamTexto() => S(17f);

    /// <summary>Lo que ocupan las primeras <paramref name="cuantas"/> letras.</summary>
    private float Ancho(string texto, int cuantas)
    {
        cuantas = Math.Clamp(cuantas, 0, texto.Length);

        // Medir descuenta el pixel de margen que anade por el antialiasing; si no, el
        // cursor se iria separando del texto una letra tras otra.
        return cuantas == 0 ? 0f : Texto.Medir(texto[..cuantas], TamTexto(), grueso: false).X - 2f;
    }

    /// <summary>
    /// A que letra corresponde una X de la ventana. Se mide letra a letra y gana el borde
    /// mas cercano: asi, al pinchar entre dos letras, el cursor cae en la que esperas y no
    /// siempre en la de la izquierda.
    /// </summary>
    public int IndiceEn(float x)
    {
        string t = Caja?.Texto ?? string.Empty;
        if (t.Length == 0) return 0;

        float x0 = S(MargenPildora) + S(18f) + S(LupaAncho) - _corrido;

        int mejor = 0;
        float distancia = Math.Abs(x - x0);
        for (int i = 1; i <= t.Length; i++)
        {
            float d = Math.Abs(x - (x0 + Ancho(t, i)));
            if (d >= distancia) continue;
            distancia = d;
            mejor = i;
        }

        return mejor;
    }

    private void DibujarCaret(ID2D1DeviceContext ctx, float x, float arribaPildora)
    {
        if (!CaretEncendido) return;

        // Dos puntos de ancho: uno solo casi desaparece sobre el fondo gris.
        Redondeado(ctx, x, arribaPildora + S(10f), x + S(2f),
                   arribaPildora + S(AltoPildora) - S(10f), S(1f), 1f, 1f, 1f, 0.9f);
    }

    /// <summary>
    /// El fondo de la pildora y del panel de resultados. Es lo unico que se ve de la
    /// ventana, que ya no lleva acrilico (ver QuitarMarco). Opaco a proposito: sin
    /// desenfoque detras, cualquier alfa deja leer la ventana de abajo. Medido al 96 %, un
    /// bloque rojo y las lineas de codigo del editor se leian a traves del panel.
    /// </summary>
    private void Fondo(ID2D1DeviceContext ctx, float izq, float arr, float der, float aba, float radio)
    {
        Redondeado(ctx, izq, arr, der, aba, radio, 0.23f, 0.23f, 0.24f, 1f);

        // El borde, mas claro que el fondo: es lo que le da el canto de cristal.
        Borde(ctx, izq, arr, der, aba, radio, 1f, 1f, 1f, 0.16f, S(1f));
    }

    private void Borde(ID2D1DeviceContext ctx, float izq, float arr, float der, float aba,
                       float radio, float r, float g, float b, float a, float grosor)
    {
        D2D1_COLOR_F color = new() { r = r, g = g, b = b, a = a };
        ctx.CreateSolidColorBrush(&color, null, out ID2D1SolidColorBrush pincel);

        D2D1_ROUNDED_RECT caja = new()
        {
            rect = new D2D_RECT_F { left = izq, top = arr, right = der, bottom = aba },
            radiusX = radio,
            radiusY = radio,
        };
        ctx.DrawRoundedRectangle(caja, pincel, grosor, null);
    }

    private float AltoMaximo() => S(AltoFranja) + S(MargenLista) * 2 + _filasMaximas * S(AltoFila);

    private void Dibujar(IReadOnlyList<Resultado> resultados, int elegido)
    {
        if (_superficie is null)
        {
            _superficie = AsegurarDispositivo().CreateDrawingSurface(
                new global::Windows.Foundation.Size(_ancho, AltoMaximo()),
                DirectXPixelFormat.B8G8R8A8UIntNormalized,
                DirectXAlphaMode.Premultiplied);

            _lista.Brush = _compositor.CreateSurfaceBrush(_superficie);
        }

        ICompositionDrawingSurfaceInterop interop = _superficie.As<ICompositionDrawingSurfaceInterop>();
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

            Pildora(ctx, desplazamiento.X, desplazamiento.Y, Caja?.Texto ?? string.Empty);

            // Los resultados van en su propio panel, separado de la pildora por el hueco
            // transparente de la franja. Termina un punto antes que la ventana: el borde se
            // dibuja centrado en la linea y la mitad de abajo se perderia.
            if (resultados.Count > 0)
            {
                float arriba = desplazamiento.Y + S(AltoFranja);
                Fondo(ctx, desplazamiento.X + S(MargenPildora), arriba,
                      desplazamiento.X + _ancho - S(MargenPildora),
                      arriba + S(MargenLista) * 2 + resultados.Count * S(AltoFila) - S(1f), S(16f));
            }

            for (int i = 0; i < resultados.Count; i++)
            {
                float y = desplazamiento.Y + S(AltoFranja) + S(MargenLista) + i * S(AltoFila);
                Fila(ctx, resultados[i], desplazamiento.X, y, i == elegido);
            }
        }
        finally
        {
            interop.EndDraw();
        }
    }

    private void Fila(ID2D1DeviceContext ctx, Resultado r, float x, float y, bool elegida)
    {
        if (elegida)
        {
            // Un rectangulo claro a poca opacidad: sobre el fondo oscuro se lee como un
            // realce. Metido dentro del panel, que empieza en MargenPildora.
            Redondeado(ctx, x + S(MargenPildora + 4f), y + S(2f), x + _ancho - S(MargenPildora + 4f), y + S(AltoFila) - S(2f),
                       S(10f), 1f, 1f, 1f, 0.14f);
        }

        // El icono si ya llego; si no, el hueco marcado con un cuadrado apenas visible.
        // El hueco no es decoracion: sin el, la fila daria un salto al aparecer el icono.
        float arribaIcono = y + (S(AltoFila) - S(LadoIcono)) / 2f;
        float izqIcono = x + S(MargenTexto);

        Icono? icono = r.Entrada.SoloSeMira ? null : Iconos.Hay(r.Entrada.Destino);
        if (icono is not null) Pintar(ctx, icono, izqIcono, arribaIcono, S(LadoIcono));
        else Redondeado(ctx, izqIcono, arribaIcono, izqIcono + S(LadoIcono), arribaIcono + S(LadoIcono),
                        S(7f), 1f, 1f, 1f, 0.07f);

        float izquierda = x + S(Sangria);

        // El nombre a plena opacidad; el destino debajo y apagado, que es donde miras
        // solo cuando dos resultados se llaman parecido.
        Texto.Dibujar(ctx, r.Entrada.Nombre, S(15f), grueso: true, 1f,
                      new System.Drawing.Point((int)izquierda, (int)(y + S(6f))));

        Texto.Dibujar(ctx, Acortar(r.Entrada.Destino), S(11f), grueso: false, 0.5f,
                      new System.Drawing.Point((int)izquierda, (int)(y + S(27f))));
    }

    /// <summary>
    /// Sube los pixeles del icono a un mapa de D2D y lo dibuja en su hueco.
    /// <para>
    /// <b>El mapa se suelta aqui mismo.</b> Es un objeto COM: dejarselo al recolector
    /// significa que ocho mapas por repintado se acumulan sin que el monton administrado
    /// crezca lo bastante para provocar una recoleccion. Medido en H8: 27 MB de iconos
    /// guardados frente a 157 MB de conjunto de trabajo, y la diferencia estaba aqui.
    /// </para>
    /// </summary>
    private static void Pintar(ID2D1DeviceContext ctx, Icono icono, float izq, float arr, float lado)
    {
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

        fixed (byte* pixeles = icono.Bgra)
        {
            ctx.CreateBitmap(
                new D2D_SIZE_U { width = (uint)icono.Ancho, height = (uint)icono.Alto },
                pixeles, (uint)(icono.Ancho * 4), propiedades, out ID2D1Bitmap1 mapa);

            try
            {
                D2D_RECT_F donde = new() { left = izq, top = arr, right = izq + lado, bottom = arr + lado };
                ctx.DrawBitmap(mapa, &donde, 1f,
                    D2D1_BITMAP_INTERPOLATION_MODE.D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, null);
            }
            finally
            {
                if (Marshal.IsComObject(mapa)) Marshal.FinalReleaseComObject(mapa);
            }
        }
    }

    private void Redondeado(ID2D1DeviceContext ctx, float izq, float arr, float der, float aba,
                            float radio, float r, float g, float b, float a)
    {
        D2D1_COLOR_F color = new() { r = r, g = g, b = b, a = a };
        ctx.CreateSolidColorBrush(&color, null, out ID2D1SolidColorBrush pincel);

        D2D1_ROUNDED_RECT caja = new()
        {
            rect = new D2D_RECT_F { left = izq, top = arr, right = der, bottom = aba },
            radiusX = radio,
            radiusY = radio,
        };
        ctx.FillRoundedRectangle(caja, pincel);
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
        _superficie?.Dispose();
        _target.Root = null;
        _raiz.Dispose();
        _target.Dispose();
    }
}
