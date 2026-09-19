using System.Collections.Concurrent;
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
internal readonly record struct AppState(HWND MainWindow, HWND[]? Windows)
{
    public bool HasWindow => !MainWindow.IsNull;

    /// <summary>
    /// Todas las ventanas de la app, no solo la primera. La lista completa ya se
    /// construia; lo que habia era un colapso a una al final. De aqui salen ciclar con
    /// la rueda y las miniaturas al pasar el raton.
    /// </summary>
    public HWND[] All => Windows ?? [];
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
    ///
    /// Concurrente porque con varios monitores hay un dock por pantalla y cada uno
    /// refresca su estado en una tarea del pool: dos hilos escribiendo un Dictionary
    /// normal a la vez pueden dejarlo corrupto y colgarse dentro de él.
    /// </summary>
    private static readonly ConcurrentDictionary<nint, uint> UwpOwners = new();

    /// <summary>
    /// Lo caro, hecho UNA vez: recorrer los procesos del sistema y todas las ventanas
    /// de primer nivel. No depende de que apps tenga cada dock, y con una pantalla por
    /// dock se repetia tres veces por segundo para sacar exactamente lo mismo.
    /// </summary>
    public sealed record Snapshot(
        List<(HWND Window, uint Pid)> Windows,
        Dictionary<uint, string> ProcessNames)
    {
        /// <summary>PID -> familia MSIX, resuelta como mucho una vez por barrido.</summary>
        public Dictionary<uint, string?> Families { get; } = [];

        /// <summary>PID -> ruta del ejecutable, resuelta como mucho una vez por barrido.</summary>
        public Dictionary<uint, string?> Paths { get; } = [];
    }

    public static Snapshot Take()
    {
        var reloj = System.Diagnostics.Stopwatch.StartNew();

        Dictionary<uint, string> names = [];
        foreach (Process process in Process.GetProcesses())
        {
            try { names[(uint)process.Id] = process.ProcessName; }
            catch { /* un proceso que no se deja consultar no es ninguna de nuestras apps */ }
            finally { process.Dispose(); }
        }

        long procesos = reloj.ElapsedMilliseconds;
        List<(HWND, uint)> windows = TopLevelWindows();

        if (Environment.GetEnvironmentVariable("DOCK_HOVER_LOG") is not null)
        {
            Console.WriteLine($"[barrido] {names.Count} procesos en {procesos} ms, " +
                $"{windows.Count} ventanas en {reloj.ElapsedMilliseconds - procesos} ms");
        }

        return new Snapshot(windows, names);
    }

    /// <summary>
    /// Resuelve de una pasada el estado de todas las apps. De una pasada y no una por
    /// una porque tanto los procesos como las ventanas se recorren enteros: hacerlo una
    /// vez por icono sería tirar el trabajo.
    /// </summary>
    public static AppState[] Check(IReadOnlyList<DockApp> apps, Snapshot snapshot)
    {
        // PID -> índice de app, para poder cruzar después con las ventanas.
        Dictionary<uint, int> owners = MapProcessesToApps(apps, snapshot);

        List<HWND>?[] found = new List<HWND>?[apps.Count];
        foreach ((HWND window, uint pid) in snapshot.Windows)
        {
            if (!owners.TryGetValue(pid, out int index)) continue;
            (found[index] ??= []).Add(window);
        }

        AppState[] result = new AppState[apps.Count];
        for (int i = 0; i < result.Length; i++)
        {
            if (found[i] is not { Count: > 0 } list) continue;
            result[i] = new AppState(list[0], [.. list]);
        }

        return result;
    }

    /// <summary>
    /// Qué proceso pertenece a qué app del dock. Las apps normales se reconocen por el
    /// nombre del ejecutable; las MSIX no tienen un .exe deducible desde el
    /// AppUserModelID, así que se comparan por nombre de familia del paquete: la parte
    /// anterior al signo de admiración del AUMID.
    /// </summary>
    private static Dictionary<uint, int> MapProcessesToApps(IReadOnlyList<DockApp> apps, Snapshot snapshot)
    {
        Dictionary<string, int> byExeName = new(StringComparer.OrdinalIgnoreCase);
        Dictionary<string, int> byFamily = new(StringComparer.OrdinalIgnoreCase);

        for (int i = 0; i < apps.Count; i++)
        {
            if (apps[i].Separator) continue;

            // Un documento o una carpeta no pueden estar "abiertos": no tienen proceso
            // propio. Sin este corte, un notas.txt en el dock se indexaría por el nombre
            // "notas" y cruzaría con cualquier proceso que se llame así.
            if (!apps[i].IsApp) continue;

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

        foreach ((uint pid, string name) in snapshot.ProcessNames)
        {
            if (byExeName.TryGetValue(name, out int direct))
            {
                owners[pid] = direct;
                continue;
            }

            if (byFamily.Count == 0) continue;

            // La familia se resuelve como mucho una vez por barrido, no una por dock:
            // son unos cientos de OpenProcess + GetPackageFamilyName y con tres
            // pantallas se hacian tres veces para sacar lo mismo.
            if (!snapshot.Families.TryGetValue(pid, out string? family))
            {
                try { family = FamilyOfProcess(pid); }
                catch { family = null; }
                snapshot.Families[pid] = family;
            }

            if (family is not null && byFamily.TryGetValue(family, out int packaged))
                owners[pid] = packaged;
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
            if (!IsRealWindow(window, desktop)) return true;

            uint pid = 0;
            PInvoke.GetWindowThreadProcessId(window, &pid);
            if (pid == 0) return true;

            found.Add((window, RealOwnerOf(window, pid)));
            return true;
        }, default);

        return found;
    }

    /// <summary>
    /// Las apps que tienen ventana y <b>no están en el dock</b>: lo que la barra de
    /// tareas enseña sin que las hayas anclado.
    ///
    /// Se identifican por la ruta de su ejecutable, no por el proceso: un navegador
    /// tiene treinta procesos y una sola entrada. Y se ordenan por nombre para que los
    /// iconos no bailen cada vez que se abre algo.
    /// </summary>
    public static List<DockApp> Unpinned(IReadOnlyList<DockApp> pinned, Snapshot snapshot)
    {
        Dictionary<uint, int> owners = MapProcessesToApps(pinned, snapshot);
        uint own = PInvoke.GetCurrentProcessId();

        // Las que YA están ancladas, por ruta: MapProcessesToApps cruza por nombre de
        // proceso y no pilla el caso de la misma app anclada con otra ruta.
        HashSet<string> yaEstan = new(StringComparer.OrdinalIgnoreCase);
        foreach (DockApp app in pinned)
        {
            if (!app.Separator && app.Target.Length > 0) yaEstan.Add(app.Target);
        }

        Dictionary<string, string> found = new(StringComparer.OrdinalIgnoreCase);

        foreach ((HWND _, uint pid) in snapshot.Windows)
        {
            if (pid == own || owners.ContainsKey(pid)) continue;
            if (PathOf(pid, snapshot) is not string path || yaEstan.Contains(path)) continue;

            found.TryAdd(path, snapshot.ProcessNames.TryGetValue(pid, out string? name) && name.Length > 0
                ? name
                : Path.GetFileNameWithoutExtension(path));
        }

        return [.. found
            .OrderBy(entry => entry.Value, StringComparer.CurrentCultureIgnoreCase)
            .Select(entry => new DockApp { Name = entry.Value, Target = entry.Key })];
    }

    /// <summary>
    /// La ruta del ejecutable de un proceso. Se recuerda por barrido: un navegador
    /// aporta varias ventanas y todas caen en el mismo proceso.
    /// </summary>
    private static unsafe string? PathOf(uint pid, Snapshot snapshot)
    {
        if (snapshot.Paths.TryGetValue(pid, out string? cached)) return cached;

        string? path = null;
        try
        {
            using SafeFileHandle handle = PInvoke.OpenProcess_SafeHandle(
                (PROCESS_ACCESS_RIGHTS)ProcessQueryLimitedInformation, false, pid);

            if (!handle.IsInvalid)
            {
                Span<char> buffer = new char[260];
                uint size = (uint)buffer.Length;

                if (PInvoke.QueryFullProcessImageName(
                        handle, PROCESS_NAME_FORMAT.PROCESS_NAME_WIN32, buffer, ref size) && size > 0)
                {
                    path = new string(buffer[..(int)size]);
                }
            }
        }
        catch { /* un proceso que no se deja consultar simplemente no sale en el dock */ }

        snapshot.Paths[pid] = path;
        return path;
    }

    /// <summary>
    /// La criba: qué cuenta como "una ventana del usuario". Se descartan las
    /// invisibles, las que tienen dueño (diálogos y flotantes), las sin título y las que
    /// DWM tiene encubiertas, que es como el shell deja ventanas fantasma por ahí.
    ///
    /// La barra de tareas no tiene título, así que se cae sola por aquí. Importa: si
    /// contara, el autoocultar inteligente vería siempre algo debajo del dock.
    /// </summary>
    private static unsafe bool IsRealWindow(HWND window, HWND desktop)
    {
        if (window == desktop) return false;
        if (!PInvoke.IsWindowVisible(window)) return false;
        if (!PInvoke.GetWindow(window, GET_WINDOW_CMD.GW_OWNER).IsNull) return false;
        if (PInvoke.GetWindowTextLength(window) == 0) return false;

        int cloaked = 0;
        PInvoke.DwmGetWindowAttribute(window, (DWMWINDOWATTRIBUTE)DwmwaCloaked, &cloaked, sizeof(int));
        return cloaked == 0;
    }

    /// <summary>
    /// Si alguna ventana del usuario se solapa con ese rectángulo de pantalla.
    ///
    /// Es lo que decide el autoocultar inteligente: sin nada debajo, el dock no tiene de
    /// qué esconderse. Se pregunta en el momento y no se guarda en ningún inventario,
    /// porque <b>mover una ventana no genera ningún aviso del shell</b>: una lista de
    /// rectángulos estaría desfasada en cuanto el usuario arrastrase algo.
    ///
    /// Las ventanas del propio dock no cuentan, claro. Las minimizadas tampoco: siguen
    /// teniendo rectángulo, pero no tapan nada.
    /// </summary>
    public static unsafe bool AnythingOver(RECT area)
    {
        bool found = false;
        HWND desktop = PInvoke.GetShellWindow();
        uint own = PInvoke.GetCurrentProcessId();

        PInvoke.EnumWindows((window, _) =>
        {
            if (!IsRealWindow(window, desktop)) return true;
            if (PInvoke.IsIconic(window)) return true;

            uint pid = 0;
            PInvoke.GetWindowThreadProcessId(window, &pid);
            if (pid == own) return true;

            if (!PInvoke.GetWindowRect(window, out RECT r)) return true;
            if (r.right <= area.left || r.left >= area.right
                || r.bottom <= area.top || r.top >= area.bottom) return true;

            found = true;
            return false;
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
