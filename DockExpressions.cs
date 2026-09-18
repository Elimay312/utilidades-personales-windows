using System.Globalization;
using Windows.UI.Composition;

namespace Dock;

/// <summary>
/// Traduce la curva de magnificación al lenguaje de expresiones de Composition.
///
/// La clave de todo M2: la posición y la escala de cada icono son <b>forma cerrada</b>
/// en la posición del cursor. No hay acumulación entre iconos, así que cada visual
/// lleva sus dos expresiones y el compositor las reevalúa solo cuando cambia la
/// propiedad compartida. Cero recálculo de layout por frame, y todo corriendo en el
/// proceso de DWM en vez de en nuestro hilo de UI.
///
/// El lenguaje tiene Sin y Clamp, que es justo lo que pide G(t). No tiene Exp, que es
/// otra razón para el coseno elevado frente a una gaussiana.
///
/// <para>
/// Las expresiones tienen un límite de longitud, y escribirlas del tirón lo pasaba:
/// cada icono repetía cuatro veces el mismo G(−c/R) y dos veces la transferencia del
/// ancho total. Así que los subtérminos compartidos se calculan UNA vez, con sus
/// propias expresiones sobre el property set, y los iconos solo los referencian. Sale
/// más corto y además se evalúa una sola vez por frame en vez de una por icono.
/// </para>
/// </summary>
internal static class DockExpressions
{
    /// <summary>Nombre del CompositionPropertySet dentro de las expresiones.</summary>
    public const string Props = "P";

    /// <summary>Cursor en coordenadas de reposo. Lo escribe el hilo de UI.</summary>
    public const string Cursor = Props + ".C";

    /// <summary>Intensidad del hover, de 0 a 1. La mueve el muelle.</summary>
    public const string Amount = Props + ".Amount";

    /// <summary>G(−c/R): el término de anclaje, común a toda la curva.</summary>
    private const string Anchor = Props + ".G0";

    /// <summary>T(anchoEnReposo): el ancho de la fila ya magnificada.</summary>
    private const string TotalWidth = Props + ".TW";

    /// <summary>Desplazamiento que mantiene la fila centrada mientras se ensancha.</summary>
    private const string Origin = Props + ".Origin";

    /// <summary>
    /// Siempre en cultura invariante: en español el separador decimal es la coma y eso
    /// rompería cada número dentro de la expresión.
    /// </summary>
    private static string F(float value) => value.ToString("G7", CultureInfo.InvariantCulture);

    /// <summary>G(t) = clamp(t,−1,1)/2 + sin(π·clamp(t,−1,1))/(2π)</summary>
    private static string G(string t)
    {
        string clamped = $"Clamp({t},-1,1)";
        return $"({clamped}*0.5 + Sin(3.14159265*{clamped})*0.15915494)";
    }

    /// <summary>G evaluada en el punto u de reposo.</summary>
    private static string GAt(in DockCurve curve, float u)
        => G($"(({F(u)} - {Cursor})*{F(1f / curve.Radius)})");

    /// <summary>T(u) − Origin, o sea la transferencia sin centrar.</summary>
    private static string Transfer(in DockCurve curve, float u)
        => $"({F(u)} + {F((curve.MaxScale - 1f) * curve.Radius)}*{Amount}*({GAt(curve, u)} - {Anchor}))";

    /// <summary>
    /// Registra en el property set los términos compartidos. Hay que llamarlo antes de
    /// crear las expresiones de los visuals.
    /// </summary>
    public static void Setup(
        Compositor compositor,
        CompositionPropertySet props,
        in DockCurve curve,
        float availableWidth)
    {
        props.InsertScalar("G0", 0f);
        props.InsertScalar("TW", curve.RestWidth);
        props.InsertScalar("Origin", 0f);

        // El orden importa: TW usa G0, y Origin usa TW.
        Start(compositor, props, "G0", G($"((0 - {Cursor})*{F(1f / curve.Radius)})"));
        Start(compositor, props, "TW", Transfer(curve, curve.RestWidth));
        Start(compositor, props, "Origin", $"(({F(availableWidth)} - {TotalWidth})*0.5)");
    }

    private static void Start(Compositor compositor, CompositionPropertySet props, string property, string expression)
    {
        ExpressionAnimation animation = compositor.CreateExpressionAnimation(expression);
        animation.SetReferenceParameter(Props, props);
        props.StartAnimation(property, animation);
    }

    /// <summary>
    /// Offset del icono. Con CenterPoint en el borde inferior izquierdo, el icono crece
    /// hacia arriba y hacia la derecha desde ahí, así que el offset es directamente el
    /// borde izquierdo proyectado.
    /// </summary>
    /// <param name="bounce">
    /// Referencia a la propiedad de rebote del propio visual. Va restando en la Y, así
    /// que el icono salta hacia arriba sin pelearse con la ExpressionAnimation que ya
    /// es dueña de Offset.
    /// </param>
    public static string IconOffset(in DockCurve curve, int index, float restTop, string bounce)
        => $"Vector3({Origin} + {Transfer(curve, curve.RestLeft(index))}, {F(restTop)} - {bounce}, 0)";

    /// <summary>Centro horizontal del elemento, para colgarle el punto de "abierta".</summary>
    public static string ItemCenter(in DockCurve curve, int index, float dotSize, float top)
    {
        string left = Transfer(curve, curve.RestLeft(index));
        string right = Transfer(curve, curve.RestRight(index));
        return $"Vector3({Origin} + ({left} + {right})*0.5 - {F(dotSize * 0.5f)}, {F(top)}, 0)";
    }

    /// <summary>
    /// Escala del icono: el ancho proyectado partido por el de reposo. Se mapean los
    /// BORDES y se restan, que es lo que garantiza que no haya solapes. Al restar, el
    /// término de anclaje se cancela solo y la expresión sale más corta.
    /// </summary>
    public static string IconScale(in DockCurve curve, int index)
    {
        float a = curve.RestLeft(index);
        float b = curve.RestRight(index);
        float growth = (curve.MaxScale - 1f) * curve.Radius;
        float content = curve.Slot(index).ContentWidth;

        string scale = $"(({F(b - a)} + {F(growth)}*{Amount}*({GAt(curve, b)} - {GAt(curve, a)}))*{F(1f / content)})";
        return $"Vector3({scale}, {scale}, 1)";
    }

    /// <summary>
    /// Borde izquierdo de la barra, con su margen. T(0) es 0 por construcción, así que
    /// el borde de la fila es exactamente el origen.
    /// </summary>
    public static string BarOffset(float padding, float top)
        => $"Vector3({Origin} - {F(padding)}, {F(top)}, 0)";

    /// <summary>
    /// Tamaño de la barra. Crece con la fila, como el Dock de macOS: la ventana es fija
    /// y del tamaño máximo, pero la barra visible sigue a los iconos.
    /// </summary>
    public static string BarSize(float padding, float height)
        => $"Vector2({TotalWidth} + {F(padding * 2f)}, {F(height)})";

    /// <summary>
    /// Solo el ancho, para la geometría del recorte redondeado, que lleva su propio
    /// Size y tiene que seguir al de la barra.
    /// </summary>
    public static string BarSizeOnly(float padding)
        => $"Vector2({TotalWidth} + {F(padding * 2f)}, 100000)";
}
