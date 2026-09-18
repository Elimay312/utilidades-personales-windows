using System.Drawing;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.System.Com;
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
        Console.WriteLine($"[drop] DragEnter ({pt.x},{pt.y}) efecto={*pdwEffect}");

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
        int slot = _dock.SlotAtScreen(pt.x, pt.y);
        *pdwEffect = DROPEFFECT.DROPEFFECT_NONE;

        Point p = new(pt.x, pt.y);
        try { _helper?.Drop(pDataObj, &p, DROPEFFECT.DROPEFFECT_COPY); }
        catch { /* solo era la miniatura */ }

        // AQUÍ y no después: la documentación dice que los data objects pasados a
        // IDropTarget dejan de ser válidos en cuanto termina la suelta. Lo que no se
        // saque antes de retornar, ya no se puede sacar.
        _dock.QueueDrop(slot, PathsOf(pDataObj));
        _dock.OnDragOutside();
    }

    private DROPEFFECT Effect(POINTL pt)
        => _dock.AcceptsDropAt(pt.x, pt.y) ? DROPEFFECT.DROPEFFECT_COPY : DROPEFFECT.DROPEFFECT_NONE;

    /// <summary>
    /// Las rutas de lo soltado.
    ///
    /// Se usa <c>SHCreateShellItemArrayFromDataObject</c> en vez de leer los formatos a
    /// mano porque entiende de una vez tanto <c>CF_HDROP</c> (rutas) como
    /// <c>CFSTR_SHELLIDLIST</c> (PIDLs), que es lo que llevan los objetos sin ruta de
    /// disco. Es lo que Microsoft recomienda explícitamente sobre leer los formatos.
    /// </summary>
    private static string[] PathsOf(IDataObject data)
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

        List<string> paths = [];
        for (uint i = 0; i < count; i++)
        {
            items.GetItemAt(i, out IShellItem item);

            // Un objeto virtual (una app de la Store, por ejemplo) no tiene ruta de
            // disco y esto falla. Aquí da igual: para abrir un fichero con una app hace
            // falta un fichero, así que se descarta.
            try
            {
                item.GetDisplayName(SIGDN.SIGDN_FILESYSPATH, out PWSTR name);
                paths.Add(name.ToString());
                System.Runtime.InteropServices.Marshal.FreeCoTaskMem((nint)name.Value);
            }
            catch
            {
                // sin ruta de disco: no sirve para esto
            }
        }

        return [.. paths];
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
