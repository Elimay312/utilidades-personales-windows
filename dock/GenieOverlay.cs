using System.Globalization;
using System.Numerics;
using System.Runtime.InteropServices;
using Windows.UI.Composition;
using Windows.UI.Composition.Desktop;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.System.WinRT.Composition;
using Windows.Win32.UI.WindowsAndMessaging;
using WinRT;

namespace Dock;

/// <summary>
/// La ventana donde se reproduce el efecto genio, y la malla que lo deforma.
///
/// Hace falta una ventana aparte porque el dock mide poco más de 150 px de alto y la
/// animación ocupa la pantalla entera. Es <c>WS_EX_TRANSPARENT</c>, o sea que el ratón
/// la atraviesa por completo: es puramente decorativa. Se crea al empezar y se destruye
/// al acabar.
///
/// <para>
/// La malla son N <c>SpriteVisual</c> del tamaño completo de la captura, cada uno
/// recortado a su franja con un <c>InsetClip</c>. El recorte va en espacio local, ANTES
/// de la transformación, así que cada visual enseña su banda y luego se deforma por su
/// cuenta. Una malla hecha de visuals, sin shaders ni Win2D.
/// </para>
///
/// Como en la magnificación, la animación entera vive en expresiones sobre un único
/// escalar de progreso: el hilo de UI arranca la animación y no vuelve a intervenir.
/// </summary>
internal sealed unsafe class GenieOverlay : IDisposable
{
    private const string ClassName = "DockGenieOverlayClass";
    public const int Slices = 40;
    private const int DurationMs = 400;

    private const uint WM_DESTROY = 0x0002;
    private const uint WM_NCHITTEST = 0x0084;
    private const uint WM_TIMER = 0x0113;
    private const int HTTRANSPARENT = -1;
    private const nuint EndTimerId = 1;

    private static readonly WNDPROC WndProcThunk = WndProc;
    private static readonly Dictionary<nint, GenieOverlay> Instances = [];
    private static ushort _classAtom;

    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _target;
    private readonly ContainerVisual _root;
    private readonly CompositionPropertySet _props;

    private CompositionSurfaceBrush? _capture;
    private Action? _onFinished;
    private bool _reverse;
    private HWND _hwnd;
    private bool _disposed;

    private GenieOverlay(Compositor compositor, RECT monitor)
    {
        _compositor = compositor;
        EnsureClassRegistered();

        fixed (char* className = ClassName)
        fixed (char* title = "Genie")
        {
            _hwnd = PInvoke.CreateWindowEx(
                // TRANSPARENT: el ratón la atraviesa entera, no estorba a nada.
                WINDOW_EX_STYLE.WS_EX_TRANSPARENT
                    | WINDOW_EX_STYLE.WS_EX_NOACTIVATE
                    | WINDOW_EX_STYLE.WS_EX_TOOLWINDOW
                    | WINDOW_EX_STYLE.WS_EX_TOPMOST,
                new PCWSTR(className),
                new PCWSTR(title),
                WINDOW_STYLE.WS_POPUP,
                monitor.left, monitor.top,
                monitor.right - monitor.left, monitor.bottom - monitor.top,
                default, default, ModuleHandle, null);
        }

        if (_hwnd.IsNull) throw new InvalidOperationException("no se pudo crear la ventana del genio");
        Instances[(nint)_hwnd.Value] = this;

        ICompositorDesktopInterop interop = _compositor.As<ICompositorDesktopInterop>();
        interop.CreateDesktopWindowTarget(_hwnd, true, out _target);

        _root = _compositor.CreateContainerVisual();
        _root.RelativeSizeAdjustment = Vector2.One;
        _target.Root = _root;

        _props = _compositor.CreatePropertySet();
        _props.InsertScalar("P", 0f);
        _props.InsertScalar("SA", 0f);
        _props.InsertScalar("SB", 0f);
    }

