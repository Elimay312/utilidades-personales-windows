using System.Runtime.InteropServices;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.UI.Shell;

namespace Lanzador;

/// <summary>
/// Una cosa que se puede abrir. <b>Destino es lo que se le pasa al shell tal cual</b>: la
/// ruta de un .lnk, un shell:AppsFolder\AUMID o, mas adelante, una URL. Nunca una cadena
/// que haya escrito el usuario (SEGURIDAD.md regla 10).
/// </summary>
/// <param name="EsFichero">
/// Lo trajo Everything, no el menu Inicio. Puntua algo por debajo de una aplicacion: si
/// escribes tres letras casi siempre quieres abrir un programa, y el fichero que se llama
/// parecido es ruido — pero un fichero que encaja mucho mejor sigue ganando.
/// </param>
/// <param name="EsCarpetaDeDisco">Para el icono (H7) y para no prometer lo que no es.</param>
/// <param name="SoloSeMira">
/// El resultado de una cuenta: se ensena y ya. Enter no abre nada, porque no hay nada que
/// abrir — y copiarlo seria el portapapeles, que es la regla 14.
/// </param>
internal sealed record Entrada(string Nombre, string Destino,
                               bool EsFichero = false, bool EsCarpetaDeDisco = false,
                               bool SoloSeMira = false)
{
    /// <summary>
    /// El nombre sin acentos, que es contra lo que se busca. Se calcula una vez al
    /// construir el indice y no en cada pulsacion: normalizar 241 cadenas diez veces por
    /// segundo es trabajo que se puede hacer una sola vez.
    /// </summary>
    public string Buscable { get; } = Coincidencia.Normalizar(Nombre);
}

/// <summary>
/// El inventario de aplicaciones. SEGURIDAD.md §3.1: solo nombres y destinos, solo de
/// donde ya mira el menu Inicio, y <b>no se escribe a disco</b> — se reconstruye al
/// arrancar.
/// </summary>
internal static class Indice
{
    /// <summary>
    /// Las dos fuentes juntas, sin nombres repetidos. <b>Hacen falta las dos</b>, medido en
    /// H1: AppsFolder trae 205 entradas y los menus Inicio 148, pero 36 de esas 148 no
    /// estan en AppsFolder — el Administrador de tareas, el Editor del registro, el panel
    /// de control y los lanzadores de algunos juegos. Ninguna de las dos sustituye a la
    /// otra.
    /// </summary>
    public static List<Entrada> Construir()
    {
        List<Entrada> todo = AppsFolder();
        todo.AddRange(Proveedores.Sistema());

        // AppsFolder gana el empate: su nombre visible es el que ensena el menu Inicio, y
        // su destino vale igual para una app de la Store que para una de escritorio.
        HashSet<string> vistos = new(todo.Select(e => e.Nombre), StringComparer.OrdinalIgnoreCase);
        foreach (Entrada e in MenusInicio())
        {
            if (vistos.Add(e.Nombre)) todo.Add(e);
        }

        return todo;
    }

    /// <summary>
    /// La carpeta virtual que pinta la lista "Todas las aplicaciones" del menu Inicio.
    /// Trae las apps de la Store y tambien las de escritorio.
    /// </summary>
    /// <remarks>
    /// ponytail: 800 ms para 205 apps, y no es el arranque de COM — medido, la segunda
    /// pasada en el mismo proceso cuesta lo mismo. Son los ~3,8 ms por app que cuesta
    /// preguntarle su nombre al repositorio de paquetes. Se paga una vez al arrancar y en
    /// segundo plano, asi que no duele. Si algun dia hay que tocarlo, el camino es
    /// cachear el resultado con la fecha de la carpeta y no releerlo si no cambio.
    /// </remarks>
    public static unsafe List<Entrada> AppsFolder()
    {
        List<Entrada> encontradas = new(256);

        if (PInvoke.SHCreateItemFromParsingName("shell:AppsFolder", null, out IShellItem? carpeta).Failed
            || carpeta is null)
        {
            return encontradas;
        }

        carpeta.BindToHandler(null, PInvoke.BHID_EnumItems, out IEnumShellItems? lista);
        if (lista is null) return encontradas;

        // De 64 en 64 y no de 1 en 1: cada Next es un salto a COM, y con 205 apps la
        // diferencia se mide (H1). Medido: el coste no esta aqui sino en GetDisplayName.
        IShellItem[] tanda = new IShellItem[64];
        while (true)
        {
            uint traidos = 0;
            lista.Next((uint)tanda.Length, tanda, &traidos);
            if (traidos == 0) break;

            for (uint i = 0; i < traidos; i++)
            {
                if (tanda[i] is null) continue;

                string nombre = Nombre(tanda[i], SIGDN.SIGDN_NORMALDISPLAY);
                string destino = Nombre(tanda[i], SIGDN.SIGDN_PARENTRELATIVEPARSING);
                if (nombre.Length > 0 && destino.Length > 0)
                {
                    encontradas.Add(new Entrada(nombre, @"shell:AppsFolder\" + destino));
                }

                tanda[i] = null!;
            }
        }

        return encontradas;
    }

    /// <summary>
    /// Los dos menus Inicio, recorriendo los .lnk. Se lanza el propio acceso directo: el
    /// shell ya sabe resolverlo, asi que <b>no hace falta IShellLink</b> y el destino del
    /// acceso directo ni se lee.
    /// </summary>
    public static List<Entrada> MenusInicio()
    {
        List<Entrada> encontradas = new(256);

        // EnumerateFiles se rinde entera si una subcarpeta da acceso denegado, y en el
        // menu Inicio comun eso pasa. Con estas opciones se salta y sigue.
        EnumerationOptions saltando = new()
        {
            RecurseSubdirectories = true,
            IgnoreInaccessible = true,
            AttributesToSkip = FileAttributes.Hidden | FileAttributes.System,
        };

        foreach (Environment.SpecialFolder cual in
                 new[] { Environment.SpecialFolder.CommonStartMenu, Environment.SpecialFolder.StartMenu })
        {
            string raiz = Environment.GetFolderPath(cual);
            if (raiz.Length == 0 || !Directory.Exists(raiz)) continue;

            foreach (string lnk in Directory.EnumerateFiles(raiz, "*.lnk", saltando))
            {
                encontradas.Add(new Entrada(Path.GetFileNameWithoutExtension(lnk), lnk));
            }
        }

        return encontradas;
    }

    /// <summary>
    /// GetDisplayName devuelve memoria del shell que hay que soltar con CoTaskMemFree, y
    /// tira si el item no sabe dar ese formato de nombre.
    /// </summary>
    private static unsafe string Nombre(IShellItem item, SIGDN formato)
    {
        try
        {
            item.GetDisplayName(formato, out PWSTR p);
            if (p.Value is null) return string.Empty;
            string s = p.ToString();
            Marshal.FreeCoTaskMem((nint)p.Value);
            return s;
        }
        catch (COMException)
        {
            return string.Empty;
        }
    }
}
