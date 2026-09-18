using Windows.Win32.Foundation;
using Windows.Win32.System.Com;
using Windows.Win32.System.Ole;
using Windows.Win32.System.SystemServices;

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
/// aquí dentro no se hace nada lento.
/// </para>
/// </summary>
internal sealed unsafe class DockDropTarget(DockWindow dock) : IDropTarget
{
    private readonly DockWindow _dock = dock;

    public void DragEnter(IDataObject pDataObj, MODIFIERKEYS_FLAGS grfKeyState, POINTL pt, DROPEFFECT* pdwEffect)
    {
        Console.WriteLine($"[drop] DragEnter pantalla=({pt.x},{pt.y})");
        *pdwEffect = Effect(pt);
    }

    public void DragOver(MODIFIERKEYS_FLAGS grfKeyState, POINTL pt, DROPEFFECT* pdwEffect)
    {
        *pdwEffect = Effect(pt);
    }

    public void DragLeave()
    {
        Console.WriteLine("[drop] DragLeave");
        _dock.OnDragOutside();
    }

    public void Drop(IDataObject pDataObj, MODIFIERKEYS_FLAGS grfKeyState, POINTL pt, DROPEFFECT* pdwEffect)
    {
        Console.WriteLine($"[drop] Drop pantalla=({pt.x},{pt.y}) icono={_dock.SlotAtScreen(pt.x, pt.y)}");
        *pdwEffect = DROPEFFECT.DROPEFFECT_NONE;
        _dock.OnDragOutside();
    }

    private DROPEFFECT Effect(POINTL pt)
    {
        int slot = _dock.SlotAtScreen(pt.x, pt.y);
        return slot >= 0 ? DROPEFFECT.DROPEFFECT_COPY : DROPEFFECT.DROPEFFECT_NONE;
    }
}
