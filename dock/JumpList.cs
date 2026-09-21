using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.UI.Shell;
using Windows.Win32.UI.Shell.Common;
using Windows.Win32.UI.Shell.PropertiesSystem;
using Windows.Win32.System.Com.StructuredStorage;
using System.Collections.Concurrent;

namespace Dock;

/// <summary>Un documento reciente de una app.</summary>
internal sealed record JumpItem(string Name, string Target);

/// <summary>
/// Los documentos recientes de una app, los mismos que salen en su lista de saltos al
/// clicar con el derecho en su botón de la barra de tareas.
///
/// <para>
/// <b>Es API pública y la implementa el sistema</b>: <c>IApplicationDocumentLists</c>.
/// Su <c>SetAppID</c> recibe <i>"the AppUserModelID of the process whose taskbar button
/// representation receives the Jump List"</i>, y nada en la documentación lo limita al
/// del propio proceso. No se parsea a mano ningún fichero de
/// <c>AutomaticDestinations</c>: eso es formato no documentado del perfil del usuario y
/// la enmienda 3 lo prohíbe expresamente.
/// </para>
///
/// <para>
/// <b>Lo que NO devuelve</b>, y es textual de la doc: <i>"It cannot retrieve a list of
/// items that the user has pinned... also cannot access custom categories or the task
/// list."</i> O sea, salen los recientes y los frecuentes; no salen los anclados ni las
/// tareas del estilo "Nueva ventana de incógnito". Es media lista de saltos.
/// </para>
///
/// <para>
/// Esto <b>lee datos del usuario</b>, o sea la regla 10, así que lleva los tres
/// cortafuegos de la enmienda 3 y se sostienen en el código, no en la buena voluntad:
/// solo se pregunta por apps que están en <c>dock.json</c>, solo desde el menú del clic
/// derecho, y no hay ni un camino que llegue aquí desde un temporizador o un hilo de
/// fondo.
/// </para>
/// </summary>
internal static unsafe class JumpList
{
    /// <summary>Cuántos recientes se piden como mucho.</summary>
    public const uint Limit = 8;

    /// <summary>
    /// Los recientes de esa app. La lista vacía es la respuesta normal para una app que
    /// no registra documentos, así que no se avisa de nada.
    /// </summary>
    /// <param name="appId">
    /// El AppUserModelID, o la ruta del ejecutable para las apps que no declaran uno.
    /// </param>
    public static List<JumpItem> Read(string appId, APPDOCLISTTYPE type = APPDOCLISTTYPE.ADLT_RECENT)
    {
        List<JumpItem> items = [];
        if (string.IsNullOrWhiteSpace(appId)) return items;

        try
        {
            var lists = (IApplicationDocumentLists)new ApplicationDocumentLists();

            fixed (char* id = appId) lists.SetAppID(new PCWSTR(id));

            Guid arrayId = typeof(IObjectArray).GUID;
            lists.GetList(type, Limit, &arrayId, out object result);

            var array = (IObjectArray)result;
            array.GetCount(out uint count);

            Guid itemId = typeof(IShellItem).GUID;
            for (uint i = 0; i < count && items.Count < Limit; i++)
            {
                array.GetAt(i, &itemId, out object entry);
                if (entry is not IShellItem item) continue;

                string? path = Display(item, SIGDN.SIGDN_FILESYSPATH);
                string name = Display(item, SIGDN.SIGDN_NORMALDISPLAY) ?? "";
                if (path is not null && name.Length > 0) items.Add(new JumpItem(name, path));
            }
        }
        catch (Exception ex)
        {
            // Una app sin lista, o un AppID que el shell no reconoce, tira excepción.
            // No es un error: simplemente no hay recientes que enseñar.
            Console.WriteLine($"[saltos] '{appId}': {ex.Message}");
        }

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

    /// <summary>
    /// PKEY_AppUserModel_ID. No está en los metadatos de proyección, así que va a mano
    /// con los valores de <c>propkey.h</c>.
    /// </summary>
    private static readonly PROPERTYKEY AppUserModelId = new()
    {
        fmtid = new Guid("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3"),
        pid = 5,
    };

    /// <summary>
    /// El AppUserModelID de cada entrada, recordado de cuando se pudo resolver. Hace
    /// falta recordarlo porque solo se puede leer de una ventana viva, y los recientes
    /// se quieren ver también con la app cerrada.
    /// </summary>
    private static readonly ConcurrentDictionary<string, string> Known = new();

    /// <summary>
    /// Con qué identificador preguntar por una entrada del dock.
    ///
    /// Las apps empaquetadas traen su AppUserModelID dentro del target. Las de siempre
    /// no: <b>la ruta del ejecutable NO vale como AppID</b>, y está medido — con la ruta
    /// de notepad la lista sale vacía, mientras que con <c>Brave</c> o
    /// <c>Microsoft.Windows.Explorer</c> salen siete y ocho documentos. El identificador
    /// de verdad lo declara la propia ventana de la app, así que se lee de ahí la
    /// primera vez que está abierta y se recuerda.
    /// </summary>
    public static string? AppIdOf(DockApp app, HWND window = default)
    {
        if (!app.IsApp) return null;

        if (app.IsShellItem)
        {
            int slash = app.Target.LastIndexOf('\\');
            return slash > 0 ? app.Target[(slash + 1)..] : null;
        }

        if (Known.TryGetValue(app.Target, out string? cached)) return cached;

        if (OfWindow(window) is string id)
        {
            Known[app.Target] = id;
            return id;
        }

        // El Explorador no declara AppUserModelID en sus ventanas —medido: el
        // almacén de propiedades no trae la clave— pero el sistema sí publica el suyo,
        // y es el que da la lista de carpetas frecuentes, que es la lista de saltos más
        // útil que hay en Windows. Es el único caso a mano.
        if (app.Target.EndsWith("\\explorer.exe", StringComparison.OrdinalIgnoreCase))
        {
            return "Microsoft.Windows.Explorer";
        }

        return null;
    }

    /// <summary>
    /// El AppUserModelID que declara una ventana, o null. Es una propiedad pública del
    /// shell y se lee con la API que existe para preguntarla; no se toca nada de la
    /// ventana.
    /// </summary>
    private static string? OfWindow(HWND window)
    {
        if (window.IsNull) return null;

        try
        {
            Guid iid = typeof(IPropertyStore).GUID;
            if (PInvoke.SHGetPropertyStoreForWindow(window, &iid, out object store).Failed) return null;

            PROPERTYKEY key = AppUserModelId;
            ((IPropertyStore)store).GetValue(&key, out PROPVARIANT value);

            // PropVariantToStringAlloc reserva con CoTaskMemAlloc y hay que liberarlo.
            // El PROPVARIANT en si no se limpia a mano: es una cadena, y CsWin32 lo
            // devuelve por valor sin dueno que liberar.
            if (PInvoke.PropVariantToStringAlloc(value, out PWSTR text).Failed) return null;

            string id = text.ToString();
            System.Runtime.InteropServices.Marshal.FreeCoTaskMem((nint)text.Value);
            return id.Length > 0 ? id : null;
        }
        catch
        {
            return null;
        }
    }
}
