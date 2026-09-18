using System.Diagnostics;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.UI.Shell;
using Windows.Win32.UI.WindowsAndMessaging;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Dock;

/// <summary>Una entrada del dock.</summary>
internal sealed class DockApp
{
    /// <summary>Nombre visible. De momento solo sirve para los mensajes de error.</summary>
    public string Name { get; init; } = "";

    /// <summary>
    /// Ruta a un ejecutable, o un nombre del espacio de nombres del shell como
    /// <c>shell:AppsFolder\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App</c>.
    ///
    /// Una sola cadena para los dos casos porque SHCreateItemFromParsingName
    /// entiende ambos: así la extracción de iconos tiene una única ruta de código
    /// para apps Win32 y para apps MSIX.
    /// </summary>
    public string Target { get; init; } = "";

    /// <summary>
    /// Una raya vertical para separar grupos, en vez de una app. Ocupa mucho menos
    /// espacio que un icono: la curva admite ranuras de ancho variable.
    /// </summary>
    public bool Separator { get; init; }

    [JsonIgnore]
    public bool IsShellItem => Target.StartsWith("shell:", StringComparison.OrdinalIgnoreCase);

    /// <summary>
    /// Si esto es una app, o un documento o carpeta que solo se abre.
    ///
    /// Importa porque un documento no puede estar "abierto": no tiene proceso ni
    /// ventana propios. Sin esta distinción, un <c>notas.txt</c> en el dock cruzaría
    /// con cualquier proceso llamado <c>notas</c> y se encendería su puntito.
    ///
    /// ponytail: la regla es "shell item o .exe"; un .bat o un .com quedarían como
    /// documentos. Si algún día molesta, la lista de extensiones se amplía aquí.
    /// </summary>
    [JsonIgnore]
    public bool IsApp => Target != DockConfig.TrashTarget
        && (IsShellItem || Target.EndsWith(".exe", StringComparison.OrdinalIgnoreCase));

    /// <summary>
    /// Si esto es una carpeta, y por tanto se puede desplegar en rejilla en vez de
    /// abrirse en el Explorador. La papelera cuenta: también tiene contenido.
    /// </summary>
    [JsonIgnore]
    public bool IsFolder => Target == DockConfig.TrashTarget || Directory.Exists(Target);

    /// <summary>
    /// Con qué se identifica esta entrada en <c>dock.local.json</c>. Los separadores
    /// no tienen target, así que se distinguen por su orden de aparición.
    /// </summary>
    public string Key(int separatorOrdinal) => Separator ? $"|separador|{separatorOrdinal}" : Target;

    /// <summary>
    /// Lanza la app delegando en el shell. Sin P/Invoke y sin CreateProcess con
    /// flags raros: es API de .NET pura (ver Fase 2).
    /// </summary>
    public void Launch()
    {
        // Una sola ruta para todo. UseShellExecute acaba en ShellExecuteEx, que
        // entiende tanto una ruta de archivo como un moniker "shell:", así que las
        // apps MSIX no necesitan nada especial.
        //
        // Antes se lanzaban con explorer.exe como proceso intermedio, y eso dejaba
        // un explorer.exe suelto apareciendo en Alt+Tab.
        Process.Start(new ProcessStartInfo(Target) { UseShellExecute = true });
    }

    /// <summary>
    /// Abre esos ficheros con esta app. Es lo que pasa al soltar algo sobre un icono.
    ///
    /// Aquí sí hacen falta dos caminos, y es el único sitio del dock donde los hay: a
    /// una app empaquetada no se le puede pasar un fichero por línea de comandos.
    /// Lanzarla a secas sí funciona con el moniker <c>shell:</c> (ver <see cref="Launch"/>),
    /// pero con fichero hay que usar la activación oficial.
    /// </summary>
    public void OpenWith(IReadOnlyList<string> paths)
    {
        // Las rutas vienen de una suelta del usuario, no de entrada libre, pero se
        // normalizan igual y se rechaza cualquiera con comillas: van dentro de un
        // lpParameters entrecomillado y una comilla suelta lo partiría en dos.
        List<string> safe = [];
        foreach (string path in paths)
        {
            if (path.Contains('"')) continue;

            try { safe.Add(Path.GetFullPath(path)); }
            catch { /* ruta imposible: se descarta */ }
        }

        if (safe.Count == 0) return;

        if (IsShellItem) ActivateForFile(safe);
        else ShellExecute(safe);
    }