    private static HINSTANCE ModuleHandle
    {
        get
        {
            HMODULE module = PInvoke.GetModuleHandle((PCWSTR)null);
            return (HINSTANCE)(nint)module;
        }
    }

    /// <summary>
    /// Reproduce el genio y se limpia solo al acabar. Devuelve false si no se pudo, y
    /// entonces quien llama debe minimizar o restaurar sin animación.
    /// </summary>
    /// <param name="reverse">
    /// Al revés: la ventana sale del icono en vez de entrar en él. Es el camino de
    /// vuelta, para cuando se restaura desde el dock.
    /// </param>
    /// <param name="onFinished">
    /// Se llama al acabar, justo antes de desmontar. En el camino de vuelta es donde se
    /// restaura la ventana de verdad: si se restaurase al empezar, aparecería entera
    /// debajo del genio y se vería asomar por los bordes.
    /// </param>
    public static bool Play(
        Compositor compositor,
        RECT monitor,
        CompositionSurfaceBrush capture,
        Vector2 captureSize,
        in GenieCurve curve,
        bool reverse = false,
        Action? onFinished = null)
    {
        GenieOverlay overlay;
        try
        {
            overlay = new GenieOverlay(compositor, monitor);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[genio] no se pudo montar: {ex.Message}");
            return false;
        }

        overlay._capture = capture;
        overlay._reverse = reverse;
        overlay._onFinished = onFinished;
        overlay.BuildMesh(capture, captureSize, curve, monitor);
        PInvoke.ShowWindow(overlay._hwnd, SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE);
        overlay.Start();
        return true;
    }

    private void BuildMesh(
        CompositionSurfaceBrush capture,
        Vector2 captureSize,
        in GenieCurve curve,
        RECT monitor)
    {
        // Las dos fases del tiempo se calculan UNA vez y las franjas solo las
        // referencian: si cada una repitiera el suavizado, las expresiones se pasarían
        // de largo, que es justo lo que ya mordió al montar la barra del dock.
        StartOn(_props, "SA", SmoothStepExpression("P.P", 0f, GenieCurve.StretchPhaseEnd));
        StartOn(_props, "SB", SmoothStepExpression("P.P", GenieCurve.StretchPhaseEnd, 1f));

        float sliceHeight = captureSize.Y / Slices;

        // La captura se dibuja en coordenadas de la VENTANA de la superposición.
        float originX = -monitor.left;
        float originY = -monitor.top;

        for (int i = 0; i < Slices; i++)
        {
            SpriteVisual slice = _compositor.CreateSpriteVisual();
            slice.Brush = capture;
            slice.Size = captureSize;

            // El recorte se aplica en espacio local, antes de la transformación: el
            // visual enseña solo su banda y luego se deforma entero.
            slice.Clip = _compositor.CreateInsetClip(
                topInset: i * sliceHeight,
                rightInset: 0f,
                bottomInset: (Slices - 1 - i) * sliceHeight,
                leftInset: 0f);

            float v0 = (float)i / Slices;
            float v1 = (i + 1f) / Slices;
            float vMid = (i + 0.5f) / Slices;

            slice.Properties.InsertScalar("Y0", 0f);
            slice.Properties.InsertScalar("SY", 1f);
            slice.Properties.InsertScalar("Neck", 1f);

            StartOn(slice.Properties, "Y0", VerticalExpression(curve, v0, originY));
            StartOn(slice.Properties, "SY",
                $"(({VerticalExpression(curve, v1, originY)} - I.Y0)*{F(1f / sliceHeight)})",
                slice);
            StartOn(slice.Properties, "Neck", NeckExpression(curve, vMid, originY));

            // Offset.Y: la franja se coloca por su borde superior, descontando lo que
            // la propia escala vertical ya desplaza su origen local.
            string offset =
                $"Vector3({CenterExpression(curve, originX)} - {HalfExpression(curve)}, " +
                $"I.Y0 - {F(i * sliceHeight)}*I.SY, 0)";

            string scale = $"Vector3({HalfExpression(curve)}*{F(2f / captureSize.X)}, I.SY, 1)";

            StartOn(slice, "Offset", offset, slice);
            StartOn(slice, "Scale", scale, slice);

            _root.Children.InsertAtTop(slice);
        }
    }

