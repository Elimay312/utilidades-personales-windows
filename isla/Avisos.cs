using System.IO.Pipes;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Windows.Win32;
using Windows.Win32.Foundation;

namespace Isla;

/// <summary>Un boton de un aviso: el id que se devuelve y lo que pone.</summary>
internal sealed record Boton(string Id, string Texto);

/// <summary>
/// Un aviso que una app de la casa le deja a la isla. Solo texto, un color y botones: es todo
/// lo que SEGURIDAD.md s.3.7 deja entrar.
///
/// <para>
/// <c>Pid</c> es el proceso al otro lado de la tuberia, y solo sirve para cederle el primer plano
/// al pulsar un boton.
/// </para>
/// </summary>
internal sealed record AvisoApp(long Numero, string App, string Titulo, string Linea, uint Color,
                                IReadOnlyList<Boton> Botones, uint Pid = 0);

/// <summary>
/// El buzon: una tuberia con nombre donde otra app deja un aviso y espera a que la persona pulse
/// uno de sus botones. La isla solo escucha; nunca llama a nadie (SEGURIDAD.md s.3.7).
///
/// <para>
/// Cada conexion es un aviso y vive mientras espera: se contesta por ella con el id del boton y se
/// cierra. Si la otra app cuelga antes, el aviso se retira solo. Los avisos esperando viven aqui,
/// en estatico, y no en la ventana: rehacerse al cambiar de pantalla no se los lleva.
/// </para>
/// </summary>
internal static partial class Avisos
{
    public const string Nombre = "IslaDinamica.avisos";

    // Los cortes de la enmienda. Una linea mas larga corta la conexion; un cuarto aviso se
    // rechaza, y quien lo mandaba sabe que tiene que avisar por su cuenta.
    private const int MaxBytes = 4096;
    private const int MaxEsperando = 3;
    private const int MaxTitulo = 80;
    private const int MaxLinea = 120;
    private const int MaxBotones = 4;
    private const int MaxTextoBoton = 16;
    private static readonly TimeSpan PlazoLectura = TimeSpan.FromSeconds(5);

    private static readonly object Cerrojo = new();
    private static readonly List<(AvisoApp Aviso, TaskCompletionSource<string> Respuesta)> Esperando = [];
    private static HWND _ventana;
    private static uint _mensaje;
    private static long _siguiente;
    private static bool _arrancado;

    [GeneratedRegex("^[a-z0-9]{1,16}$")]
    private static partial Regex IdValido();

    /// <summary>A donde se avisa de que algo cambio. Se llama en cada creacion de la ventana.</summary>
    public static void Ventana(HWND ventana, uint mensaje)
    {
        _ventana = ventana;
        _mensaje = mensaje;
    }

    /// <summary>Empieza a escuchar. Una vez por proceso.</summary>
    public static void Arrancar()
    {
        if (_arrancado) return;
        _arrancado = true;
        _ = Task.Run(Escuchar);
    }

    /// <summary>Los que esperan respuesta, el mas antiguo primero.</summary>
    public static IReadOnlyList<AvisoApp> Pendientes
    {
        get { lock (Cerrojo) return Esperando.Select(e => e.Aviso).ToArray(); }
    }

    /// <summary>
    /// La persona pulso un boton. Desde el hilo de UI, justo despues del clic: la isla recibio
    /// esa entrada, y por eso puede cederle el primer plano a la app que aviso, a esa y una vez.
    /// </summary>
    public static void Responder(long numero, string boton)
    {
        lock (Cerrojo)
        {
            foreach ((AvisoApp aviso, TaskCompletionSource<string> respuesta) in Esperando)
            {
                if (aviso.Numero != numero) continue;
                if (aviso.Pid != 0) PInvoke.AllowSetForegroundWindow(aviso.Pid);
                respuesta.TrySetResult(boton);
            }
        }
    }

    private static void Despertar()
    {
        if (!_ventana.IsNull) PInvoke.PostMessage(_ventana, _mensaje, default, default);
    }

    private static async Task Escuchar()
    {
        while (true)
        {
            NamedPipeServerStream tubo;
            try
            {
                tubo = Crear();
            }
            catch (Exception ex)
            {
                // El nombre lo tiene otro proceso, o no se pudo poner la ACL. Sin buzon la isla
                // sigue igual; las apps de al lado avisaran por su cuenta.
                Console.Error.WriteLine($"[isla] sin buzon de avisos: {ex.Message}");
                return;
            }

            try
            {
                await tubo.WaitForConnectionAsync().ConfigureAwait(false);
            }
            catch
            {
                tubo.Dispose();
                await Task.Delay(1000).ConfigureAwait(false);
                continue;
            }

            _ = Atender(tubo);
        }
    }

    /// <summary>
    /// La tuberia con su ACL: permitir a la cuenta de la sesion y denegar NETWORK. .NET no pone
    /// PIPE_REJECT_REMOTE_CLIENTS, asi que es la denegacion la que cierra la puerta a otra
    /// maquina; y la denegacion va primero y gana (SEGURIDAD.md s.3.7).
    /// </summary>
    private static NamedPipeServerStream Crear()
    {
        PipeSecurity seguridad = new();
        seguridad.AddAccessRule(new PipeAccessRule(
            new SecurityIdentifier(WellKnownSidType.NetworkSid, null),
            PipeAccessRights.FullControl, AccessControlType.Deny));
        seguridad.AddAccessRule(new PipeAccessRule(
            WindowsIdentity.GetCurrent().User!, PipeAccessRights.FullControl, AccessControlType.Allow));

        // Una instancia mas que los que pueden esperar: la cuarta se acepta para poder cerrarla
        // en vez de dejar al otro lado colgado.
        return NamedPipeServerStreamAcl.Create(
            Nombre, PipeDirection.InOut, MaxEsperando + 1, PipeTransmissionMode.Byte,
            PipeOptions.Asynchronous, 0, 0, seguridad);
    }