    /// <summary>App normal: el fichero va como argumento, entrecomillado.</summary>
    private unsafe void ShellExecute(IReadOnlyList<string> paths)
    {
        string arguments = string.Join(' ', paths.Select(p => $"\"{p}\""));

        fixed (char* file = Target)
        fixed (char* args = arguments)
        {
            SHELLEXECUTEINFOW info = new()
            {
                cbSize = (uint)sizeof(SHELLEXECUTEINFOW),

                // NOASYNC porque no vamos a morirnos justo después, y NO_UI para que un
                // fallo no plante un diálogo modal delante del usuario: se loguea y ya.
                fMask = SeeMaskNoAsync | SeeMaskNoUi,
                lpFile = new PCWSTR(file),
                lpParameters = new PCWSTR(args),
                nShow = (int)SHOW_WINDOW_CMD.SW_SHOWNORMAL,
            };

            // Sin verbo: el de defecto. Nunca "runas" — ver la enmienda 2 de SEGURIDAD.md.
            if (!PInvoke.ShellExecuteEx(ref info))
                Console.WriteLine($"[dock] '{Name}' no pudo abrir {arguments}");
        }
    }

    /// <summary>
    /// App empaquetada: activación oficial, que es lo que acaba en el evento
    /// <c>FileActivated</c> de la app. No requiere elevación y funciona desde una app
    /// no empaquetada como esta.
    /// </summary>
    private unsafe void ActivateForFile(IReadOnlyList<string> paths)
    {
        int slash = Target.LastIndexOf('\\');
        if (slash < 0) return;

        string aumid = Target[(slash + 1)..];

        // ponytail: solo el primero. Montar un IShellItemArray de varios pide
        // SHCreateShellItemArrayFromIDLists y andar con PIDLs; si algún día hace falta
        // soltar varios sobre una app de la Store, es aquí.
        Guid itemId = typeof(IShellItem).GUID;
        object item;
        fixed (char* path = paths[0])
        {
            if (PInvoke.SHCreateItemFromParsingName(new PCWSTR(path), null, &itemId, out item).Failed) return;
        }

        Guid arrayId = typeof(IShellItemArray).GUID;
        if (PInvoke.SHCreateShellItemArrayFromShellItem((IShellItem)item, &arrayId, out object array).Failed) return;

        // Objeto in-proc a propósito: la nota de la propia interfaz dice que
        // CLSCTX_LOCAL_SERVER es para procesos que nacen solo para lanzar algo. Un dock
        // vive todo lo que dure la sesión.
        var manager = (IApplicationActivationManager)new ApplicationActivationManager();

        fixed (char* id = aumid)
        fixed (char* verb = "open")
        {
            try
            {
                manager.ActivateForFile(new PCWSTR(id), (IShellItemArray)array, new PCWSTR(verb), out _);
                return;
            }
            catch (Exception ex)
            {
                // 0x80270254: "no es compatible con el contrato especificado". No todas
                // las apps de AppsFolder son UWP. Paint, por ejemplo, es un Win32
                // EMPAQUETADO: no declara el contrato de activación por fichero, y coge
                // la ruta por línea de comandos como cualquier programa de siempre.
                // Para esas vale ActivateApplication, que es el otro método de esta
                // misma interfaz y sí lleva argumentos.
                Console.WriteLine($"[dock] '{Name}' no acepta ficheros por contrato ({ex.Message.Trim()}), se prueba por argumentos");
            }
        }

        fixed (char* id = aumid)
        fixed (char* args = $"\"{paths[0]}\"")
        {
            try
            {
                manager.ActivateApplication(new PCWSTR(id), new PCWSTR(args), ACTIVATEOPTIONS.AO_NONE, out _);
            }
            catch (Exception ex)
            {
                // Que la app diga que no sabe abrir eso es un no aceptable, no un fallo
                // del dock: se anota y se sigue.
                Console.WriteLine($"[dock] '{Name}' no pudo abrir {paths[0]}: {ex.Message.Trim()}");
            }
        }
    }

