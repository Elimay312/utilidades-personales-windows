using System.Drawing;
using System.Runtime.InteropServices;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.System.Com;
using Windows.Win32.System.Com.StructuredStorage;
using Windows.Win32.System.Ole;
using Windows.Win32.System.SystemServices;
using Windows.Win32.UI.Shell;

namespace Dock;

/// <summary>
/// Recibe lo que se suelta encima del dock.
///
/// Sujeto a la enmienda 2 de SEGURIDAD.md. Es puramente receptivo: solo ve lo que el
/// usuario suelta sobre esta ventana, con su gesto. No es el portapapeles, que sigue
/// prohibido.
///
/// <para>
/// Es <c>IDropTarget</c> y no <c>WM_DROPFILES</c> porque hace falta saber la posición
/// <b>durante</b> el arrastre, para iluminar el icono de debajo. <c>WM_DROPFILES</c>
/// solo avisa al soltar, y <c>DragQueryPoint</c> da esa única posición y nada más.
/// </para>
///
/// <para>
/// El hilo que registra esto tiene que estar bombeando mensajes. Si se bloquea, la app
/// que esté arrastrando por encima <b>se cuelga hasta que nosotros cerremos</b>: es el
/// fallo que la documentación de RegisterDragDrop describe con esas palabras. Por eso
/// aquí no se lanza nada: se extraen las rutas y se deja el trabajo encolado.
/// </para>
/// </summary>
internal sealed unsafe class DockDropTarget(DockWindow dock) : IDropTarget
{
    private readonly DockWindow _dock = dock;

    /// <summary>
    /// El que dibuja la miniatura que sigue al cursor. Es cosa del DESTINO, no del
    /// origen: sin llamarlo, arrastrar sobre el dock enseña un cursor pelado mientras
    /// que sobre cualquier otra ventana se ve el fichero. Es puramente visual, así que
    /// si no se puede crear se sigue sin él.
    /// </summary>
    private readonly IDropTargetHelper? _helper = CreateHelper();

    public void DragEnter(IDataObject pDataObj, MODIFIERKEYS_FLAGS grfKeyState, POINTL pt, DROPEFFECT* pdwEffect)
    {
        *pdwEffect = Effect(pt);

        Point p = new(pt.x, pt.y);
        try { _helper?.DragEnter(_dock.Handle, pDataObj, &p, DROPEFFECT.DROPEFFECT_COPY); }
        catch { /* solo era la miniatura */ }
    }

    public void DragOver(MODIFIERKEYS_FLAGS grfKeyState, POINTL pt, DROPEFFECT* pdwEffect)
    {
        *pdwEffect = Effect(pt);

        Point p = new(pt.x, pt.y);
        try { _helper?.DragOver(&p, DROPEFFECT.DROPEFFECT_COPY); }
        catch { /* solo era la miniatura */ }
    }

    public void DragLeave()
    {
        _dock.OnDragOutside();
        try { _helper?.DragLeave(); }
        catch { /* solo era la miniatura */ }
    }

    public void Drop(IDataObject pDataObj, MODIFIERKEYS_FLAGS grfKeyState, POINTL pt, DROPEFFECT* pdwEffect)
    {
        _dock.AcceptsDropAt(pt.x, pt.y);
        *pdwEffect = DROPEFFECT.DROPEFFECT_NONE;

        Point p = new(pt.x, pt.y);
        try { _helper?.Drop(pDataObj, &p, DROPEFFECT.DROPEFFECT_COPY); }
        catch { /* solo era la miniatura */ }

        // AQUÍ y no después: la documentación dice que los data objects pasados a
        // IDropTarget dejan de ser válidos en cuanto termina la suelta. Lo que no se
        // saque antes de retornar, ya no se puede sacar.
        _dock.QueueDrop(ItemsOf(pDataObj));
        _dock.OnDragOutside();
    }

    private DROPEFFECT Effect(POINTL pt)
        => _dock.AcceptsDropAt(pt.x, pt.y) ? DROPEFFECT.DROPEFFECT_COPY : DROPEFFECT.DROPEFFECT_NONE;

    /// <summary>
    /// Qué se ha soltado, ya resuelto.
    ///
    /// Se usa <c>SHCreateShellItemArrayFromDataObject</c> en vez de leer los formatos a
    /// mano porque entiende de una vez tanto <c>CF_HDROP</c> (rutas) como
    /// <c>CFSTR_SHELLIDLIST</c> (PIDLs), que es lo que llevan los objetos sin ruta de
    /// disco, como las apps de la Store. Es lo que Microsoft recomienda explícitamente.
    /// </summary>
    private static DroppedItem[] ItemsOf(IDataObject data)
    {
        Guid iid = typeof(IShellItemArray).GUID;
        if (PInvoke.SHCreateShellItemArrayFromDataObject(
                (System.Runtime.InteropServices.ComTypes.IDataObject)(object)data,
                &iid,
                out object obj).Failed)
        {
            return [];
        }

        var items = (IShellItemArray)obj;
        items.GetCount(out uint count);

        List<DroppedItem> result = [];
        for (uint i = 0; i < count; i++)
        {
            items.GetItemAt(i, out IShellItem item);
            if (Resolve(item) is DroppedItem resolved) result.Add(resolved);
        }

        return [.. result];
    }

