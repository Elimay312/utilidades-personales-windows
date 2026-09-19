using System.Runtime.InteropServices;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.System.Com;

// System.IServiceProvider tambien existe y el nombre choca: aqui siempre es el de COM.
using IServiceProvider = Windows.Win32.System.Com.IServiceProvider;
using Windows.Win32.UI.Shell;
using Windows.Win32.UI.WindowsAndMessaging;

namespace QuickLook;

/// <summary>
/// Que archivo tiene seleccionado la ventana del Explorador que esta delante.
/// Ver SEGURIDAD.md §3.2.
///
/// <para>
/// <b>Se habla con el Explorador desde fuera.</b> <c>IShellWindows</c> es la via publica y
/// documentada de automatizar el Explorador: COM fuera de proceso, sin inyectar nada, sin
/// leer memoria ajena y sin tocar el portapapeles. La regla 4 sigue intacta.
/// </para>
///
/// <para>
/// <b>Y se pregunta lo minimo.</b> <c>SVGIO_SELECTION</c>: no se enumera el contenido de la
/// carpeta ni carpetas que el usuario no tenga abiertas. De lo que devuelve el shell se
/// saca la ruta y se ignora todo lo demas. No hay ningun camino de codigo que llegue aqui
/// que no venga del espacio pulsado.
/// </para>
///
/// <para>
/// <b>Por que no IWebBrowser2</b>, que es lo que sale en todos los ejemplos de C++ para
/// sacar el HWND de cada ventana: vive en la type library de <c>shdocvw</c>, fuera de lo
/// que CsWin32 genera. No hace falta — el <c>IDispatch</c> del Explorador tambien
/// implementa <c>IServiceProvider</c>, y <c>IShellBrowser</c> hereda de <c>IOleWindow</c>,
/// asi que su <c>GetWindow</c> da el mismo HWND con el que emparejar.
/// </para>
/// </summary>
internal static unsafe class Selection
{
    /// <summary>CLSID_ShellWindows. No viene proyectado como constante.</summary>
    private static readonly Guid CLSID_ShellWindows = new("9BA05972-F6A8-11CF-A442-00A0C90A8F39");

    /// <summary>SID_STopLevelBrowser: el navegador de nivel superior de una ventana del shell.</summary>
    private static readonly Guid SID_STopLevelBrowser = new("4C96BE40-915C-11CF-99D3-00AA004AE837");

    /// <summary>QL_LOG=1 para ver por donde se corta el baile de COM.</summary>
    private static readonly bool Trace = Environment.GetEnvironmentVariable("QL_LOG") == "1";

    /// <summary>
    /// La ruta del elemento seleccionado en <paramref name="front"/>, o null si no hay
    /// nada seleccionado, si no es una ventana del shell, o si el Explorador no contesta.
    ///
    /// Devolver null nunca puede tirar el programa: el hook seguiria comiendose la barra
    /// espaciadora del usuario.
    /// </summary>
    public static string? Path(HWND front)
    {
        try
        {
            IShellView? view = View(front);
            if (view is null) return null;

            // IFolderView2 y no IFolderView: la vista moderna del Explorador implementa
            // las dos, pero pedir la vieja falla en algunas carpetas virtuales.
            var folder = (IFolderView2)view;

            // GetSelection y no Items(SVGIO_SELECTION, IID_IShellItemArray). MEDIDO: con
            // un archivo REALMENTE seleccionado —comprobado por separado preguntandole a
            // Shell.Application, que decia 1 elemento— Items contestaba 0x80070490
            // (ERROR_NOT_FOUND) una y otra vez. GetSelection es la llamada que existe
            // para esto en IFolderView2, y contesta a la primera.
            //
            // fNoneImpliesFolder = false: sin seleccion se quiere "nada", no la carpeta
            // entera. Ensenar una carpeta porque el usuario no habia marcado ningun
            // archivo seria peor que no abrir.
            folder.GetSelection(false, out IShellItemArray? items);
            if (items is null) return null;

            items.GetCount(out uint count);
            if (count == 0) return null;

            // Uno cada vez. Con varios seleccionados se mira el primero; la lista entera
            // es cosa del M8, no de aqui.
            items.GetItemAt(0, out IShellItem item);

            PWSTR name;
            item.GetDisplayName(SIGDN.SIGDN_FILESYSPATH, &name);
            try
            {
                return name.Value is null ? null : name.ToString();
            }
            finally
            {
                Marshal.FreeCoTaskMem((nint)name.Value);
            }
        }
        catch (Exception ex)
        {
            // Lo normal aqui es que la seleccion no tenga ruta de disco: "Este equipo",
            // una carpeta virtual, un resultado de busqueda en OneDrive. No es un error.
            Console.WriteLine($"[seleccion] sin ruta: {ex.Message}");
            return null;
        }
    }

