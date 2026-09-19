using System.Numerics;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Direct2D;
using Windows.Win32.Graphics.Direct2D.Common;
using Windows.Win32.Graphics.DirectWrite;

namespace Lanzador;

/// <summary>
/// El texto de el lanzador, con DirectWrite sobre nuestra propia superficie de dibujo.
/// Fuera de XAML no hay atajo: hay que medir la cadena, pedir una superficie del tamano
/// justo y pintarla a mano.
/// </summary>
internal static unsafe class Texto
{
    private static IDWriteFactory? _factory;

    /// <summary>
    /// Un formato por (tamano en pixeles FISICOS, peso).
    ///
    /// La clave lleva ya el tamano escalado, asi que el DPI va dentro y no puede pasar
    /// lo que le paso al dock: un formato unico creado con la escala de la primera
    /// pantalla que pintara, que medía mal en todas las demas.
    ///
    /// Sin candado a proposito: todo esto vive en el hilo que tiene la DispatcherQueue.
    /// </summary>
    private static readonly Dictionary<(float, bool), IDWriteTextFormat> Formatos = [];

    /// <summary>
    /// Mide antes de dibujar. Hace falta el tamano ANTES de tener superficie, porque la
    /// superficie se pide ya con ese tamano y porque de ahi sale si el titulo cabe o
    /// hay que pasearlo.
    /// </summary>
    public static Vector2 Medir(string texto, float px, bool grueso)
    {
        DWRITE_TEXT_METRICS m;
        Disposicion(texto, px, grueso).GetMetrics(&m);

        // Un pixel de margen por lado: DirectWrite mide la caja de texto, pero el
        // antialiasing de una curva puede salirse de ella y quedaria recortado.
        return new Vector2(MathF.Ceiling(m.width) + 2f, MathF.Ceiling(m.height) + 2f);
    }

    /// <summary>Pinta el texto en blanco con el alfa dado, en el contexto que se le de.</summary>
    public static void Dibujar(ID2D1DeviceContext ctx, string texto, float px, bool grueso,
                               float alpha, System.Drawing.Point en)
    {
        D2D1_COLOR_F tinta = new() { r = 1f, g = 1f, b = 1f, a = alpha };
        ctx.CreateSolidColorBrush(&tinta, null, out ID2D1SolidColorBrush pincel);

        ctx.DrawTextLayout(
            new D2D_POINT_2F { x = en.X + 1f, y = en.Y + 1f },
            Disposicion(texto, px, grueso),
            pincel,
            D2D1_DRAW_TEXT_OPTIONS.D2D1_DRAW_TEXT_OPTIONS_NONE);
    }

    private static IDWriteTextLayout Disposicion(string texto, float px, bool grueso)
    {
        // El formato PRIMERO y en su propia linea. Metido como argumento de
        // CreateTextLayout no vale: C# evalua el receptor antes que los argumentos, asi
        // que se leeria _factory estando todavia a null -- y es Formato quien la crea.
        IDWriteTextFormat formato = Formato(px, grueso);

        fixed (char* v = texto)
        {
            _factory!.CreateTextLayout(
                new PCWSTR(v), (uint)texto.Length, formato,
                // Ancho de sobra: el recorte lo hace el clip del visual, no DirectWrite.
                8000f, 200f, out IDWriteTextLayout layout);
            return layout;
        }
    }

    private static IDWriteTextFormat Formato(float px, bool grueso)
    {
        if (Formatos.TryGetValue((px, grueso), out IDWriteTextFormat? cache)) return cache;

        if (_factory is null)
        {
            Guid iid = typeof(IDWriteFactory).GUID;
            PInvoke.DWriteCreateFactory(DWRITE_FACTORY_TYPE.DWRITE_FACTORY_TYPE_SHARED, &iid, out object f)
                .ThrowOnFailure();
            _factory = (IDWriteFactory)f;
        }

        // Segoe UI Variable Text es la de Windows 11 para tamanos pequenos. Si no
        // estuviera, DirectWrite cae a la del sistema por su cuenta.
        //
        fixed (char* familia = "Segoe UI Variable Text")
        fixed (char* local = "")
        {
            _factory.CreateTextFormat(
                new PCWSTR(familia), null,
                grueso ? DWRITE_FONT_WEIGHT.DWRITE_FONT_WEIGHT_SEMI_BOLD
                       : DWRITE_FONT_WEIGHT.DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE.DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH.DWRITE_FONT_STRETCH_NORMAL,
                px, new PCWSTR(local), out IDWriteTextFormat fmt);

            // Sin salto de linea: un titulo largo se resuelve paseandolo, no
            // partiendolo en dos renglones.
            fmt.SetWordWrapping(DWRITE_WORD_WRAPPING.DWRITE_WORD_WRAPPING_NO_WRAP);

            Formatos[(px, grueso)] = fmt;
            return fmt;
        }
    }
}
