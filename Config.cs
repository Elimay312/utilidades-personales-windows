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

    /// <summary>
    /// Argumentos con los que se lanza. Se pasan al shell entre comillas si hace falta,
    /// y existen solo para lo que el usuario escriba a mano en dock.json: nada de lo que
    /// se arrastra al dock los lleva.
    /// </summary>
    public string Arguments { get; init; } = "";

    /// <summary>
    /// De dónde sacar el icono, si no es del propio <see cref="Target"/>. Vacío casi
    /// siempre.
    ///
    /// Existe por los accesos directos que apuntan a un lanzador compartido: el de
    /// VALORANT va a <c>RiotClientServices.exe</c> con
    /// <c>--launch-product=valorant</c>, así que sacar el icono del destino enseñaba la
    /// cara del cliente de Riot. El .lnk sí declara el suyo.
    /// </summary>
    public string IconTarget { get; init; } = "";

    /// <summary>De dónde se saca el icono de verdad.</summary>
    [JsonIgnore]
    public string IconSource => IconTarget.Length > 0 ? IconTarget : Target;

    [JsonIgnore]
    public bool IsShellItem => Target.StartsWith("shell:", StringComparison.OrdinalIgnoreCase);

    /// <summary>
    /// Una dirección web u otro protocolo: <c>https://…</c>, <c>mailto:…</c>,
    /// <c>ms-settings:…</c>. No es un fichero, así que ni se le normalizan las barras ni
    /// se comprueba que exista — quien sabe si existe es el shell al abrirla.
    ///
    /// La <c>C:</c> de una ruta también encaja en "letra más dos puntos", por eso el
    /// esquema tiene que traer al menos dos caracteres.
    /// </summary>
    [JsonIgnore]
    public bool IsUrl => !IsShellItem
        && Target.IndexOf(':') > 1
        && Target[..Target.IndexOf(':')].All(c => char.IsAsciiLetterOrDigit(c) || c is '+' or '.' or '-');

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
    public bool IsApp => Target != DockConfig.TrashTarget && !IsUrl
        && (IsShellItem || Target.EndsWith(".exe", StringComparison.OrdinalIgnoreCase));

    /// <summary>
    /// Si esto es una carpeta, y por tanto se puede desplegar en rejilla en vez de
    /// abrirse en el Explorador. La papelera cuenta: también tiene contenido.
    /// </summary>
    [JsonIgnore]
    public bool IsFolder => Target == DockConfig.TrashTarget || (!IsUrl && Directory.Exists(Target));

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
        ProcessStartInfo info = new(Target) { UseShellExecute = true };
        if (Arguments.Length > 0) info.Arguments = Arguments;

        Process.Start(info);
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

/// <summary>Lo que una pantalla concreta quiere ver, dentro de dock.json.</summary>
internal sealed record DockScreen
{
    public List<DockApp> Apps { get; init; } = [];

    /// <summary>
    /// Solo lo usan los perfiles: dentro de un perfil, una pantalla puede tener su
    /// propia lista. En un bloque de <c>pantallas</c> normal se queda vacío.
    /// </summary>
    public Dictionary<string, DockScreen> Pantallas { get; init; } = [];
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

    /// <summary>
    /// Si las apps abiertas que <b>no</b> están ancladas salen también, detrás de un
    /// separador, como hacen la barra de tareas y el dock de macOS.
    ///
    /// Se van y vienen solas al abrir y cerrar cosas, así que el dock cambia de ancho.
    /// A quien le moleste, lo pone a false y solo ve lo que ancló.
    /// </summary>
    public bool ShowRunning { get; init; } = true;

    public List<DockApp> Apps { get; init; } = [];

    /// <summary>
    /// Listas propias por pantalla, con el nombre de dispositivo como clave
    /// (<c>\\.\DISPLAY2</c>). La que no tenga bloque ve <see cref="Apps"/>.
    ///
    /// El nombre de dispositivo no es el identificador perfecto —es posicional, así que
    /// reordenar las salidas de la gráfica puede intercambiarlos— pero es estable entre
    /// arranques con un montaje fijo, que es el caso que importa. El HMONITOR no vale:
    /// cambia cada vez.
    /// </summary>
    public Dictionary<string, DockScreen> Pantallas { get; init; } = [];

    /// <summary>
    /// Juegos de iconos entre los que rotar con un atajo: trabajo, juegos, lo que sea.
    /// Cada uno sustituye a <see cref="Apps"/>, y puede llevar dentro su propio bloque
    /// de pantallas. Sin perfil activo se ve la lista de siempre.
    /// </summary>
    public Dictionary<string, DockScreen> Perfiles { get; init; } = [];

