using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.UI.Input.KeyboardAndMouse;

namespace Dock;

/// <summary>
/// El atajo que rota entre perfiles de dock.
///
/// <para>
/// <b>No es un hook.</b> <c>RegisterHotKey</c> no observa el teclado: le pide a Windows
/// que mande <c>WM_HOTKEY</c> a <b>nuestra</b> ventana cuando se pulse <b>una</b>
/// combinación concreta. No se carga nada en ningún proceso ajeno y no se ve ninguna
/// otra tecla — es lo que hace cualquier app que registre un atajo. La regla 3 de
/// SEGURIDAD.md sigue intacta.
/// </para>
///
/// <para>
/// Lo único que conviene saber: la combinación queda <b>reservada en todo el sistema</b>
/// mientras el dock vive, así que si otra app ya la tenía, el registro falla y se avisa
/// por consola en vez de quedarse callado.
/// </para>
/// </summary>
internal static class Profiles
{
    /// <summary>Identificador del atajo dentro de nuestra ventana. Solo hay uno.</summary>
    private const int HotkeyId = 1;

    /// <summary>
    /// Registra la combinación que diga la config. Devuelve si se pudo.
    /// </summary>
    public static bool Register(HWND window, string shortcut)
    {
        if (Parse(shortcut) is not (HOT_KEY_MODIFIERS mods, uint key)) return false;

        // NOREPEAT: mantener pulsado no rota veinte perfiles.
        if (!PInvoke.RegisterHotKey(window, HotkeyId, mods | HOT_KEY_MODIFIERS.MOD_NOREPEAT, key))
        {
            Console.WriteLine($"[perfil] el atajo '{shortcut}' ya lo tiene otra app");
            return false;
        }

        Console.WriteLine($"[perfil] atajo '{shortcut}' registrado");
        return true;
    }

    public static void Unregister(HWND window) => PInvoke.UnregisterHotKey(window, HotkeyId);

    /// <summary>
    /// Cuál toca ahora. La rotación incluye <b>el estado sin perfil</b>, que es la
    /// cadena vacía: así siempre se puede volver a la lista de siempre sin editar nada.
    /// </summary>
    public static string Next(DockConfig config, string current)
    {
        List<string> cycle = ["", .. config.Perfiles.Keys];

        int at = cycle.IndexOf(current);
        return cycle[(at + 1) % cycle.Count];
    }

    /// <summary>
    /// Convierte <c>"Ctrl+Alt+D"</c> en lo que quiere <c>RegisterHotKey</c>. Devuelve
    /// null si no se entiende, y entonces no se registra nada.
    ///
    /// Solo letras, dígitos y F1–F24: es un atajo para rotar entre dos o tres listas, no
    /// un editor de combinaciones.
    /// </summary>
    private static (HOT_KEY_MODIFIERS Mods, uint Key)? Parse(string shortcut)
    {
        if (string.IsNullOrWhiteSpace(shortcut)) return null;

        HOT_KEY_MODIFIERS mods = 0;
        uint key = 0;

        foreach (string raw in shortcut.Split('+', StringSplitOptions.RemoveEmptyEntries))
        {
            string part = raw.Trim();

            switch (part.ToLowerInvariant())
            {
                case "ctrl" or "control": mods |= HOT_KEY_MODIFIERS.MOD_CONTROL; continue;
                case "alt": mods |= HOT_KEY_MODIFIERS.MOD_ALT; continue;
                case "shift": mods |= HOT_KEY_MODIFIERS.MOD_SHIFT; continue;
                case "win" or "windows": mods |= HOT_KEY_MODIFIERS.MOD_WIN; continue;
            }

            if (part.Length == 1 && char.IsAsciiLetterOrDigit(part[0]))
            {
                key = char.ToUpperInvariant(part[0]);
                continue;
            }

            if (part.Length is 2 or 3 && (part[0] is 'F' or 'f')
                && int.TryParse(part[1..], out int number) && number is >= 1 and <= 24)
            {
                key = (uint)(0x70 + number - 1);   // VK_F1 = 0x70
                continue;
            }

            Console.WriteLine($"[perfil] no entiendo '{part}' en el atajo '{shortcut}'");
            return null;
        }

        if (mods == 0 || key == 0)
        {
            Console.WriteLine($"[perfil] el atajo '{shortcut}' necesita al menos un modificador y una tecla");
            return null;
        }

        return (mods, key);
    }
}