    private const uint SeeMaskNoAsync = 0x00000100;
    private const uint SeeMaskNoUi = 0x00000400;
}

/// <summary>
/// Contenido de dock.json.
///
/// Es un <c>record</c> y no una clase por una razón concreta: reordenar, quitar y añadir
/// reconstruían el objeto campo a campo, y los tres sitios se olvidaron de
/// <c>Magnification</c>. El usuario apagaba la lupa, arrastraba un icono y volvía sola.
/// Con <c>config with { Apps = ... }</c> el campo que se añada mañana se copia gratis.
/// </summary>
internal sealed record DockConfig
{
    /// <summary>Tamaño del icono en reposo, en unidades lógicas (96 DPI).</summary>
    public int IconSize { get; init; } = 48;

    /// <summary>Separación entre iconos, en unidades lógicas.</summary>
    public int IconSpacing { get; init; } = 16;

    /// <summary>
    /// Si el dock se esconde solo, dejando asomar una franja en el borde inferior.
    /// </summary>
    public bool AutoHide { get; init; } = true;

    /// <summary>
    /// Si el dock se lanza al iniciar sesión. Escribe en
    /// HKCU\Software\Microsoft\Windows\CurrentVersion\Run, visible en el
    /// Administrador de tareas. Ponlo a false y la entrada se borra sola.
    /// </summary>
    public bool AutoStart { get; init; }

    /// <summary>
    /// Cuánto crece el icono justo bajo el cursor.
    ///
    /// <b>1.0 la apaga del todo</b>, que es como viene macOS de fábrica: allí la
    /// magnificación es una casilla que hay que ir a marcar. A 2.0 el bulto empuja a
    /// los vecinos tanto que cuesta acertarle a un icono; 1.3 se nota sin estorbar.
    /// </summary>
    public float Magnification { get; init; } = 1.3f;

    /// <summary>
    /// Si la papelera va al final del dock, como en macOS. Se puede quitar
    /// arrastrándola fuera igual que cualquier otro icono.
    /// </summary>
    public bool Trash { get; init; } = true;

    public List<DockApp> Apps { get; init; } = [];

    /// <summary>
    /// Lo que decía <c>dock.json</c> antes de aplicar <c>dock.local.json</c>. Hace falta
    /// para poder escribir la superposición: sin la lista original no hay forma de saber
    /// si una entrada es un añadido del usuario o una que ya venía.
    /// </summary>
    [JsonIgnore]
    public List<DockApp> BaseApps { get; init; } = [];

