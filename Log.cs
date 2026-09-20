using System.Diagnostics;

namespace QuickLook;

/// <summary>
/// La traza de diagnostico, detras de <c>QL_LOG=1</c>.
///
/// <para>
/// <b>Con marca de tiempo, y no es un adorno.</b> En este proyecto el metodo de prueba se ha
/// equivocado mas veces que el codigo, y tres de esas veces la duda era la misma: si una
/// linea del log habia pasado antes o despues de lo que la sonda acababa de hacer. Sin
/// milisegundos no se puede contestar, y se acaba teorizando sobre el orden de los hechos en
/// vez de leerlo.
/// </para>
///
/// <para>
/// No escribe nada a disco: sale por la consola, que es donde la sonda la recoge. La regla 10
/// sigue intacta.
/// </para>
/// </summary>
internal static class Log
{
    /// <summary>Si la traza esta encendida. Se lee una vez.</summary>
    public static readonly bool On = Environment.GetEnvironmentVariable("QL_LOG") == "1";

    private static readonly Stopwatch Clock = Stopwatch.StartNew();

    public static void Line(string message)
    {
        if (On) Console.WriteLine($"[{Clock.ElapsedMilliseconds,7} ms] {message}");
    }
}
