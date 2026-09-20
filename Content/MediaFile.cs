using System.Numerics;
using Windows.Media.Core;
using Windows.Media.Playback;
using Windows.Storage;
using Windows.UI.Composition;

namespace QuickLook;

/// <summary>
/// Un video o un audio sonando dentro del panel. Ver SEGURIDAD.md §3.3.
///
/// <para>
/// <b>Sin dependencia nueva, y esta es la pieza que lo hace posible.</b>
/// <c>MediaPlayer.GetSurface(compositor)</c> devuelve una <c>ICompositionSurface</c> que
/// entra en el arbol de visuals como cualquier otro pincel. Por eso no hace falta VLCJ, ni
/// LibVLC, ni un reproductor en una ventana aparte encima del panel: el video es un visual
/// mas, con sus esquinas redondeadas y su recorte, y lo compone DWM con todo lo demas.
/// </para>
///
/// <para>
/// <b>Lo unico que hay que hacer bien es soltarlo.</b> Un <c>MediaPlayer</c> huerfano sigue
/// sonando aunque su ventana ya no exista, y el usuario no tiene forma de callarlo salvo
/// matar el proceso. Por eso <see cref="Dispose"/> pausa ANTES de soltar, y por eso se llama
/// desde todos los caminos por los que el panel puede morir, incluido el de excepcion.
/// </para>
/// </summary>
internal sealed class MediaFile : IDisposable
{
    private MediaPlayer? _player;
    private MediaPlayerSurface? _surface;

    private MediaFile(MediaPlayer player, MediaPlayerSurface? surface)
    {
        _player = player;
        _surface = surface;
    }

    /// <summary>El pincel con el video, o null si es audio y no hay nada que dibujar.</summary>
    public CompositionSurfaceBrush? Brush { get; private init; }

    /// <summary>Por donde va, de 0 a 1. Cero si todavia no se sabe cuanto dura.</summary>
    public float Progress
    {
        get
        {
            if (_player?.PlaybackSession is not MediaPlaybackSession session) return 0f;

            double total = session.NaturalDuration.TotalSeconds;
            return total <= 0d ? 0f : (float)Math.Clamp(session.Position.TotalSeconds / total, 0d, 1d);
        }
    }

    /// <summary>
    /// Abre <paramref name="path"/> y empieza a reproducir. Devuelve null si el sistema no
    /// sabe abrirlo —un codec que no tiene, un archivo roto—, y entonces el panel se queda
    /// con la miniatura quieta, que sigue siendo una vista previa util.
    /// </summary>
    public static MediaFile? Open(Compositor compositor, string path, bool video, Vector2 size)
    {
        MediaPlayer? player = null;
        try
        {
            player = new MediaPlayer
            {
                IsLoopingEnabled = true,

                // El video entra mudo: esto es un vistazo, y que se te ponga a sonar un
                // trailer por pulsar espacio asusta. El audio SI suena, porque en un audio
                // el sonido es todo el contenido y verlo mudo no informa de nada.
                IsMuted = video,
                AutoPlay = false,
            };

            MediaPlayerSurface? surface = null;
            CompositionSurfaceBrush? brush = null;

            if (video)
            {
                // El tamano de la superficie se fija ANTES de pedirla: es la resolucion a
                // la que se compone, no un recorte, y pedirla despues no la cambia.
                player.SetSurfaceSize(new global::Windows.Foundation.Size(
                    Math.Max(1f, size.X), Math.Max(1f, size.Y)));

                surface = player.GetSurface(compositor);
                brush = compositor.CreateSurfaceBrush(surface.CompositionSurface);
            }

            // StorageFile y NO MediaSource.CreateFromUri, aunque lo segundo sea sincrono y
            // mas corto.
            //
            // auditar.ps1 lo caza: la regla 6 busca "Uri(" entre otras cosas, y aunque aqui
            // sea una ruta local, ese grep es lo que sostiene la promesa de que este
            // programa no habla con nadie — que es la mitad del argumento por el que se
            // puede tener un hook de teclado dentro. Antes que meterle una excepcion al
            // grep, se cambia la llamada: un documento con excepciones deja de ser una
            // puerta.
            //
            // El await va a un hilo del pool por lo mismo que en PdfFile: este hilo es el
            // que despacha la DispatcherQueue del compositor.
            StorageFile file = Task.Run(async () => await StorageFile.GetFileFromPathAsync(path))
                .GetAwaiter().GetResult();

            player.Source = MediaSource.CreateFromStorageFile(file);
            player.Play();

            return new MediaFile(player, surface) { Brush = brush };
        }
        catch (Exception ex)
        {
            // Si algo falla a medias, el reproductor ya podria estar sonando.
            player?.Pause();
            player?.Dispose();

            Console.WriteLine($"[media] no se pudo reproducir {System.IO.Path.GetFileName(path)}: {ex.Message}");
            return null;
        }
    }

    public void Dispose()
    {
        if (_player is null) return;

        Log.Line("[media] soltado");

        // Pausar ANTES de soltar. Dispose por si solo no garantiza que el audio pare en el
        // mismo instante, y lo que no puede pasar es que el panel desaparezca y el sonido
        // siga.
        try { _player?.Pause(); } catch { /* ya estaba muerto */ }

        _surface?.Dispose();
        _surface = null;

        _player?.Dispose();
        _player = null;
    }
}
