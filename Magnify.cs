using System.Globalization;

namespace Dock;

/// <summary>
/// Una ranura del dock: cuánto espacio ocupa y cuánto de ese espacio se pinta.
/// Un icono ocupa su tamaño más la separación; un separador ocupa mucho menos y
/// pinta solo una línea fina.
/// </summary>
internal readonly record struct DockSlot(float Width, float ContentWidth);

/// <summary>
/// La curva de magnificación del dock.
///
/// La regla que lo gobierna todo: <b>la escala se define, la posición se integra</b>.
/// Se define una función de escala s(u) alrededor del cursor, y la posición de cada
/// icono sale de la integral de esa escala, no de escalar posiciones sueltas. Es lo
/// que hace que los iconos nunca se solapen ni dejen huecos, sin corregir nada a
/// mano, y es lo que distingue un dock que se siente bien de uno que resbala.
///
/// Con c = cursor en coordenadas de reposo, R = radio de influencia y M = escala
/// máxima:
///
///   s(u) = 1 + (M-1)·f(t),  t = (u-c)/R,  f(t) = (1+cos(π·t))/2  si |t|≤1, si no 0
///   T(u) = ∫₀ᵘ s = u + (M-1)·R·[ G((u-c)/R) − G(−c/R) ]
///   G(t) = clamp(t,−1,1)/2 + sin(π·clamp(t,−1,1))/(2π)
///
/// Y por elemento se mapean los BORDES, no el centro:
///   Lᵢ = T(izquierda)   Rᵢ = T(derecha)   escalaᵢ = (Rᵢ−Lᵢ)/anchoEnReposo
///
/// Se eligió el coseno elevado y no una gaussiana por dos razones: su derivada es 0
/// en los bordes del radio (así no hay costura donde empieza el efecto) y el lenguaje
/// de expresiones de Composition tiene Sin y Clamp pero no Exp.
///
/// <para>
/// Las ranuras son de ancho VARIABLE. La transferencia integra sobre u y le da igual
/// cómo esté partida la fila, así que soportar separadores estrechos no cuesta nada
/// en la matemática: solo hay que acumular los bordes en vez de multiplicar.
/// </para>
/// </summary>
internal readonly struct DockCurve
{
    private readonly DockSlot[] _slots;

    /// <summary>Bordes acumulados de las ranuras. Tiene un elemento más que _slots.</summary>
    private readonly float[] _edges;

    public DockCurve(IReadOnlyList<DockSlot> slots, float radius, float maxScale)
    {
        _slots = [.. slots];
        _edges = new float[_slots.Length + 1];

        float running = 0f;
        for (int i = 0; i < _slots.Length; i++)
        {
            _edges[i] = running;
            running += _slots[i].Width;
        }
        _edges[^1] = running;

        Radius = radius;
        MaxScale = maxScale;
    }

    /// <summary>Radio de influencia del cursor, en coordenadas de reposo.</summary>
    public float Radius { get; }

    /// <summary>Escala máxima, justo bajo el cursor.</summary>
    public float MaxScale { get; }

    public int Count => _slots?.Length ?? 0;

    /// <summary>Ancho total de la fila en reposo.</summary>
    public float RestWidth => _edges is null || _edges.Length == 0 ? 0f : _edges[^1];

    /// <summary>
    /// Cuánto se ensancha la fila como máximo al magnificarse. El ancho total es
    /// exactamente RestWidth + (M−1)·R mientras el bulto no toque los extremos, así
    /// que el dock no "respira" al mover el ratón por el centro.
    /// </summary>
    public float MaxGrowth => (MaxScale - 1f) * Radius;

    public DockSlot Slot(int index) => _slots[index];

    /// <summary>Principio de la ranura i, en coordenadas de reposo.</summary>
    public float SlotStart(int index) => _edges[index];

    /// <summary>Borde izquierdo de lo que se PINTA en la ranura i, centrado en ella.</summary>
    public float RestLeft(int index)
        => _edges[index] + (_slots[index].Width - _slots[index].ContentWidth) * 0.5f;

    /// <summary>Borde derecho de lo que se pinta en la ranura i.</summary>
    public float RestRight(int index) => RestLeft(index) + _slots[index].ContentWidth;

    /// <summary>Primitiva de la curva. G(−1) = −½ y G(1) = +½ por construcción.</summary>
    public static float G(float t)
    {
        t = Math.Clamp(t, -1f, 1f);
        return t * 0.5f + MathF.Sin(MathF.PI * t) / (2f * MathF.PI);
    }

    /// <summary>
    /// Función de transferencia: dónde acaba el punto u cuando el cursor está en c.
    /// <paramref name="amount"/> va de 0 a 1 y sirve para entrar y salir del hover
    /// sin saltos.
    /// </summary>
    public float Transfer(float u, float cursor, float amount = 1f)
    {
        float k = (MaxScale - 1f) * amount * Radius;
        return u + k * (G((u - cursor) / Radius) - G(-cursor / Radius));
    }

    /// <summary>
    /// Desplazamiento del conjunto para que la fila quede centrada en el ancho dado.
    /// Se recalcula con el cursor porque la fila se ensancha al magnificarse.
    /// </summary>
    public float Origin(float availableWidth, float cursor, float amount = 1f)
        => (availableWidth - Transfer(RestWidth, cursor, amount)) * 0.5f;

    /// <summary>Posición en pantalla del punto u.</summary>
    public float Project(float u, float availableWidth, float cursor, float amount = 1f)
        => Origin(availableWidth, cursor, amount) + Transfer(u, cursor, amount);

    /// <summary>
    /// Invierte la proyección: dado un x de pantalla, devuelve el punto en
    /// coordenadas de REPOSO que cae ahí.
    ///
    /// Hace falta porque el cursor llega en coordenadas distorsionadas mientras que
    /// la curva está definida en coordenadas de reposo. Usar el x de pantalla
    /// directamente como c es exactamente lo que produce el resbalón de los clones
    /// malos del Dock.
    ///
    /// Project es monótona creciente con derivada entre 1 y M, así que basta una
    /// bisección; 40 iteraciones dejan el error por debajo de una millonésima del
    /// ancho.
    /// </summary>
    public float Invert(float screenX, float availableWidth, float amount = 1f)
    {
        float low = 0f;
        float high = RestWidth;

        for (int i = 0; i < 40; i++)
        {
            float mid = (low + high) * 0.5f;
            if (Project(mid, availableWidth, mid, amount) < screenX) low = mid;
            else high = mid;
        }

        return (low + high) * 0.5f;
    }

    /// <summary>Índice de la ranura que contiene esa coordenada de reposo, o -1.</summary>
    public int SlotAt(float restPosition)
    {
        for (int i = 0; i < Count; i++)
        {
            if (restPosition >= _edges[i] && restPosition < _edges[i + 1]) return i;
        }
        return -1;
    }
}

