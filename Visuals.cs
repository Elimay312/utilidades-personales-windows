using Windows.UI;
using Windows.UI.Composition;
using Windows.Win32;
using Windows.Win32.System.WinRT;

namespace QuickLook;

/// <summary>
/// El compositor del proceso.
///
/// <para>
/// Es <c>Windows.UI.Composition</c> del sistema, no la del WinAppSDK: las animaciones
/// corren en el proceso de DWM, asi que son inmunes a que nuestro hilo se bloquee
/// decodificando un PDF. Ese es el argumento que decidio el stack en el dock y vale
/// igual aqui.
/// </para>
///
/// <para>
/// Un solo <c>Compositor</c> para todo el proceso. Admite varios
/// <c>DesktopWindowTarget</c>, que es como cualquier app con varias ventanas lo hace.
/// </para>
/// </summary>
internal static unsafe class Visuals
{
    // Hay que conservarlo vivo: si se recoge, el compositor se queda sin cola de
    // despacho en este hilo.
    private static object? _dispatcherQueueController;

    private static Compositor? _compositor;

    public static Compositor Compositor
    {
        get
        {
            EnsureDispatcherQueue();
            return _compositor ??= new Compositor();
        }
    }

    /// <summary>
    /// Windows.UI.Composition exige una DispatcherQueue en el hilo antes de poder crear
    /// el Compositor. Una sola por proceso basta.
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
    /// Acrilico, con caida a color solido si el sistema no lo soporta. El panel sigue
    /// siendo usable en ese caso: solo se ve mas plano.
    /// </summary>
    public static CompositionBrush CreateAcrylicBrush()
    {
        try
        {
            return Compositor.CreateHostBackdropBrush();
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[acrilico] no disponible, se usa color solido: {ex.Message}");
            return Compositor.CreateColorBrush(Color.FromArgb(220, 28, 28, 32));
        }
    }
}
