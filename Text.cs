using System.Numerics;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Direct2D;
using Windows.Win32.Graphics.Direct2D.Common;
using Windows.Win32.Graphics.DirectWrite;

namespace QuickLook;

/// <summary>
/// El texto del panel, con DirectWrite sobre nuestra propia superficie de dibujo.
///
/// <para>
/// <b>Un formato por (tamano, escala, peso)</b>, y no uno solo: con pantallas de DPI
/// distinto —aqui hay al 100%, al 125% y al 175%— reutilizar el formato de la primera
/// pantalla mide y dibuja con el tamano de letra equivocado en la segunda. La fabrica si
/// puede ser unica, que no depende del DPI. Es el fallo que ya se pago en el dock.
/// </para>
///
/// <para>
/// Sin candado a proposito: todo esto vive en el hilo que tiene la DispatcherQueue de
/// Composition, que es uno solo.
/// </para>
/// </summary>
internal static unsafe class Text
{
    private static IDWriteFactory? _factory;

    private static readonly Dictionary<(float Size, float Scale, bool Bold), IDWriteTextFormat> Formats = [];

    /// <summary>Cuanto ocupa ese texto, para poder reservarle el hueco antes de dibujarlo.</summary>
    public static Vector2 Measure(string text, float size, float scale, bool bold, float maxWidth)
    {
        IDWriteTextLayout layout = LayoutOf(text, size, scale, bold, maxWidth);
        DWRITE_TEXT_METRICS metrics;
        layout.GetMetrics(&metrics);

        return new Vector2(MathF.Ceiling(metrics.width), MathF.Ceiling(metrics.height));
    }

    /// <summary>Dibuja el texto en el contexto que se le de, con el color y la opacidad dados.</summary>
    public static void Draw(
        ID2D1DeviceContext context, string text, float size, float scale, bool bold,
        float maxWidth, System.Drawing.Point at, float alpha)
    {
        D2D1_COLOR_F ink = new() { r = 1f, g = 1f, b = 1f, a = alpha };
        context.CreateSolidColorBrush(&ink, null, out ID2D1SolidColorBrush brush);

        context.DrawTextLayout(
            new D2D_POINT_2F { x = at.X, y = at.Y },
            LayoutOf(text, size, scale, bold, maxWidth),
            brush,
            D2D1_DRAW_TEXT_OPTIONS.D2D1_DRAW_TEXT_OPTIONS_NONE);
    }

    private static IDWriteTextLayout LayoutOf(string text, float size, float scale, bool bold, float maxWidth)
    {
        IDWriteTextFormat format = FormatFor(size, scale, bold);

        fixed (char* value = text)
        {
            _factory!.CreateTextLayout(
                new PCWSTR(value), (uint)text.Length, format, maxWidth, 10000f, out IDWriteTextLayout layout);
            return layout;
        }
    }

    private static IDWriteTextFormat FormatFor(float size, float scale, bool bold)
    {
        if (Formats.TryGetValue((size, scale, bold), out IDWriteTextFormat? cached)) return cached;

        if (_factory is null)
        {
            Guid iid = typeof(IDWriteFactory).GUID;
            PInvoke.DWriteCreateFactory(DWRITE_FACTORY_TYPE.DWRITE_FACTORY_TYPE_SHARED, &iid, out object factory)
                .ThrowOnFailure();

            _factory = (IDWriteFactory)factory;
        }

        // Segoe UI Variable es la de Windows 11; si no esta, DirectWrite cae a la del
        // sistema por su cuenta y no hay que hacer nada.
        fixed (char* family = "Segoe UI Variable Text")
        fixed (char* locale = "")
        {
            _factory.CreateTextFormat(
                new PCWSTR(family),
                null,
                bold ? DWRITE_FONT_WEIGHT.DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT.DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE.DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH.DWRITE_FONT_STRETCH_NORMAL,
                size * scale,
                new PCWSTR(locale),
                out IDWriteTextFormat format);

            Formats[(size, scale, bold)] = format;
            return format;
        }
    }
}
