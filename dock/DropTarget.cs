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
    /// <b>Manda la ruta de disco, y el AppUserModelID solo cuando no hay ninguna.</b>
    /// El plan decía justo lo contrario, y estaba mal: Chromium y Electron le ponen un
    /// AppUserModelID a sus accesos directos para que Windows agrupe sus ventanas en la
    /// barra de tareas, pero ese id <b>no</b> es una entrada del shell. El de Brave es
    /// literalmente <c>Brave</c>. Y <c>SHCreateItemFromParsingName</c> acepta
    /// <c>shell:AppsFolder\Brave</c> sin rechistar e incluso devuelve un icono, así
    /// que tampoco sirve para distinguirlo: el fallo solo se veía después, cuando el
    /// dock buscaba esa app por nombre de familia de paquete —que Brave no tiene— y
    /// nunca la detectaba como abierta.
    ///
    /// Un objeto de verdad virtual (una app de la Store arrastrada desde el menú
    /// Inicio) no tiene ruta de disco, y ahí el AUMID sí es la única identidad que hay.
    /// </summary>
    private static DroppedItem? Resolve(IShellItem item)
    {
        string name = Display(item, SIGDN.SIGDN_NORMALDISPLAY) ?? "";
        string? aumid = AumidOf(item);
        string? path = Display(item, SIGDN.SIGDN_FILESYSPATH);

        if (path is null) return AsApp(aumid, name);

        // Un .url es el acceso directo que Steam deja por cada juego. Se guarda por
        // su URL y con su icono, no por la ruta del fichero: ver InternetShortcutOf.
        if (path.EndsWith(".url", StringComparison.OrdinalIgnoreCase)
            && InternetShortcut.Read(path) is { } internet)
        {
            return new DroppedItem(internet.Url, name, null, internet.Icon);
        }

        if (!path.EndsWith(".lnk", StringComparison.OrdinalIgnoreCase))
            return new DroppedItem(path, name, path);

        // Un acceso directo se guarda por su destino, no por el .lnk: el fichero puede
        // moverse o borrarse y lo que el usuario quería era la app. Pero se le sacan
        // además el icono y los argumentos, que son suyos y no del destino.
        if (ShortcutOf(path) is { } atajo)
            return new DroppedItem(atajo.Target, name, path, atajo.Icon, atajo.Arguments);

        // Sin destino de fichero: es el acceso directo de una app empaquetada, y
        // entonces su AUMID sí es lo correcto.
        return AsApp(aumid, name) ?? new DroppedItem(path, name, path);
    }

    private static DroppedItem? AsApp(string? aumid, string name)
        => string.IsNullOrEmpty(aumid) ? null : new DroppedItem(@"shell:AppsFolder\" + aumid, name, null);

    private static string? AumidOf(IShellItem item)
    {
        if (item is not IShellItem2 item2) return null;

        try
        {
            PROPERTYKEY key = PInvoke.PKEY_AppUserModel_ID;
            PWSTR value;
            item2.GetString(&key, &value);
            string id = value.ToString();
            Marshal.FreeCoTaskMem((nint)value.Value);
            return string.IsNullOrEmpty(id) ? null : id;
        }
        catch
        {
            // No lo lleva, que es lo normal en un fichero cualquiera.
            return null;
        }
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
    private static (string Target, string? Icon, string Arguments)? ShortcutOf(string path)
    {
        try
        {
            var link = (IShellLinkW)new ShellLink();
            fixed (char* file = path)
            {
                ((IPersistFile)link).Load(new PCWSTR(file), STGM.STGM_READ);
            }

            string target;
            Span<char> buffer = stackalloc char[260];
            fixed (char* text = buffer)
            {
                link.GetPath(new PWSTR(text), buffer.Length, null, 0);
                target = new PWSTR(text).ToString();
            }

            if (string.IsNullOrEmpty(target)) return null;

            // El icono del acceso directo, si declara uno propio. Solo sirve si es un
            // fichero entero: "shell32.dll,3" apunta a un índice dentro de un recurso y
            // el extractor del dock no sabe de índices — en ese caso se cae al destino,
            // que es lo que hacía siempre.
            string? icon = null;
            Span<char> iconPath = stackalloc char[260];
            fixed (char* text = iconPath)
            {
                link.GetIconLocation(new PWSTR(text), iconPath.Length, out int index);

                string raw = new PWSTR(text).ToString();
                if (index == 0 && raw.Length > 0)
                {
                    string expanded = Environment.ExpandEnvironmentVariables(raw).Replace("/", "\\");
                    if (File.Exists(expanded)) icon = expanded;
                }
            }

            string arguments;
            Span<char> args = stackalloc char[512];
            fixed (char* text = args)
            {
                link.GetArguments(new PWSTR(text), args.Length);
                arguments = new PWSTR(text).ToString();
            }

            return (target, icon, arguments);
        }
        catch
        {
            return null;
        }
    }

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
/// <summary>
/// Algo que el usuario ha soltado en el dock.
///
/// <paramref name="IconSource"/> y <paramref name="Arguments"/> existen por los accesos
/// directos: el de VALORANT apunta a <c>RiotClientServices.exe</c> con
/// <c>--launch-product=valorant</c> y su propio .ico. Resolviéndolo solo al destino se
/// perdían las dos cosas, así que el dock enseñaba la cara del cliente de Riot y al
/// clicarlo habría abierto el cliente, no el juego.
/// </summary>
internal readonly record struct DroppedItem(
    string Target, string Name, string? FilePath, string? IconSource = null, string Arguments = "");

/// <summary>Lee un acceso directo de internet. Lo usan la suelta y <see cref="Steam"/>.</summary>
internal static class InternetShortcut
{
    /// <summary>
    /// A dónde apunta un acceso directo de internet (<c>.url</c>), y de dónde saca su
    /// icono.
    ///
    /// Es lo que Steam deja en el menú Inicio por cada juego instalado, y no es un
    /// <c>.lnk</c>: es un fichero de texto plano estilo INI, con su
    /// <c>URL=steam://rungameid/19680</c> y su <c>IconFile=…\xxxx.ico</c>. Guardarlo por
    /// la ruta del <c>.url</c> —que es lo que se hacía— ataba la entrada del dock a un
    /// fichero que Steam reescribe al actualizar la biblioteca, y encima dejaba el icono
    /// en manos del manejador del shell.
    ///
    /// Se lee a mano y no con <c>IUniformResourceLocator</c> porque es un INI de cuatro
    /// líneas y la interfaz COM pide además <c>IPropertySetStorage</c> para sacar el
    /// icono: más P/Invokes para leer lo mismo.
    /// </summary>
    public static (string Url, string? Icon)? Read(string path)
    {
        try
        {
            string? url = null;
            string? icon = null;
            int index = 0;

            foreach (string line in File.ReadLines(path))
            {
                int equals = line.IndexOf('=');
                if (equals <= 0) continue;

                string key = line[..equals].Trim();
                string value = line[(equals + 1)..].Trim();

                if (key.Equals("URL", StringComparison.OrdinalIgnoreCase)) url ??= value;
                else if (key.Equals("IconFile", StringComparison.OrdinalIgnoreCase)) icon ??= value;
                else if (key.Equals("IconIndex", StringComparison.OrdinalIgnoreCase)) int.TryParse(value, out index);
            }

            if (string.IsNullOrEmpty(url)) return null;

            // Mismo corte que con los .lnk: el icono solo vale si es un fichero entero.
            // "shell32.dll,3" apunta a un índice dentro de un recurso y el extractor del
            // dock no sabe de índices.
            if (index != 0 || string.IsNullOrEmpty(icon)) return (url, null);

            string expanded = Environment.ExpandEnvironmentVariables(icon).Replace("/", "\\");
            return (url, File.Exists(expanded) ? expanded : null);
        }
        catch (Exception ex)
        {
            // Un .url ilegible no es un error: se cae al camino de siempre y la entrada
            // se guarda por la ruta del fichero.
            Console.WriteLine($"[drop] no se pudo leer '{Path.GetFileName(path)}': {ex.Message}");
            return null;
        }
    }
}
