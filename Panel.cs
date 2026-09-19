using System.Numerics;
using System.Runtime.InteropServices;
using Windows.UI;
using Windows.UI.Composition;
using Windows.UI.Composition.Desktop;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.System.WinRT.Composition;
using Windows.Win32.UI.WindowsAndMessaging;
using WinRT;

namespace QuickLook;

/// <summary>
/// El panel de la vista previa.
///
/// <para>
/// <b>Nunca roba el foco.</b> <c>WS_EX_NOACTIVATE</c> no basta: hay que contestar
/// <c>MA_NOACTIVATE</c> a <c>WM_MOUSEACTIVATE</c>, o el primer clic activa la ventana.
/// Y aqui no es solo estetica: si el panel robase el foco, el Explorador perderia el
/// resaltado de la seleccion y el segundo espacio no llegaria por el mismo camino que
/// el primero.
/// </para>
///
/// <para>
/// Recibe raton —no lleva <c>WS_EX_TRANSPARENT</c>— porque la rueda pasara paginas de PDF
/// y desplazara el texto. El raton llega a la ventana que esta debajo del cursor sin
/// necesidad de foco, asi que las dos cosas conviven.
/// </para>
/// </summary>
internal sealed unsafe class Panel : IDisposable
{
    private const string ClassName = "QuickLookPanelClass";

    private const uint WM_MOUSEACTIVATE = 0x0021;
    private const int MA_NOACTIVATE = 3;

    /// <summary>IDC_ARROW. CsWin32 no proyecta los cursores del sistema como constante.</summary>
    private const int IdcArrow = 32512;

    /// <summary>Tamano maximo del panel, en fraccion del area de trabajo.</summary>
    private const float MaxWidth = 0.62f;
    private const float MaxHeight = 0.72f;

    /// <summary>Margen alrededor del contenido, en unidades logicas.</summary>
    private const float Pad = 16f;

    /// <summary>Alto del pie con el nombre y los datos.</summary>
    private const float CaptionHeight = 46f;

    /// <summary>La ficha no depende del contenido, asi que tiene tamano fijo.</summary>
    private const float CardWidth = 460f;
    private const float CardHeight = 300f;

    private static readonly WNDPROC WndProcThunk = WndProc;
    private static readonly Dictionary<nint, Panel> Instances = [];

    private static ushort _classAtom;

    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _target;
    private readonly ContainerVisual _root;
    private readonly float _scale;

    private HWND _hwnd;
    private bool _disposed;

    private Panel(int x, int y, int w, int h, float scale)
    {
        _compositor = Visuals.Compositor;
        _scale = scale;

        EnsureClassRegistered();

        fixed (char* className = ClassName)
        fixed (char* title = "QuickLook")
        {
            _hwnd = PInvoke.CreateWindowEx(
                WINDOW_EX_STYLE.WS_EX_NOACTIVATE
                    | WINDOW_EX_STYLE.WS_EX_TOOLWINDOW
                    | WINDOW_EX_STYLE.WS_EX_TOPMOST,
                new PCWSTR(className),
                new PCWSTR(title),
                WINDOW_STYLE.WS_POPUP,
                x, y, w, h,
                default, default, ModuleHandle, null);
        }

        if (_hwnd.IsNull) throw new InvalidOperationException("no se pudo crear el panel");
        Instances[(nint)_hwnd.Value] = this;

        ICompositorDesktopInterop interop = _compositor.As<ICompositorDesktopInterop>();
        interop.CreateDesktopWindowTarget(_hwnd, true, out _target);

        _root = _compositor.CreateContainerVisual();
        _root.RelativeSizeAdjustment = Vector2.One;
        _target.Root = _root;
    }

    private static HINSTANCE ModuleHandle => (HINSTANCE)(nint)PInvoke.GetModuleHandle((PCWSTR)null);

