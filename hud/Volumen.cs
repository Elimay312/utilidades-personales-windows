using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Media.Audio;
using Windows.Win32.Media.Audio.Endpoints;
using Windows.Win32.System.Com;

namespace Hud;

/// <summary>
/// El volumen maestro de la SALIDA (SEGURIDAD.md §3.2).
///
/// <para>
/// Es la misma API publica que usa el control de volumen de la barra de tareas:
/// <c>IAudioEndpointVolume</c> sobre el endpoint <c>eRender</c>. El microfono
/// (<c>eCapture</c>) no se abre nunca y <c>auditar.ps1</c> lo comprueba. Un escalar de
/// 0 a 1 <b>no es audio</b>: no se abre ningun flujo, no hay nada que grabar.
/// </para>
///
/// <para>
/// A diferencia de la isla, aqui si se ESCRIBE, y siempre viene de una tecla que
/// acabas de pulsar. No hay ningun camino que llame a <see cref="Poner"/> desde un
/// temporizador, y asi debe seguir.
/// </para>
/// </summary>
internal static unsafe class Volumen
{
    /// <summary>
    /// La firma que el Panel (panel-de-control) pone en cada cambio de volumen que hace:
    /// <c>kPanelVolumeContext</c> en su <c>system/audio.h</c>. Llega en el aviso de COM como
    /// <c>guidEventContext</c>, asi que reconocerla es leer lo que el sistema ya manda: los
    /// dos procesos siguen sin hablarse.
    /// </summary>
    public static readonly Guid ContextoPanel = new("5B0D7C34-8A41-4C2E-9F3A-612D7E94B01C");

    private static IAudioEndpointVolume? _endpoint;
    private static IMMDeviceEnumerator? _enumerador;
    private static long _siguienteIntento;

    /// <summary>
    /// Lo pone un hilo de COM y lo consume el de UI en el siguiente <see cref="Abrir"/>,
    /// de ahi el <c>volatile</c>.
    /// </summary>
    private static volatile bool _otroDispositivo;

    /// <summary>
    /// El objeto al que COM le avisa de los cambios. <b>Hay que guardarlo en un campo</b>:
    /// si lo recoge el GC, el CCW muere y COM acaba llamando a memoria liberada.
    /// </summary>
    private static Aviso? _aviso;

    /// <summary>
    /// El otro CCW, el que avisa de que has cambiado de altavoces. Mismo motivo para
    /// guardarlo en un campo, y ademas vive en el ENUMERADOR y no en el endpoint: tiene
    /// que sobrevivir justo a lo que anuncia.
    /// </summary>
    private static Cambio? _cambio;

    /// <summary>Si el aviso esta puesto. Si se cae, alguien tiene que volver a ponerlo.</summary>
    public static bool Escuchando => _aviso is not null;

    /// <summary>
    /// Pide que COM avise a <paramref name="ventana"/> con <paramref name="mensaje"/>
    /// cada vez que cambie el volumen, lo cambie quien lo cambie.
    ///
    /// <para>
    /// Sustituye al sondeo de 250 ms que tuvo este proyecto hasta aqui. El sondeo
    /// funcionaba, pero llegaba tarde hasta un cuarto de segundo y preguntaba cuatro
    /// veces por segundo para nada el 99% del tiempo.
    /// </para>
    /// </summary>
    public static void Escuchar(HWND ventana, uint mensaje)
    {
        if (_aviso is not null) return;

        try
        {
            IAudioEndpointVolume endpoint = Abrir();

            // Y que avise tambien de que has cambiado de altavoces, que es lo que este
            // fichero daba por hecho que notaria solo y no notaba (SEGURIDAD.md 3.2).
            if (_cambio is null)
            {
                Cambio c = new(ventana, mensaje);
                _enumerador!.RegisterEndpointNotificationCallback(c);
                _cambio = c;
            }

            Aviso a = new(ventana, mensaje);
            endpoint.RegisterControlChangeNotify(a);
            _aviso = a;
        }
        catch
        {
            Caido();
        }
    }

