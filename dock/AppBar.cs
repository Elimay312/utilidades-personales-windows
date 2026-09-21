using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.UI.Shell;

namespace Dock;

/// <summary>
/// El dock declarado ante el shell como barra de herramientas de escritorio.
///
/// Es lo que el usuario pidió como "hacerlo como app", y resulta ser una sola API:
/// <c>SHAppBarMessage</c>. Es de <c>Shell32</c>, pública desde XP, actúa sobre
/// <b>nuestro propio HWND</b> y no necesita elevación. Su único uso documentado es
/// literalmente "soy una barra de herramientas de escritorio", que es lo que somos.
///
/// Da dos cosas que no se podían tener a mano:
///
/// <list type="bullet">
/// <item>Reserva de espacio de verdad. En palabras de la doc: <i>"The system prevents
/// other applications from using the desktop area used by an appbar"</i>. El área de
/// trabajo se encoge y una ventana maximizada deja de meterse debajo del dock.</item>
/// <item>Avisos del sistema —<c>ABN_POSCHANGED</c>, <c>ABN_FULLSCREENAPP</c>— en vez de
/// sondear.</item>
/// </list>
///
/// <c>ABM_SETSTATE</c> <b>no está aquí y no va a estar</b>: escribe el ajuste global de
/// la barra de tareas del usuario, devuelve siempre TRUE y no hay forma documentada de
/// restaurarlo. Ver la enmienda 3 de SEGURIDAD.md.
/// </summary>
internal sealed unsafe class AppBar : IDisposable
{
    // Valores de shellapi.h. CsWin32 no los genera porque no están en los metadatos
    // de proyección, solo en la documentación.
    private const uint ABM_NEW = 0x00000000;
    private const uint ABM_REMOVE = 0x00000001;
    private const uint ABM_QUERYPOS = 0x00000002;
    private const uint ABM_SETPOS = 0x00000003;
    private const uint ABM_ACTIVATE = 0x00000006;
    private const uint ABM_WINDOWPOSCHANGED = 0x00000009;

    /// <summary>Cambió el tamaño, la posición o la visibilidad de la barra de tareas.</summary>
    public const nuint ABN_POSCHANGED = 0x0000001;

    /// <summary>Se abrió o se cerró una app a pantalla completa.</summary>
    public const nuint ABN_FULLSCREENAPP = 0x0000002;

    private readonly HWND _hwnd;
    private bool _registered;

    /// <summary>
    /// Registra la ventana. <paramref name="callbackMessage"/> es el id con el que
    /// llegarán los avisos <c>ABN_*</c>, en <c>wParam</c>.
    /// </summary>
    public AppBar(HWND hwnd, uint callbackMessage)
    {
        _hwnd = hwnd;

        APPBARDATA data = Data();
        data.uCallbackMessage = callbackMessage;

        _registered = PInvoke.SHAppBarMessage(ABM_NEW, ref data) != 0;
        if (!_registered) Console.WriteLine("[appbar] ABM_NEW rechazado");
    }

    public bool Registered => _registered;

    /// <summary>
    /// Pide el hueco del borde inferior y devuelve el rectángulo que el sistema
    /// concede, que <b>no tiene por qué ser el que se pidió</b>: la doc avisa de que
    /// <i>"the system adjusts the rectangle purely by rectangle subtraction; it makes
    /// no effort to preserve the rectangle's initial size"</i>, y de que si compartimos
    /// borde con la barra de tareas ella se queda en el filo y a nosotros nos empuja
    /// hacia dentro.
    ///
    /// Por eso hay que colocarse en lo que devuelve esto y no en lo que se pidió: si
    /// se ignorara y se recalculara desde el área de trabajo, cada reserva movería el
    /// dock un poco más arriba y la siguiente otro poco. Una escalera infinita.
    /// </summary>
    public RECT Reserve(RECT wanted)
    {
        if (!_registered) return wanted;

        APPBARDATA data = Data();
        data.uEdge = PInvoke.ABE_BOTTOM;
        data.rc = wanted;

        PInvoke.SHAppBarMessage(ABM_QUERYPOS, ref data);
        PInvoke.SHAppBarMessage(ABM_SETPOS, ref data);
        return data.rc;
    }

    /// <summary>
    /// Se manda al recibir <c>WM_ACTIVATE</c> y <c>WM_WINDOWPOSCHANGED</c>, y no es
    /// opcional: <i>"Sending these messages ensures that the system properly sets the
    /// z-order of any autohide appbars on the same edge"</i>. Compartimos borde con la
    /// barra de tareas, que está justo en autoocultar.
    /// </summary>
    public void Activated() => Send(ABM_ACTIVATE);

    public void Moved() => Send(ABM_WINDOWPOSCHANGED);

    private void Send(uint message)
    {
        if (!_registered) return;

        APPBARDATA data = Data();
        PInvoke.SHAppBarMessage(message, ref data);
    }

    private APPBARDATA Data() => new()
    {
        cbSize = (uint)sizeof(APPBARDATA),
        hWnd = _hwnd,
    };

    /// <summary>
    /// <i>"An application should always send ABM_REMOVE before destroying an appbar."</i>
    /// Sin esto el sistema sigue reservándonos el hueco después de morir.
    /// </summary>
    public void Dispose()
    {
        if (!_registered) return;

        _registered = false;
        APPBARDATA data = Data();
        PInvoke.SHAppBarMessage(ABM_REMOVE, ref data);
    }
}
