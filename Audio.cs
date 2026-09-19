using Windows.Win32;
using Windows.Win32.Media.Audio;
using Windows.Win32.Media.Audio.Endpoints;
using Windows.Win32.System.Com;

namespace Isla;

/// <summary>
/// El medidor de pico de la SALIDA de audio (SEGURIDAD.md §3.3).
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
/// El dispositivo es el de salida (<c>eRender</c>). El microfono no se abre nunca.
/// </para>
/// </summary>
internal static unsafe class Audio
{
    private static IAudioMeterInformation? _medidor;
    private static long _siguienteIntento;

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
            // Cambiar de dispositivo de salida invalida el medidor. Se tira, pero NO se
            // reabre en la siguiente lectura: montar el enumerador de COM no es gratis y
            // esto se llama ocho veces por segundo. Si el fallo es permanente, reabrirlo
            // cada vez cuesta mas que la funcion entera.
            _medidor = null;
            _siguienteIntento = Environment.TickCount64 + 2000;
            return 0f;
        }
    }

    private static IAudioMeterInformation Abrir()
    {
        IMMDeviceEnumerator enumerador = (IMMDeviceEnumerator)new MMDeviceEnumerator();
        // eRender es la SALIDA. La otra direccion no aparece en este proyecto y la
        // auditoria lo comprueba.
        enumerador.GetDefaultAudioEndpoint(
            EDataFlow.eRender,
            ERole.eMultimedia,
            out IMMDevice dispositivo);

        Guid iid = typeof(IAudioMeterInformation).GUID;
        dispositivo.Activate(&iid, CLSCTX.CLSCTX_ALL, null, out object medidor);
        return (IAudioMeterInformation)medidor;
    }
}
