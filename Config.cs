namespace Lanzador;

internal static class Config
{
    /// <summary>
    /// Vive en %LOCALAPPDATA%, no junto al ejecutable. El dock aprendio por las malas que
    /// un <c>dotnet clean</c> se lleva por delante la carpeta de compilacion y con ella la
    /// configuracion del usuario, sin avisar.
    /// </summary>
    public static string Carpeta => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Lanzador");
}
