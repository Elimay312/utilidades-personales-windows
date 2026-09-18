using System.Diagnostics;
using Windows.Win32;
using Windows.Win32.Foundation;
using Microsoft.Win32.SafeHandles;
using Windows.Win32.System.Threading;

namespace Dock;

/// <summary>
/// Si las apps del dock están abiertas, para pintarles el punto debajo.
///
/// Solo LEE: pregunta qué procesos hay y a qué paquete pertenecen. No enumera ni toca
/// ventanas ajenas, que es lo que prohíbe la Fase 2. Saber que Paint está abierto no
/// requiere mirar sus ventanas.
/// </summary>
internal static class Running
{
    private const uint ProcessQueryLimitedInformation = 0x1000;

    /// <summary>
    /// Resuelve de una pasada el estado de todas las apps. De una pasada y no una por
    /// una porque el caso MSIX obliga a recorrer la lista de procesos, y hacerlo una
    /// vez por icono sería tirar el trabajo.
    /// </summary>
    public static bool[] Check(IReadOnlyList<DockApp> apps)
    {
        bool[] result = new bool[apps.Count];

        // Las apps normales se resuelven por nombre de ejecutable, que es barato.
        for (int i = 0; i < apps.Count; i++)
        {
            if (apps[i].Separator || apps[i].IsShellItem) continue;

            string name = Path.GetFileNameWithoutExtension(apps[i].Target);
            result[i] = Process.GetProcessesByName(name).Length > 0;
        }

        // Las MSIX no tienen un .exe deducible desde el AppUserModelID, así que se
        // comparan por nombre de familia del paquete: la parte anterior al '!'.
        List<int> packaged = [];
        for (int i = 0; i < apps.Count; i++)
        {
            if (!apps[i].Separator && apps[i].IsShellItem) packaged.Add(i);
        }

        if (packaged.Count == 0) return result;

        HashSet<string> familias = RunningPackageFamilies();
        foreach (int i in packaged)
        {
            string? family = FamilyOf(apps[i].Target);
            if (family is not null) result[i] = familias.Contains(family);
        }

        return result;
    }

    /// <summary>De "shell:AppsFolder\Familia!App" saca "Familia".</summary>
    private static string? FamilyOf(string target)
    {
        int slash = target.LastIndexOf('\\');
        if (slash < 0) return null;

        string aumid = target[(slash + 1)..];
        int bang = aumid.IndexOf('!');
        return bang > 0 ? aumid[..bang] : null;
    }

    private static HashSet<string> RunningPackageFamilies()
    {
        HashSet<string> families = new(StringComparer.OrdinalIgnoreCase);

        foreach (Process process in Process.GetProcesses())
        {
            try
            {
                using SafeFileHandle handle = PInvoke.OpenProcess_SafeHandle(
                    (PROCESS_ACCESS_RIGHTS)ProcessQueryLimitedInformation, false, (uint)process.Id);
                if (handle.IsInvalid) continue;

                // Primera llamada solo para saber el tamaño. Un proceso que no está
                // empaquetado devuelve APPMODEL_ERROR_NO_PACKAGE y se salta solo.
                uint length = 0;
                if (PInvoke.GetPackageFamilyName(handle, ref length, default)
                    != WIN32_ERROR.ERROR_INSUFFICIENT_BUFFER)
                {
                    continue;
                }

                Span<char> buffer = new char[length];
                if (PInvoke.GetPackageFamilyName(handle, ref length, buffer) == WIN32_ERROR.ERROR_SUCCESS)
                    families.Add(new string(buffer[..((int)length - 1)]));
            }
            catch
            {
                // Un proceso que no se deja abrir no es un error: simplemente no es
                // ninguna de nuestras apps.
            }
            finally
            {
                process.Dispose();
            }
        }

        return families;
    }
}