    /// <summary>
    /// Devuelve el aviso. Importa hacerlo en el cierre limpio: dejar un CCW registrado
    /// en un endpoint que sigue vivo es pedirle a COM que llame a un objeto muerto.
    /// </summary>
    public static void Callar()
    {
        SoltarEndpoint();

        if (_cambio is not null)
        {
            try
            {
                _enumerador?.UnregisterEndpointNotificationCallback(_cambio);
            }
            catch
            {
                // Si el enumerador ya no esta, el registro se fue con el.
            }

            _cambio = null;
        }

        _enumerador = null;
    }

    /// <summary>
    /// Suelta el endpoint y su aviso de volumen, y nada mas. El aviso de dispositivo se
    /// queda puesto a proposito: vive en el enumerador para sobrevivir a esto.
    /// </summary>
    private static void SoltarEndpoint()
    {
        if (_aviso is not null)
        {
            try
            {
                _endpoint?.UnregisterControlChangeNotify(_aviso);
            }
            catch
            {
                // Si el endpoint ya no esta, el registro se fue con el.
            }

            _aviso = null;
        }

        _endpoint = null;
    }

    /// <summary>
    /// Lo que COM llama cuando cambia el volumen. <b>Llega en un hilo del pool</b>, asi
    /// que aqui no se toca nada: se le manda un mensaje a NUESTRA ventana y el hilo de
    /// UI lee el estado cuando le toque. Es lo que hace la isla con los eventos de
    /// medios, y por la misma razon.
    /// </summary>
    private sealed class Aviso(HWND ventana, uint mensaje) : IAudioEndpointVolumeCallback
    {
        public void OnNotify(AUDIO_VOLUME_NOTIFICATION_DATA* datos)
        {
            // wParam 1: lo cambio el deslizador del Panel, que ya ensena el nivel.
            bool delPanel = datos != null && datos->guidEventContext == ContextoPanel;
            PInvoke.PostMessage(ventana, mensaje, (nuint)(delPanel ? 1 : 0), default);
        }
    }

    /// <summary>
    /// Lo que COM llama cuando cambia la lista de dispositivos de audio. De los cinco
    /// metodos de la interfaz <b>solo uno hace algo</b> (SEGURIDAD.md 3.2).
    ///
    /// <para>
    /// <b>El id que llega no se mira nunca.</b> Al aviso no se le pregunta CUAL es el
    /// nuevo predeterminado, solo se usa que HAY uno nuevo; el dispositivo se vuelve a
    /// pedir por el mismo <c>GetDefaultAudioEndpoint</c> de siempre. El HUD no tiene
    /// inventario de dispositivos, igual que no tiene inventario de ventanas, y
    /// <c>auditar.ps1</c> lo comprueba.
    /// </para>
    /// </summary>
    private sealed class Cambio(HWND ventana, uint mensaje) : IMMNotificationClient
    {
        public void OnDefaultDeviceChanged(EDataFlow flujo, ERole rol, PCWSTR _)
        {
            // Solo la SALIDA, y solo el rol que abrimos. Windows manda un aviso POR ROL:
            // medido, eConsole y eMultimedia llegan con 8 ms de diferencia, asi que sin
            // este filtro se soltaria el endpoint dos veces por cada cambio.
            if (flujo != EDataFlow.eRender || rol != ERole.eMultimedia) return;

            // Igual que Aviso: llega en un hilo del pool, asi que aqui no se toca el
            // endpoint. Se marca y se avisa a NUESTRA ventana; lo suelta el hilo de UI.
            _otroDispositivo = true;
            PInvoke.PostMessage(ventana, mensaje, default, default);
        }

        public void OnDeviceAdded(PCWSTR id) { }

        public void OnDeviceRemoved(PCWSTR id) { }