    /// <summary>
    /// Con qué se rota entre perfiles: <c>"Ctrl+Alt+D"</c>. Vacío, no se registra nada.
    /// </summary>
    public string AtajoPerfil { get; init; } = "";

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

    /// <summary>
    /// Lee dock.json tal cual, <b>sin resolver de qué pantalla es</b>. Para eso está
    /// <see cref="For"/>: el fichero se lee una vez y cada dock saca su lista de ahí.
    /// </summary>
    public static DockConfig Load(string path)
    {
        DockConfig config = JsonSerializer.Deserialize<DockConfig>(File.ReadAllText(path), Options)
            ?? throw new InvalidDataException($"{path} está vacío");

        return config with { Magnification = Math.Clamp(config.Magnification, 1f, 2.5f) };
    }

    /// <summary>
    /// La lista de iconos de UNA pantalla, ya validada y con la superposición local
    /// aplicada. Si la pantalla no tiene bloque propio, ve la lista de por defecto.
    /// </summary>
    /// <param name="device">
    /// Nombre de dispositivo del monitor, del estilo <c>\\.\DISPLAY2</c>.
    /// </param>
    public DockConfig For(string device)
    {
        DockLocal local = DockLocal.Load();

        // El perfil activo manda sobre la lista de siempre, y dentro del perfil una
        // pantalla puede tener la suya. Si el perfil guardado ya no existe en
        // dock.json, se ignora y se ve la lista normal.
        Perfiles.TryGetValue(local.Perfil, out DockScreen? perfil);

        bool propia = (perfil?.Pantallas ?? Pantallas).TryGetValue(device, out DockScreen? own);
        List<DockApp> baseApps = Validate(propia ? own!.Apps : perfil?.Apps ?? Apps);

        // La papelera se añade aquí y no en dock.json para que esté por defecto sin
        // que el usuario tenga que escribirla. Entra en la lista BASE, así que se
        // reordena y se quita arrastrando como cualquier otra: la superposición local
        // ya sabe recordar que se quitó.
        if (Trash)
        {
            baseApps.Add(new DockApp { Name = "Papelera", Target = TrashTarget, Separator = false });
        }

        return this with
        {
            BaseApps = baseApps,
            // Si la pantalla declara su propia lista en dock.json, no hereda la
            // superposicion de por defecto: seria meterle los iconos que el usuario
            // arrastro a OTRA pantalla dentro de una lista que puso a mano.
            // La clave de la superposición lleva el perfil delante: reordenar en
            // "juegos" no puede reordenar "trabajo". Sin perfil la clave es solo la
            // pantalla, que es lo que ya había guardado.
            //
            // Y con un perfil puesto tampoco se hereda la superposición de por
            // defecto, por lo mismo: sería meterle dentro los iconos que el usuario
            // arrastró estando en OTRO perfil.
            Apps = local.For(Key(local.Perfil, device), fallback: !propia && perfil is null)
                .ApplyTo(baseApps),
        };
    }

    /// <summary>
    /// Con qué clave se guarda la superposición local. Meter el perfil en la misma
    /// cadena que la pantalla evita un nivel más de anidamiento en dock.local.json, y
    /// sin perfil sale exactamente la clave de antes, así que lo ya guardado sigue
    /// valiendo.
    /// </summary>
    public static string Key(string profile, string device)
        => profile.Length == 0 ? device : $"{profile}|{device}";

    /// <summary>
    /// Sigue a una app que se ha mudado de carpeta al actualizarse.
    ///
    /// Discord, Slack, Teams y todo lo empaquetado con Squirrel se instalan en
    /// <c>…\Discord\app-1.0.9258\Discord.exe</c>: el número de versión va EN LA RUTA,
    /// así que la primera actualización deja la ruta anclada apuntando a una carpeta que
    /// ya no existe. Sin esto, <see cref="Validate"/> tiraba la entrada y el icono
    /// desaparecía del dock sin que el usuario tuviera forma de saber por qué —el aviso
    /// va a la consola, y arrancando al iniciar sesión no hay consola que leer—.
    ///
    /// Se busca a la hermana de esa carpeta que empieza igual y tiene el mismo fichero
    /// dentro, quedándose con la versión más alta.
    ///
    /// ponytail: solo se mira la carpeta que contiene al fichero. Un
    /// <c>…\app-1.2.3\bin\x.exe</c> no se seguiría; si algún día aparece uno, se sube
    /// por los padres hasta dar con el tramo que lleva número.
    /// </summary>
    public static string Repair(string target)
    {
        // Todo lo que no sea "ruta con carpeta padre versionada" sale de aquí sin tocar
        // el disco: shell items, URLs, separadores y las rutas normales, que son casi
        // todas.
        string folder = Path.GetDirectoryName(target) ?? "";
        string? prefix = VersionPrefix(Path.GetFileName(folder));
        if (prefix is null || File.Exists(target)) return target;

        string parent = Path.GetDirectoryName(folder) ?? "";
        string file = Path.GetFileName(target);

        string? best = null;
        try
        {
            foreach (string sibling in Directory.EnumerateDirectories(parent, prefix + "*"))
            {
                if (File.Exists(Path.Combine(sibling, file)))
                    best = Newest(prefix, best, Path.GetFileName(sibling));
            }
        }
        catch (Exception ex)
        {
            // Sin permiso para listar, o la carpeta padre tampoco existe ya. No es un
            // error: simplemente no hay a dónde seguir.
            Console.WriteLine($"[config] no se pudo buscar '{file}' en {parent}: {ex.Message}");
            return target;
        }

        // Sin traza aquí: esto se llama también por cada clave guardada en
        // dock.local.json, y una app actualizada soltaría la misma línea seis veces
        // con tres pantallas. Quien avisa es Validate, que sabe de qué entrada habla.
        return best is null ? target : Path.Combine(parent, best, file);
    }

