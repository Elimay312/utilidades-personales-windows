using System.Diagnostics;
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
    public bool IsApp => IsShellItem
        || Target.EndsWith(".exe", StringComparison.OrdinalIgnoreCase);

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
}

/// <summary>Contenido de dock.json.</summary>
internal sealed class DockConfig
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

    public static string DefaultPath =>
        Path.Combine(AppContext.BaseDirectory, "dock.json");

    public static DockConfig Load(string path)
    {
        DockConfig config = JsonSerializer.Deserialize<DockConfig>(File.ReadAllText(path), Options)
            ?? throw new InvalidDataException($"{path} está vacío");

        List<DockApp> baseApps = Validate(config.Apps);

        return new DockConfig
        {
            IconSize = config.IconSize,
            IconSpacing = config.IconSpacing,
            AutoHide = config.AutoHide,
            AutoStart = config.AutoStart,
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

    public static string DefaultPath =>
        Path.Combine(AppContext.BaseDirectory, "dock.local.json");

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
