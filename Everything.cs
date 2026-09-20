using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.System.DataExchange;
using Windows.Win32.UI.WindowsAndMessaging;

namespace Lanzador;

/// <summary>
/// La conversacion con Everything, por el buzon que el propio Everything deja puesto.
/// <para>
/// SEGURIDAD.md §3.7: es el canal que su SDK documenta para esto, y es el mismo que usa su
/// linea de comandos. No se toca su proceso, no se lee su memoria, no se abre su base de
/// datos: se le manda una pregunta y contesta si quiere. <b>Everything no es una
/// dependencia nuestra</b> — si no esta, el lanzador ensena solo aplicaciones y lo dice.
/// </para>
/// <para>
/// Las constantes y el reparto de bytes estan copiados de <c>ipc/everything_ipc.h</c> del
/// Everything-SDK, no escritos de memoria: un campo mal alineado aqui no da un error, da
/// basura, y eso se tarda mucho mas en ver.
/// </para>
/// </summary>
internal static unsafe class Everything
{
    /// <summary>everything_ipc.h: EVERYTHING_IPC_WNDCLASSW.</summary>
    private const string Clase = "EVERYTHING_TASKBAR_NOTIFICATION";

    /// <summary>everything_ipc.h: EVERYTHING_IPC_COPYDATAQUERYW = 2.</summary>
    private const nuint ConsultaW = 2;

    /// <summary>
    /// Reparto de EVERYTHING_IPC_QUERYW: cinco DWORD y detras la cadena terminada en cero.
    /// <code>
    /// DWORD reply_hwnd;               //  0
    /// DWORD reply_copydata_message;   //  4
    /// DWORD search_flags;             //  8
    /// DWORD offset;                   // 12
    /// DWORD max_results;              // 16
    /// WCHAR search_string[1];         // 20
    /// </code>
    /// </summary>
    private const int CabeceraConsulta = 20;

    /// <summary>
    /// Reparto de EVERYTHING_IPC_LISTW: siete DWORD y detras los items de tres DWORD cada
    /// uno. Los <c>*_offset</c> cuentan desde el principio de la estructura, no del item.
    /// <code>
    /// DWORD totfolders, totfiles, totitems;      //  0  4  8
    /// DWORD numfolders, numfiles, numitems;      // 12 16 20
    /// DWORD offset;                              // 24
    /// EVERYTHING_IPC_ITEMW items[1];             // 28
    /// </code>
    /// </summary>
    private const int CabeceraLista = 28;
    private const int TamItem = 12;

    /// <summary>everything_ipc.h: EVERYTHING_IPC_FOLDER.</summary>
    private const uint EsCarpeta = 0x00000001;

    /// <summary>El buzon, o nulo si Everything no esta corriendo.</summary>
    public static HWND Buzon()
    {
        fixed (char* c = Clase) return PInvoke.FindWindow(new PCWSTR(c), (PCWSTR)null);
    }

    /// <summary>
    /// Manda la pregunta. La respuesta NO vuelve aqui: llega despues como otro
    /// WM_COPYDATA a <paramref name="nuestro"/>, con <paramref name="mensaje"/> en dwData.
    /// </summary>
    public static bool Preguntar(HWND buzon, HWND nuestro, uint mensaje, string consulta, uint cuantos)
    {
        int bytes = CabeceraConsulta + (consulta.Length + 1) * 2;
        byte[] peticion = new byte[bytes];

        fixed (byte* p = peticion)
        {
            uint* campo = (uint*)p;

            // "only 32bits are required to store a window handle. (even on x64)" — lo dice
            // el propio header, asi que el truncado es el contrato y no un descuido.
            campo[0] = (uint)(nint)nuestro.Value;
            campo[1] = mensaje;
            campo[2] = 0;          // sin flags: ni mayusculas, ni palabra entera, ni regex
            campo[3] = 0;          // desde el primer resultado
            campo[4] = cuantos;

            char* texto = (char*)(p + CabeceraConsulta);
            consulta.AsSpan().CopyTo(new Span<char>(texto, consulta.Length));
            texto[consulta.Length] = '\0';

            COPYDATASTRUCT sobre = new()
            {
                dwData = ConsultaW,
                cbData = (uint)bytes,
                lpData = p,
            };

            // SendMessage y no PostMessage: el buffer vive en nuestra pila y tiene que
            // seguir ahi mientras Everything lo copia.
            //
            // Pero con LIMITE DE TIEMPO. Medido, la llamada vuelve en 0,3-0,8 ms porque
            // Everything solo acepta la pregunta y busca por su cuenta. Aun asi es una
            // llamada sincrona a OTRO proceso: si Everything se cuelga, nuestra ventana
            // se cuelga con el y no hay salida. Un segundo es mil veces lo que tarda, y
            // ABORTIFHUNG corta antes si el sistema ya lo da por colgado.
            nuint resultado = 0;
            LRESULT ok = PInvoke.SendMessageTimeout(
                buzon, PInvoke.WM_COPYDATA,
                (WPARAM)(nuint)(nint)nuestro.Value, (LPARAM)(nint)(&sobre),
                SEND_MESSAGE_TIMEOUT_FLAGS.SMTO_ABORTIFHUNG | SEND_MESSAGE_TIMEOUT_FLAGS.SMTO_NORMAL,
                1000, &resultado);

            return ok != 0 && resultado != 0;
        }
    }

    /// <summary>
    /// Deshace un EVERYTHING_IPC_LISTW. Lo que vuelve son <b>rutas</b>: no se abre ninguno
    /// de esos ficheros ni se mira dentro (SEGURIDAD.md regla 12).
    /// </summary>
    public static List<Entrada> Leer(void* datos, uint bytes)
    {
        List<Entrada> encontrados = [];
        if (datos is null || bytes < CabeceraLista) return encontrados;

        byte* p = (byte*)datos;
        uint cuantos = ((uint*)p)[5];   // numitems

        // El tamano que dice el sobre manda sobre el que dice la cabecera: si no cuadran,
        // el que miente es el de dentro, y leer de mas seria leer memoria de otro.
        if (CabeceraLista + (long)cuantos * TamItem > bytes) return encontrados;

        for (uint i = 0; i < cuantos; i++)
        {
            uint* item = (uint*)(p + CabeceraLista + i * TamItem);
            uint marcas = item[0];
            if (item[1] >= bytes || item[2] >= bytes) continue;

            string nombre = new((char*)(p + item[1]));
            string carpeta = new((char*)(p + item[2]));
            if (nombre.Length == 0) continue;

            string completo = carpeta.Length == 0 ? nombre : Path.Combine(carpeta, nombre);
            encontrados.Add(new Entrada(nombre, completo, EsFichero: true, EsCarpetaDeDisco: (marcas & EsCarpeta) != 0));
        }

        return encontrados;
    }
}