    /// <summary>
    /// De un elemento del shell a lo que el dock guardaría de él.
    ///
    /// El orden importa: <b>primero el AppUserModelID</b>. Un acceso directo del menú
    /// Inicio puede llevar el AUMID dentro, y ese gana sobre su ruta, porque es lo que
    /// identifica a la app de verdad. Solo si no lo lleva se mira la ruta.
    /// </summary>
    private static DroppedItem? Resolve(IShellItem item)
    {
        string name = Display(item, SIGDN.SIGDN_NORMALDISPLAY) ?? "";

        if (item is IShellItem2 item2)
        {
            try
            {
                PROPERTYKEY key = PInvoke.PKEY_AppUserModel_ID;
                PWSTR aumid;
                item2.GetString(&key, &aumid);
                string id = aumid.ToString();
                Marshal.FreeCoTaskMem((nint)aumid.Value);

                if (!string.IsNullOrEmpty(id))
                    return new DroppedItem(@"shell:AppsFolder\" + id, name, null);
            }
            catch
            {
                // No es una app: sigue por la ruta.
            }
        }

        if (Display(item, SIGDN.SIGDN_FILESYSPATH) is not string path) return null;

        // Un acceso directo se guarda por su destino, no por el .lnk: el fichero puede
        // desaparecer o moverse y lo que el usuario quería era la app.
        string target = path.EndsWith(".lnk", StringComparison.OrdinalIgnoreCase)
            ? TargetOfShortcut(path) ?? path
            : path;

        return new DroppedItem(target, name, path);
    }

    private static string? Display(IShellItem item, SIGDN kind)
    {
        try
        {
            item.GetDisplayName(kind, out PWSTR value);
            string text = value.ToString();
            Marshal.FreeCoTaskMem((nint)value.Value);
            return string.IsNullOrEmpty(text) ? null : text;
        }
        catch
        {
            // Un objeto virtual no tiene ruta de disco, y eso no es un error.
            return null;
        }
    }

    /// <summary>
    /// A dónde apunta un acceso directo.
    ///
    /// Solo se LEE, nunca se escribe: <c>IPersistFile::Save</c> está prohibido por la
    /// enmienda 2 de SEGURIDAD.md, porque escribir accesos directos es persistencia.
    ///
    /// Sin <c>Resolve</c>: su comportamiento por defecto busca el destino por
    /// subdirectorios y volúmenes y, si no lo encuentra, <b>abre un diálogo modal</b>.
    /// En un dock eso es inaceptable.
    /// </summary>
    private static string? TargetOfShortcut(string path)
    {
        try
        {
            var link = (IShellLinkW)new ShellLink();
            fixed (char* file = path)
            {
                ((IPersistFile)link).Load(new PCWSTR(file), STGM.STGM_READ);
            }

            Span<char> buffer = stackalloc char[260];
            fixed (char* text = buffer)
            {
                link.GetPath(new PWSTR(text), buffer.Length, null, 0);
                string target = new PWSTR(text).ToString();
                return string.IsNullOrEmpty(target) ? null : target;
            }
        }
        catch
        {
            return null;
        }
    }

    private static IDropTargetHelper? CreateHelper()
    {
        try
        {
            Guid clsid = PInvoke.CLSID_DragDropHelper;
            Guid iid = typeof(IDropTargetHelper).GUID;
            PInvoke.CoCreateInstance(&clsid, null, CLSCTX.CLSCTX_INPROC_SERVER, &iid, out object helper)
                .ThrowOnFailure();

            return (IDropTargetHelper)helper;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[drop] sin miniatura de arrastre: {ex.Message}");
            return null;
        }
    }
}

/// <summary>Algo que el usuario soltó sobre el dock, ya resuelto.</summary>
/// <param name="Target">Lo que iría en la configuración: una ruta o un shell:AppsFolder.</param>
/// <param name="Name">Nombre visible.</param>
/// <param name="FilePath">
/// Su ruta en disco, o null si es un objeto virtual. Es lo que se le pasa a una app al
/// abrirlo: para eso hace falta un fichero de verdad.
/// </param>
internal readonly record struct DroppedItem(string Target, string Name, string? FilePath);