    /// <summary>
    /// Abre el panel centrado en el monitor de <paramref name="near"/>, que es la ventana
    /// del Explorador desde la que se pulso el espacio. Devuelve null si algo falla: un
    /// fallo aqui no puede tirar el programa, porque el hook seguiria comiendose la barra
    /// espaciadora del usuario.
    /// </summary>
    public static Panel? Open(HWND near, Preview preview)
    {
        Panel? panel = null;
        try
        {
            (int x, int y, int w, int h, float scale) = Layout(near, preview);
            panel = new Panel(x, y, w, h, scale);
            panel.Build(preview, new Vector2(w, h));
            PInvoke.ShowWindow(panel._hwnd, SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE);
            return panel;
        }
        catch (Exception ex)
        {
            // MEDIDO: la ventana se crea ANTES que su contenido, asi que si Build falla
            // el HWND ya existe. Sin este Dispose se quedaba una ventana huerfana, sin
            // nada dibujado y sin nadie que la cerrara — y encima confundia a la sonda,
            // que la encontraba por clase y la daba por buena.
            panel?.Dispose();

            Console.WriteLine($"[panel] no se pudo abrir: {ex.Message}");
            if (Environment.GetEnvironmentVariable("QL_LOG") == "1") Console.WriteLine(ex);
            return null;
        }
    }

    /// <summary>Que tamano y sitio le toca, en pixeles fisicos del monitor de al lado.</summary>
    private static (int X, int Y, int W, int H, float Scale) Layout(HWND near, Preview preview)
    {
        HMONITOR monitor = PInvoke.MonitorFromWindow(near, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTOPRIMARY);

        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        PInvoke.GetMonitorInfo(monitor, ref info);

        RECT work = info.rcWork;
        int availableW = work.right - work.left;
        int availableH = work.bottom - work.top;

        // El DPI sale de la ventana ajena a proposito: es la pantalla donde esta mirando
        // el usuario, y con monitores a escalas distintas no coincide con la nuestra
        // hasta que la ventana ya esta creada ahi.
        float scale = PInvoke.GetDpiForWindow(near) / 96f;
        if (scale <= 0f) scale = 1f;

        (int w, int h) = Size(preview, scale, availableW, availableH);

        int x = work.left + (availableW - w) / 2;
        int y = work.top + (availableH - h) / 2;

        return (x, y, w, h, scale);
    }

    /// <summary>
    /// El tamano del panel. Una miniatura manda sobre la caja y se encaja conservando su
    /// proporcion: una foto apaisada y una vertical no caben en la misma caja sin
    /// deformarse, y una miniatura deformada no se reconoce. Una ficha no depende del
    /// contenido, asi que es fija.
    ///
    /// Logica pura, y con un caso feo detras: en un monitor pequeno el maximo puede
    /// quedar por debajo de la ficha fija, asi que el resultado se recorta al area
    /// disponible SIEMPRE, tambien para la ficha. Lo comprueba --check.
    /// </summary>
    internal static (int W, int H) Size(Preview preview, float scale, int availableW, int availableH)
    {
        int maxW = Math.Max(1, (int)(availableW * MaxWidth));
        int maxH = Math.Max(1, (int)(availableH * MaxHeight));

        float pad = Pad * scale;
        float caption = CaptionHeight * scale;

        if (preview.IsThumbnail && preview.Image is Pixels image)
        {
            float boxW = MathF.Max(1f, maxW - pad * 2f);
            float boxH = MathF.Max(1f, maxH - pad * 2f - caption);

            float fit = MathF.Min(boxW / image.Width, boxH / image.Height);

            // Nunca se agranda por encima del tamano nativo: estirar una miniatura solo
            // ensena los pixeles mas grandes.
            fit = MathF.Min(fit, 1f);

            int tw = (int)MathF.Ceiling(image.Width * fit + pad * 2f);
            int th = (int)MathF.Ceiling(image.Height * fit + pad * 2f + caption);
            return (Math.Clamp(tw, 1, maxW), Math.Clamp(th, 1, maxH));
        }

        return (Math.Clamp((int)(CardWidth * scale), 1, maxW),
                Math.Clamp((int)(CardHeight * scale), 1, maxH));
    }

