using Windows.Foundation;
using Windows.Graphics.Imaging;
using Windows.Media.Control;
using Windows.Storage;
using Windows.Storage.FileProperties;
using Windows.Storage.Streams;
using Windows.Win32;
using Windows.Win32.Foundation;

namespace Isla;

/// <summary>
/// Lo que suena ahora mismo. Se pinta y se tira: SEGURIDAD.md regla 12 prohibe guardar
/// un historial, asi que solo existe esto y dura lo que dure la cancion.
/// </summary>
internal sealed record Cancion(
    string Titulo,
    string Artista,
    string App,
    TimeSpan Posicion,
    TimeSpan Duracion,
    byte[]? Arte,
    bool Sonando,
    bool PuedeAnterior,
    bool PuedeSiguiente,
    bool PuedePlayPausa,
    uint Tinte,
    int Sesiones);

/// <summary>
/// El puente con el canal de medios de Windows (SEGURIDAD.md §3.1 y §3.2).
///
/// <para>
/// <b>Todo el async de WinRT vive en el pool de hilos.</b> Los eventos de
/// GlobalSystemMediaTransportControlsSessionManager llegan en un hilo cualquiera, y los
/// objetos de composicion solo se pueden tocar desde el hilo que tiene la
/// DispatcherQueue. En vez de montar un SynchronizationContext, aqui se deja el
/// resultado empaquetado en un record plano y se despierta a la ventana con un
/// PostMessage. El hilo de UI recoge lo ultimo que haya y no espera a nadie.
/// </para>
///
/// <para>
/// Y no hay ningun temporizador: se sondea una vez al arrancar y despues solo se
/// reacciona a lo que avisa el sistema.
/// </para>
/// </summary>
internal static class Medios
{
    /// <summary>
    /// Lado al que se decodifica la caratula. 192 da de sobra para los 92 logicos a
    /// cualquier escala hasta el 200%, y decodificar una vez sale mas barato que
    /// volver a hacerlo si cambia el DPI.
    /// </summary>
    public const int ArteLado = 192;

    private static GlobalSystemMediaTransportControlsSessionManager? _gestor;
    private static GlobalSystemMediaTransportControlsSession? _sesion;
    private static HWND _ventana;
    private static uint _mensaje;

    // La sesion que elegiste con un clic en el nombre de la app (SEGURIDAD.md s.3.1,
    // enmienda del 23-09-2026). Solo su AUMID, que es lo que ya se pinta, y solo mientras
    // exista: si esa app cierra su sesion, vuelve a mandar la que elige Windows.
    private static string? _elegida;
    // Cuantas sesiones hay, para dibujar el < > solo si hay a donde ir. La cuenta, no la lista.
    private static int _sesiones;

    private static readonly object Candado = new();
    private static Cancion? _ultima;

    // La caratula se decodifica UNA VEZ POR CANCION, no por evento.
    //
    // Medido con Spotify, que a diferencia de Brave empuja la linea de tiempo cada dos
    // por tres: decodificar 192x192 y barrer el color dominante en cada aviso disparo
    // la CPU en reposo de 0.0% a 11.7%. La clave es titulo+artista porque la referencia
    // a la miniatura es un objeto nuevo en cada lectura y no se puede comparar.
    private static string _claveArte = string.Empty;
    private static byte[]? _arte;
    private static uint _tinte;

    // Reintentos de la caratula mientras la sesion aun no la tiene (ver Leer). Un solo
    // reintento en vuelo a la vez: con Spotify llegan avisos a rafagas y cada uno
    // programaria el suyo.
    private const int EsperaArteMs = 10_000;
    private static string _claveEsperando = string.Empty;
    private static long _esperandoDesde;
    private static int _reintentoPendiente;

    // Guardados en campos para poder darse de baja con la MISMA instancia: un grupo de
    // metodos crea un delegate nuevo cada vez que se escribe.
    private static readonly TypedEventHandler<GlobalSystemMediaTransportControlsSession, MediaPropertiesChangedEventArgs>
        AlCambiarProps = (_, _) => _ = Task.Run(Leer);
    private static readonly TypedEventHandler<GlobalSystemMediaTransportControlsSession, PlaybackInfoChangedEventArgs>
        AlCambiarEstado = (_, _) => _ = Task.Run(Leer);
    private static readonly TypedEventHandler<GlobalSystemMediaTransportControlsSession, TimelinePropertiesChangedEventArgs>
        AlCambiarTiempo = (_, _) => _ = Task.Run(Leer);

    /// <summary>Lo ultimo que se leyo. Lo consulta el hilo de UI al recibir el aviso.</summary>
    public static Cancion? Ultima
    {
        get { lock (Candado) { return _ultima; } }
    }

