using System.Numerics;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Direct2D;
using Windows.Win32.Graphics.Direct2D.Common;
using Windows.Win32.Graphics.DirectWrite;

namespace Hud;

/// <summary>
/// Los iconos del HUD, que son glifos de Segoe Fluent Icons pintados con DirectWrite
/// sobre nuestra propia superficie. Fuera de XAML no hay atajo: hay que medir, pedir
/// una superficie del tamano justo y pintar a mano.
///
/// <para>
/// Son cinco: el altavoz con cero, una, dos y tres ondas, y el silenciado. Usar la
/// fuente de iconos del sistema sale mas barato y mas nitido que dibujar un altavoz a
/// mano, y ademas son exactamente los mismos simbolos que ensena Windows.
/// </para>
/// </summary>
internal static unsafe class Glifos
{
    // Segoe Fluent Icons, mismos puntos de codigo que Segoe MDL2 Assets.
    public const string Volumen0 = "";
    public const string Volumen1 = "";
    public const string Volumen2 = "";
    public const string Volumen3 = "";
    public const string Silenciado = "";

    /// <summary>Los cinco, en el orden en que se apilan en el arbol de visuals.</summary>
    public static readonly string[] Todos = [Volumen0, Volumen1, Volumen2, Volumen3, Silenciado];

    private static IDWriteFactory? _factory;

    /// <summary>
    /// Un formato por tamano en pixeles FISICOS. La clave lleva ya el tamano escalado,
    /// asi que el DPI va dentro: es la trampa que la isla anoto del dock, un formato
    /// unico creado con la escala de la primera pantalla y que medía mal en el resto.
    ///
    /// Sin candado a proposito: todo esto vive en el hilo de la DispatcherQueue.
    /// </summary>
    private static readonly Dictionary<float, IDWriteTextFormat> Formatos = [];

    /// <summary>
    /// Que glifo toca. Logica pura y por eso comprobable: los cortes entre una onda y
    /// otra son la clase de cosa que se desplaza sin que nadie se entere.
    /// </summary>
    public static int Indice(int porcentaje, bool silenciado)
    {
        if (silenciado) return 4;
        if (porcentaje <= 0) return 0;
        if (porcentaje <= 33) return 1;
        if (porcentaje <= 66) return 2;
        return 3;
    }

    /// <summary>
    /// Mide antes de dibujar: la superficie se pide ya con el tamano, asi que hace
    /// falta saberlo antes de tenerla.
    /// </summary>
    public static Vector2 Medir(string glifo, float px)
    {
        DWRITE_TEXT_METRICS m;
        Disposicion(glifo, px).GetMetrics(&m);

        // Un pixel de margen por lado: DirectWrite mide la caja de texto, pero el
        // antialiasing de una curva puede salirse de ella y quedaria recortado.
        return new Vector2(MathF.Ceiling(m.width) + 2f, MathF.Ceiling(m.height) + 2f);
    }

    /// <summary>Pinta el glifo en blanco. La opacidad la pone el visual, no la tinta.</summary>
    public static void Dibujar(ID2D1DeviceContext ctx, string glifo, float px, System.Drawing.Point en)
    {
        D2D1_COLOR_F tinta = new() { r = 1f, g = 1f, b = 1f, a = 1f };
        ctx.CreateSolidColorBrush(&tinta, null, out ID2D1SolidColorBrush pincel);

        ctx.DrawTextLayout(
            new D2D_POINT_2F { x = en.X + 1f, y = en.Y + 1f },
            Disposicion(glifo, px),
            pincel,
            D2D1_DRAW_TEXT_OPTIONS.D2D1_DRAW_TEXT_OPTIONS_NONE);
    }

    private static IDWriteTextLayout Disposicion(string glifo, float px)
    {
        // El formato PRIMERO y en su propia linea. Metido como argumento de
        // CreateTextLayout no vale: C# evalua el receptor antes que los argumentos, asi
        // que se leeria _factory estando todavia a null -- y es Formato quien la crea.
        IDWriteTextFormat formato = Formato(px);

        fixed (char* v = glifo)
        {
            _factory!.CreateTextLayout(new PCWSTR(v), (uint)glifo.Length, formato,
                400f, 400f, out IDWriteTextLayout layout);
            return layout;
        }
    }

    private static IDWriteTextFormat Formato(float px)
    {
        if (Formatos.TryGetValue(px, out IDWriteTextFormat? cache)) return cache;

        if (_factory is null)
        {
            Guid iid = typeof(IDWriteFactory).GUID;
            PInvoke.DWriteCreateFactory(DWRITE_FACTORY_TYPE.DWRITE_FACTORY_TYPE_SHARED, &iid, out object f)
                .ThrowOnFailure();
            _factory = (IDWriteFactory)f;
        }

        fixed (char* familia = "Segoe Fluent Icons")
        fixed (char* local = "")
        {
            _factory.CreateTextFormat(
                new PCWSTR(familia), null,
                DWRITE_FONT_WEIGHT.DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE.DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH.DWRITE_FONT_STRETCH_NORMAL,
                px, new PCWSTR(local), out IDWriteTextFormat fmt);

            fmt.SetWordWrapping(DWRITE_WORD_WRAPPING.DWRITE_WORD_WRAPPING_NO_WRAP);
            Formatos[px] = fmt;
            return fmt;
        }
    }

    /// <summary>
    /// Los cortes del mapeo, que es lo unico de este fichero que se puede romper en
    /// silencio. Lo demas o pinta o no pinta, y eso se ve.
    /// </summary>
    public static void SelfCheck()
    {
        // El silencio gana siempre, incluso al 100%: si no, al silenciar con el volumen
        // alto seguirias viendo tres ondas.
        Assert(Indice(100, true) == 4, "silenciado al 100% tiene que ser el mudo");
        Assert(Indice(0, true) == 4, "silenciado a 0 tambien");

        // Los cuatro tramos, y sus bordes exactos por los dos lados.
        Assert(Indice(0, false) == 0, "0% -> sin ondas");
        Assert(Indice(1, false) == 1, "1% ya es una onda");
        Assert(Indice(33, false) == 1, "33% sigue siendo una");
        Assert(Indice(34, false) == 2, "34% pasa a dos");
        Assert(Indice(66, false) == 2, "66% sigue siendo dos");
        Assert(Indice(67, false) == 3, "67% pasa a tres");
        Assert(Indice(100, false) == 3, "100% -> tres ondas");

        // Y que los cinco indices caen dentro del array, que es lo que evita que un
        // retoque de los cortes reviente el arbol de visuals en tiempo de ejecucion.
        for (int p = -5; p <= 105; p++)
        {
            foreach (bool mudo in (bool[])[false, true])
            {
                int i = Indice(p, mudo);
                Assert(i >= 0 && i < Todos.Length, $"indice {i} fuera de rango en {p}%");
            }
        }

        Console.WriteLine("[hud] glifos: 9 cortes + 222 indices OK");
    }

    private static void Assert(bool condicion, string queFallo)
    {
        if (!condicion) throw new InvalidOperationException($"glifos: {queFallo}");
    }
}
