using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Direct2D;
using Windows.Win32.Graphics.Direct2D.Common;
using Windows.Win32.Graphics.DirectWrite;

namespace Renombrar;

/// <summary>
/// El texto, sobre nuestra propia superficie de dibujo. Fuera de XAML no hay etiquetas:
/// se mide con DirectWrite y se pinta con Direct2D, que es lo que ya hacen el dock y la
/// isla.
/// </summary>
internal static unsafe class Texto
{
    internal const float Cuerpo = 13f;
    internal const float Margen = 14f;

    /// <summary>Alto de una fila a 96 ppp. Lo demas se calcula a partir de aqui.</summary>
    internal const float AltoFila = 30f;

    private static IDWriteFactory? _fabrica;

    /// <summary>
    /// Un formato por escala y peso, no uno solo: con pantallas de DPI distinto, el
    /// primero que se cree decidiria el tamano de letra de todos los demas. Es la trampa
    /// que el dock ya pago con sus etiquetas.
    /// </summary>
    private static readonly Dictionary<(float Escala, bool Fuerte), IDWriteTextFormat> Formatos = [];

    /// <summary>
    /// Una fila de la tabla: el nombre de antes, la flecha, y el de despues o el motivo
    /// por el que no va a poder ser.
    /// </summary>
    internal static void Fila(ID2D1DeviceContext ctx, Fila fila, float ancho, float escala)
    {
        bool mal = fila.Estado is not (Estado.Ok or Estado.SinCambio);
        bool apagada = fila.Estado == Estado.SinCambio;

        float alto = AltoFila * escala;
        float margen = Margen * escala;
        float mitad = MathF.Floor(ancho * 0.46f);

        // La barra de color a la izquierda. Es lo unico que se ve de un vistazo cuando la
        // lista tiene doscientas filas: el nombre hay que leerlo, el color no.
        Barra(ctx, Color(fila.Estado), margen * 0.5f, alto);

        Escribe(ctx, fila.Antes, margen, alto, mitad - margen * 2f, escala,
                apagada ? 0.40f : 0.62f, false);

        Escribe(ctx, "→", mitad - margen, alto, margen * 2f, escala, 0.35f, false);

        string derecha = mal && fila.Motivo.Length > 0 ? fila.Motivo : fila.Despues;
        Escribe(ctx, derecha, mitad + margen * 0.5f, alto, ancho - mitad - margen * 1.5f, escala,
                apagada ? 0.40f : 1f, !apagada && !mal, mal);
    }

    /// <summary>La linea de la franja de arriba: donde estas y cuantos van a cambiar.</summary>
    internal static void Cabecera(ID2D1DeviceContext ctx, string texto, float ancho, float alto, float escala)
        => Escribe(ctx, texto, Margen * escala, alto, ancho - Margen * 2f * escala, escala, 0.88f, true);

    /// <summary>El color de un estado. Lo unico que hay que mirar para saber si algo va mal.</summary>
    private static (float R, float G, float B) Color(Estado e) => e switch
    {
        Estado.Ok => (0.42f, 0.78f, 0.52f),
        Estado.SinCambio => (0.45f, 0.45f, 0.48f),
        _ => (0.95f, 0.42f, 0.40f),
    };

    private static void Barra(ID2D1DeviceContext ctx, (float R, float G, float B) c, float x, float alto)
    {
        D2D1_COLOR_F color = new() { r = c.R, g = c.G, b = c.B, a = 0.9f };
        ctx.CreateSolidColorBrush(&color, null, out ID2D1SolidColorBrush brocha);

        float grosor = 3f;
        D2D1_ROUNDED_RECT barra = new()
        {
            rect = new D2D_RECT_F { left = x, top = alto * 0.22f, right = x + grosor, bottom = alto * 0.78f },
            radiusX = grosor * 0.5f,
            radiusY = grosor * 0.5f,
        };
        ctx.FillRoundedRectangle(&barra, brocha);
    }