    /// <summary>
    /// Lo que va antes del primer dígito del nombre de una carpeta, o null si no lleva
    /// número de versión detrás. De <c>app-1.0.9258</c> sale <c>app-</c>.
    ///
    /// El prefijo tiene que existir: una carpeta que empieza por dígito no marca familia
    /// ninguna, y con prefijo vacío el patrón de búsqueda se comería a todas las
    /// hermanas.
    /// </summary>
    internal static string? VersionPrefix(string folder)
    {
        int digit = folder.AsSpan().IndexOfAnyInRange('0', '9');
        return digit > 0 && Version.TryParse(folder[digit..], out _) ? folder[..digit] : null;
    }

    /// <summary>De dos carpetas de la misma familia, la de versión más alta.</summary>
    internal static string? Newest(string prefix, string? a, string? b)
    {
        if (a is null) return b;
        if (b is null) return a;

        return Version.TryParse(a[prefix.Length..], out Version? va)
            && Version.TryParse(b[prefix.Length..], out Version? vb)
            && vb > va ? b : a;
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

            // Una URL se queda tal cual: normalizarle las barras convertiria
            // https:// en https:\ y el shell dejaria de reconocerla.
            if (app.IsUrl)
            {
                valid.Add(app);
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
            // Repair antes de comprobar que existe: una app que se ha actualizado
            // sigue ahi, solo que una carpeta mas alla.
            string stored = app.Target.Replace('/', '\\');
            string target = Repair(stored);
            if (target != stored) Console.WriteLine($"[config] '{app.Name}' se actualizó: {target}");

            // Directory.Exists además de File.Exists: en el dock caben carpetas, y
            // Process.Start con UseShellExecute las abre igual de bien que un .exe.
            if (!app.IsShellItem && !File.Exists(target) && !Directory.Exists(target))
            {
                Console.WriteLine($"[config] omitida '{app.Name}': no existe {target}");
                continue;
            }

            // El icono se normaliza igual que el target, y por el mismo motivo:
            // SHCreateItemFromParsingName no acepta barras normales.
            valid.Add(new DockApp
            {
                Name = app.Name,
                Target = target,
                Arguments = app.Arguments,
                IconTarget = app.IconTarget.Replace("/", "\\"),
            });
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
internal sealed record DockLocal
{
    /// <summary>Claves en el orden en que se quieren ver.</summary>
    public List<string> Orden { get; init; } = [];

    /// <summary>Entradas que no están en dock.json, añadidas arrastrando.</summary>
    public List<DockApp> Anadidas { get; init; } = [];

    /// <summary>Claves de dock.json que el usuario sacó del dock.</summary>
    public List<string> Quitadas { get; init; } = [];

    /// <summary>
    /// Una superposición por pantalla, con la misma clave que
    /// <see cref="DockConfig.Pantallas"/>. El tipo se referencia a sí mismo porque el
    /// bloque de una pantalla tiene exactamente la misma forma que la raíz; el
    /// "pantallas" de dentro de un bloque no se usa y se queda vacío.
    ///
    /// Los tres campos de la raíz siguen valiendo, y hacen de por defecto: es lo que ve
    /// una pantalla que todavía no se ha tocado, y es lo que hace que el
    /// dock.local.json que ya existía no se pierda al actualizar.
    /// </summary>
    public Dictionary<string, DockLocal> Pantallas { get; init; } = [];

    /// <summary>
    /// Qué perfil está puesto ahora mismo, o vacío para la lista de siempre. Vive aquí
    /// y no en dock.json porque lo escribe el dock al rotar, y dock.json es del usuario.
    /// </summary>
    public string Perfil { get; init; } = "";

    /// <summary>Deja guardado el perfil activo, sin tocar nada más del fichero.</summary>
    public static void SaveProfile(string profile)
    {
        DockLocal file = Load();

        try
        {
            File.WriteAllText(DefaultPath, JsonSerializer.Serialize(file with { Perfil = profile }, Write));
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[perfil] no se pudo guardar: {ex.Message}");
        }
    }

    /// <summary>La superposición de una pantalla, o la de por defecto si no tiene.</summary>
    public DockLocal For(string device, bool fallback = true) =>
        Pantallas.TryGetValue(device, out DockLocal? own) ? own
        : fallback ? this : new DockLocal();

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
            DockLocal local = JsonSerializer.Deserialize<DockLocal>(File.ReadAllText(DefaultPath), DockConfig.Options)
                ?? new DockLocal();

            // Las claves también pasan por Repair, y no solo los targets: si la app se
            // ha actualizado, Validate devuelve ya la ruta nueva y la de este fichero
            // dejaría de casar. El icono sobreviviría, pero perdería su sitio y se iría
            // al final del dock hasta que el usuario volviera a arrastrar algo.
            return local.Repaired();
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
    public static void Save(string device, IReadOnlyList<DockApp> baseApps, IReadOnlyList<DockApp> current)
    {
        List<string> before = KeysOf(baseApps);
        List<string> after = KeysOf(current);

        // Se parte de lo que ya hay en el fichero y solo se toca el bloque de ESTA
        // pantalla y ESTE perfil: mover un icono en una pantalla no puede reordenar las
        // otras, ni reordenar "juegos" tocar "trabajo".
        DockLocal file = Load();
        file.Pantallas[DockConfig.Key(file.Perfil, device)] = new DockLocal
        {
            Orden = after,
            Quitadas = [.. before.Where(k => !after.Contains(k, StringComparer.OrdinalIgnoreCase))],
            Anadidas = [.. current
                .Where((app, i) => !app.Separator
                    && !before.Contains(after[i], StringComparer.OrdinalIgnoreCase))],
        };

        try
        {
            File.WriteAllText(DefaultPath, JsonSerializer.Serialize(file, Write));
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[config] no se pudo guardar {Path.GetFileName(DefaultPath)}: {ex.Message}");
        }
    }

    /// <summary>
    /// Este mismo bloque con las rutas de las apps que se hayan actualizado ya
    /// seguidas. Ver <see cref="DockConfig.Repair"/>.
    /// </summary>
    private DockLocal Repaired() => new()
    {
        Orden = [.. Orden.Select(DockConfig.Repair)],
        Quitadas = [.. Quitadas.Select(DockConfig.Repair)],
        Anadidas = Anadidas,
        Perfil = Perfil,
        Pantallas = Pantallas.ToDictionary(par => par.Key, par => par.Value.Repaired()),
    };

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

internal static class ConfigSelfCheck
{
    /// <summary>
    /// Lo puro de seguir a una app que se actualiza: reconocer la carpeta versionada y
    /// quedarse con la version mas alta. Lo que toca disco se mide con un arbol de
    /// mentira, que no pinta en el repo.
    /// </summary>
    public static void Run()
    {
        // La familia es lo que va antes del numero.
        Assert(DockConfig.VersionPrefix("app-1.0.9258") == "app-", "no reconocio app-1.0.9258");
        Assert(DockConfig.VersionPrefix("Discord") is null, "una carpeta sin numero no es una familia");

        // Sin prefijo no hay familia: con "" el patron de busqueda se comeria a TODAS
        // las hermanas y la app podria acabar apuntando a cualquier cosa.
        Assert(DockConfig.VersionPrefix("1.0.9258") is null, "un nombre que empieza por digito no es una familia");

        // Un numero suelto tampoco: "Steam 2" no es una version.
        Assert(DockConfig.VersionPrefix("Steam 2") is null, "'2' no es un numero de version");

        // Comparacion por version y no por texto: "1.0.10" va DESPUES de "1.0.9",
        // aunque ordenando como cadenas saldria antes.
        Assert(DockConfig.Newest("app-", "app-1.0.9", "app-1.0.10") == "app-1.0.10", "1.0.10 es mas nueva que 1.0.9");
        Assert(DockConfig.Newest("app-", "app-1.0.10", "app-1.0.9") == "app-1.0.10", "y da igual en que orden lleguen");
        Assert(DockConfig.Newest("app-", null, "app-1.0.1") == "app-1.0.1", "la primera candidata gana a nada");

        Console.WriteLine("[check] apps que se actualizan: OK");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException($"self-check: {message}");
    }
}