    private void Start()
    {
        // Lineal se siente muerta: el genio arranca despacio, coge velocidad por el
        // medio y se mete de golpe en el icono. Eso es lo que da la sensación de que
        // algo se está tragando la ventana, y no la duración a secas.
        CompositionEasingFunction ease = _compositor.CreateCubicBezierEasingFunction(
            new Vector2(0.45f, 0f), new Vector2(0.2f, 1f));

        ScalarKeyFrameAnimation run = _compositor.CreateScalarKeyFrameAnimation();
        run.InsertKeyFrame(0f, _reverse ? 1f : 0f);
        run.InsertKeyFrame(1f, _reverse ? 0f : 1f, ease);
        run.Duration = TimeSpan.FromMilliseconds(DurationMs);
        _props.StartAnimation("P", run);

        // Sin esto el genio no se absorbe: se queda quieto en su último fotograma y
        // desaparece de golpe cuando se destruye la superposición. Aunque en p=1 la
        // malla mide exactamente lo que el icono, sigue siendo la ventana entera
        // apelmazada encima de él, y se ve. Disolver el último tramo es lo que lo
        // convierte en "se lo ha tragado" en vez de "ha parado y ha desaparecido".
        ScalarKeyFrameAnimation fade = _compositor.CreateScalarKeyFrameAnimation();
        if (_reverse)
        {
            // De vuelta se materializa saliendo del icono, que es el mismo tramo pero
            // recorrido al revés.
            fade.InsertKeyFrame(0f, 0f);
            fade.InsertKeyFrame(0.3f, 1f);
            fade.InsertKeyFrame(1f, 1f);
        }
        else
        {
            fade.InsertKeyFrame(0f, 1f);
            fade.InsertKeyFrame(0.7f, 1f);
            fade.InsertKeyFrame(1f, 0f);
        }
        fade.Duration = TimeSpan.FromMilliseconds(DurationMs);
        _root.StartAnimation("Opacity", fade);

        // La limpieza va por temporizador y no por callback de la animación: así el
        // desmontaje ocurre en el mismo hilo que lo montó, sin saltos de hilo.
        PInvoke.SetTimer(_hwnd, EndTimerId, (uint)(DurationMs + 80), null);
    }

    // --- Expresiones -------------------------------------------------------------

    private static string F(float value) => value.ToString("G7", CultureInfo.InvariantCulture);

    /// <summary>Suavizado clásico t*t*(3-2t), que es lo que usa la curva en C#.</summary>
    private static string SmoothStepExpression(string value, float edge0, float edge1)
    {
        string t = $"Clamp(({value} - {F(edge0)})*{F(1f / (edge1 - edge0))},0,1)";
        return $"({t}*{t}*(3 - 2*{t}))";
    }

    /// <summary>Y de pantalla del punto v, interpolando entre las tres posiciones clave.</summary>
    private static string VerticalExpression(in GenieCurve curve, float v, float originY)
    {
        float rest = curve.Window.Top + v * curve.Window.Height + originY;
        float stretched = curve.Window.Top + v * (curve.Target.Bottom - curve.Window.Top) + originY;
        float absorbed = curve.Target.Top + v * curve.Target.Height + originY;

        return $"Lerp(Lerp({F(rest)},{F(stretched)},P.SA),{F(absorbed)},P.SB)";
    }

