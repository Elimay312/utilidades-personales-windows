using Windows.Win32;
using Windows.Win32.Foundation;

namespace QuickLook;

/// <summary>
/// Quien esta delante, por el nombre de su clase de ventana.
///
/// <para>
/// Vive aparte porque lo preguntan tres sitios y tiene que contestar lo mismo a los tres:
/// el hook, para decidir si el espacio es nuestro; el panel, para cerrarse cuando el
/// usuario se va a otra app; y <c>Selection</c>, para saber si toca el camino del
/// Escritorio. Si la lista de clases estuviera duplicada, el dia que se anada una se
/// arreglaria en dos sitios y en el tercero no.
/// </para>
///
/// <para>
/// Es solo lectura y no toca ninguna ventana ajena: lee el nombre de una clase, que es
/// publico. Ver SEGURIDAD.md §2 regla 13.
/// </para>
/// </summary>
internal static unsafe class Foreground
{
    /// <summary>
    /// Ventana de carpeta, Explorador antiguo, y las dos del Escritorio. Es la lista que
    /// el §3.1 nombra como cortafuegos del hook.
    /// </summary>
    private static readonly string[] Explorer = ["CabinetWClass", "ExploreWClass", "WorkerW", "Progman"];

    /// <summary>El Escritorio no sale al iterar IShellWindows: tiene su propia consulta.</summary>
    private static readonly string[] Desktop = ["WorkerW", "Progman"];

    public static bool IsExplorer(HWND window) => Is(window, Explorer);

    public static bool IsDesktop(HWND window) => Is(window, Desktop);

    public static bool Is(HWND window, string[] classes)
    {
        if (window.IsNull) return false;

        Span<char> buffer = stackalloc char[64];
        fixed (char* p = buffer)
        {
            int n = PInvoke.GetClassName(window, p, buffer.Length);
            if (n <= 0) return false;
            return classes.Contains(new string(p, 0, n), StringComparer.Ordinal);
        }
    }
}
