namespace Isla;

/// <summary>
/// Convierte los deltas de WM_MOUSEWHEEL en unidades enteras, proporcionales al delta real.
/// La rueda manda 120 por muesca; el touchpad (PTP) manda muchos deltas sueltos de cualquier
/// tamano. Antes se hacia <c>delta / 120</c> entero: el touchpad perdia los pequenos y contaba
/// muescas enteras con los grandes, y un gesto corto se llevaba medio volumen.
///
/// <para>
/// Solo guarda el decimal que sobra, nunca un nivel: quien la usa lee, suma y escribe. El
/// decimal se tira si pasan <see cref="OlvidoMs"/> sin eventos, mirado en el evento siguiente
/// y no con un temporizador (SEGURIDAD.md s.3.3).
/// </para>
/// </summary>
internal sealed class Rueda
{
    // --- la sensibilidad se ajusta aqui ---
    /// <summary>Unidades por muesca de rueda (120 de delta). En el volumen, puntos de %.</summary>
    public double PorMuesca = 2.0;
    /// <summary>Multiplica <see cref="PorMuesca"/> cuando el delta es del touchpad.</summary>
    public double SensibilidadTouchpad = 0.5;
    /// <summary>
    /// El touchpad con desplazamiento natural manda delta negativo al subir los dedos. Con esto,
    /// dedos arriba = mas, como la rueda hacia arriba.
    /// </summary>
    public bool InvertirTouchpad = true;
    /// <summary>Sin eventos este rato, el decimal acumulado se olvida.</summary>
    public long OlvidoMs = 400;

    private double _resto;
    private long _ultimo;

    /// <summary>Las unidades enteras que toca aplicar con este delta; el decimal se guarda.</summary>
    public int Pasos(short delta) => Pasos(delta, Environment.TickCount64);

    internal int Pasos(short delta, long ahora)
    {
        if (ahora - _ultimo > OlvidoMs) _resto = 0;
        _ultimo = ahora;

        // ponytail: heuristica, un evento del touchpad que caiga en multiplo de 120 cuenta como
        // rueda. Si se nota, distinguir por dispositivo con Raw Input.
        double factor = delta % 120 != 0 ? SensibilidadTouchpad * (InvertirTouchpad ? -1 : 1) : 1.0;
        _resto += delta / 120.0 * PorMuesca * factor;

        // Trunca hacia cero en los dos sentidos; al cambiar de sentido el resto se come solo.
        int enteros = (int)_resto;
        _resto -= enteros;
        return enteros;
    }
}
