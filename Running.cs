using System.Diagnostics;
using Microsoft.Win32.SafeHandles;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Dwm;
using Windows.Win32.System.Threading;
using Windows.Win32.UI.WindowsAndMessaging;

namespace Dock;

/// <summary>Estado de una app del dock: su ventana principal, si tiene alguna.</summary>
///
/// <remarks>
/// "Abierta" quiere decir <b>que tiene ventana</b>, no que haya un proceso vivo. Con
/// los procesos a secas el Explorador salía siempre abierto, porque explorer.exe es el
/// shell y nunca se va, y el puntito mentía.
/// </remarks>
internal readonly record struct AppState(HWND MainWindow)
{
    public bool HasWindow => !MainWindow.IsNull;
}

/// <summary>
/// Averigua qué apps del dock están abiertas y dónde está su ventana.
///
/// Enumera ventanas, y por eso está sujeto a la enmienda 1 de SEGURIDAD.md: solo se usa
/// para saber qué iconos llevan puntito y para localizar la app del icono que el usuario
/// acaba de clicar. El resultado se usa y se tira, y no se inventaría nada.
/// </summary>
internal static class Running
{
    private const uint ProcessQueryLimitedInformation = 0x1000;

    /// <summary>DWMWA_CLOAKED: si DWM tiene la ventana oculta aunque sea "visible".</summary>
    private const uint DwmwaCloaked = 14;

    /// <summary>Las apps UWP no son dueñas de su ventana: la hospeda este marco.</summary>
    private const string UwpFrameClass = "ApplicationFrameWindow";

    /// <summary>Dentro del marco UWP, la ventana que sí pertenece a la app.</summary>
    private const string UwpCoreClass = "Windows.UI.Core.CoreWindow";

    /// <summary>
    /// Marco UWP -> PID de la app, recordado de cuando sí se pudo resolver.
    ///
    /// Al minimizarse una app UWP, Windows le saca la CoreWindow de dentro del marco y
    /// la deja como ventana de primer nivel aparte. A partir de ahí FindWindowEx no
    /// encuentra nada dentro del marco y la ventana dejaría de cruzar con su icono: el
    /// dock creería que la app está cerrada y el siguiente clic abriría OTRA instancia,
    /// que es exactamente lo que pasaba con la Calculadora.
    /// </summary>
    private static readonly Dictionary<nint, uint> UwpOwners = [];

    /// <summary>
    /// Resuelve de una pasada el estado de todas las apps. De una pasada y no una por
    /// una porque tanto los procesos como las ventanas se recorren enteros: hacerlo una
    /// vez por icono sería tirar el trabajo.
    /// </summary>
    public static AppState[] Check(IReadOnlyList<DockApp> apps)
    {
        AppState[] result = new AppState[apps.Count];

        // PID -> índice de app, para poder cruzar después con las ventanas.
        Dictionary<uint, int> owners = MapProcessesToApps(apps);

        foreach ((HWND window, uint pid) in TopLevelWindows())
        {
            if (!owners.TryGetValue(pid, out int index)) continue;
            if (result[index].HasWindow) continue;

            result[index] = new AppState(window);
        }

        return result;
    }

    /// <summary>
    /// Qué proceso pertenece a qué app del dock. Las apps normales se reconocen por el
    /// nombre del ejecutable; las MSIX no tienen un .exe deducible desde el
    /// AppUserModelID, así que se comparan por nombre de familia del paquete: la parte
    /// anterior al signo de admiración del AUMID.
    /// </summary>
    private static Dictionary<uint, int> MapProcessesToApps(IReadOnlyList<DockApp> apps)
    {
        Dictionary<string, int> byExeName = new(StringComparer.OrdinalIgnoreCase);
        Dictionary<string, int> byFamily = new(StringComparer.OrdinalIgnoreCase);

        for (int i = 0; i < apps.Count; i++)
        {
            if (apps[i].Separator) continue;

            if (apps[i].IsShellItem)
            {
                if (FamilyOf(apps[i].Target) is string family) byFamily[family] = i;
            }
            else
            {
                byExeName[Path.GetFileNameWithoutExtension(apps[i].Target)] = i;
            }
        }

        Dictionary<uint, int> owners = [];

        foreach (Process process in Process.GetProcesses())
        {
            try
            {
                uint pid = (uint)process.Id;

                if (byExeName.TryGetValue(process.ProcessName, out int direct))
                {
                    owners[pid] = direct;
                    continue;
                }

                if (byFamily.Count == 0) continue;
                if (FamilyOfProcess(pid) is string family && byFamily.TryGetValue(family, out int packaged))
                    owners[pid] = packaged;
            }
            catch
            {
                // Un proceso que no se deja consultar no es un error: simplemente no es
                // ninguna de nuestras apps.
            }
            finally
            {
                process.Dispose();
            }
        }

        return owners;
    }