    /// <summary>
    /// La vista activa de la ventana del shell que esta delante, o null si la de delante
    /// no es ninguna de ellas.
    /// </summary>
    private static IShellView? View(HWND front)
    {
        // CLSCTX_ALL y no solo LOCAL_SERVER: el Explorador puede servir la coleccion
        // desde mas de un contexto segun como este arrancado el shell.
        HRESULT created = PInvoke.CoCreateInstance(CLSID_ShellWindows, null, CLSCTX.CLSCTX_ALL,
            out IShellWindows windows);
        if (Trace) Console.WriteLine($"[seleccion] CoCreateInstance = 0x{(uint)created.Value:X8}");
        if (created.Failed) return null;

        // El Escritorio no sale al iterar: tiene su propia consulta.
        if (Foreground.IsDesktop(front))
        {
            object empty = null!;
            object? shell = windows.FindWindowSW(
                empty, empty, ShellWindowTypeConstants.SWC_DESKTOP, out _,
                ShellWindowFindWindowOptions.SWFO_NEEDDISPATCH);

            return shell is null ? null : ViewOf(shell, default);
        }

        int total = windows.Count;
        if (Trace) Console.WriteLine($"[seleccion] {total} ventana(s) de shell, buscando 0x{(nint)front.Value:X}");

        for (int i = 0; i < total; i++)
        {
            object? shell = windows.Item(i);
            if (shell is null) continue;

            IShellView? view = ViewOf(shell, front);
            if (view is not null) return view;
        }

        return null;
    }

    /// <summary>
    /// De una ventana del shell a su vista activa. Si <paramref name="match"/> no es nulo,
    /// solo contesta cuando esa ventana es justo esa: nunca se habla con ventanas del
    /// Explorador que no sean la que el usuario esta mirando.
    /// </summary>
    private static IShellView? ViewOf(object shell, HWND match)
    {
        try
        {
            var provider = (IServiceProvider)shell;
            provider.QueryService(SID_STopLevelBrowser, out IShellBrowser browser);

            if (!match.IsNull)
            {
                HWND owned;
                browser.GetWindow(&owned);

                // MEDIDO: con el Explorador de Windows 11 esto NO devuelve el marco.
                // Con una ventana de carpeta en primer plano (0x812B6) la coleccion daba
                // 0x390EA2 y 0x51402, y ninguna coincidia: son las ventanas de las
                // PESTAÑAS, que cuelgan del marco. Por eso se compara el ancestro raiz.
                HWND root = PInvoke.GetAncestor(owned, GET_ANCESTOR_FLAGS.GA_ROOT);
                bool visible = PInvoke.IsWindowVisible(owned);
                if (Trace) Console.WriteLine($"[seleccion]   pestaña 0x{(nint)owned.Value:X} raiz 0x{(nint)root.Value:X} visible={visible}");

                // Y visible, porque un marco con varias pestañas tiene varias ventanas de
                // shell y solo la de delante esta visible.
                if (root != match || !visible) return null;
            }

            browser.QueryActiveShellView(out IShellView view);
            return view;
        }
        catch (Exception ex)
        {
            // Una ventana de Internet Explorer heredado, o una que se esta cerrando: no
            // es la que buscamos y no es un error.
            if (Trace) Console.WriteLine($"[seleccion]   descartada: {ex.Message}");
            return null;
        }
    }

}
