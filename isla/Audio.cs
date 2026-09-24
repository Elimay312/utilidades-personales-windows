using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Media.Audio;
using Windows.Win32.Media.Audio.Endpoints;
using Windows.Win32.System.Com;
using Windows.Win32.UI.Shell.PropertiesSystem;

namespace Isla;

/// <summary>Una fila del mezclador: de quien es, como se llama su exe, a que nivel esta.</summary>
internal sealed record AppAudio(uint Pid, string Nombre, float Nivel, bool Activa, string Ruta = "");

/// <summary>
/// Lo que la isla saca de la SALIDA de audio (SEGURIDAD.md §3.3): dos numeros y un
/// nombre. El pico, el nivel, y de que dispositivo son los dos.
///
/// <para>
/// <b>Esto no es grabar.</b> <c>GetPeakValue</c> devuelve UN float entre 0 y 1: el pico
/// de la ultima pasada del mezclador. No es una muestra, no es un espectro, no es un
/// fotograma de audio, y de un escalar por pasada no se reconstruye contenido. Lo que
/// si seria grabar es <c>IAudioCaptureClient</c> con loopback, que entrega el buffer de
/// sonido real: eso esta prohibido por la regla 11 y lo comprueba <c>auditar.ps1</c>.
/// </para>
///
/// <para>
/// El dispositivo es el de salida (<c>eRender</c>). El microfono no se abre nunca. Y del
/// dispositivo se lee UNA propiedad, su nombre: no se enumera la lista de dispositivos
/// de esta maquina, que es la otra mitad del corte y la que comprueba el centinela.
/// </para>
///
/// <para>
/// El volumen se LEE, y solo se escribe con la rueda sobre el panel abierto (SEGURIDAD.md
/// s.3.3, enmienda del 23-09-2026). Las teclas siguen siendo del HUD.
/// </para>
/// </summary>
internal static unsafe class Audio
{
    /// <summary>
    /// La firma que el Panel (panel-de-control) pone en cada cambio de volumen que hace:
    /// <c>kPanelVolumeContext</c> en su <c>system/audio.h</c>. Llega en el aviso de COM como
    /// <c>guidEventContext</c>, asi que reconocerla es leer lo que el sistema ya manda: los
    /// dos procesos siguen sin hablarse.
    /// </summary>
    public static readonly Guid ContextoPanel = new("5B0D7C34-8A41-4C2E-9F3A-612D7E94B01C");

    private static IMMDeviceEnumerator? _enumerador;
    private static IAudioMeterInformation? _medidor;
    private static IAudioEndpointVolume? _volumen;

    /// <summary>El nombre del dispositivo, en memoria y mientras dure. Nunca a disco.</summary>
    private static string? _nombre;

    private static long _siguienteIntento;

    /// <summary>
    /// Los dos objetos a los que COM le avisa. <b>Hay que guardarlos en un campo</b>: si
    /// los recoge el GC, el CCW muere y COM acaba llamando a memoria liberada.
    /// </summary>
    private static Nivel? _nivel;

    private static Cambio? _cambio;

    private static HWND _ventana;
    private static uint _msgNivel;
    private static uint _msgDispositivo;

    /// <summary>Si el aviso de nivel esta puesto. Si se cae, alguien tiene que reponerlo.</summary>
    public static bool Escuchando => _nivel is not null;

    /// <summary>
    /// Pide que COM avise a <paramref name="ventana"/> cuando cambie el nivel
    /// (<paramref name="msgNivel"/>) y cuando cambies de altavoces
    /// (<paramref name="msgDispositivo"/>).
    ///
    /// <para>
    /// Sustituye al sondeo de 2 Hz que tuvo este fichero, que estaba anotado aqui mismo
    /// como deuda: <c>GetMasterVolumeLevelScalar</c> cruza al servicio de audio, y
    /// medido a 8 Hz subia la CPU en reposo de 0,42 % a 2,29 % -- mas que todo lo demas
    /// junto. Por eso iba a 2 Hz, y por eso llegaba hasta medio segundo tarde. El aviso
    /// no cuesta nada y no llega tarde.
    /// </para>
    /// </summary>
    public static void Escuchar(HWND ventana, uint msgNivel, uint msgDispositivo)
    {
        (_ventana, _msgNivel, _msgDispositivo) = (ventana, msgNivel, msgDispositivo);
        Enganchar();
    }