        public void OnDeviceStateChanged(PCWSTR id, DEVICE_STATE estado) { }

        public void OnPropertyValueChanged(PCWSTR id, PROPERTYKEY clave) { }
    }

    /// <summary>El nivel en porcentaje y si esta silenciado, o null si no se pudo leer.</summary>
    public static (int Porcentaje, bool Mudo)? Leer()
    {
        try
        {
            IAudioEndpointVolume v = Abrir();
            v.GetMasterVolumeLevelScalar(out float nivel);
            v.GetMute(out Windows.Win32.Foundation.BOOL mudo);
            return ((int)MathF.Round(Math.Clamp(nivel, 0f, 1f) * 100f), (bool)mudo);
        }
        catch
        {
            Caido();
            return null;
        }
    }

    /// <summary>Pone el nivel. Solo se llama desde una tecla.</summary>
    public static void Poner(int porcentaje)
    {
        try
        {
            Abrir().SetMasterVolumeLevelScalar(Math.Clamp(porcentaje, 0, 100) / 100f, null);
        }
        catch
        {
            Caido();
        }
    }

    public static void Silenciar(bool mudo)
    {
        try
        {
            Abrir().SetMute(mudo, null);
        }
        catch
        {
            Caido();
        }
    }

    /// <summary>
    /// Algo de COM ha fallado. Se tira todo, pero NO se reabre en la siguiente llamada:
    /// montar el enumerador de COM no es gratis, y si el fallo es permanente reintentarlo
    /// sin pausa cuesta mas que la funcion entera. Es lo que la isla anoto en su medidor
    /// de pico. La red de seguridad de HudWindow lo reintenta cada 2 s.
    ///
    /// <para>
    /// <b>Este no es el camino del cambio de altavoces</b>, aunque este fichero lo creyo
    /// durante seis commits. Medido: cambiar de dispositivo no hace fallar a nadie --
    /// 47 muestras, cero excepciones -- asi que aqui no se llegaba nunca y el HUD se
    /// quedaba pegado al dispositivo viejo. De eso avisa ahora <see cref="Cambio"/>.
    /// </para>
    /// </summary>
    private static void Caido()
    {
        SoltarEndpoint();

        // Si COM falla puede haberse caido el servicio de audio entero, y con el el
        // enumerador. Se tira tambien, para que la proxima vuelta lo monte de cero y
        // vuelva a registrar los dos avisos: un enumerador muerto dejaria al HUD sordo a
        // los cambios de dispositivo para siempre, y eso no se nota hasta que molesta.
        _cambio = null;
        _enumerador = null;
        _siguienteIntento = Environment.TickCount64 + 2000;
    }

    private static IAudioEndpointVolume Abrir()
    {
        // Cambiar de dispositivo NO invalida el endpoint viejo: sigue contestando, y
        // contesta del dispositivo ANTERIOR. Medido con una sonda que dejo uno abierto y
        // cambio el predeterminado: 47 muestras, cero excepciones, y el viejo diciendo
        // 38% cuando el real era 100%. Hay que soltarlo a mano, y se hace aqui porque es
        // el sitio por donde pasan Leer, Poner y Silenciar: una guarda en vez de tres.
        //
        // ponytail: el aviso de volumen se vuelve a registrar cuando pase la red de los
        // 2 s (HudWindow.Red), no en el acto. El techo es ese: durante <=2 s despues de
        // cambiar de altavoces, un cambio de volumen hecho por OTRA app no saca el HUD.
        // Las teclas van bien desde el primer instante, porque pasan por aqui. Subirlo
        // seria guardar ventana y mensaje en estaticos y reregistrar aqui mismo.
        if (_otroDispositivo)
        {
            _otroDispositivo = false;
            SoltarEndpoint();
        }

        if (_endpoint is not null) return _endpoint;
        if (Environment.TickCount64 < _siguienteIntento) throw new InvalidOperationException("en espera");

        _enumerador ??= (IMMDeviceEnumerator)new MMDeviceEnumerator();
        // eRender es la SALIDA. La otra direccion no aparece en este proyecto y la
        // auditoria lo comprueba.
        _enumerador.GetDefaultAudioEndpoint(EDataFlow.eRender, ERole.eMultimedia, out IMMDevice d);

        Guid iid = typeof(IAudioEndpointVolume).GUID;
        d.Activate(&iid, CLSCTX.CLSCTX_ALL, null, out object v);
        _endpoint = (IAudioEndpointVolume)v;
        return _endpoint;
    }