    internal static readonly JsonSerializerOptions Options = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
    };

    /// <summary>
    /// Dónde vive la configuración: <c>%LOCALAPPDATA%\Dock</c>, y <b>no</b> junto al
    /// ejecutable.
    ///
    /// Estaba junto al ejecutable, que durante el desarrollo es la carpeta de
    /// compilación. Un <c>dotnet clean</c>, borrar <c>bin\</c> o mover el repo se
    /// llevaba por delante el dock.json Y el dock.local.json con todo lo que el usuario
    /// hubiera reordenado y añadido, sin avisar.
    /// </summary>
    public static string Folder => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Dock");

    public static string DefaultPath => Path.Combine(Folder, "dock.json");

    /// <summary>
    /// Crea la carpeta y, la primera vez, copia el dock.json que viene junto al
    /// ejecutable como semilla. A partir de ahí el de al lado ya no se mira.
    /// </summary>
    public static void EnsureSeeded()
    {
        try
        {
            Directory.CreateDirectory(Folder);
            if (File.Exists(DefaultPath)) return;

            string seed = Path.Combine(AppContext.BaseDirectory, "dock.json");
            if (File.Exists(seed))
            {
                File.Copy(seed, DefaultPath);
                Console.WriteLine($"[config] primera vez: copiado a {DefaultPath}");
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[config] no se pudo preparar {Folder}: {ex.Message}");
        }
    }

    /// <summary>
    /// Nombre de parsing de la papelera. Es un objeto virtual del shell, sin ruta de
    /// disco, así que va por el mismo camino que las apps de la Store: de aquí salen
    /// tanto su icono como su apertura.
    /// </summary>
    public const string TrashTarget = "shell:RecycleBinFolder";

    public static DockConfig Load(string path)
    {
        DockConfig config = JsonSerializer.Deserialize<DockConfig>(File.ReadAllText(path), Options)
            ?? throw new InvalidDataException($"{path} está vacío");

        List<DockApp> baseApps = Validate(config.Apps);

        // La papelera se añade aquí y no en dock.json para que esté por defecto sin
        // que el usuario tenga que escribirla. Entra en la lista BASE, así que se
        // reordena y se quita arrastrando como cualquier otra: la superposición local
        // ya sabe recordar que se quitó.
        if (config.Trash)
        {
            baseApps.Add(new DockApp { Name = "Papelera", Target = TrashTarget, Separator = false });
        }

        return new DockConfig
        {
            IconSize = config.IconSize,
            IconSpacing = config.IconSpacing,
            AutoHide = config.AutoHide,
            AutoStart = config.AutoStart,
            Magnification = Math.Clamp(config.Magnification, 1f, 2.5f),
            Trash = config.Trash,
            BaseApps = baseApps,
            Apps = DockLocal.Load().ApplyTo(baseApps),
        };
    }

    /// <summary>
    /// Deja pasar solo las entradas utilizables, normalizando las rutas. Una entrada
    /// mala no debe tumbar el dock: se avisa y se omite.
    /// </summary>
    public static List<DockApp> Validate(IEnumerable<DockApp> apps)
    {
        List<DockApp> valid = [];
        foreach (DockApp app in apps)
        {
            if (app.Separator)
            {
                valid.Add(app);
                continue;
            }

            if (string.IsNullOrWhiteSpace(app.Target))
            {
                Console.WriteLine($"[config] omitida '{app.Name}': sin target");
                continue;
            }

            // SHCreateItemFromParsingName NO acepta barras normales: son nombres de
            // parsing del shell, no rutas de archivo. File.Exists sí las acepta, así
            // que sin normalizar aquí la entrada pasa la validación y revienta
            // después con E_INVALIDARG.
            //
            // Se normalizan todas, también las de shell:AppsFolder, para que en el
            // JSON nunca haga falta escapar una barra invertida. Los AppUserModelID
            // no contienen barras normales, así que no hay nada que romper.
            string target = app.Target.Replace('/', '\\');

            // Directory.Exists además de File.Exists: en el dock caben carpetas, y
            // Process.Start con UseShellExecute las abre igual de bien que un .exe.
            if (!app.IsShellItem && !File.Exists(target) && !Directory.Exists(target))
            {
                Console.WriteLine($"[config] omitida '{app.Name}': no existe {target}");
                continue;
            }

            valid.Add(new DockApp { Name = app.Name, Target = target });
        }

        return valid;
    }
}

/// <summary>
/// Lo que el dock cambia por su cuenta: el orden, lo añadido y lo quitado.
///
/// Vive aparte de <c>dock.json</c> a propósito. Ese fichero es del usuario, con sus
/// comentarios, y reescribirlo se los llevaría por delante. Aquí solo hay lo que el
/// dock decide, y borrarlo devuelve la configuración escrita a mano.
///
/// <para>
/// No dispara la recarga en caliente: el FileSystemWatcher filtra por <c>dock.json</c>
/// exacto (ver <c>Program.WatchConfig</c>), así que escribir el de al lado no provoca un
/// Build. Si algún día ese filtro se abriera, esto entraría en bucle.
/// </para>
/// </summary>
internal sealed class DockLocal
{
    /// <summary>Claves en el orden en que se quieren ver.</summary>
    public List<string> Orden { get; init; } = [];

    /// <summary>Entradas que no están en dock.json, añadidas arrastrando.</summary>
    public List<DockApp> Anadidas { get; init; } = [];

    /// <summary>Claves de dock.json que el usuario sacó del dock.</summary>
    public List<string> Quitadas { get; init; } = [];

