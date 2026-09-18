using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.UI.Shell;

namespace Dock;

/// <summary>Un elemento de dentro de una carpeta del dock.</summary>
internal sealed record StackItem(string Name, string Target, IconBitmap? Icon, bool IsFolder);

/// <summary>
/// Lo que hay dentro de una carpeta del dock.
///
/// Se enumera con el shell y no con <c>Directory.GetFiles</c> por dos razones: funciona
/// igual con carpetas virtuales como la papelera, y da los nombres tal y como los
/// enseña el Explorador, que es lo que el usuario espera leer.
/// </summary>
internal static unsafe class StackItems
{
    /// <summary>
    /// Tope de elementos. Un desplegable no es un explorador de archivos: si hay más,
    /// para eso está abrir la carpeta.
    /// </summary>
    public const int Limit = 20;

    /// <summary>
    /// Lee la carpeta y extrae los iconos. <b>Va en segundo plano</b>: extraer un icono
    /// del shell puede tardar decenas de milisegundos y aquí se hacen veinte.
    /// </summary>
    /// <param name="back">
    /// Carpeta a la que vuelve la primera entrada, o null si esta es la raíz.
    /// </param>
    public static List<StackItem> Read(string folder, string? back = null)
    {
        List<StackItem> items = [];

        // La entrada para volver, si venimos de otra carpeta. Va la primera y con el
        // icono de la carpeta padre, que es lo que hace obvio a dónde lleva.
        if (back is not null)
        {
            IconBitmap? arriba = null;
            try { arriba = Icons.Extract(back); }
            catch { /* sin icono: se sigue */ }

            items.Add(new StackItem("Atrás", back, arriba, true));
        }

        Guid itemId = typeof(IShellItem).GUID;
        object root;
        fixed (char* path = folder)
        {
            if (PInvoke.SHCreateItemFromParsingName(new PCWSTR(path), null, &itemId, out root).Failed) return items;
        }

        Guid enumId = typeof(IEnumShellItems).GUID;
        Guid handler = PInvoke.BHID_EnumItems;

        object enumerator;
        try
        {
            ((IShellItem)root).BindToHandler(null, &handler, &enumId, out enumerator);
        }
        catch
        {
            // No es una carpeta, o no se deja enumerar. No es un error: simplemente no
            // hay nada que desplegar y el clic la abrirá como siempre.
            return items;
        }

        var children = (IEnumShellItems)enumerator;
        IShellItem[] one = new IShellItem[1];

        while (items.Count < Limit)
        {
            uint got = 0;
            children.Next(1, one, &got);
            if (got == 0 || one[0] is null) break;

            IShellItem child = one[0];
            string? path = Display(child, SIGDN.SIGDN_FILESYSPATH) ?? Display(child, SIGDN.SIGDN_DESKTOPABSOLUTEPARSING);
            string name = Display(child, SIGDN.SIGDN_NORMALDISPLAY) ?? "";

            if (path is null) continue;

            IconBitmap? icon = null;
            try { icon = Icons.Extract(path); }
            catch { /* sin icono: se dibuja el hueco y se sigue */ }

            items.Add(new StackItem(name, path, icon, Directory.Exists(path)));
        }

        // Las carpetas primero, como el Explorador: al entrar en una suele quererse
        // seguir bajando. La entrada de volver se queda fuera del orden, la primera.
        int desde = back is null ? 0 : 1;
        items.Sort(desde, items.Count - desde, Comparer<StackItem>.Create((a, b) => a.IsFolder == b.IsFolder
            ? string.Compare(a.Name, b.Name, StringComparison.CurrentCultureIgnoreCase)
            : b.IsFolder.CompareTo(a.IsFolder)));

        return items;
    }

    private static string? Display(IShellItem item, SIGDN kind)
    {
        try
        {
            item.GetDisplayName(kind, out PWSTR value);
            string text = value.ToString();
            System.Runtime.InteropServices.Marshal.FreeCoTaskMem((nint)value.Value);
            return string.IsNullOrEmpty(text) ? null : text;
        }
        catch
        {
            return null;
        }
    }
}