/// <summary>
/// Comprobación ejecutable de las propiedades que tienen que cumplirse siempre.
/// Sin framework de tests: si la curva se rompe, esto falla ruidosamente.
/// </summary>
internal static class MagnifySelfCheck
{
    public static void Run()
    {
        // A propósito con ranuras de anchos DISTINTOS, que es el caso general desde
        // que existen los separadores.
        DockSlot icono = new(80f, 60f);
        DockSlot separador = new(24f, 2f);
        DockCurve curve = new(
            [icono, icono, separador, icono, icono, separador, icono],
            radius: 200f,
            maxScale: 2f);

        float width = curve.RestWidth + curve.MaxGrowth;
        float[] cursores = [0f, 30f, 120f, curve.RestWidth * 0.5f, curve.RestWidth - 40f, curve.RestWidth];

        foreach (float c in cursores)
        {
            SinHuecosNiSolapes(curve, width, c);
            Monotona(curve, width, c);
        }

        AnchoConstanteEnElCentro(curve);
        LaInversionDevuelveElPunto(curve, width);
        LasRanurasSeLocalizan(curve);

        Console.WriteLine("[check] curva de magnificación: OK");
    }

    /// <summary>
    /// Las ranuras particionan la fila: la suma de sus anchos proyectados tiene que
    /// dar exactamente el ancho proyectado total, sin perder ni ganar píxeles. Y lo
    /// que se pinta dentro de cada una nunca invade la siguiente.
    /// </summary>
    private static void SinHuecosNiSolapes(DockCurve curve, float width, float cursor)
    {
        float suma = 0f;
        for (int i = 0; i < curve.Count; i++)
        {
            float inicio = curve.Project(curve.SlotStart(i), width, cursor);
            float fin = curve.Project(curve.SlotStart(i) + curve.Slot(i).Width, width, cursor);
            Assert(fin > inicio, $"la ranura {i} salió de ancho <= 0 (cursor={cursor})");
            suma += fin - inicio;
        }

        float total = curve.Project(curve.RestWidth, width, cursor) - curve.Project(0f, width, cursor);
        Assert(MathF.Abs(suma - total) < 0.01f,
            $"las ranuras no particionan la fila: {suma} vs {total} (cursor={cursor})");

        for (int i = 0; i < curve.Count - 1; i++)
        {
            float derecha = curve.Project(curve.RestRight(i), width, cursor);
            float izquierda = curve.Project(curve.RestLeft(i + 1), width, cursor);
            Assert(izquierda >= derecha - 0.001f,
                $"el elemento {i + 1} se solapa con el {i} (cursor={cursor})");
        }
    }