    /// <summary>
    /// Pone los dos avisos, o los que falten. Idempotente a proposito: lo llaman el
    /// arranque, la red de seguridad y el cambio de dispositivo.
    /// </summary>
    private static void Enganchar()
    {
        if (_ventana.IsNull) return;

        try
        {
            IAudioEndpointVolume v = AbrirVolumen();

            // El aviso de cambio de dispositivo va en el ENUMERADOR y no en el endpoint:
            // tiene que sobrevivir justo a lo que anuncia.
            if (_cambio is null)
            {
                Cambio c = new(_ventana, _msgDispositivo);
                _enumerador!.RegisterEndpointNotificationCallback(c);
                _cambio = c;
            }

            if (_nivel is null)
            {
                Nivel n = new(_ventana, _msgNivel);
                v.RegisterControlChangeNotify(n);
                _nivel = n;
            }
        }
        catch
        {
            Caido();
        }
    }

    /// <summary>
    /// Devuelve los avisos. Importa en el cierre limpio y al rehacerse la ventana:
    /// dejar un CCW registrado apuntando a una ventana que ya no existe es pedirle a
    /// COM que le mande mensajes a un HWND muerto.
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
        _ventana = default;
    }

    /// <summary>
    /// Has cambiado de altavoces. Lo llama el hilo de UI cuando le llega el mensaje del
    /// aviso, nunca el hilo de COM.
    ///
    /// <para>
    /// <b>El endpoint viejo no se cae solo.</b> Lo midio el HUD con una sonda que dejaba
    /// uno abierto y cambiaba el predeterminado: 47 muestras, cero excepciones, y el
    /// endpoint viejo contestando 38 % cuando el real era 100 %. Aqui pasaba lo mismo
    /// con el medidor de pico: la onda seguia latiendo con el audio del dispositivo
    /// anterior. Los dos se tiraban solo cuando algo lanzaba, y no lanzaba nunca.
    /// </para>
    /// </summary>
    public static void OtroDispositivo()
    {
        SoltarEndpoint();
        _nombre = null;
        Enganchar();
    }

    /// <summary>
    /// El pico actual, o 0 si no se puede leer. Nunca lanza: una onda quieta es un mal
    /// menor comparado con tirar la isla por un cambio de altavoces.
    /// </summary>
    public static float Pico()
    {
        try
        {
            if (_medidor is null)
            {
                if (Environment.TickCount64 < _siguienteIntento) return 0f;
                _medidor = Abrir();
            }

            _medidor.GetPeakValue(out float pico);
            return pico;
        }
        catch
        {
            Caido();
            return 0f;
        }
    }

    /// <summary>
    /// El volumen maestro de la salida, de 0 a 1, o -1 si no se pudo leer.
    /// </summary>
    public static float Volumen()
    {
        try
        {
            AbrirVolumen().GetMasterVolumeLevelScalar(out float v);
            return v;
        }
        catch
        {
            Caido();
            return -1f;
        }
    }

    /// <summary>
    /// La rueda sobre el panel abierto: <paramref name="pasos"/> del 1 % arriba o abajo,
    /// redondeado al 1 % y acotado a [0, 1]. Se lee, se suma y se escribe; no hay nivel
    /// guardado que reponer, asi que lo que cambien las teclas se respeta.
    /// En el hilo de UI, como Volumen(): el endpoint se abrio aqui y es una llamada corta.
    /// </summary>
    public static void Ajustar(int pasos)
    {
        try
        {
            IAudioEndpointVolume v = AbrirVolumen();
            v.GetMasterVolumeLevelScalar(out float nivel);
            v.SetMasterVolumeLevelScalar(Math.Clamp(MathF.Round((nivel + pasos * 0.01f) * 100f) / 100f, 0f, 1f), null);
        }
        catch
        {
            Caido();
        }
    }

    /// <summary>
    /// El mezclador: una fila por app que tiene audio abierto en la salida predeterminada
    /// (SEGURIDAD.md s.3.3, enmienda del 23-09-2026). Solo se llama con el mezclador abierto,
    /// y lo que devuelve lo suelta quien lo pidio al cerrarlo. Las sesiones de un mismo proceso
    /// -- Brave abre una por pestana -- van en una fila; su nivel es el de la primera.
    /// </summary>
    public static IReadOnlyList<AppAudio> Apps()
    {
        List<AppAudio> apps = [];
        try
        {
            foreach ((IAudioSessionControl2 control, uint pid) in Sesiones())
            {
                control.GetState(out AudioSessionState estado);
                if (estado == AudioSessionState.AudioSessionStateExpired) continue;
                if (apps.Exists(a => a.Pid == pid)) continue;

                bool sistema = control.IsSystemSoundsSession().Value == 0;
                string? ruta = sistema ? null : IslaWindow.RutaExe(pid);
                string? nombre = sistema ? "Sistema" : ruta is null ? null : Path.GetFileNameWithoutExtension(ruta);
                if (nombre is null) continue;
                // brave.exe es Brave: la mayuscula y nada mas, que el nombre es el del exe.
                nombre = char.ToUpperInvariant(nombre[0]) + nombre[1..];

                ((ISimpleAudioVolume)control).GetMasterVolume(out float nivel);
                apps.Add(new AppAudio(pid, nombre, nivel, estado == AudioSessionState.AudioSessionStateActive, ruta ?? ""));
            }
        }
        catch
        {
            Caido();
        }

        // Las que suenan primero, y el resto por nombre: la lista no salta de orden cada segundo.
        apps.Sort((a, b) => a.Activa != b.Activa ? (a.Activa ? -1 : 1) : string.Compare(a.Nombre, b.Nombre, StringComparison.OrdinalIgnoreCase));
        return apps;
    }

    /// <summary>
    /// El nivel de una app, detras de un gesto sobre su fila y solo entonces. A todas sus
    /// sesiones, que para ti son una sola fila.
    /// </summary>
    public static void AjustarApp(uint pid, float nivel)
    {
        try
        {
            foreach ((IAudioSessionControl2 control, uint suyo) in Sesiones())
                if (suyo == pid) ((ISimpleAudioVolume)control).SetMasterVolume(Math.Clamp(nivel, 0f, 1f), null);
        }
        catch
        {
            Caido();
        }
    }

    private static List<(IAudioSessionControl2, uint)> Sesiones()
    {
        List<(IAudioSessionControl2, uint)> todas = [];
        Guid iid = typeof(IAudioSessionManager2).GUID;
        Salida().Activate(&iid, CLSCTX.CLSCTX_ALL, null, out object gestor);
        IAudioSessionEnumerator lista = ((IAudioSessionManager2)gestor).GetSessionEnumerator();
        lista.GetCount(out int n);
        for (int i = 0; i < n; i++)
        {
            lista.GetSession(i, out IAudioSessionControl s);
            var control = (IAudioSessionControl2)s;
            control.GetProcessId(out uint pid);
            todas.Add((control, pid));
        }
        return todas;
    }

    /// <summary>
    /// Como se llama el dispositivo por el que esta saliendo el sonido, o cadena vacia
    /// si no se pudo leer.
    ///
    /// <para>
    /// Es <b>una</b> propiedad del endpoint que ya teniamos abierto, la misma que ensena
    /// el control de volumen de Windows. No se enumera nada: la isla no sabe ni puede
    /// saber que otras salidas tiene esta maquina (SEGURIDAD.md §3.3).
    /// </para>
    /// </summary>
    public static string Dispositivo()
    {
        if (_nombre is not null) return _nombre;

        try
        {
            Salida().OpenPropertyStore(STGM.STGM_READ, out IPropertyStore almacen);
            PROPERTYKEY clave = PInvoke.PKEY_Device_FriendlyName;
            almacen.GetValue(&clave, out Windows.Win32.System.Com.StructuredStorage.PROPVARIANT valor);

            _nombre = valor.Anonymous.Anonymous.Anonymous.pwszVal.ToString();
            PInvoke.PropVariantClear(ref valor);
            return _nombre;
        }
        catch
        {
            Caido();
            return "";
        }
    }

    /// <summary>
    /// Lo que COM llama cuando cambia el nivel. <b>Llega en un hilo del pool</b>, asi que
    /// aqui no se toca nada: se le manda un mensaje a NUESTRA ventana y el hilo de UI lo
    /// lee cuando le toque. Es lo mismo que hace <see cref="Medios"/> con los eventos de
    /// reproduccion, y por la misma razon.
    /// </summary>
    private sealed class Nivel(HWND ventana, uint mensaje) : IAudioEndpointVolumeCallback
    {
        public void OnNotify(AUDIO_VOLUME_NOTIFICATION_DATA* datos)
        {
            // wParam 1: lo cambio el deslizador del Panel, que ya ensena el nivel.
            bool delPanel = datos != null && datos->guidEventContext == ContextoPanel;
            PInvoke.PostMessage(ventana, mensaje, (nuint)(delPanel ? 1 : 0), default);
        }
    }

    /// <summary>
    /// Lo que COM llama cuando cambia la lista de dispositivos. De los cinco metodos
    /// <b>solo uno hace algo</b> (SEGURIDAD.md §3.3).
    ///
    /// <para>
    /// El id que llega <b>no se mira</b>. Al aviso no se le pregunta cual es el nuevo
    /// predeterminado: se usa solo que hay uno nuevo, y el dispositivo se vuelve a pedir
    /// por el mismo <c>GetDefaultAudioEndpoint</c> de siempre.
    /// </para>
    /// </summary>
    private sealed class Cambio(HWND ventana, uint mensaje) : IMMNotificationClient
    {
        public void OnDefaultDeviceChanged(EDataFlow flujo, ERole rol, PCWSTR _)
        {
            // Solo la SALIDA y solo el rol que abrimos. Windows manda un aviso POR ROL:
            // medido en el HUD, eConsole y eMultimedia llegan con 8 ms de diferencia, y
            // sin este filtro la isla avisaria dos veces por cada cambio.
            if (flujo != EDataFlow.eRender || rol != ERole.eMultimedia) return;

            PInvoke.PostMessage(ventana, mensaje, default, default);
        }

        public void OnDeviceAdded(PCWSTR id) { }

        public void OnDeviceRemoved(PCWSTR id) { }

        public void OnDeviceStateChanged(PCWSTR id, DEVICE_STATE estado) { }

        public void OnPropertyValueChanged(PCWSTR id, PROPERTYKEY clave) { }
    }

    /// <summary>
    /// Suelta el endpoint, el medidor y el aviso de nivel. El aviso de dispositivo NO se
    /// toca: vive en el enumerador para sobrevivir a esto.
    /// </summary>
    private static void SoltarEndpoint()
    {
        if (_nivel is not null)
        {
            try
            {
                _volumen?.UnregisterControlChangeNotify(_nivel);
            }
            catch
            {
                // Si el endpoint ya no esta, el registro se fue con el.
            }

            _nivel = null;
        }

        _volumen = null;
        _medidor = null;
    }

    /// <summary>
    /// Algo de COM ha fallado. Se tira todo, pero NO se reabre en la siguiente llamada:
    /// montar el enumerador no es gratis y <see cref="Pico"/> se llama veinte veces por
    /// segundo. Si el fallo es permanente, reabrirlo cada vez cuesta mas que la funcion
    /// entera.
    ///
    /// <para>
    /// <b>Este no es el camino del cambio de altavoces</b>, aunque este fichero lo creyera
    /// desde el primer dia: cambiar de dispositivo no hace fallar a nadie. De eso avisa
    /// <see cref="Cambio"/>.
    /// </para>
    /// </summary>
    private static void Caido()
    {
        SoltarEndpoint();
        _nombre = null;

        // Si COM falla puede haberse caido el servicio de audio entero, y con el el
        // enumerador. Se tira tambien para que la proxima vuelta lo monte de cero y
        // vuelva a registrar los dos avisos.
        _cambio = null;
        _enumerador = null;
        _siguienteIntento = Environment.TickCount64 + 2000;
    }

    private static IMMDevice Salida()
    {
        _enumerador ??= (IMMDeviceEnumerator)new MMDeviceEnumerator();
        // eRender es la SALIDA. La otra direccion no aparece en este proyecto y la
        // auditoria lo comprueba.
        _enumerador.GetDefaultAudioEndpoint(EDataFlow.eRender, ERole.eMultimedia, out IMMDevice d);
        return d;
    }

    private static IAudioEndpointVolume AbrirVolumen()
    {
        if (_volumen is not null) return _volumen;

        Guid iid = typeof(IAudioEndpointVolume).GUID;
        Salida().Activate(&iid, CLSCTX.CLSCTX_ALL, null, out object v);
        _volumen = (IAudioEndpointVolume)v;
        return _volumen;
    }

    private static IAudioMeterInformation Abrir()
    {
        Guid iid = typeof(IAudioMeterInformation).GUID;
        Salida().Activate(&iid, CLSCTX.CLSCTX_ALL, null, out object medidor);
        return (IAudioMeterInformation)medidor;
    }
}
