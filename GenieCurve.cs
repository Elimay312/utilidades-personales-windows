using System.Globalization;

namespace Dock;

/// <summary>Un rectángulo en píxeles de pantalla.</summary>
internal readonly record struct Box(float Left, float Top, float Right, float Bottom)
{
    public float Width => Right - Left;
    public float Height => Bottom - Top;
    public float CenterX => (Left + Right) * 0.5f;
    public float HalfWidth => Width * 0.5f;
}

/// <summary>
/// La deformación del efecto genio: una ventana que se derrite hacia el icono del dock.
///
/// Misma disciplina que la curva de magnificación: se mapean los <b>bordes</b> de cada
/// franja y la escala se deriva de la diferencia, en vez de escalar centros. Así las
/// franjas encajan sin huecos ni solapes por construcción.
///
/// Dos movimientos superpuestos, tal como lo formula el plugin Magic Lamp de compiz:
///
/// <b>En vertical, dos fases.</b> Primero la ventana se estira desde su borde superior
/// hasta alcanzar el icono; después todo el conjunto se absorbe dentro de él.
///
/// <b>En horizontal, el cuello del embudo.</b> El ancho de cada franja no depende de su
/// posición en la ventana sino de <i>hasta dónde ha bajado</i>: cuanto más cerca del
/// icono, más estrecha. La transición entre un ancho y otro es una <b>sigmoide</b>, y de
/// ahí sale la silueta de cuello que hace que se reconozca como el genio y no como un
/// simple encogimiento.
///
/// <para>
/// El lenguaje de expresiones de Composition no tiene <c>Exp</c> — el mismo motivo por
/// el que en la magnificación se eligió el coseno elevado sobre la gaussiana. Aquí la
/// sigmoide sí hace falta, así que se emula con <c>Pow(e, x)</c>, que sí existe.
/// </para>
/// </summary>
internal readonly struct GenieCurve
{
    /// <summary>Dónde acaba la fase de estirado y empieza la de absorción.</summary>
    public const float StretchPhaseEnd = 0.45f;

    /// <summary>Pendiente de la sigmoide. 10 es el valor de compiz.</summary>
    public const float SigmoidSlope = 10f;

    public GenieCurve(Box window, Box target, int slices)
    {
        Window = window;
        Target = target;
        Slices = Math.Max(1, slices);
    }

    public Box Window { get; }
    public Box Target { get; }
    public int Slices { get; }

    /// <summary>Altura de una franja en la captura, sin deformar.</summary>
    public float SliceHeight => Window.Height / Slices;

    /// <summary>Sigmoide cruda, sin normalizar.</summary>
    public static float RawSigmoid(float t)
        => 1f / (1f + MathF.Exp(-SigmoidSlope * (t - 0.5f)));

    /// <summary>
    /// Sigmoide normalizada para que valga exactamente 0 en t=0 y 1 en t=1. Sin
    /// normalizar, la ventana en reposo ya saldría un poco encogida.
    /// </summary>
    public static float Sigmoid(float t)
    {
        t = Math.Clamp(t, 0f, 1f);
        float low = RawSigmoid(0f);
        return (RawSigmoid(t) - low) / (RawSigmoid(1f) - low);
    }

    /// <summary>Suavizado clásico, para que las fases no arranquen ni paren de golpe.</summary>
    public static float SmoothStep(float edge0, float edge1, float value)
    {
        if (edge1 <= edge0) return value >= edge1 ? 1f : 0f;
        float t = Math.Clamp((value - edge0) / (edge1 - edge0), 0f, 1f);
        return t * t * (3f - 2f * t);
    }

    /// <summary>Avance de la fase de estirado, de 0 a 1.</summary>
    public float StretchProgress(float p) => SmoothStep(0f, StretchPhaseEnd, p);

    /// <summary>Avance de la fase de absorción, de 0 a 1.</summary>
    public float AbsorbProgress(float p) => SmoothStep(StretchPhaseEnd, 1f, p);

    /// <summary>
    /// Y de pantalla del punto que está a la altura relativa <paramref name="v"/> de la
    /// ventana (0 arriba, 1 abajo), en el instante <paramref name="p"/>.
    /// </summary>
    public float VerticalAt(float v, float p)
    {
        float rest = Window.Top + v * Window.Height;

        // Fase 1: el borde superior se queda y el inferior baja hasta el icono.
        float stretched = Window.Top + v * (Target.Bottom - Window.Top);

        // Fase 2: el conjunto entero cabe dentro del icono.
        float absorbed = Target.Top + v * Target.Height;

        float formed = float.Lerp(rest, stretched, StretchProgress(p));
        return float.Lerp(formed, absorbed, AbsorbProgress(p));
    }

    /// <summary>
    /// El cuello del embudo: cuánto se ha estrechado el material que ha llegado hasta
    /// la altura <paramref name="y"/>. Devuelve 1 arriba del todo (ancho de ventana) y
    /// 0 en el icono (ancho de icono).
    /// </summary>
    public float NeckAt(float y)
    {
        float span = Target.Bottom - Window.Top;
        if (MathF.Abs(span) < 0.001f) return 1f;

        return Sigmoid((Target.Bottom - y) / span);
    }

    /// <summary>Extremos horizontales de la franja <paramref name="index"/>.</summary>
    public (float Left, float Right) HorizontalAt(int index, float p)
    {
        float v = (index + 0.5f) / Slices;
        float neck = NeckAt(VerticalAt(v, p));

        float half = float.Lerp(Window.HalfWidth, Target.HalfWidth, 1f - neck);
        float center = float.Lerp(Window.CenterX, Target.CenterX, 1f - neck);

        // La deformación horizontal entra con la fase de estirado: en reposo la malla
        // tiene que coincidir con la captura, píxel a píxel.
        float shape = StretchProgress(p);
        half = float.Lerp(Window.HalfWidth, half, shape);
        center = float.Lerp(Window.CenterX, center, shape);

        // Y la de absorción la lleva hasta el icono EXACTAMENTE. Sin esto queda un
        // residuo: la sigmoide se acerca a cero pero no llega, así que al final la
        // franja seguía siendo unos píxeles más ancha que el icono. Lo detectó el
        // self-check, no el ojo.
        float absorb = AbsorbProgress(p);
        half = float.Lerp(half, Target.HalfWidth, absorb);
        center = float.Lerp(center, Target.CenterX, absorb);

        return (center - half, center + half);
    }

    public float TopOf(int index, float p) => VerticalAt((float)index / Slices, p);

    public float BottomOf(int index, float p) => VerticalAt((index + 1f) / Slices, p);
}