    /// <summary>T tiene que ser monótona creciente: si no, los iconos se cruzarían.</summary>
    private static void Monotona(DockCurve curve, float width, float cursor)
    {
        float anterior = float.NegativeInfinity;
        for (float u = 0f; u <= curve.RestWidth; u += curve.RestWidth / 200f)
        {
            float actual = curve.Project(u, width, cursor);
            Assert(actual > anterior, $"T no es monótona en u={u} (cursor={cursor})");
            anterior = actual;
        }
    }

    /// <summary>
    /// Mientras el bulto cabe entero dentro de la fila, el ancho total es constante:
    /// el dock no se ensancha ni se encoge al mover el ratón por el centro.
    /// </summary>
    private static void AnchoConstanteEnElCentro(DockCurve curve)
    {
        float esperado = curve.RestWidth + curve.MaxGrowth;

        for (float c = curve.Radius; c <= curve.RestWidth - curve.Radius; c += 10f)
        {
            float ancho = curve.Transfer(curve.RestWidth, c) - curve.Transfer(0f, c);
            Assert(MathF.Abs(ancho - esperado) < 0.01f,
                $"el ancho respira: {ancho} en vez de {esperado} (cursor={c})");
        }
    }

    /// <summary>Invertir la proyección devuelve el punto de partida.</summary>
    private static void LaInversionDevuelveElPunto(DockCurve curve, float width)
    {
        for (float u = 0f; u <= curve.RestWidth; u += 37f)
        {
            float pantalla = curve.Project(u, width, u);
            float vuelta = curve.Invert(pantalla, width);
            Assert(MathF.Abs(vuelta - u) < 0.5f,
                $"la inversión no cuadra: {u} -> {pantalla} -> {vuelta}");
        }
    }

    /// <summary>El centro de cada ranura cae en esa ranura, y no en la de al lado.</summary>
    private static void LasRanurasSeLocalizan(DockCurve curve)
    {
        for (int i = 0; i < curve.Count; i++)
        {
            float centro = curve.SlotStart(i) + curve.Slot(i).Width * 0.5f;
            Assert(curve.SlotAt(centro) == i,
                $"el centro de la ranura {i} se localizó como {curve.SlotAt(centro)}");
        }
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(
                string.Format(CultureInfo.InvariantCulture, "self-check curva: {0}", message));
    }
}