    public static void Arrancar(HWND ventana, uint mensaje)
    {
        _ventana = ventana;
        _mensaje = mensaje;
        _ = Task.Run(Inicio);
    }

    private static async Task Inicio()
    {
        try
        {
            _gestor = await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();
            _gestor.CurrentSessionChanged += (_, _) => _ = Task.Run(Enganchar);
            _gestor.SessionsChanged += (_, _) => _ = Task.Run(Enganchar);
            await Enganchar();
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[isla] no hay canal de medios: {ex.Message}");
        }
    }

    /// <summary>Se suscribe a la sesion que manda ahora y se da de baja de la anterior.</summary>
    private static async Task Enganchar() => await Enganchar(Cual());

    /// <summary>
    /// La que toca ensenar: la que elegiste, si sigue ahi, o la que elige Windows. La lista se
    /// pide, se mira y se suelta aqui mismo; solo sale de aqui cuantas hay.
    /// </summary>
    private static GlobalSystemMediaTransportControlsSession? Cual()
    {
        if (_gestor is null) return null;

        IReadOnlyList<GlobalSystemMediaTransportControlsSession> todas = _gestor.GetSessions();
        _sesiones = todas.Count;

        if (_elegida is not null)
        {
            // Dos pestanas del navegador comparten AUMID: si la que se ensena sigue viva y es
            // de la app elegida, se queda esa y no la primera que coincida.
            GlobalSystemMediaTransportControlsSession? misma = null;
            foreach (GlobalSystemMediaTransportControlsSession s in todas)
            {
                if (s.SourceAppUserModelId != _elegida) continue;
                if (ReferenceEquals(s, _sesion)) return s;
                misma ??= s;
            }
            if (misma is not null) return misma;
            _elegida = null;
        }

        return _gestor.GetCurrentSession();
    }

    /// <summary>
    /// Clic en el nombre de la app: pasa a la sesion siguiente. Detras de un clic y nunca de
    /// un temporizador, como los mandos (SEGURIDAD.md s.3.2).
    /// </summary>
    public static void Rotar() => _ = Task.Run(async () =>
    {
        try
        {
            if (_gestor is null) return;
            IReadOnlyList<GlobalSystemMediaTransportControlsSession> todas = _gestor.GetSessions();
            if (todas.Count < 2) return;

            // Por referencia, que distingue dos pestanas del mismo navegador; si WinRT dio
            // otro envoltorio para la misma sesion, por AUMID.
            int i = 0;
            while (i < todas.Count && !ReferenceEquals(todas[i], _sesion)) i++;
            if (i == todas.Count)
            {
                i = 0;
                while (i < todas.Count && todas[i].SourceAppUserModelId != _sesion?.SourceAppUserModelId) i++;
            }
            GlobalSystemMediaTransportControlsSession siguiente = todas[(i + 1) % todas.Count];

            _elegida = siguiente.SourceAppUserModelId;
            _sesiones = todas.Count;
            await Enganchar(siguiente);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[isla] rotando sesion: {ex.Message}");
        }
    });

    private static async Task Enganchar(GlobalSystemMediaTransportControlsSession? nueva)
    {
        if (!ReferenceEquals(nueva, _sesion))
        {
            if (_sesion is not null)
            {
                _sesion.MediaPropertiesChanged -= AlCambiarProps;
                _sesion.PlaybackInfoChanged -= AlCambiarEstado;
                _sesion.TimelinePropertiesChanged -= AlCambiarTiempo;
            }

            _sesion = nueva;

            if (_sesion is not null)
            {
                _sesion.MediaPropertiesChanged += AlCambiarProps;
                _sesion.PlaybackInfoChanged += AlCambiarEstado;
                _sesion.TimelinePropertiesChanged += AlCambiarTiempo;
            }
        }

        await Leer();
    }