    // --- el paso, que es logica pura ---------------------------------------------------

    /// <summary>
    /// A donde va el volumen al pulsar una tecla. Se trabaja en enteros y se salta al
    /// siguiente multiplo del paso, no se suma: asi el nivel se alinea a la rejilla en
    /// la primera pulsacion aunque otra app lo hubiera dejado en un 37%.
    ///
    /// <para>
    /// Devuelve el porcentaje nuevo, que puede ser el mismo si ya estabas en el tope.
    /// Quien llama distingue ese caso y da el squash en vez de mover la barra.
    /// </para>
    /// </summary>
    public static int Paso(int actual, int paso, bool arriba)
    {
        actual = Math.Clamp(actual, 0, 100);
        paso = Math.Clamp(paso, 1, 25);

        int destino = arriba
            ? (actual / paso + 1) * paso
            : (actual % paso == 0 ? actual - paso : actual / paso * paso);

        return Math.Clamp(destino, 0, 100);
    }

    /// <summary>
    /// Lo unico de este fichero que se rompe en silencio. Un paso que se atasca cerca
    /// del tope no falla: simplemente deja de subir el ultimo escalon, y eso se nota
    /// tarde y mal.
    /// </summary>
    public static void SelfCheck()
    {
        // Lo normal, en los dos sentidos.
        Assert(Paso(50, 2, true) == 52, "50 sube a 52");
        Assert(Paso(50, 2, false) == 48, "50 baja a 48");

        // Desalineado: la primera pulsacion alinea a la rejilla en vez de sumar.
        Assert(Paso(37, 5, true) == 40, "37 con paso 5 sube a 40, no a 42");
        Assert(Paso(37, 5, false) == 35, "37 con paso 5 baja a 35");

        // Los topes. El de arriba es el que importa: con 99 y paso 2, sumar daria 101 y
        // quedarse en 99 seria no llegar nunca al 100.
        Assert(Paso(99, 2, true) == 100, "99 llega al 100 y no se queda corto");
        Assert(Paso(100, 2, true) == 100, "100 no se pasa");
        Assert(Paso(1, 2, false) == 0, "1 llega al 0");
        Assert(Paso(0, 2, false) == 0, "0 no baja de cero");

        // Y no se atasca: desde cualquier sitio, subiendo siempre se llega al 100 y
        // bajando al 0, en un numero razonable de pulsaciones. Esto es lo que caza un
        // paso que devuelve el mismo valor a mitad de camino.
        foreach (int paso in (int[])[1, 2, 3, 7, 10, 25])
        {
            for (int inicio = 0; inicio <= 100; inicio++)
            {
                int v = inicio, n = 0;
                while (v < 100 && n++ < 200) v = Paso(v, paso, arriba: true);
                Assert(v == 100, $"subiendo desde {inicio} con paso {paso} se atasco en {v}");

                v = inicio; n = 0;
                while (v > 0 && n++ < 200) v = Paso(v, paso, arriba: false);
                Assert(v == 0, $"bajando desde {inicio} con paso {paso} se atasco en {v}");
            }
        }

        Console.WriteLine("[hud] paso de volumen: 8 casos + 1212 recorridos OK");
    }

    private static void Assert(bool condicion, string queFallo)
    {
        if (!condicion) throw new InvalidOperationException($"volumen: {queFallo}");
    }
}