    private static async Task Atender(NamedPipeServerStream tubo)
    {
        using (tubo)
        {
            AvisoApp? aviso = Leer(await LeerLinea(tubo).ConfigureAwait(false));
            if (aviso is null) return;
            if (PInvoke.GetNamedPipeClientProcessId(tubo.SafePipeHandle, out uint pid)) aviso = aviso with { Pid = pid };

            TaskCompletionSource<string> respuesta = new(TaskCreationOptions.RunContinuationsAsynchronously);
            lock (Cerrojo)
            {
                if (Esperando.Count >= MaxEsperando) return;
                Esperando.Add((aviso, respuesta));
            }
            // Quien avisa, nunca lo que dice: el titulo de un evento es tan personal como una
            // cancion (regla 12).
            Console.WriteLine($"[isla] aviso de {aviso.App}");
            Despertar();

            // Lo primero que pase: que la persona conteste, o que el otro lado cuelgue. El otro
            // lado no vuelve a escribir, asi que una lectura que termina es que se fue.
            byte[] uno = new byte[1];
            Task<int> colgo = tubo.ReadAsync(uno).AsTask();
            Task gana;
            try
            {
                gana = await Task.WhenAny(respuesta.Task, colgo).ConfigureAwait(false);
            }
            finally
            {
                lock (Cerrojo) Esperando.RemoveAll(e => e.Aviso.Numero == aviso.Numero);
                Despertar();
            }

            if (gana != respuesta.Task || !tubo.IsConnected) return;
            try
            {
                byte[] linea = Encoding.UTF8.GetBytes(
                    JsonSerializer.Serialize(new Dictionary<string, string> { ["boton"] = respuesta.Task.Result }) + "\n");
                await tubo.WriteAsync(linea).ConfigureAwait(false);
                await tubo.FlushAsync().ConfigureAwait(false);
            }
            catch
            {
                // Se fue justo al contestar. No hay a quien decirselo.
            }
        }
    }

    /// <summary>Una linea, con techo de tamano y de tiempo. Nada, si no llega entera.</summary>
    private static async Task<string?> LeerLinea(NamedPipeServerStream tubo)
    {
        using CancellationTokenSource plazo = new(PlazoLectura);
        byte[] buffer = new byte[MaxBytes];
        int llenos = 0;
        try
        {
            while (llenos < buffer.Length)
            {
                int leidos = await tubo.ReadAsync(buffer.AsMemory(llenos), plazo.Token).ConfigureAwait(false);
                if (leidos == 0) return null;
                int fin = Array.IndexOf(buffer, (byte)'\n', llenos, leidos);
                llenos += leidos;
                if (fin >= 0) return Encoding.UTF8.GetString(buffer, 0, fin);
            }
        }
        catch
        {
            return null;
        }
        return null;  // 4 KB sin salto de linea: no es un aviso
    }

    /// <summary>
    /// El JSON del aviso, validado campo a campo. Lo que no encaja se recorta; lo que no tiene
    /// sentido rechaza el aviso entero.
    /// </summary>
    private static AvisoApp? Leer(string? linea)
    {
        if (string.IsNullOrWhiteSpace(linea)) return null;
        try
        {
            using JsonDocument doc = JsonDocument.Parse(linea);
            JsonElement raiz = doc.RootElement;
            if (raiz.ValueKind != JsonValueKind.Object) return null;

            string titulo = Cadena(raiz, "titulo", MaxTitulo);
            if (titulo.Length == 0) return null;

            List<Boton> botones = [];
            if (raiz.TryGetProperty("botones", out JsonElement lista) && lista.ValueKind == JsonValueKind.Array)
            {
                foreach (JsonElement b in lista.EnumerateArray())
                {
                    if (botones.Count == MaxBotones || b.ValueKind != JsonValueKind.Object) break;
                    string id = Cadena(b, "id", 16);
                    string texto = Cadena(b, "texto", MaxTextoBoton);
                    if (IdValido().IsMatch(id) && texto.Length > 0) botones.Add(new Boton(id, texto));
                }
            }

            return new AvisoApp(
                Interlocked.Increment(ref _siguiente),
                Cadena(raiz, "app", 24) is { Length: > 0 } app ? app : "?",
                titulo,
                Cadena(raiz, "linea", MaxLinea),
                Color(Cadena(raiz, "color", 7)),
                botones);
        }
        catch (JsonException)
        {
            return null;
        }
    }

    private static string Cadena(JsonElement padre, string clave, int max)
    {
        if (!padre.TryGetProperty(clave, out JsonElement v) || v.ValueKind != JsonValueKind.String) return string.Empty;
        // Sin saltos de linea ni controles: esto se pinta en una sola linea.
        string s = new((v.GetString() ?? string.Empty).Where(c => !char.IsControl(c)).ToArray());
        s = s.Trim();
        return s.Length <= max ? s : s[..max];
    }

    private static uint Color(string hex)
    {
        if (hex.Length != 7 || hex[0] != '#') return 0;
        return uint.TryParse(hex.AsSpan(1), System.Globalization.NumberStyles.HexNumber, null, out uint rgb) ? rgb : 0;
    }
}