    /// <summary>
    /// El cuello del embudo. El lenguaje no tiene Exp, así que la sigmoide se emula con
    /// Pow sobre e — el mismo motivo por el que la magnificación usa un coseno elevado.
    /// </summary>
    private static string NeckExpression(in GenieCurve curve, float v, float originY)
    {
        float span = curve.Target.Bottom - curve.Window.Top;
        float invSpan = MathF.Abs(span) < 0.001f ? 0f : 1f / span;
        float low = GenieCurve.RawSigmoid(0f);
        float invRange = 1f / (GenieCurve.RawSigmoid(1f) - low);

        string y = VerticalExpression(curve, v, originY);
        string fx = $"Clamp(({F(curve.Target.Bottom + originY)} - {y})*{F(invSpan)},0,1)";
        string raw = $"(1/(1 + Pow(2.71828183,-{F(GenieCurve.SigmoidSlope)}*({fx} - 0.5))))";

        return $"(({raw} - {F(low)})*{F(invRange)})";
    }

    private static string HalfExpression(in GenieCurve curve)
    {
        float wh = curve.Window.HalfWidth;
        float th = curve.Target.HalfWidth;
        // Primero el embudo, luego la absorción, que lleva al icono exactamente.
        return $"Lerp(Lerp({F(wh)},Lerp({F(wh)},{F(th)},1 - I.Neck),P.SA),{F(th)},P.SB)";
    }

    private static string CenterExpression(in GenieCurve curve, float originX)
    {
        float wc = curve.Window.CenterX + originX;
        float tc = curve.Target.CenterX + originX;
        return $"Lerp(Lerp({F(wc)},Lerp({F(wc)},{F(tc)},1 - I.Neck),P.SA),{F(tc)},P.SB)";
    }

    private void StartOn(CompositionObject target, string property, string expression, Visual? self = null)
    {
        ExpressionAnimation animation = _compositor.CreateExpressionAnimation(expression);
        animation.SetReferenceParameter("P", _props);
        if (self is not null) animation.SetReferenceParameter("I", self);
        target.StartAnimation(property, animation);
    }

    // --- Ventana -----------------------------------------------------------------

    private static void EnsureClassRegistered()
    {
        if (_classAtom != 0) return;

        fixed (char* className = ClassName)
        {
            WNDCLASSEXW wc = new()
            {
                cbSize = (uint)Marshal.SizeOf<WNDCLASSEXW>(),
                lpfnWndProc = WndProcThunk,
                hInstance = ModuleHandle,
                lpszClassName = new PCWSTR(className),
                hbrBackground = default,
            };

            _classAtom = PInvoke.RegisterClassEx(in wc);
        }
    }

    private static LRESULT WndProc(HWND hwnd, uint msg, WPARAM wParam, LPARAM lParam)
    {
        Instances.TryGetValue((nint)hwnd.Value, out GenieOverlay? self);

        switch (msg)
        {
            // Decorativa: todo lo atraviesa.
            case WM_NCHITTEST:
                return new LRESULT(HTTRANSPARENT);

            case WM_TIMER when wParam.Value == EndTimerId:
                // Primero la ventana de verdad y luego el desmontaje, en el mismo paso
                // del bucle de mensajes: así no hay ni un fotograma sin ninguna de las
                // dos.
                self?._onFinished?.Invoke();
                self?.Dispose();
                return new LRESULT(0);

            case WM_DESTROY:
                Instances.Remove((nint)hwnd.Value);
                return new LRESULT(0);
        }

        return PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        if (!_hwnd.IsNull)
        {
            PInvoke.KillTimer(_hwnd, EndTimerId);
            PInvoke.DestroyWindow(_hwnd);
            _hwnd = default;
        }

        _target.Root = null;
        _root.Dispose();
        _target.Dispose();

        // La captura de una ventana grande son varios MB. Sin soltarla aquí, cada
        // minimizado deja su fotograma en memoria para siempre: se midió, +5 MB por
        // animación. La superficie es dueña de los píxeles, así que va también.
        (_capture?.Surface as CompositionDrawingSurface)?.Dispose();
        _capture?.Dispose();
        _capture = null;
    }
}
