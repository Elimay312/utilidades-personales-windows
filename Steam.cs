using System.Collections.Concurrent;
using Microsoft.Win32;

namespace Dock;

/// <summary>
/// En qué carpeta está instalado un juego de Steam.
///
/// <para>
/// Existe porque el dock reconoce que una app está abierta <b>por el nombre de su
/// ejecutable</b>, y el acceso directo de un juego de Steam no nombra ninguno: dice
/// <c>steam://rungameid/19680</c> y nada más. Sin esto, un juego anclado no se encendía
/// nunca y, al abrirlo, salía un <b>segundo</b> icono en la zona de apps abiertas sin
/// anclar —el del .exe del juego, que el dock no sabía relacionar con el icono que el
/// usuario ya tenía puesto—.
/// </para>
///
/// <para>
/// Sujeto al apartado 3.8 de SEGURIDAD.md. Se leen dos ficheros de texto de Steam, en
/// lectura, y <b>solo los del juego que el usuario ya ancló</b>: nunca se enumera la
/// biblioteca. De ellos sale una ruta de carpeta y nada más. Lo que sigue prohibido por
/// escrito son los ficheros con datos de cuenta, y <c>auditar.ps1</c> lo comprueba.
/// </para>
/// </summary>
internal static class Steam
{
    private const string Prefix = "steam://rungameid/";

    /// <summary>
    /// Target -> carpeta, resuelto una vez por sesión.
    ///
    /// La caché no sobra: el barrido de apps abiertas corre tres veces por segundo y
    /// esto son dos lecturas de disco. Se guardan también los fallos, que si no un juego
    /// desinstalado costaría dos lecturas por barrido para volver a decir que no está.
    ///
    /// Concurrente porque con varias pantallas hay un dock por monitor y cada uno
    /// refresca su estado en una tarea del pool.
    ///
    /// ponytail: instalar un juego con el dock abierto no se nota hasta reiniciarlo. El
    /// día que moleste, se invalida al ver que la carpeta apareció.
    /// </summary>
    private static readonly ConcurrentDictionary<string, string?> Cache = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>
    /// La carpeta donde está instalado el juego al que apunta esa entrada del dock, o
    /// null si no es de Steam o no está instalado.
    /// </summary>
    public static string? FolderOf(string target) => AppIdOf(target) is uint id
        ? Cache.GetOrAdd(target, _ => Resolve(id))
        : null;

    /// <summary>
    /// El identificador del juego, tanto si la entrada guarda ya la URL como si guarda
    /// la ruta del .url.
    ///
    /// Los dos casos hacen falta: las entradas nuevas guardan la URL, pero las que ya
    /// estaban en dock.local.json de antes guardan la ruta del fichero, y esas tienen
    /// que seguir funcionando sin que el usuario vuelva a arrastrar nada.
    /// </summary>
    internal static uint? AppIdOf(string target)
    {
        if (target.EndsWith(".url", StringComparison.OrdinalIgnoreCase))
        {
            return InternetShortcut.Read(target) is { } shortcut ? AppIdOf(shortcut.Url) : null;
        }

        // Un rungameid de 64 bits es un atajo que el usuario metió en Steam a mano, no
        // un juego de la tienda: no tiene appmanifest y no hay nada que buscar.
        return target.StartsWith(Prefix, StringComparison.OrdinalIgnoreCase)
            && uint.TryParse(target[Prefix.Length..], out uint id)
            ? id
            : null;
    }

    private static string? Resolve(uint id)
    {
        if (Root() is not string root) return null;

        foreach (string library in Libraries(root))
        {
            string steamapps = Path.Combine(library, "steamapps");
            string manifest = Path.Combine(steamapps, $"appmanifest_{id}.acf");
            if (!File.Exists(manifest)) continue;

            // Del manifiesto se saca SOLO la carpeta. El nombre, las fechas y el tiempo
            // jugado están ahí al lado y no se miran: ver el apartado 3.8.
            if (Value(File.ReadAllText(manifest), "installdir") is not string dir) continue;

            string folder = Path.Combine(steamapps, "common", dir);
            if (!Directory.Exists(folder)) continue;

            Console.WriteLine($"[steam] {id} está instalado en {folder}");
            return folder;
        }

        return null;
    }