    private static async Task Leer()
    {
        GlobalSystemMediaTransportControlsSession? s = _sesion;
        if (s is null) { Publicar(null); return; }

        try
        {
            GlobalSystemMediaTransportControlsSessionMediaProperties? p = await s.TryGetMediaPropertiesAsync();
            if (p is null) { Publicar(null); return; }

            GlobalSystemMediaTransportControlsSessionTimelineProperties t = s.GetTimelineProperties();
            GlobalSystemMediaTransportControlsSessionPlaybackInfo info = s.GetPlaybackInfo();
            GlobalSystemMediaTransportControlsSessionPlaybackControls mandos = info.Controls;
            string clave = p.Title + "" + p.Artist;
            if (clave != _claveArte)
            {
                _arte = await Arte(p.Thumbnail);
                _tinte = Dominante(_arte);

                // El navegador suele avisar del titulo nuevo antes de tener la miniatura:
                // la primera lectura llega sin ella o no se puede abrir todavia. En vez de
                // dar la cancion por vista sin caratula, se vuelve a mirar cada segundo
                // hasta que aparezca o pasen EsperaArteMs.
                if (_arte is not null)
                {
                    _claveArte = clave;
                }
                else
                {
                    if (clave != _claveEsperando) { _claveEsperando = clave; _esperandoDesde = Environment.TickCount64; }

                    if (Environment.TickCount64 - _esperandoDesde > EsperaArteMs)
                        _claveArte = clave; // de verdad no tiene; se deja el relleno
                    else if (Interlocked.Exchange(ref _reintentoPendiente, 1) == 0)
                        _ = Task.Delay(1000).ContinueWith(_ =>
                        {
                            Interlocked.Exchange(ref _reintentoPendiente, 0);
                            return Leer();
                        });
                }
            }

            Publicar(new Cancion(
                string.IsNullOrWhiteSpace(p.Title) ? "Sin titulo" : p.Title,
                p.Artist ?? string.Empty,
                // Del AUMID que llega, recortado. SEGURIDAD.md §3.1: no se resuelve el
                // proceso dueno ni se busca su ventana ni se mira su ejecutable.
                NombreCorto(s.SourceAppUserModelId ?? string.Empty),
                t.Position,
                t.EndTime - t.StartTime,
                _arte,
                info.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing,
                // Un boton que la sesion no admite NO se dibuja, en vez de dibujarlo y
                // que no haga nada. Lo dice la propia API y esta escrito en
                // SEGURIDAD.md §3.2.
                mandos.IsPreviousEnabled,
                mandos.IsNextEnabled,
                mandos.IsPlayEnabled || mandos.IsPauseEnabled,
                _tinte,
                _sesiones));
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[isla] leyendo la sesion: {ex.Message}");
        }
    }

    /// <summary>
    /// La caratula que trae la propia sesion, decodificada a BGRA premultiplicado.
    /// SEGURIDAD.md regla 7: no se busca ninguna en internet ni en disco.
    /// </summary>
    private static async Task<byte[]?> Arte(IRandomAccessStreamReference? referencia, int lado = ArteLado)
    {
        if (referencia is null) return null;

        try
        {
            using IRandomAccessStreamWithContentType flujo = await referencia.OpenReadAsync();
            BitmapDecoder decodificador = await BitmapDecoder.CreateAsync(flujo);

            // Llenar y recortar por el centro, no aplastar. Las miniaturas de un video
            // son 16:9 y forzarlas a un cuadrado deja las caras ovaladas; medido con la
            // primera que trajo Brave. Se escala por el lado CORTO y se recorta el
            // sobrante del largo, que es lo que hace la isla de macOS.
            uint ancho = decodificador.PixelWidth;
            uint alto = decodificador.PixelHeight;
            if (ancho == 0 || alto == 0) return null;

            double factor = lado / (double)Math.Min(ancho, alto);
            uint sw = (uint)Math.Max(lado, Math.Round(ancho * factor));
            uint sh = (uint)Math.Max(lado, Math.Round(alto * factor));

            BitmapTransform escala = new()
            {
                ScaledWidth = sw,
                ScaledHeight = sh,
                InterpolationMode = BitmapInterpolationMode.Fant,
                // Bounds se aplica DESPUES de escalar, asi que esto es el recorte.
                Bounds = new BitmapBounds
                {
                    X = (sw - (uint)lado) / 2,
                    Y = (sh - (uint)lado) / 2,
                    Width = (uint)lado,
                    Height = (uint)lado,
                },
            };

            PixelDataProvider datos = await decodificador.GetPixelDataAsync(
                BitmapPixelFormat.Bgra8,
                BitmapAlphaMode.Premultiplied,
                escala,
                ExifOrientationMode.IgnoreExifOrientation,
                ColorManagementMode.DoNotColorManage);

            return datos.DetachPixelData();
        }
        catch
        {
            // Una caratula que no se puede decodificar no es motivo para quedarse sin
            // isla: se cae al cuadrado de relleno.
            return null;
        }
    }

    // --- lo que se le pide a la sesion (SEGURIDAD.md §3.2) -------------------------
    //
    // Es lo mismo que hace la tecla de play del teclado: misma API, misma sesion, y
    // siempre detras de un clic del usuario en un boton que esta viendo. No hay ningun
    // camino que llame a esto desde un temporizador, y asi debe seguir.

    public static void Alternar() => Pedir(s => s.TryTogglePlayPauseAsync());

    public static void Anterior() => Pedir(s => s.TrySkipPreviousAsync());

    public static void Siguiente() => Pedir(s => s.TrySkipNextAsync());

    public static void Buscar(TimeSpan donde) => Pedir(s => s.TryChangePlaybackPositionAsync(donde.Ticks));