/// <summary>
/// Comprobación ejecutable de la deformación. Sin framework de tests: si la curva se
/// rompe, esto falla ruidosamente.
/// </summary>
internal static class GenieSelfCheck
{
    public static void Run()
    {
        Box window = new(200f, 100f, 1400f, 900f);
        Box target = new(940f, 1010f, 1000f, 1070f);
        GenieCurve curve = new(window, target, slices: 40);

        EnReposoCoincideConLaCaptura(curve);
        LasFranjasEncajan(curve);
        AlFinalCabeEnElIcono(curve);
        LaSigmoideEstaNormalizada();

        Console.WriteLine("[check] curva del genio: OK");
    }

    /// <summary>
    /// En p=0 la malla tiene que ser la captura sin tocar. Si no, al empezar la
    /// animación se vería un salto.
    /// </summary>
    private static void EnReposoCoincideConLaCaptura(GenieCurve curve)
    {
        for (int i = 0; i < curve.Slices; i++)
        {
            float top = curve.TopOf(i, 0f);
            float esperadoTop = curve.Window.Top + i * curve.SliceHeight;
            Assert(MathF.Abs(top - esperadoTop) < 0.01f,
                $"en reposo la franja {i} no está en su sitio: {top} en vez de {esperadoTop}");

            (float left, float right) = curve.HorizontalAt(i, 0f);
            Assert(MathF.Abs(left - curve.Window.Left) < 0.01f
                && MathF.Abs(right - curve.Window.Right) < 0.01f,
                $"en reposo la franja {i} no tiene el ancho de la ventana: {left}..{right}");
        }
    }

    /// <summary>
    /// La propiedad central: el borde inferior de una franja es el superior de la
    /// siguiente, en todo instante. Si falla, se ven costuras o solapes.
    /// </summary>
    private static void LasFranjasEncajan(GenieCurve curve)
    {
        foreach (float p in new[] { 0f, 0.15f, 0.3f, 0.45f, 0.6f, 0.8f, 1f })
        {
            for (int i = 0; i < curve.Slices - 1; i++)
            {
                float abajo = curve.BottomOf(i, p);
                float arriba = curve.TopOf(i + 1, p);
                Assert(MathF.Abs(abajo - arriba) < 0.01f,
                    $"hueco entre las franjas {i} y {i + 1} en p={p}: {abajo} vs {arriba}");
            }

            // Y nunca hacia atrás: si la Y dejara de crecer, las franjas se cruzarían.
            for (int i = 0; i < curve.Slices; i++)
            {
                Assert(curve.BottomOf(i, p) >= curve.TopOf(i, p) - 0.01f,
                    $"la franja {i} salió del revés en p={p}");
            }
        }
    }

    /// <summary>Al acabar, todo tiene que estar dentro del icono.</summary>
    private static void AlFinalCabeEnElIcono(GenieCurve curve)
    {
        for (int i = 0; i < curve.Slices; i++)
        {
            float top = curve.TopOf(i, 1f);
            float bottom = curve.BottomOf(i, 1f);
            Assert(top >= curve.Target.Top - 0.5f && bottom <= curve.Target.Bottom + 0.5f,
                $"la franja {i} se sale del icono al final: {top}..{bottom}");

            (float left, float right) = curve.HorizontalAt(i, 1f);
            Assert(left >= curve.Target.Left - 1f && right <= curve.Target.Right + 1f,
                $"la franja {i} es más ancha que el icono al final: {left}..{right}");
        }
    }

    /// <summary>Sin normalizar, la ventana en reposo saldría ya encogida.</summary>
    private static void LaSigmoideEstaNormalizada()
    {
        Assert(MathF.Abs(GenieCurve.Sigmoid(0f)) < 0.0001f, "la sigmoide no vale 0 en 0");
        Assert(MathF.Abs(GenieCurve.Sigmoid(1f) - 1f) < 0.0001f, "la sigmoide no vale 1 en 1");

        float anterior = -1f;
        for (float t = 0f; t <= 1f; t += 0.02f)
        {
            float actual = GenieCurve.Sigmoid(t);
            Assert(actual >= anterior, $"la sigmoide no es monótona en t={t}");
            anterior = actual;
        }
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(
                string.Format(CultureInfo.InvariantCulture, "self-check genio: {0}", message));
    }
}
