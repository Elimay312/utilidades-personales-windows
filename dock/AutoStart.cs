using Microsoft.Win32;

namespace Dock;

/// <summary>
/// Autoarranque al iniciar sesión.
///
/// Va en HKCU\Software\Microsoft\Windows\CurrentVersion\Run y en ningún otro sitio.
/// Esa clave sale listada en la pestaña Inicio del Administrador de tareas, así que
/// el usuario la ve y la puede quitar desde ahí sin tocar el registro.
///
/// Nada de tareas programadas, servicios ni carpeta Startup oculta: la Fase 2 los
/// prohíbe por ser persistencia que el usuario no ve.
/// </summary>
internal static class AutoStart
{
    private const string Key = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "Dock";

    /// <summary>
    /// Deja el registro acorde con la config. Solo escribe si hace falta, para no
    /// tocar el registro en cada arranque.
    /// </summary>
    public static void Sync(bool wanted)
    {
        try
        {
            using RegistryKey key = Registry.CurrentUser.CreateSubKey(Key, writable: true);

            string? current = key.GetValue(ValueName) as string;
            string desired = $"\"{Environment.ProcessPath}\"";

            if (wanted)
            {
                if (current == desired) return;
                key.SetValue(ValueName, desired, RegistryValueKind.String);
                Console.WriteLine($@"[autoarranque] escrito HKCU\{Key}\{ValueName} = {desired}");
            }
            else
            {
                if (current is null) return;
                key.DeleteValue(ValueName, throwOnMissingValue: false);
                Console.WriteLine($@"[autoarranque] borrado HKCU\{Key}\{ValueName}");
            }
        }
        catch (Exception ex)
        {
            // No poder tocar el registro no es motivo para no arrancar el dock.
            Console.WriteLine($"[autoarranque] no se pudo aplicar: {ex.Message}");
        }
    }
}