    private static void Pedir(Func<GlobalSystemMediaTransportControlsSession, IAsyncOperation<bool>> que)
    {
        GlobalSystemMediaTransportControlsSession? s = _sesion;
        if (s is null) return;

        // Al pool: estas llamadas cruzan a otro proceso y el hilo de UI es el que esta
        // animando la isla.
        _ = Task.Run(async () =>
        {
            try { await que(s); }
            catch (Exception ex) { Console.Error.WriteLine($"[isla] mando: {ex.Message}"); }
        });
    }

    /// <summary>
    /// El color que manda en la caratula, para tenir el panel. Es la media de los
    /// pixeles con la desviacion respecto al gris amplificada: la media a secas de una
    /// foto sale casi siempre parda, y parda al 14% no se ve. Amplificar la separacion
    /// del gris saca el color que uno diria que tiene el disco.
    ///
    /// Devuelve 0 si no hay caratula. Se calcula aqui, en el pool, y no en el hilo de UI.
    /// </summary>
    private static uint Dominante(byte[]? bgra)
    {
        if (bgra is null || bgra.Length < 4) return 0u;

        long b = 0, g = 0, r = 0, n = 0;
        // Uno de cada 16 pixeles: 2300 muestras de 36000 bastan para una media y sale
        // dieciseis veces mas barato.
        for (int i = 0; i + 3 < bgra.Length; i += 64)
        {
            if (bgra[i + 3] < 128) continue;
            b += bgra[i];
            g += bgra[i + 1];
            r += bgra[i + 2];
            n++;
        }
        if (n == 0) return 0u;

        float mr = r / (float)n, mg = g / (float)n, mb = b / (float)n;
        float gris = (mr + mg + mb) / 3f;

        static byte Realza(float canal, float gris)
            => (byte)Math.Clamp(gris + (canal - gris) * 2.6f, 0f, 255f);

        return ((uint)Realza(mr, gris) << 16) | ((uint)Realza(mg, gris) << 8) | Realza(mb, gris);
    }

    /// <summary>Lado al que se decodifica el icono de una app del mezclador: 20 logicos al 200 %.</summary>
    public const int IconoLado = 48;

    /// <summary>
    /// El icono de un exe, el que Windows ensena en el Explorador, en BGRA de IconoLado. Para el
    /// mezclador (SEGURIDAD.md s.3.3): no se abre el exe a mano ni se leen sus recursos. Null si
    /// Windows no lo da, que es lo normal con las apps de la Store.
    /// </summary>
    /// <summary>Lo pide en el pool y, si llega, lo deja en <paramref name="destino"/> y avisa a la ventana.</summary>
    public static void PedirIcono(string rutaExe, IDictionary<string, byte[]?> destino, HWND ventana, uint mensaje)
        => _ = Task.Run(async () =>
        {
            byte[]? icono = await Icono(rutaExe);
            if (icono is null) return;
            destino[rutaExe] = icono;
            PInvoke.PostMessage(ventana, mensaje, default, default);
        });

    private static async Task<byte[]?> Icono(string rutaExe)
    {
        try
        {
            StorageFile exe = await StorageFile.GetFileFromPathAsync(rutaExe);
            using StorageItemThumbnail icono = await exe.GetThumbnailAsync(ThumbnailMode.SingleItem, IconoLado);
            if (icono is null) return null;
            return await Arte(RandomAccessStreamReference.CreateFromStream(icono), IconoLado);
        }
        catch
        {
            return null;
        }
    }

    /// <summary>
    /// El AUMID, legible. La Store manda <c>SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify</c>: lo
    /// que va tras el <c>!</c> es el id de la app, y casi siempre es su nombre. Cuando es el
    /// generico <c>App</c>, el nombre del paquete. Las apps de escritorio llegan como
    /// <c>chrome.exe</c> o <c>Brave</c>. Solo se recorta texto: no se busca nada.
    /// </summary>
    internal static string NombreCorto(string aumid)
    {
        string s = aumid;
        int bang = s.LastIndexOf('!');
        if (bang >= 0 && bang < s.Length - 1)
        {
            string id = s[(bang + 1)..];
            if (!id.Equals("App", StringComparison.OrdinalIgnoreCase)) return id;
            string paquete = s[..bang];
            int guion = paquete.IndexOf('_');
            if (guion > 0) paquete = paquete[..guion];
            return paquete[(paquete.LastIndexOf('.') + 1)..];
        }
        return s.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ? s[..^4] : s;
    }

    private static void Publicar(Cancion? c)
    {
        lock (Candado) { _ultima = c; }
        if (!_ventana.IsNull) PInvoke.PostMessage(_ventana, _mensaje, default, default);
    }
}