    /// <summary>Del target de un elemento del shell saca el nombre de familia.</summary>
    private static string? FamilyOf(string target)
    {
        int slash = target.LastIndexOf('\\');
        if (slash < 0) return null;

        string aumid = target[(slash + 1)..];
        int bang = aumid.IndexOf('!');
        return bang > 0 ? aumid[..bang] : null;
    }

    private static string? FamilyOfProcess(uint pid)
    {
        using SafeFileHandle handle = PInvoke.OpenProcess_SafeHandle(
            (PROCESS_ACCESS_RIGHTS)ProcessQueryLimitedInformation, false, pid);
        if (handle.IsInvalid) return null;

        // Primera llamada solo para saber el tamaño. Un proceso que no está empaquetado
        // devuelve APPMODEL_ERROR_NO_PACKAGE y se descarta solo.
        uint length = 0;
        if (PInvoke.GetPackageFamilyName(handle, ref length, default) != WIN32_ERROR.ERROR_INSUFFICIENT_BUFFER)
            return null;

        Span<char> buffer = new char[length];
        return PInvoke.GetPackageFamilyName(handle, ref length, buffer) == WIN32_ERROR.ERROR_SUCCESS
            ? new string(buffer[..((int)length - 1)])
            : null;
    }

    /// <summary>
    /// Ventanas candidatas a ser "la ventana" de una app, con el PID de quien de verdad
    /// las posee. Se descartan las invisibles, las que tienen dueño (diálogos y
    /// flotantes), las sin título y las que DWM tiene encubiertas, que es como el shell
    /// deja ventanas fantasma por ahí.
    /// </summary>
    private static unsafe List<(HWND Window, uint Pid)> TopLevelWindows()
    {
        List<(HWND, uint)> found = [];

        // El escritorio es una ventana de explorer.exe CON título ("Program Manager"),
        // así que pasaba todos los filtros: el Explorador salía siempre abierto y el
        // clic minimizaba el escritorio entero.
        HWND desktop = PInvoke.GetShellWindow();

        PInvoke.EnumWindows((window, _) =>
        {
            if (window == desktop) return true;
            if (!PInvoke.IsWindowVisible(window)) return true;
            if (!PInvoke.GetWindow(window, GET_WINDOW_CMD.GW_OWNER).IsNull) return true;
            if (PInvoke.GetWindowTextLength(window) == 0) return true;

            int cloaked = 0;
            PInvoke.DwmGetWindowAttribute(window, (DWMWINDOWATTRIBUTE)DwmwaCloaked, &cloaked, sizeof(int));
            if (cloaked != 0) return true;

            uint pid = 0;
            PInvoke.GetWindowThreadProcessId(window, &pid);
            if (pid == 0) return true;

            found.Add((window, RealOwnerOf(window, pid)));
            return true;
        }, default);

        return found;
    }

    /// <summary>
    /// El PID de quien de verdad es dueño de la ventana.
    ///
    /// Las apps UWP no poseen su propia ventana: la visible es un ApplicationFrameWindow
    /// de ApplicationFrameHost.exe, y la app real vive en una ventana hija. Sin este
    /// rodeo, la Calculadora parecería pertenecer al host y nunca cruzaría con su icono.
    /// </summary>
    private static unsafe uint RealOwnerOf(HWND window, uint hostPid)
    {
        if (ClassNameOf(window) != UwpFrameClass) return hostPid;

        fixed (char* child = UwpCoreClass)
        {
            HWND core = PInvoke.FindWindowEx(window, default, new PCWSTR(child), default);
            if (!core.IsNull)
            {
                uint pid = 0;
                PInvoke.GetWindowThreadProcessId(core, &pid);
                if (pid != 0)
                {
                    UwpOwners[(nint)window.Value] = pid;
                    return pid;
                }
            }
        }

        // Minimizada: la CoreWindow ya no está dentro. Vale lo que se resolvió mientras
        // estaba a la vista, que el dock consulta cada segundo.
        return UwpOwners.TryGetValue((nint)window.Value, out uint remembered) ? remembered : hostPid;
    }

    private static unsafe string ClassNameOf(HWND window)
    {
        Span<char> buffer = stackalloc char[64];
        fixed (char* p = buffer)
        {
            int length = PInvoke.GetClassName(window, p, buffer.Length);
            return length > 0 ? new string(buffer[..length]) : string.Empty;
        }
    }
}
