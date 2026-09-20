using Microsoft.Win32;

namespace QuickLook;

/// <summary>
/// Autoarranque al iniciar sesion.
///
/// <para>
/// Va en <c>HKCU\Software\Microsoft\Windows\CurrentVersion\Run</c> y en ningun otro sitio.
/// Esa clave sale listada en la pestana Inicio del Administrador de tareas, asi que el
/// usuario la ve y la puede quitar desde ahi sin tocar el registro. Nada de tareas
/// programadas, servicios ni carpeta Startup: la regla 7 los prohibe por ser persistencia
/// que el usuario no ve.
/// </para>
///
/// <para>
/// Es el <b>unico</b> sitio del registro donde este programa escribe, y el unico estado que
/// guarda entre sesiones.
/// </para>
/// </summary>
internal static class AutoStart
{
    private const string Key = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "QuickLook";

    /// <summary>
    /// Deja el registro acorde con la config. Solo escribe si hace falta, para no tocar el
    /// registro en cada arranque.
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
                Console.WriteLine($@"[autoarranque] escrito HKCU\{Key}\{ValueName}");
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
            // No poder tocar el registro no es motivo para no arrancar.
            Console.WriteLine($"[autoarranque] no se pudo aplicar: {ex.Message}");
        }
    }
}