    public static string DefaultPath => Path.Combine(DockConfig.Folder, "dock.local.json");

    private static readonly JsonSerializerOptions Write = new()
    {
        WriteIndented = true,

        // Sin esto, System.Text.Json escapa las barras invertidas de las rutas como
        // \ y el fichero deja de ser legible a ojo, que es media gracia de que
        // la config viva en texto plano (Fase 2, corolarios).
        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    public static DockLocal Load()
    {
        if (!File.Exists(DefaultPath)) return new DockLocal();

        try
        {
            return JsonSerializer.Deserialize<DockLocal>(File.ReadAllText(DefaultPath), DockConfig.Options)
                ?? new DockLocal();
        }
        catch (Exception ex)
        {
            // Que este fichero esté roto no puede dejar sin dock: se ignora y se sigue
            // con lo que diga dock.json.
            Console.WriteLine($"[config] {Path.GetFileName(DefaultPath)} ilegible, se ignora: {ex.Message}");
            return new DockLocal();
        }
    }

    /// <summary>
    /// Escribe la superposición deduciéndola de la diferencia entre lo que decía
    /// dock.json y lo que hay ahora. Se deduce en vez de llevarla a mano para que no
    /// haya dos estados que mantener en sincronía.
    /// </summary>
    public static void Save(IReadOnlyList<DockApp> baseApps, IReadOnlyList<DockApp> current)
    {
        List<string> before = KeysOf(baseApps);
        List<string> after = KeysOf(current);

        DockLocal local = new()
        {
            Orden = after,
            Quitadas = [.. before.Where(k => !after.Contains(k, StringComparer.OrdinalIgnoreCase))],
            Anadidas = [.. current
                .Where((app, i) => !app.Separator
                    && !before.Contains(after[i], StringComparer.OrdinalIgnoreCase))],
        };

        try
        {
            File.WriteAllText(DefaultPath, JsonSerializer.Serialize(local, Write));
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[config] no se pudo guardar {Path.GetFileName(DefaultPath)}: {ex.Message}");
        }
    }

    /// <summary>Aplica la superposición sobre la lista de dock.json.</summary>
    public List<DockApp> ApplyTo(IReadOnlyList<DockApp> baseApps)
    {
        List<string> keys = KeysOf(baseApps);

        List<DockApp> result = [];
        for (int i = 0; i < baseApps.Count; i++)
        {
            if (!Quitadas.Contains(keys[i], StringComparer.OrdinalIgnoreCase)) result.Add(baseApps[i]);
        }

        // Las añadidas pasan por el mismo filtro que las de dock.json: si el usuario
        // borró después el .exe que había arrastrado, la entrada se cae sola.
        result.AddRange(DockConfig.Validate(Anadidas));

        if (Orden.Count == 0) return result;

        // OrderBy de LINQ es estable, así que lo que no esté en "orden" se va al final
        // conservando el orden de dock.json. Eso hace que añadir una app a mano en
        // dock.json siga funcionando aunque ya exista una superposición.
        List<string> final = KeysOf(result);
        return [.. result
            .Select((app, i) => (app, rank: Orden.FindIndex(
                k => k.Equals(final[i], StringComparison.OrdinalIgnoreCase))))
            .OrderBy(pair => pair.rank < 0 ? int.MaxValue : pair.rank)
            .Select(pair => pair.app)];
    }

    /// <summary>
    /// La clave de cada entrada. Los separadores no tienen target, así que van por su
    /// orden de aparición.
    ///
    /// ponytail: quitar un separador desplaza el ordinal de los siguientes y esas
    /// entradas del fichero dejan de casar, con lo que se van al final. Con uno o dos
    /// separadores no llega a notarse; si algún día molesta, se les pone un id en
    /// dock.json.
    /// </summary>
    private static List<string> KeysOf(IReadOnlyList<DockApp> apps)
    {
        List<string> keys = [];
        int separators = 0;

        foreach (DockApp app in apps)
        {
            keys.Add(app.Key(app.Separator ? separators++ : 0));
        }

        return keys;
    }
}
