using System.Numerics;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Direct2D;
using Windows.Win32.Graphics.Direct2D.Common;
using Windows.Win32.Graphics.DirectWrite;

namespace Dock;

/// <summary>
/// El texto de las etiquetas que salen encima del icono al pasar por encima.
///
/// Es la primera vez que este dock dibuja texto. Hasta ahora todo eran iconos del shell
/// y rectángulos de color, así que aquí entra DirectWrite: se mide la cadena, se pinta
/// la pastilla del tamaño justo, y se dibuja encima. Todo sobre nuestra propia
/// superficie de dibujo, la misma que ya usan los iconos.
/// </summary>
internal static unsafe class Labels
{
    /// <summary>Tamaño de la letra en píxeles de DIP, a 96 ppp.</summary>
    private const float FontSize = 13f;

    /// <summary>Margen del texto dentro de la pastilla.</summary>
    private const float PaddingX = 10f;
    private const float PaddingY = 5f;

    private static IDWriteFactory? _factory;
    private static IDWriteTextFormat? _format;

    /// <summary>
    /// Mide el texto y devuelve el tamaño de la pastilla que lo contiene.
    /// Se mide aparte de dibujar porque el visual necesita saber su tamaño ANTES de
    /// tener superficie: la expresión que lo centra sobre el icono lleva el ancho
    /// horneado dentro.
    /// </summary>
    public static Vector2 Measure(string text, float scale)
    {
        IDWriteTextLayout layout = LayoutOf(text, scale);
        DWRITE_TEXT_METRICS metrics;
        layout.GetMetrics(&metrics);

        return new Vector2(
            MathF.Ceiling(metrics.width + PaddingX * 2f * scale),
            MathF.Ceiling(metrics.height + PaddingY * 2f * scale));
    }

    /// <summary>Pinta la pastilla y el texto en el contexto que se le dé.</summary>
    public static void Draw(ID2D1DeviceContext context, string text, float scale, Vector2 size, System.Drawing.Point at)
    {
        D2D1_COLOR_F ink = new() { r = 1f, g = 1f, b = 1f, a = 0.95f };
        D2D1_COLOR_F chipColor = new() { r = 0.08f, g = 0.08f, b = 0.1f, a = 0.92f };

        context.CreateSolidColorBrush(&chipColor, null, out ID2D1SolidColorBrush chipBrush);
        context.CreateSolidColorBrush(&ink, null, out ID2D1SolidColorBrush inkBrush);

        float radius = size.Y * 0.32f;
        D2D1_ROUNDED_RECT chip = new()
        {
            rect = new D2D_RECT_F
            {
                left = at.X + 0.5f,
                top = at.Y + 0.5f,
                right = at.X + size.X - 0.5f,
                bottom = at.Y + size.Y - 0.5f,
            },
            radiusX = radius,
            radiusY = radius,
        };

        context.FillRoundedRectangle(&chip, chipBrush);
        context.DrawTextLayout(
            new D2D_POINT_2F { x = at.X + PaddingX * scale, y = at.Y + PaddingY * scale },
            LayoutOf(text, scale),
            inkBrush,
            D2D1_DRAW_TEXT_OPTIONS.D2D1_DRAW_TEXT_OPTIONS_NONE);
    }

    /// <summary>
    /// Solo el texto, sin pastilla, centrado verticalmente en una fila de la altura
    /// dada. Es lo que usa el menú del clic derecho, que trae su propio fondo.
    /// </summary>
    public static void DrawRow(ID2D1DeviceContext context, string text, float scale, System.Drawing.Point at, float rowHeight)
    {
        D2D1_COLOR_F ink = new() { r = 1f, g = 1f, b = 1f, a = 0.95f };
        context.CreateSolidColorBrush(&ink, null, out ID2D1SolidColorBrush brush);

        IDWriteTextLayout layout = LayoutOf(text, scale);
        DWRITE_TEXT_METRICS metrics;
        layout.GetMetrics(&metrics);

        context.DrawTextLayout(
            new D2D_POINT_2F { x = at.X, y = at.Y + (rowHeight - metrics.height) * 0.5f },
            layout,
            brush,
            D2D1_DRAW_TEXT_OPTIONS.D2D1_DRAW_TEXT_OPTIONS_NONE);
    }

    private static IDWriteTextLayout LayoutOf(string text, float scale)
    {
        EnsureFormat(scale);

        fixed (char* value = text)
        {
            _factory!.CreateTextLayout(new PCWSTR(value), (uint)text.Length, _format!, 1000f, 100f, out IDWriteTextLayout layout);
            return layout;
        }
    }

    private static void EnsureFormat(float scale)
    {
        if (_format is not null) return;

        Guid iid = typeof(IDWriteFactory).GUID;
        PInvoke.DWriteCreateFactory(DWRITE_FACTORY_TYPE.DWRITE_FACTORY_TYPE_SHARED, &iid, out object factory)
            .ThrowOnFailure();

        _factory = (IDWriteFactory)factory;

        // Segoe UI Variable es la de Windows 11; si no está, DirectWrite cae a la de
        // sistema por su cuenta y no hay que hacer nada.
        fixed (char* family = "Segoe UI Variable Text")
        fixed (char* locale = "")
        {
            _factory.CreateTextFormat(
                new PCWSTR(family),
                null,
                DWRITE_FONT_WEIGHT.DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE.DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH.DWRITE_FONT_STRETCH_NORMAL,
                FontSize * scale,
                new PCWSTR(locale),
                out _format);
        }
    }
}