    /// <summary>
    /// Dónde está instalado Steam, según su propia clave de HKCU. Es una LECTURA de una
    /// ruta de instalación: no se escribe nada, y de la cuenta no hay nada ahí.
    /// </summary>
    private static string? Root()
    {
        try
        {
            using RegistryKey? key = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam");

            // Steam la guarda con barras normales.
            return key?.GetValue("SteamPath") is string path && Directory.Exists(path)
                ? path.Replace('/', '\\')
                : null;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[steam] no se pudo leer dónde está Steam: {ex.Message}");
            return null;
        }
    }

    /// <summary>
    /// Las bibliotecas donde puede estar un juego. La propia carpeta de Steam siempre, y
    /// las que declare <c>libraryfolders.vdf</c>: los juegos se reparten entre discos.
    /// </summary>
    private static List<string> Libraries(string root)
    {
        List<string> all = [root];
        string file = Path.Combine(root, "steamapps", "libraryfolders.vdf");

        try
        {
            if (File.Exists(file)) all.AddRange(Values(File.ReadAllText(file), "path"));
        }
        catch (Exception ex)
        {
            // Sin el fichero solo se mira la carpeta de Steam, que es donde está la
            // biblioteca por defecto. No es un error.
            Console.WriteLine($"[steam] no se pudo leer libraryfolders.vdf: {ex.Message}");
        }

        return all;
    }

    /// <summary>El primer valor de esa clave, o null si no está.</summary>
    internal static string? Value(string vdf, string key) => Values(vdf, key).FirstOrDefault();

    /// <summary>
    /// Los valores de esa clave en un fichero VDF/ACF.
    ///
    /// Una línea de VDF es <c>"clave"</c>, tabuladores, <c>"valor"</c>, así que partir
    /// por la comilla deja la clave en la posición 1 y el valor en la 3. No hace falta
    /// entender el formato entero —que es un árbol con llaves— porque lo único que se
    /// busca son dos claves sueltas, y ninguna de las dos aparece anidada dos veces.
    /// </summary>
    internal static List<string> Values(string vdf, string key)
    {
        List<string> found = [];

        foreach (string line in vdf.Split('\n'))
        {
            string[] parts = line.Split('"');
            if (parts.Length < 4 || !parts[1].Equals(key, StringComparison.OrdinalIgnoreCase)) continue;

            // VDF escapa la barra invertida, así que las rutas vienen dobladas.
            found.Add(parts[3].Replace(@"\\", @"\"));
        }

        return found;
    }
}

internal static class SteamSelfCheck
{
    /// <summary>
    /// Lo puro: sacar el appid de una entrada del dock y leer un valor de un VDF. Lo que
    /// toca disco depende de que haya Steam instalado, asi que se mide a mano.
    /// </summary>
    public static void Run()
    {
        Assert(Steam.AppIdOf("steam://rungameid/19680") == 19680, "no saco el appid de la URL");
        Assert(Steam.AppIdOf("C:/Windows/explorer.exe") is null, "un .exe no es un juego");
        Assert(Steam.AppIdOf("steam://open/games") is null, "no toda URL de steam es un juego");

        // Un atajo que no es de la tienda lleva un rungameid de 64 bits. No tiene
        // manifiesto: sin este corte se buscaria un appmanifest que no existe.
        Assert(Steam.AppIdOf("steam://rungameid/13584585583693168640") is null,
            "un rungameid de 64 bits no es un appid");

        const string vdf = """
            "libraryfolders"
            {
            	"0"
            	{
            		"path"		"C:\\Program Files (x86)\\Steam"
            	}
            	"1"
            	{
            		"path"		"D:\\SteamLibrary"
            	}
            }
            """;

        List<string> paths = Steam.Values(vdf, "path");
        Assert(paths.Count == 2, $"esperaba dos bibliotecas, salieron {paths.Count}");

        // La barra invertida viene doblada en el fichero: sin deshacerlo, la ruta no
        // existe y el juego no se encuentra nunca.
        Assert(paths[0] == @"C:\Program Files (x86)\Steam", $"ruta mal leida: {paths[0]}");
        Assert(paths[1] == @"D:\SteamLibrary", $"ruta mal leida: {paths[1]}");

        const string acf = """
            "AppState"
            {
            	"appid"		"19680"
            	"installdir"		"Alice Madness Returns"
            }
            """;

        Assert(Steam.Value(acf, "installdir") == "Alice Madness Returns", "no saco la carpeta del manifiesto");
        Assert(Steam.Value(acf, "SizeOnDisk") is null, "una clave que no esta no puede inventarse");

        Console.WriteLine("[check] juegos de Steam: OK");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException($"self-check: {message}");
    }
}