    /// <summary>
    /// El chip: mismo material que el dock y que la isla, para que las tres utilidades
    /// se vean de la misma familia.
    /// </summary>
    private void Build(Preview preview, Vector2 size)
    {
        ContainerVisual chip = _compositor.CreateContainerVisual();
        chip.RelativeSizeAdjustment = Vector2.One;

        CompositionRoundedRectangleGeometry round = _compositor.CreateRoundedRectangleGeometry();
        round.Size = size;
        round.CornerRadius = new Vector2(14f * _scale);
        chip.Clip = _compositor.CreateGeometricClip(round);

        SpriteVisual material = _compositor.CreateSpriteVisual();
        material.RelativeSizeAdjustment = Vector2.One;
        material.Brush = Visuals.CreateAcrylicBrush();
        chip.Children.InsertAtBottom(material);

        SpriteVisual tint = _compositor.CreateSpriteVisual();
        tint.RelativeSizeAdjustment = Vector2.One;
        tint.Brush = _compositor.CreateColorBrush(Color.FromArgb(52, 255, 255, 255));
        chip.Children.InsertAtTop(tint);

        _root.Children.InsertAtBottom(chip);

        float pad = Pad * _scale;
        float caption = CaptionHeight * _scale;

        if (preview.Image is Pixels image)
        {
            SpriteVisual picture = _compositor.CreateSpriteVisual();
            picture.Brush = Visuals.CreateBitmapBrush(image);

            if (preview.IsThumbnail)
            {
                // La miniatura llena el panel menos el margen y el pie.
                picture.Size = new Vector2(size.X - pad * 2f, size.Y - pad * 2f - caption);
                picture.Offset = new Vector3(pad, pad, 0f);

                // Esquinas mas cerradas que las del chip: una esquina interior con el
                // mismo radio que la exterior se ve mas suelta de lo que esta.
                CompositionRoundedRectangleGeometry inner = _compositor.CreateRoundedRectangleGeometry();
                inner.Size = picture.Size;
                inner.CornerRadius = new Vector2(8f * _scale);
                picture.Clip = _compositor.CreateGeometricClip(inner);
            }
            else
            {
                // El icono se queda a su tamano y centrado sobre el pie.
                picture.Size = new Vector2(image.Width, image.Height);
                picture.Offset = new Vector3(
                    (size.X - image.Width) * 0.5f,
                    (size.Y - caption - image.Height) * 0.5f,
                    0f);
            }

            _root.Children.InsertAtTop(picture);
        }

        Vector2 captionSize = new(size.X - pad * 2f, caption);
        SpriteVisual pie = _compositor.CreateSpriteVisual();
        pie.Size = captionSize;
        pie.Offset = new Vector3(pad, size.Y - caption - pad * 0.4f, 0f);
        pie.Brush = Visuals.CreateCaptionBrush(preview.Title, preview.Detail, captionSize, _scale);
        _root.Children.InsertAtTop(pie);
    }

    private static LRESULT WndProc(HWND hwnd, uint msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
            // Se puede clicar sin que el Explorador pierda el foco.
            case WM_MOUSEACTIVATE:
                return new LRESULT(MA_NOACTIVATE);
        }

        return PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);
    }

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
                hCursor = PInvoke.LoadCursor(default, new PCWSTR((char*)IdcArrow)),
            };

            _classAtom = PInvoke.RegisterClassEx(in wc);
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        _target.Root = null;
        _root.Dispose();
        _target.Dispose();

        if (!_hwnd.IsNull)
        {
            Instances.Remove((nint)_hwnd.Value);
            PInvoke.DestroyWindow(_hwnd);
            _hwnd = default;
        }
    }
}
