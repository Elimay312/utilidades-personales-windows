using System.Globalization;

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
/// </summary>
internal static class DockExpressions
{
    /// <summary>Nombre del CompositionPropertySet dentro de las expresiones.</summary>
    public const string Props = "P";

    /// <summary>Cursor en coordenadas de reposo.</summary>
    public const string Cursor = Props + ".C";

    /// <summary>Intensidad del hover, de 0 a 1.</summary>
    public const string Amount = Props + ".Amount";

    /// <summary>
    /// Siempre en cultura invariante: en español el separador decimal es la coma y
    /// eso rompería cada número dentro de la expresión.
    /// </summary>
    private static string F(float value) => value.ToString("R", CultureInfo.InvariantCulture);

    /// <summary>G(t) = clamp(t,−1,1)/2 + sin(π·clamp(t,−1,1))/(2π)</summary>
    private static string G(string t)
    {
        // Clamp aparece dos veces, así que se calcula sobre el mismo subtérmino.
        string clamped = $"Clamp({t},-1,1)";
        return $"({clamped}*0.5 + Sin(3.14159265*{clamped})*0.15915494)";
    }

    /// <summary>T(u) = u + (M−1)·Amount·R·[ G((u−c)/R) − G(−c/R) ]</summary>
    private static string Transfer(in DockCurve curve, float u)
    {
        float invRadius = 1f / curve.Radius;
        float growth = (curve.MaxScale - 1f) * curve.Radius;

        string gu = G($"(({F(u)} - {Cursor})*{F(invRadius)})");
        string g0 = G($"((0 - {Cursor})*{F(invRadius)})");

        return $"({F(u)} + {F(growth)}*{Amount}*({gu} - {g0}))";
    }

    /// <summary>Desplazamiento que mantiene la fila centrada mientras se ensancha.</summary>
    private static string Origin(in DockCurve curve, float availableWidth)
        => $"(({F(availableWidth)} - {Transfer(curve, curve.RestWidth)})*0.5)";

    /// <summary>Posición en pantalla del punto u.</summary>
    private static string Project(in DockCurve curve, float availableWidth, float u)
        => $"({Origin(curve, availableWidth)} + {Transfer(curve, u)})";

    /// <summary>
    /// Offset del icono. Con CenterPoint en el borde inferior izquierdo, el icono
    /// crece hacia arriba y hacia la derecha desde ahí, así que el offset es
    /// directamente el borde izquierdo proyectado.
    /// </summary>
    public static string IconOffset(in DockCurve curve, float availableWidth, int index, float restTop)
        => $"Vector3({Project(curve, availableWidth, curve.RestLeft(index))}, {F(restTop)}, 0)";

    /// <summary>
    /// Escala del icono: el ancho proyectado partido por el de reposo. Se mapean los
    /// BORDES y se restan, que es lo que garantiza que no haya solapes.
    /// </summary>
    public static string IconScale(in DockCurve curve, int index)
    {
        string left = Transfer(curve, curve.RestLeft(index));
        string right = Transfer(curve, curve.RestRight(index));
        string scale = $"(({right} - {left})*{F(1f / curve.IconSize)})";
        return $"Vector3({scale}, {scale}, 1)";
    }

    /// <summary>Borde izquierdo de la barra de fondo, con su margen.</summary>
    public static string BarOffset(in DockCurve curve, float availableWidth, float padding, float top)
        => $"Vector3({Project(curve, availableWidth, 0f)} - {F(padding)}, {F(top)}, 0)";

    /// <summary>
    /// Ancho de la barra de fondo. Crece con la fila, como el Dock de macOS: la
    /// ventana es fija y del tamaño máximo, pero la barra visible sigue a los iconos.
    /// </summary>
    public static string BarSize(in DockCurve curve, float padding, float height)
        => $"Vector2({Transfer(curve, curve.RestWidth)} + {F(padding * 2f)}, {F(height)})";
}