    private static void Escribe(ID2D1DeviceContext ctx, string texto, float x, float alto, float hueco,
                                float escala, float tinta, bool fuerte, bool rojo = false)
    {
        if (texto.Length == 0) return;

        D2D1_COLOR_F color = rojo
            ? new D2D1_COLOR_F { r = 1f, g = 0.55f, b = 0.52f, a = 0.95f }
            : new D2D1_COLOR_F { r = 1f, g = 1f, b = 1f, a = tinta };
        ctx.CreateSolidColorBrush(&color, null, out ID2D1SolidColorBrush brocha);

        IDWriteTextLayout trazado = Cabe(texto, hueco, escala, fuerte);
        DWRITE_TEXT_METRICS metricas;
        trazado.GetMetrics(&metricas);

        ctx.DrawTextLayout(
            new D2D_POINT_2F { x = x, y = (alto - metricas.height) * 0.5f },
            trazado,
            brocha,
            D2D1_DRAW_TEXT_OPTIONS.D2D1_DRAW_TEXT_OPTIONS_NONE);
    }

    /// <summary>
    /// El texto recortado a lo que cabe, con puntos suspensivos. Se recorta midiendo, no
    /// contando caracteres: "Recibo_2024-03-12_001.pdf" y "IIIIIIIIIIIIIIIIIIIIIIIII"
    /// tienen las mismas letras y no ocupan lo mismo ni de lejos.
    /// </summary>
    private static IDWriteTextLayout Cabe(string texto, float hueco, float escala, bool fuerte)
    {
        IDWriteTextLayout trazado = Trazar(texto, hueco, escala, fuerte);
        if (!SePasa(trazado, hueco)) return trazado;

        // Busqueda binaria sobre el numero de letras. Con nombres de 40 caracteres son
        // seis medidas en vez de cuarenta, y esto corre por cada fila visible.
        int bajo = 0, alto = texto.Length;
        while (bajo < alto)
        {
            int medio = (bajo + alto + 1) / 2;
            if (SePasa(Trazar(texto[..medio] + "…", hueco, escala, fuerte), hueco)) alto = medio - 1;
            else bajo = medio;
        }

        return Trazar(texto[..bajo] + "…", hueco, escala, fuerte);
    }

    private static bool SePasa(IDWriteTextLayout trazado, float hueco)
    {
        DWRITE_TEXT_METRICS m;
        trazado.GetMetrics(&m);
        return m.width > hueco;
    }

    private static IDWriteTextLayout Trazar(string texto, float hueco, float escala, bool fuerte)
    {
        IDWriteTextFormat formato = Formato(escala, fuerte);

        fixed (char* valor = texto)
        {
            _fabrica!.CreateTextLayout(new PCWSTR(valor), (uint)texto.Length, formato,
                                       MathF.Max(hueco, 1f), 1000f, out IDWriteTextLayout trazado);
            return trazado;
        }
    }

    private static IDWriteTextFormat Formato(float escala, bool fuerte)
    {
        if (Formatos.TryGetValue((escala, fuerte), out IDWriteTextFormat? guardado)) return guardado;

        if (_fabrica is null)
        {
            Guid iid = typeof(IDWriteFactory).GUID;
            PInvoke.DWriteCreateFactory(DWRITE_FACTORY_TYPE.DWRITE_FACTORY_TYPE_SHARED, &iid, out object f)
                   .ThrowOnFailure();
            _fabrica = (IDWriteFactory)f;
        }

        // Segoe UI Variable es la de Windows 11; si no esta, DirectWrite cae a la del
        // sistema por su cuenta y no hay que hacer nada.
        fixed (char* familia = "Segoe UI Variable Text")
        fixed (char* idioma = "")
        {
            _fabrica.CreateTextFormat(
                new PCWSTR(familia),
                null,
                fuerte ? DWRITE_FONT_WEIGHT.DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT.DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE.DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH.DWRITE_FONT_STRETCH_NORMAL,
                Cuerpo * escala,
                new PCWSTR(idioma),
                out IDWriteTextFormat formato);

            formato.SetWordWrapping(DWRITE_WORD_WRAPPING.DWRITE_WORD_WRAPPING_NO_WRAP);
            Formatos[(escala, fuerte)] = formato;
            return formato;
        }
    }
}
